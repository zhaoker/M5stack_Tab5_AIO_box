/**
 * @file wifi_manager.c
 * @brief WiFi 连接管理实现
 *
 * 支持：
 *   - 多个 WiFi 凭据持久化存储
 *   - 当前可用 AP 扫描
 *   - 使用已保存网络自动连接 / 手动连接
 *
 * 注意：ESP32-P4 本身不含 WiFi 硬件，需通过 SDIO 与板载 ESP32-C6 模块协作。
 */
#include "wifi_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "bsp/m5stack_tab5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT     BIT0
#define WIFI_FAIL_BIT          BIT1
#define WIFI_MAX_RETRY         5
#define WIFI_STORE_VERSION     1
#define WIFI_NVS_KEY_NETWORKS  "networks"
#define WIFI_NVS_KEY_LAST_SSID "last_ssid"
#define WIFI_NVS_KEY_LEGACY_SSID "ssid"
#define WIFI_NVS_KEY_LEGACY_PASS "pass"
#define WIFI_AUTO_FALLBACK_TASK_STACK 6144

typedef struct {
    uint32_t version;
    uint32_t count;
    wifi_saved_network_t items[WIFI_MAX_SAVED_NETWORKS];
} wifi_store_t;

static EventGroupHandle_t  s_wifi_event_group = NULL;
static wifi_state_t        s_state = WIFI_STATE_IDLE;
static wifi_state_cb_t     s_cb = NULL;
static void               *s_cb_user_data = NULL;
static int                 s_retry_num = 0;
static bool                s_initialized = false;
static bool                s_netif_created = false;
static bool                s_wifi_unavailable = false;
static bool                s_manual_disconnect = false;
static bool                s_wifi_started = false;
static bool                s_wifi_compat_configured = false;
static bool                s_connect_requested = false;
static bool                s_auto_connect_mode = false;
static bool                s_auto_fallback_running = false;
static esp_netif_t        *s_sta_netif = NULL;
static portMUX_TYPE        s_init_mutex_lock = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t   s_init_mutex_storage;
static SemaphoreHandle_t   s_init_mutex = NULL;
static wifi_ap_record_t     s_scan_records[WIFI_MAX_SCAN_RESULTS];
static wifi_store_t         s_scan_store;
static wifi_ap_record_t     s_auto_scan_records[WIFI_MAX_SCAN_RESULTS];
static wifi_store_t         s_auto_scan_store;
static uint16_t             s_scan_record_count = 0;
static uint16_t             s_auto_scan_record_count = 0;
static char                 s_scan_current_ssid[WIFI_MAX_SSID_LEN];
static char                 s_current_target_ssid[WIFI_MAX_SSID_LEN];
static char                 s_auto_attempted_ssids[WIFI_MAX_SAVED_NETWORKS][WIFI_MAX_SSID_LEN];
static size_t               s_auto_attempted_count = 0;

static void set_state(wifi_state_t st)
{
    s_state = st;
    if (s_cb) {
        s_cb(st, s_cb_user_data);
    }
}

static SemaphoreHandle_t get_init_mutex(void)
{
    if (s_init_mutex) {
        return s_init_mutex;
    }

    portENTER_CRITICAL(&s_init_mutex_lock);
    if (!s_init_mutex) {
        s_init_mutex = xSemaphoreCreateMutexStatic(&s_init_mutex_storage);
    }
    portEXIT_CRITICAL(&s_init_mutex_lock);
    return s_init_mutex;
}

#include "wifi_manager_compat.inc"

static bool ipv6_addr_is_valid_global(const esp_ip6_addr_t *addr)
{
    if (!addr) {
        return false;
    }

    if ((ESP_IP6_ADDR_BLOCK1(addr) & 0xF000U) == 0xF000U) {
        return false;
    }

    return esp_netif_ip6_get_addr_type((esp_ip6_addr_t *)addr) == ESP_IP6_ADDR_IS_GLOBAL;
}

static void init_store(wifi_store_t *store)
{
    memset(store, 0, sizeof(*store));
    store->version = WIFI_STORE_VERSION;
}

static int find_saved_network_index(const wifi_store_t *store, const char *ssid)
{
    uint32_t i;

    if (!store || !ssid || !ssid[0]) {
        return -1;
    }

    for (i = 0; i < store->count && i < WIFI_MAX_SAVED_NETWORKS; ++i) {
        if (strncmp(store->items[i].ssid, ssid, WIFI_MAX_SSID_LEN) == 0) {
            return (int)i;
        }
    }

    return -1;
}

#include "wifi_manager_store.inc"

static esp_err_t ensure_wifi_started(void)
{
    esp_err_t err;

    if (s_wifi_started) {
        configure_started_wifi_compat();
        return ESP_OK;
    }

    err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_NOT_INIT) {
        return err;
    }
    if (err == ESP_OK) {
        s_wifi_started = true;
        configure_started_wifi_compat();
        return ESP_OK;
    }

    return err;
}

static void reset_auto_attempts(void)
{
    memset(s_auto_attempted_ssids, 0, sizeof(s_auto_attempted_ssids));
    s_auto_attempted_count = 0;
}

static bool auto_attempted_contains(const char *ssid)
{
    size_t i;

    if (!ssid || !ssid[0]) {
        return true;
    }

    for (i = 0; i < s_auto_attempted_count; ++i) {
        if (strncmp(s_auto_attempted_ssids[i], ssid, WIFI_MAX_SSID_LEN) == 0) {
            return true;
        }
    }

    return false;
}

static void auto_attempted_add(const char *ssid)
{
    if (!ssid || !ssid[0] || auto_attempted_contains(ssid)) {
        return;
    }
    if (s_auto_attempted_count >= WIFI_MAX_SAVED_NETWORKS) {
        return;
    }

    strlcpy(s_auto_attempted_ssids[s_auto_attempted_count], ssid,
            sizeof(s_auto_attempted_ssids[s_auto_attempted_count]));
    s_auto_attempted_count++;
}

static void prioritize_last_saved_network(wifi_store_t *store)
{
    char last_ssid[WIFI_MAX_SSID_LEN] = {0};
    wifi_saved_network_t last_item;
    int last_index;

    if (!store || store->count == 0) {
        return;
    }

    if (load_last_ssid(last_ssid, sizeof(last_ssid)) != ESP_OK) {
        return;
    }

    last_index = find_saved_network_index(store, last_ssid);
    if (last_index <= 0) {
        return;
    }

    last_item = store->items[last_index];
    memmove(&store->items[1], &store->items[0],
            (size_t)last_index * sizeof(store->items[0]));
    store->items[0] = last_item;
}

static int find_visible_saved_network_index(const wifi_store_t *store,
                                            const wifi_ap_record_t *records,
                                            uint16_t ap_count,
                                            bool skip_attempted)
{
    uint32_t saved_index;

    if (!store || !records || store->count == 0) {
        return -1;
    }

    for (saved_index = 0;
         saved_index < store->count && saved_index < WIFI_MAX_SAVED_NETWORKS;
         ++saved_index) {
        uint16_t ap_index;
        const char *saved_ssid = store->items[saved_index].ssid;

        if (!saved_ssid[0]) {
            continue;
        }
        if (skip_attempted && auto_attempted_contains(saved_ssid)) {
            continue;
        }

        for (ap_index = 0; ap_index < ap_count; ++ap_index) {
            const char *visible_ssid = (const char *)records[ap_index].ssid;

            if (!visible_ssid[0]) {
                continue;
            }
            if (strncmp(saved_ssid, visible_ssid, WIFI_MAX_SSID_LEN) == 0) {
                return (int)saved_index;
            }
        }
    }

    return -1;
}

static esp_err_t scan_visible_saved_network(wifi_store_t *store,
                                            bool skip_attempted,
                                            int *out_store_index)
{
    uint16_t ap_count = WIFI_MAX_SCAN_RESULTS;
    esp_err_t err;

    if (!store || !out_store_index) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_store_index = -1;
    memset(s_auto_scan_records, 0, sizeof(s_auto_scan_records));
    s_auto_scan_record_count = 0;

    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, s_auto_scan_records);
    if (err != ESP_OK) {
        return err;
    }
    s_auto_scan_record_count = ap_count;
    log_scan_records("Auto scan AP", s_auto_scan_records, ap_count);

    *out_store_index = find_visible_saved_network_index(store,
                                                        s_auto_scan_records,
                                                        ap_count,
                                                        skip_attempted);
    return (*out_store_index >= 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t connect_internal(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = { 0 };
    wifi_ap_record_t cached_ap = {0};
    esp_err_t err;
    bool was_started;
    bool was_connected;

    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_wifi_unavailable) {
        ESP_LOGW(TAG, "WiFi unavailable (C6 not responding), skipping connect");
        return ESP_FAIL;
    }

    if (!s_initialized) {
        err = wifi_manager_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password ? password : "", sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_config.sta.failure_retry_cnt = 1;

    if (select_cached_ap_record(ssid, &cached_ap)) {
        wifi_config.sta.channel = cached_ap.primary;
        apply_ap_country_if_needed(&cached_ap);
        log_ap_record_detail("Connecting using cached AP hint", &cached_ap);
    } else {
        ESP_LOGW(TAG, "No cached AP details for SSID %s; connecting by SSID only", ssid);
    }

    s_manual_disconnect = false;
    s_connect_requested = true;
    s_retry_num = 0;
    strlcpy(s_current_target_ssid, ssid, sizeof(s_current_target_ssid));
    if (s_auto_connect_mode) {
        auto_attempted_add(ssid);
    }
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    was_started = s_wifi_started;
    was_connected = (s_state == WIFI_STATE_CONNECTED);

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    err = ensure_wifi_started();
    if (err != ESP_OK) {
        return err;
    }

    if (was_started) {
        if (was_connected) {
            err = esp_wifi_disconnect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_NOT_CONNECT) {
                ESP_LOGW(TAG, "esp_wifi_disconnect before connect failed: %s", esp_err_to_name(err));
            }
        }

        err = esp_wifi_connect();
        if (err != ESP_OK) {
            return err;
        }
    }

    save_last_ssid(ssid);
    set_state(WIFI_STATE_CONNECTING);
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    return ESP_OK;
}

static void auto_fallback_task(void *arg)
{
    int store_index = -1;
    esp_err_t err;

    (void)arg;

    err = load_store(&s_auto_scan_store);
    if (err != ESP_OK || s_auto_scan_store.count == 0 ||
        s_manual_disconnect || !s_auto_connect_mode) {
        goto fail;
    }

    prioritize_last_saved_network(&s_auto_scan_store);
    ESP_LOGI(TAG, "Auto fallback: scanning for saved WiFi networks");
    err = scan_visible_saved_network(&s_auto_scan_store, true, &store_index);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Auto fallback found no untried visible saved network: %s",
                 esp_err_to_name(err));
        goto fail;
    }

    ESP_LOGI(TAG, "Auto fallback: connecting visible saved SSID %s",
             s_auto_scan_store.items[store_index].ssid);
    s_auto_fallback_running = false;
    err = connect_internal(s_auto_scan_store.items[store_index].ssid,
                           s_auto_scan_store.items[store_index].password);
    mbedtls_platform_zeroize(&s_auto_scan_store, sizeof(s_auto_scan_store));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Auto fallback connect failed: %s", esp_err_to_name(err));
        goto fail;
    }
    vTaskDelete(NULL);
    return;

fail:
    s_auto_fallback_running = false;
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    set_state(WIFI_STATE_FAILED);
    vTaskDelete(NULL);
}

static bool start_auto_fallback_task(void)
{
    if (!s_auto_connect_mode || s_auto_fallback_running || s_manual_disconnect) {
        return false;
    }

    s_auto_fallback_running = true;
    if (xTaskCreate(auto_fallback_task, "wifi_auto_fb",
                    WIFI_AUTO_FALLBACK_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        s_auto_fallback_running = false;
        return false;
    }

    return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_started = true;
        configure_started_wifi_compat();
        if (s_connect_requested && !s_manual_disconnect) {
            esp_wifi_connect();
            set_state(WIFI_STATE_CONNECTING);
        } else if (s_state == WIFI_STATE_IDLE) {
            set_state(WIFI_STATE_DISCONNECTED);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        s_wifi_started = false;
        s_wifi_compat_configured = false;
        if (!s_connect_requested) {
            set_state(WIFI_STATE_DISCONNECTED);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        if (event) {
            ESP_LOGW(TAG, "STA disconnected: reason=%u(%s) rssi=%d ssid=%.*s",
                     event->reason,
                     disconnect_reason_name(event->reason),
                     event->rssi,
                     (int)event->ssid_len,
                     (const char *)event->ssid);
        }

        if (s_manual_disconnect || !s_connect_requested) {
            s_retry_num = 0;
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
            set_state(WIFI_STATE_DISCONNECTED);
            return;
        }

        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry connecting... (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            ESP_LOGW(TAG, "Connect to SSID %s failed after retries", s_current_target_ssid);
            if (!s_auto_fallback_running && !start_auto_fallback_task()) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                set_state(WIFI_STATE_FAILED);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_auto_connect_mode = false;
        s_auto_fallback_running = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        set_state(WIFI_STATE_CONNECTED);
        if (s_sta_netif) {
            esp_netif_create_ip6_linklocal(s_sta_netif);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6");
        if (event) {
            ESP_LOGI(TAG, "IPv6 addr received");
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    esp_err_t ret;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    SemaphoreHandle_t init_mutex = get_init_mutex();

    if (s_initialized) {
        return ESP_OK;
    }

    if (init_mutex) {
        xSemaphoreTake(init_mutex, portMAX_DELAY);
    }

    if (s_initialized) {
        if (init_mutex) {
            xSemaphoreGive(init_mutex);
        }
        return ESP_OK;
    }

    configure_c6_sdio_drive_capability();
    bsp_set_wifi_power_enable(true);

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
    }

    if (!s_netif_created) {
        ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
            if (init_mutex) {
                xSemaphoreGive(init_mutex);
            }
            return ret;
        }
        s_sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (s_sta_netif) {
            ESP_LOGI(TAG, "Reusing existing default WiFi STA netif");
        } else {
            s_sta_netif = esp_netif_create_default_wifi_sta();
        }
        if (!s_sta_netif) {
            ESP_LOGW(TAG, "esp_netif_create_default_wifi_sta failed");
            if (init_mutex) {
                xSemaphoreGive(init_mutex);
            }
            return ESP_FAIL;
        }
        s_netif_created = true;
    }

    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_init failed: %s (WiFi unavailable)", esp_err_to_name(ret));
        s_wifi_unavailable = true;
        if (init_mutex) {
            xSemaphoreGive(init_mutex);
        }
        return ret;
    }

    ret = esp_wifi_set_country_code("CN", false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_country_code(CN) failed: %s", esp_err_to_name(ret));
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WIFI_EVENT handler register failed: %s", esp_err_to_name(ret));
        if (init_mutex) {
            xSemaphoreGive(init_mutex);
        }
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IP_EVENT handler register failed: %s", esp_err_to_name(ret));
        if (init_mutex) {
            xSemaphoreGive(init_mutex);
        }
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6,
                                              &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IP_EVENT_GOT_IP6 handler register failed: %s", esp_err_to_name(ret));
        if (init_mutex) {
            xSemaphoreGive(init_mutex);
        }
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        if (init_mutex) {
            xSemaphoreGive(init_mutex);
        }
        return ret;
    }

    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(WIFI_PS_NONE) failed: %s", esp_err_to_name(ret));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    if (init_mutex) {
        xSemaphoreGive(init_mutex);
    }
    return ESP_OK;
}

void wifi_manager_set_cb(wifi_state_cb_t cb, void *user_data)
{
    s_cb = cb;
    s_cb_user_data = user_data;
}

esp_err_t wifi_manager_auto_connect(void)
{
    int index = -1;
    esp_err_t err;

    if (!s_initialized) {
        err = wifi_manager_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    err = load_store(&s_auto_scan_store);
    if (err != ESP_OK || s_auto_scan_store.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    prioritize_last_saved_network(&s_auto_scan_store);
    reset_auto_attempts();
    s_auto_connect_mode = true;
    s_auto_fallback_running = false;
    s_manual_disconnect = false;
    s_connect_requested = false;
    s_retry_num = 0;
    memset(s_current_target_ssid, 0, sizeof(s_current_target_ssid));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    err = ensure_wifi_started();
    if (err != ESP_OK) {
        s_auto_connect_mode = false;
        set_state(WIFI_STATE_FAILED);
        return err;
    }

    set_state(WIFI_STATE_CONNECTING);
    ESP_LOGI(TAG, "Auto connect: scanning for visible saved WiFi networks");
    err = scan_visible_saved_network(&s_auto_scan_store, false, &index);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Auto connect: no visible saved WiFi network matched saved credentials");
        s_auto_connect_mode = false;
        s_auto_fallback_running = false;
        s_connect_requested = false;
        set_state(WIFI_STATE_DISCONNECTED);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Auto connect: connecting visible saved SSID %s",
             s_auto_scan_store.items[index].ssid);
    err = connect_internal(s_auto_scan_store.items[index].ssid,
                           s_auto_scan_store.items[index].password);
    mbedtls_platform_zeroize(&s_auto_scan_store, sizeof(s_auto_scan_store));
    return err;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool save_creds)
{
    esp_err_t err;

    s_auto_connect_mode = false;
    s_auto_fallback_running = false;
    reset_auto_attempts();

    if (save_creds) {
        err = wifi_manager_save_network(ssid, password ? password : "");
        if (err != ESP_OK) {
            return err;
        }
    }

    return connect_internal(ssid, password ? password : "");
}

esp_err_t wifi_manager_disconnect(void)
{
    esp_err_t err;

    s_manual_disconnect = true;
    s_connect_requested = false;
    s_auto_connect_mode = false;
    s_auto_fallback_running = false;
    s_retry_num = 0;
    memset(s_current_target_ssid, 0, sizeof(s_current_target_ssid));
    reset_auto_attempts();
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    if (!s_initialized) {
        set_state(WIFI_STATE_DISCONNECTED);
        return ESP_OK;
    }

    err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_CONN) {
        set_state(WIFI_STATE_DISCONNECTED);
        return ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    set_state(WIFI_STATE_DISCONNECTED);
    return ESP_OK;
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

esp_err_t wifi_manager_get_saved_ssid(char *ssid_buf, size_t buf_len)
{
    wifi_store_t store;
    char last_ssid[WIFI_MAX_SSID_LEN] = {0};
    int index = 0;

    if (!ssid_buf || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (load_store(&store) != ESP_OK || store.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (load_last_ssid(last_ssid, sizeof(last_ssid)) == ESP_OK) {
        int last_index = find_saved_network_index(&store, last_ssid);
        if (last_index >= 0) {
            index = last_index;
        }
    }

    strlcpy(ssid_buf, store.items[index].ssid, buf_len);
    mbedtls_platform_zeroize(&store, sizeof(store));
    return ESP_OK;
}

size_t wifi_manager_get_saved_networks(wifi_saved_network_t *out_networks, size_t max_items)
{
    wifi_store_t store;
    size_t count;

    if (load_store(&store) != ESP_OK) {
        return 0;
    }

    count = store.count;
    if (count > max_items) {
        count = max_items;
    }

    if (out_networks && count > 0) {
        memcpy(out_networks, store.items, count * sizeof(wifi_saved_network_t));
    }

    mbedtls_platform_zeroize(&store, sizeof(store));
    return count;
}

esp_err_t wifi_manager_save_network(const char *ssid, const char *password)
{
    wifi_store_t store;
    int index;

    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (load_store(&store) != ESP_OK) {
        init_store(&store);
    }

    index = find_saved_network_index(&store, ssid);
    if (index < 0) {
        if (store.count >= WIFI_MAX_SAVED_NETWORKS) {
            return ESP_ERR_NO_MEM;
        }
        index = (int)store.count;
        store.count++;
    }

    strlcpy(store.items[index].ssid, ssid, sizeof(store.items[index].ssid));
    strlcpy(store.items[index].password, password ? password : "", sizeof(store.items[index].password));

    esp_err_t err = persist_store(&store);
    mbedtls_platform_zeroize(&store, sizeof(store));
    return err;
}

esp_err_t wifi_manager_clear_saved_networks(void)
{
    wifi_store_t store;
    init_store(&store);

    esp_err_t err = persist_store(&store);
    if (err == ESP_OK) {
        err = save_last_ssid(NULL);
    }
    return err;
}

esp_err_t wifi_manager_delete_network(const char *ssid)
{
    wifi_store_t store;
    char last_ssid[WIFI_MAX_SSID_LEN] = {0};
    int index;
    uint32_t i;
    esp_err_t err;

    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    err = load_store(&store);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    index = find_saved_network_index(&store, ssid);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    for (i = (uint32_t)index; i + 1 < store.count; ++i) {
        store.items[i] = store.items[i + 1];
    }
    memset(&store.items[store.count - 1], 0, sizeof(store.items[store.count - 1]));
    store.count--;

    err = persist_store(&store);
    if (err != ESP_OK) {
        return err;
    }

    if (load_last_ssid(last_ssid, sizeof(last_ssid)) == ESP_OK && strncmp(last_ssid, ssid, sizeof(last_ssid)) == 0) {
        if (store.count > 0) {
            save_last_ssid(store.items[0].ssid);
        } else {
            save_last_ssid(NULL);
        }
    }

    return ESP_OK;
}

esp_err_t wifi_manager_connect_saved(const char *ssid)
{
    wifi_store_t store;
    int index;
    esp_err_t err = load_store(&store);

    s_auto_connect_mode = false;
    s_auto_fallback_running = false;
    reset_auto_attempts();

    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    index = find_saved_network_index(&store, ssid);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    err = connect_internal(store.items[index].ssid, store.items[index].password);
    mbedtls_platform_zeroize(&store, sizeof(store));
    return err;
}

esp_err_t wifi_manager_scan_networks(wifi_scan_result_t *out_results, size_t max_items, size_t *out_count)
{
    bool has_current = false;
    uint16_t ap_count = WIFI_MAX_SCAN_RESULTS;
    size_t written = 0;
    esp_err_t err;
    uint16_t i;

    if (!out_count || !out_results || max_items == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    if (!s_initialized) {
        err = wifi_manager_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    s_connect_requested = (s_state == WIFI_STATE_CONNECTING || s_state == WIFI_STATE_CONNECTED);
    err = ensure_wifi_started();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        return err;
    }

    memset(s_scan_records, 0, sizeof(s_scan_records));
    s_scan_record_count = 0;
    err = esp_wifi_scan_get_ap_records(&ap_count, s_scan_records);
    if (err != ESP_OK) {
        return err;
    }
    s_scan_record_count = ap_count;
    log_scan_records("Manual scan AP", s_scan_records, ap_count);

    if (load_store(&s_scan_store) != ESP_OK) {
        init_store(&s_scan_store);
    } else {
        for (uint32_t j = 0; j < s_scan_store.count && j < WIFI_MAX_SAVED_NETWORKS; ++j) {
            mbedtls_platform_zeroize(s_scan_store.items[j].password,
                                     sizeof(s_scan_store.items[j].password));
        }
    }
    memset(s_scan_current_ssid, 0, sizeof(s_scan_current_ssid));
    has_current = (wifi_manager_get_current_ssid(s_scan_current_ssid, sizeof(s_scan_current_ssid)) == ESP_OK);

    if (ap_count > max_items) {
        ap_count = (uint16_t)max_items;
    }

    for (i = 0; i < ap_count; ++i) {
        if (s_scan_records[i].ssid[0] == '\0') {
            continue;
        }

        memset(&out_results[written], 0, sizeof(out_results[written]));
        strlcpy(out_results[written].ssid, (const char *)s_scan_records[i].ssid, sizeof(out_results[written].ssid));
        out_results[written].rssi = s_scan_records[i].rssi;
        out_results[written].authmode = (uint8_t)s_scan_records[i].authmode;
        out_results[written].saved = (find_saved_network_index(&s_scan_store, (const char *)s_scan_records[i].ssid) >= 0);
        out_results[written].connected = has_current && (strncmp(s_scan_current_ssid, (const char *)s_scan_records[i].ssid, sizeof(s_scan_current_ssid)) == 0);
        written++;
    }

    *out_count = written;
    return ESP_OK;
}

esp_err_t wifi_manager_get_current_ssid(char *ssid_buf, size_t buf_len)
{
    wifi_ap_record_t ap_info;
    esp_err_t err;

    if (!ssid_buf || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized || !s_wifi_started || s_state != WIFI_STATE_CONNECTED) {
        return ESP_ERR_NOT_FOUND;
    }

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        return err;
    }

    strlcpy(ssid_buf, (const char *)ap_info.ssid, buf_len);
    return ESP_OK;
}

bool wifi_manager_has_ipv4(void)
{
    esp_netif_ip_info_t ip_info;

    if (!s_sta_netif) {
        return false;
    }

    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) != ESP_OK) {
        return false;
    }

    return ip_info.ip.addr != 0;
}

bool wifi_manager_has_ipv6_global(void)
{
    esp_ip6_addr_t ip6;

    if (!s_sta_netif) {
        return false;
    }

    memset(&ip6, 0, sizeof(ip6));
    if (esp_netif_get_ip6_global(s_sta_netif, &ip6) != ESP_OK) {
        return false;
    }

    return ipv6_addr_is_valid_global(&ip6);
}

esp_err_t wifi_manager_get_current_ip_text(char *ipv4_buf, size_t ipv4_len,
                                           char *ipv6_buf, size_t ipv6_len)
{
    esp_netif_ip_info_t ip_info;
    esp_ip6_addr_t ip6;
    bool has_ip = false;

    if ((!ipv4_buf || ipv4_len == 0) && (!ipv6_buf || ipv6_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ipv4_buf && ipv4_len > 0) {
        ipv4_buf[0] = '\0';
    }
    if (ipv6_buf && ipv6_len > 0) {
        ipv6_buf[0] = '\0';
    }

    if (!s_sta_netif || s_state != WIFI_STATE_CONNECTED) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(&ip_info, 0, sizeof(ip_info));
    if (ipv4_buf && ipv4_len > 0 &&
        esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        snprintf(ipv4_buf, ipv4_len, IPSTR, IP2STR(&ip_info.ip));
        has_ip = true;
    }

    memset(&ip6, 0, sizeof(ip6));
    if (ipv6_buf && ipv6_len > 0 &&
        esp_netif_get_ip6_global(s_sta_netif, &ip6) == ESP_OK &&
        ipv6_addr_is_valid_global(&ip6)) {
        snprintf(ipv6_buf, ipv6_len, "%x:%x:%x:%x:%x:%x:%x:%x",
                 ESP_IP6_ADDR_BLOCK1(&ip6), ESP_IP6_ADDR_BLOCK2(&ip6),
                 ESP_IP6_ADDR_BLOCK3(&ip6), ESP_IP6_ADDR_BLOCK4(&ip6),
                 ESP_IP6_ADDR_BLOCK5(&ip6), ESP_IP6_ADDR_BLOCK6(&ip6),
                 ESP_IP6_ADDR_BLOCK7(&ip6), ESP_IP6_ADDR_BLOCK8(&ip6));
        has_ip = true;
    }

    return has_ip ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_get_all_ipv6_text(char addrs[][WIFI_IPV6_TEXT_LEN],
                                         size_t max_items,
                                         size_t *out_count)
{
    esp_ip6_addr_t ip6_addrs[WIFI_MAX_IPV6_ADDRS];
    char tmp[WIFI_MAX_IPV6_ADDRS][WIFI_IPV6_TEXT_LEN];
    size_t tmp_count = 0;
    size_t written = 0;
    int count;

    if (out_count) {
        *out_count = 0;
    }
    if (!addrs || max_items == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < max_items; i++) {
        addrs[i][0] = '\0';
    }
    if (!s_sta_netif || s_state != WIFI_STATE_CONNECTED) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(ip6_addrs, 0, sizeof(ip6_addrs));
    memset(tmp, 0, sizeof(tmp));
    count = esp_netif_get_all_ip6(s_sta_netif, ip6_addrs);
    if (count <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (count > WIFI_MAX_IPV6_ADDRS) {
        count = WIFI_MAX_IPV6_ADDRS;
    }

    for (int i = 0; i < count; i++) {
        bool all_zero = true;
        for (size_t j = 0; j < sizeof(ip6_addrs[i].addr) / sizeof(ip6_addrs[i].addr[0]); j++) {
            if (ip6_addrs[i].addr[j] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero || tmp_count >= WIFI_MAX_IPV6_ADDRS) {
            continue;
        }
        snprintf(tmp[tmp_count], WIFI_IPV6_TEXT_LEN, "%x:%x:%x:%x:%x:%x:%x:%x",
                 ESP_IP6_ADDR_BLOCK1(&ip6_addrs[i]), ESP_IP6_ADDR_BLOCK2(&ip6_addrs[i]),
                 ESP_IP6_ADDR_BLOCK3(&ip6_addrs[i]), ESP_IP6_ADDR_BLOCK4(&ip6_addrs[i]),
                 ESP_IP6_ADDR_BLOCK5(&ip6_addrs[i]), ESP_IP6_ADDR_BLOCK6(&ip6_addrs[i]),
                 ESP_IP6_ADDR_BLOCK7(&ip6_addrs[i]), ESP_IP6_ADDR_BLOCK8(&ip6_addrs[i]));
        tmp_count++;
    }

    for (size_t pass = 0; pass < 2 && written < max_items; pass++) {
        for (size_t i = 0; i < tmp_count && written < max_items; i++) {
            bool starts_with_f = (tmp[i][0] == 'f' || tmp[i][0] == 'F');
            if ((pass == 0 && starts_with_f) || (pass == 1 && !starts_with_f)) {
                continue;
            }
            strlcpy(addrs[written], tmp[i], WIFI_IPV6_TEXT_LEN);
            written++;
        }
    }

    if (out_count) {
        *out_count = written;
    }
    return written > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool wifi_manager_is_saved_network(const char *ssid)
{
    wifi_store_t store;

    if (load_store(&store) != ESP_OK) {
        return false;
    }

    return find_saved_network_index(&store, ssid) >= 0;
}
