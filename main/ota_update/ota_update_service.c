#include "ota_update_service.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i18n/app_i18n.h"
#include "mbedtls/sha256.h"
#include "storage/tf_card.h"
#include "wifi_manager.h"

#define OTA_HTTP_TIMEOUT_MS 12000
#define OTA_HTTP_MAX_ATTEMPTS 3
#define OTA_HTTP_RETRY_DELAY_MS 1200
#define OTA_MANIFEST_MAX_BYTES 32768
#define OTA_DOWNLOAD_BUFFER_SIZE 4096
#define OTA_PSRAM_STAGE_BUFFER_SIZE (128 * 1024)
#define OTA_STAGE_WRITE_CHUNK_SIZE 4096
#define OTA_STAGE_SLOW_WRITE_LOG_MS 250
#define OTA_STAGE_SLOW_FLUSH_LOG_MS 600
#define OTA_DOWNLOAD_MAX_BPS (384 * 1024)
#define OTA_DOWNLOAD_IDLE_DELAY_MS 20
#define OTA_DOWNLOAD_STALL_TIMEOUT_MS 30000
#define OTA_DOWNLOAD_YIELD_MS 1
#define OTA_FLASH_SECTOR_SIZE 4096
#define OTA_TASK_STACK 12288
#define OTA_TASK_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define OTA_TASK_STACK_FALLBACK_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define OTA_CHECK_TASK_PRIO 3
#define OTA_DOWNLOAD_TASK_PRIO 2
#define OTA_PROJECT_NAME "M5stack_tab5_ssh_client"

static const char *TAG = "ota_update";
static SemaphoreHandle_t s_lock;
static ota_update_status_t s_status;
static bool s_check_task_running;
static bool s_download_task_running;
static bool s_updater_task_running;
static char s_expected_sha256[65];
static char s_manifest_version[64];
static char s_manifest_build_time[64];
static uint64_t s_manifest_size;
static bool s_manifest_ota_blocked;
static char s_updater_expected_sha256[65];
static char s_updater_manifest_version[64];
static char s_updater_manifest_build_time[64];
static uint64_t s_updater_manifest_size;

static void ota_lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void ota_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void copy_text(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    snprintf(dst, dst_len, "%s", src ? TRS(src) : "");
}

static void set_state_locked(ota_update_state_t state, const char *message)
{
    s_status.state = state;
    s_status.busy = (state == OTA_UPDATE_STATE_CHECKING || state == OTA_UPDATE_STATE_DOWNLOADING ||
                     state == OTA_UPDATE_STATE_UPDATER_INSTALLING ||
                     s_status.updater_busy);
    if (message) {
        copy_text(s_status.status, sizeof(s_status.status), message);
    }
}

static void set_error(const char *message)
{
    ota_lock();
    set_state_locked(OTA_UPDATE_STATE_ERROR, message);
    copy_text(s_status.error, sizeof(s_status.error), message);
    s_status.busy = s_check_task_running || s_download_task_running || s_updater_task_running ||
                    s_status.updater_busy;
    ota_unlock();
    ESP_LOGW(TAG, "%s", message ? message : "OTA error");
}

static bool partition_scheme_supports_internal_ota(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    const esp_partition_t *main_app = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "main_app");
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (!factory || !main_app) {
        ESP_LOGW(TAG,
                 "internal OTA partition scheme unavailable: factory=%p main_app=%p",
                 (const void *)factory,
                 (const void *)main_app);
        return false;
    }
    if (running && running->address != main_app->address) {
        ESP_LOGW(TAG,
                 "running partition is not main_app: label=%s address=0x%lx main_app=0x%lx",
                 running->label,
                 (unsigned long)running->address,
                 (unsigned long)main_app->address);
    }
    return true;
}

static void update_launcher_mode_locked(void)
{
    s_status.launcher_mode = !partition_scheme_supports_internal_ota();
    if (s_status.launcher_mode) {
        s_status.update_available = false;
        s_status.updater_update_available = false;
        s_status.ready_to_reboot = false;
        s_status.progress = 0;
        s_status.downloaded = 0;
        s_status.total = 0;
        s_status.speed_bps = 0;
        copy_text(s_status.status,
                  sizeof(s_status.status),
                  "无OTA分区");
    }
}

static void log_ota_task_heap(const char *task_name, const char *phase)
{
    ESP_LOGW(TAG,
             "%s task %s stack=%u internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
             task_name ? task_name : "ota",
             phase ? phase : "heap",
             (unsigned)OTA_TASK_STACK,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void log_ota_heap(const char *phase)
{
    ESP_LOGW(TAG,
             "%s heap internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
             phase ? phase : "ota",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static BaseType_t ota_create_task(TaskFunction_t task_func,
                                  const char *task_name,
                                  UBaseType_t priority)
{
    BaseType_t ret = xTaskCreateWithCaps(task_func,
                                         task_name,
                                         OTA_TASK_STACK,
                                         NULL,
                                         priority,
                                         NULL,
                                         OTA_TASK_STACK_CAPS);
    if (ret == pdPASS) {
        return ret;
    }

    log_ota_task_heap(task_name, "PSRAM stack create failed; trying internal");
    ret = xTaskCreateWithCaps(task_func,
                              task_name,
                              OTA_TASK_STACK,
                              NULL,
                              priority,
                              NULL,
                              OTA_TASK_STACK_FALLBACK_CAPS);
    if (ret != pdPASS) {
        log_ota_task_heap(task_name, "create failed");
    }
    return ret;
}

static void ota_delete_current_task(void)
{
    vTaskDeleteWithCaps(NULL);
}

static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out, size_t out_len);

static bool ensure_update_dir(void)
{
    struct stat st = {0};
    if (stat(OTA_UPDATE_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    if (mkdir(OTA_UPDATE_DIR, 0775) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

static bool environment_ready(char *reason, size_t reason_len, bool require_internal_ota)
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;

    if (require_internal_ota && s_status.launcher_mode) {
        copy_text(reason, reason_len, "无OTA分区");
        return false;
    }
    if (wifi_manager_get_state() != WIFI_STATE_CONNECTED || !wifi_manager_has_ipv4()) {
        copy_text(reason, reason_len, "WiFi 未连接");
        return false;
    }
    if (!require_internal_ota) {
        reason[0] = '\0';
        return true;
    }
    if (!tf_card_is_mounted()) {
        copy_text(reason, reason_len, "未检测到 TF 卡");
        return false;
    }
    if (esp_vfs_fat_info(tf_card_get_mount_point(), &total_bytes, &free_bytes) != ESP_OK) {
        copy_text(reason, reason_len, "无法读取 TF 卡容量");
        return false;
    }
    if (free_bytes < OTA_UPDATE_MIN_FREE_BYTES) {
        copy_text(reason, reason_len, "TF 卡剩余空间不足 20MB");
        return false;
    }
    if (!ensure_update_dir()) {
        copy_text(reason, reason_len, "无法创建 /update 目录");
        return false;
    }
    reason[0] = '\0';
    return true;
}

static void get_current_version(char *out, size_t out_len)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    copy_text(out, out_len, desc ? desc->version : "");
}

static void get_factory_updater_version(char *out, size_t out_len)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    esp_app_desc_t desc = {0};

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!factory) {
        return;
    }
    if (esp_ota_get_partition_description(factory, &desc) == ESP_OK) {
        copy_text(out, out_len, desc.version);
    }
}

static bool is_ascii_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parse_time_version_value(const char *version, uint64_t *out_value)
{
    const char *p = version;
    uint64_t value = 0;

    if (!p || !out_value) {
        return false;
    }
    if (*p == 'v' || *p == 'V') {
        ++p;
    }
    for (int i = 0; i < 8; ++i) {
        if (!is_ascii_digit(p[i])) {
            return false;
        }
        value = value * 10ULL + (uint64_t)(p[i] - '0');
    }
    if (p[8] != '.') {
        return false;
    }
    p += 9;
    for (int i = 0; i < 4; ++i) {
        if (!is_ascii_digit(p[i])) {
            return false;
        }
        value = value * 10ULL + (uint64_t)(p[i] - '0');
    }
    if (p[4] != '\0') {
        return false;
    }
    *out_value = value;
    return true;
}

static int compare_latest_to_current_version(const char *latest, const char *current, bool *strict_time_version)
{
    uint64_t latest_value = 0;
    uint64_t current_value = 0;
    bool latest_ok = parse_time_version_value(latest, &latest_value);
    bool current_ok = parse_time_version_value(current, &current_value);

    if (strict_time_version) {
        *strict_time_version = latest_ok && current_ok;
    }
    if (latest_ok && current_ok) {
        if (latest_value > current_value) {
            return 1;
        }
        if (latest_value < current_value) {
            return -1;
        }
        return 0;
    }

    if (strcmp(latest ? latest : "", current ? current : "") == 0) {
        return 0;
    }
    return 1;
}

static void url_encode_component(const char *src, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;

    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] && out + 1 < dst_len; ++i) {
        unsigned char c = (unsigned char)src[i];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (safe) {
            dst[out++] = (char)c;
        } else if (out + 3 < dst_len) {
            dst[out++] = '%';
            dst[out++] = hex[(c >> 4) & 0xF];
            dst[out++] = hex[c & 0xF];
        } else {
            break;
        }
    }
    dst[out] = '\0';
}

static bool url_uses_https(const char *url)
{
    return url && strncmp(url, "https://", 8) == 0;
}

static void make_plain_http_download_url(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (url_uses_https(src)) {
        snprintf(dst, dst_len, "http://%s", src + 8);
        return;
    }
    copy_text(dst, dst_len, src);
}

static bool flush_download_stage(FILE *fp,
                                 const unsigned char *stage,
                                 unsigned char *write_buffer,
                                 size_t *stage_used,
                                 uint64_t *written,
                                 uint64_t *write_us_total,
                                 char *reason,
                                 size_t reason_len)
{
    size_t offset = 0;
    size_t used = stage_used ? *stage_used : 0;
    int64_t flush_start_us = esp_timer_get_time();
    uint64_t before_written = written ? *written : 0;

    if (!fp || !stage || !stage_used || used == 0) {
        return true;
    }
    while (offset < used) {
        size_t chunk = used - offset;
        if (chunk > OTA_STAGE_WRITE_CHUNK_SIZE) {
            chunk = OTA_STAGE_WRITE_CHUNK_SIZE;
        }
        const unsigned char *write_src = stage + offset;
        if (write_buffer) {
            memcpy(write_buffer, stage + offset, chunk);
            write_src = write_buffer;
        }
        int64_t write_start_us = esp_timer_get_time();
        if (fwrite(write_src, 1, chunk, fp) != chunk) {
            copy_text(reason, reason_len, "写入 TF 卡失败");
            return false;
        }
        int64_t write_us = esp_timer_get_time() - write_start_us;
        if (write_us >= OTA_STAGE_SLOW_WRITE_LOG_MS * 1000LL) {
            ESP_LOGW(TAG,
                     "slow TF write chunk: %u bytes in %lld ms",
                     (unsigned)chunk,
                     (long long)(write_us / 1000));
        }
        if (write_us_total) {
            *write_us_total += (uint64_t)write_us;
        }
        if (written) {
            *written += (uint64_t)chunk;
        }
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_YIELD_MS));
    }
    int64_t flush_us = esp_timer_get_time() - flush_start_us;
    if (flush_us >= OTA_STAGE_SLOW_FLUSH_LOG_MS * 1000LL) {
        ESP_LOGW(TAG,
                 "slow TF stage flush: %u bytes in %lld ms, written=%llu",
                 (unsigned)used,
                 (long long)(flush_us / 1000),
                 (unsigned long long)(written ? *written : before_written + used));
    }
    *stage_used = 0;
    return true;
}

static void apply_download_speed_limit(uint64_t downloaded, int64_t start_us)
{
    if (OTA_DOWNLOAD_MAX_BPS <= 0 || downloaded < OTA_PSRAM_STAGE_BUFFER_SIZE || start_us <= 0) {
        return;
    }

    int64_t elapsed_us = esp_timer_get_time() - start_us;
    int64_t target_us = (int64_t)((downloaded * 1000000ULL) / OTA_DOWNLOAD_MAX_BPS);
    int64_t delay_us = target_us - elapsed_us;
    if (delay_us <= 2000) {
        return;
    }
    if (delay_us > 80000) {
        delay_us = 80000;
    }
    vTaskDelay(pdMS_TO_TICKS((uint32_t)((delay_us + 999) / 1000)));
}

static void get_device_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    if (!out || out_len == 0) {
        return;
    }
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BASE);
    if (err != ESP_OK) {
        err = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    }
    if (err != ESP_OK) {
        err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read device mac failed: %s", esp_err_to_name(err));
        snprintf(out, out_len, "unknown");
        return;
    }
    snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void build_manifest_url(char *out, size_t out_len)
{
    char current[64];
    char enc_version[128];
    char device[24];
    const char *language = "zh-CN";

    get_current_version(current, sizeof(current));
    url_encode_component(current, enc_version, sizeof(enc_version));
    get_device_id(device, sizeof(device));
    switch (app_i18n_get_language()) {
    case APP_LANGUAGE_ZH_TW:
        language = "zh-TW";
        break;
    case APP_LANGUAGE_EN:
        language = "en";
        break;
    case APP_LANGUAGE_ZH_CN:
    case APP_LANGUAGE_UNSET:
    default:
        language = "zh-CN";
        break;
    }
    snprintf(out,
             out_len,
             "%s?project=%s&version=%s&device=%s&language=%s",
             OTA_UPDATE_SERVER_MANIFEST_URL,
             OTA_PROJECT_NAME,
             enc_version,
             device,
             language);
}

static bool json_get_string(cJSON *root, const char *name, char *out, size_t out_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    copy_text(out, out_len, item->valuestring);
    return true;
}

static uint64_t json_get_u64(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsNumber(item) && item->valuedouble > 0) {
        return (uint64_t)item->valuedouble;
    }
    return 0;
}

static bool json_get_child_string(cJSON *root, const char *child_name, const char *name, char *out, size_t out_len)
{
    cJSON *child = cJSON_GetObjectItemCaseSensitive(root, child_name);
    if (!cJSON_IsObject(child)) {
        return false;
    }
    return json_get_string(child, name, out, out_len);
}

static uint64_t json_get_child_u64(cJSON *root, const char *child_name, const char *name)
{
    cJSON *child = cJSON_GetObjectItemCaseSensitive(root, child_name);
    if (!cJSON_IsObject(child)) {
        return 0;
    }
    return json_get_u64(child, name);
}

static bool file_exists_with_size(const char *path, uint64_t *out_size)
{
    struct stat st = {0};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    if (out_size) {
        *out_size = (uint64_t)st.st_size;
    }
    return true;
}

static char *read_small_file(const char *path, size_t max_bytes, char *reason, size_t reason_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        copy_text(reason, reason_len, "无法读取升级请求文件");
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        copy_text(reason, reason_len, "升级请求文件定位失败");
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0 || (size_t)len > max_bytes) {
        fclose(fp);
        copy_text(reason, reason_len, "升级请求文件异常");
        return NULL;
    }
    rewind(fp);
    char *buf = calloc(1, (size_t)len + 1);
    if (!buf) {
        fclose(fp);
        copy_text(reason, reason_len, "读取升级请求时内存不足");
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        copy_text(reason, reason_len, "升级请求文件读取失败");
        return NULL;
    }
    fclose(fp);
    return buf;
}

/* Long-running OTA tasks are kept in a separate include for readability. */
#include "ota_update_tasks.inc"

esp_err_t ota_update_service_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    ota_lock();
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_UPDATE_STATE_IDLE;
    get_current_version(s_status.current_version, sizeof(s_status.current_version));
    get_factory_updater_version(s_status.updater_current_version,
                                sizeof(s_status.updater_current_version));
    copy_text(s_status.status, sizeof(s_status.status), "等待检测更新。");
    update_launcher_mode_locked();
    ota_unlock();
    char reason[160];
    if (!s_status.launcher_mode) {
        (void)load_ready_package_from_tf(reason, sizeof(reason), true);
    }
    return ESP_OK;
}

esp_err_t ota_update_scan_ready_package(void)
{
    char reason[160];
    ota_lock();
    bool launcher_mode = s_status.launcher_mode;
    ota_unlock();
    if (launcher_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (load_ready_package_from_tf(reason, sizeof(reason), true)) {
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void ota_update_get_status(ota_update_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    ota_lock();
    *out_status = s_status;
    ota_unlock();
}

esp_err_t ota_update_start_check(void)
{
    ota_lock();
    bool busy = s_check_task_running || s_download_task_running || s_updater_task_running;
    ota_unlock();
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ota_create_task(ota_check_task, "ota_check", OTA_CHECK_TASK_PRIO) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_update_start_download(void)
{
    ota_lock();
    bool launcher_mode = s_status.launcher_mode;
    bool can_start = !s_check_task_running && !s_download_task_running && !s_updater_task_running &&
                     s_status.update_available;
    ota_unlock();
    if (launcher_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!can_start) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ota_create_task(ota_download_task, "ota_download", OTA_DOWNLOAD_TASK_PRIO) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_update_start_updater_install(void)
{
    ota_lock();
    bool launcher_mode = s_status.launcher_mode;
    bool can_start = !s_check_task_running && !s_download_task_running && !s_updater_task_running &&
                     s_status.updater_update_available && s_status.updater_url[0] &&
                     s_updater_expected_sha256[0] && s_updater_manifest_size > 0;
    ota_unlock();
    if (launcher_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!can_start) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ota_create_task(ota_updater_install_task, "ota_updater", OTA_DOWNLOAD_TASK_PRIO) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_update_reboot_to_updater(void)
{
    char reason[160];
    ota_lock();
    bool launcher_mode = s_status.launcher_mode;
    ota_unlock();
    if (launcher_mode) {
        set_error("无OTA分区");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!load_ready_package_from_tf(reason, sizeof(reason), false)) {
        set_error(reason);
        return ESP_ERR_INVALID_STATE;
    }
    if (!verify_firmware_file_sha256(OTA_UPDATE_FIRMWARE_PATH,
                                     s_expected_sha256,
                                     s_manifest_size,
                                     reason,
                                     sizeof(reason))) {
        set_error(reason);
        return ESP_ERR_INVALID_CRC;
    }

    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    if (!factory) {
        set_error("未找到 factory updater 分区");
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set boot factory updater failed: %s", esp_err_to_name(err));
        set_error("切换到 updater 分区失败");
        return err;
    }
    ESP_LOGI(TAG, "restart to factory updater");
    esp_restart();
    return ESP_OK;
}

const char *ota_update_state_text(ota_update_state_t state)
{
    switch (state) {
    case OTA_UPDATE_STATE_IDLE:
        return TRS("等待检测");
    case OTA_UPDATE_STATE_CHECKING:
        return TRS("检测中");
    case OTA_UPDATE_STATE_UP_TO_DATE:
        return TRS("已是最新");
    case OTA_UPDATE_STATE_UPDATE_AVAILABLE:
        return TRS("发现更新");
    case OTA_UPDATE_STATE_DOWNLOADING:
        return TRS("下载中");
    case OTA_UPDATE_STATE_READY_TO_REBOOT:
        return TRS("准备重启");
    case OTA_UPDATE_STATE_UPDATER_INSTALLING:
        return TRS("更新UPLOAD");
    case OTA_UPDATE_STATE_ERROR:
        return TRS("出错");
    default:
        return TRS("未知");
    }
}
