#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"
#include "fonts/MI_font_60_time.h"
#include "lvgl.h"
#include "diagnostics/mem_diag.h"
#include "storage/server_config.h"
#include "storage/tf_card.h"
#include "settings/app_settings.h"
#include "hw/tab5_keyboard.h"
#include "ota_update/ota_update_service.h"
#include "power/power_monitor.h"
#include "screenshot/screenshot_service.h"
#include "screensaver/screensaver_service.h"
#include "time_sync/time_sync_service.h"
#include "ui/ui_audio_background.h"
#include "ui/ui_language_select.h"
#include "wifi_manager.h"
#include "ui/ui_server_list.h"

static const char *TAG = "main";

#define BOOT_BG_TASK_STACK      8192
#define BOOT_BG_TASK_PRIORITY   1
#define BOOT_BG_TASK_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define BOOT_BG_TASK_CORE 1
#else
#define BOOT_BG_TASK_CORE tskNO_AFFINITY
#endif

static void show_startup_splash(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1016), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, &APP_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_width(title, LV_PCT(96));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, "Starting...");
    lv_obj_set_style_text_font(title, &MI_font_60_time, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(title);
}

static void on_wifi_state_for_time_sync(wifi_state_t state, void *user_data)
{
    (void)user_data;
    if (state == WIFI_STATE_CONNECTED) {
        time_sync_service_on_wifi_connected();
    }
}

static void boot_background_task(void *arg)
{
    bool stack_with_caps = (uintptr_t)arg != 0U;

    ESP_LOGI(TAG, "boot background services start on core %d", xPortGetCoreID());

    esp_err_t tf_ret = tf_card_mount_on_boot();
    if (tf_ret != ESP_OK) {
        ESP_LOGW(TAG, "TF card is not available on boot: %s", esp_err_to_name(tf_ret));
    } else {
        (void)ota_update_scan_ready_package();
    }
    mem_diag_checkpoint("after tf_card_mount_on_boot");

    esp_err_t screenshot_ret = screenshot_service_start();
    if (screenshot_ret != ESP_OK) {
        ESP_LOGW(TAG, "screenshot service start failed: %s", esp_err_to_name(screenshot_ret));
    }
    mem_diag_checkpoint("after screenshot_service_start");

    wifi_manager_init();
    mem_diag_checkpoint("after wifi_manager_init");
    wifi_manager_set_cb(on_wifi_state_for_time_sync, NULL);
    mem_diag_checkpoint("after wifi callback set");

    esp_err_t wifi_ret = wifi_manager_auto_connect();
    if (wifi_ret != ESP_OK) {
        ESP_LOGI(TAG, "No visible saved WiFi network, waiting for manual connection");
    }
    mem_diag_checkpoint("after wifi auto connect request");

    if (stack_with_caps) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
}

static esp_err_t start_boot_background_services(void)
{
    TaskHandle_t task = NULL;
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(boot_background_task,
                                                     "boot_bg",
                                                     BOOT_BG_TASK_STACK,
                                                     (void *)1,
                                                     BOOT_BG_TASK_PRIORITY,
                                                     &task,
                                                     BOOT_BG_TASK_CORE,
                                                     BOOT_BG_TASK_STACK_CAPS);
    if (ret != pdPASS) {
        ret = xTaskCreatePinnedToCore(boot_background_task,
                                      "boot_bg",
                                      BOOT_BG_TASK_STACK,
                                      (void *)0,
                                      BOOT_BG_TASK_PRIORITY,
                                      &task,
                                      BOOT_BG_TASK_CORE);
    }
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting M5Stack Tab5 SSH Terminal");
    mem_diag_checkpoint_reset();
    mem_diag_checkpoint("boot start");
    lvgl_port_cfg_t lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    /* ── NVS Flash 初始化（WiFi/配置持久化依赖） ── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    mem_diag_checkpoint("after nvs_flash_init");

    /* ── 事件循环（esp_wifi 依赖） ── */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    mem_diag_checkpoint("after event loop");

    /* ── 服务器配置初始化 ── */
    server_config_init();
    mem_diag_checkpoint("after server_config_init");
    app_settings_init();
    mem_diag_checkpoint("after app_settings_init");
    app_i18n_init();
    mem_diag_checkpoint("after app_i18n_init");
    app_settings_ensure_timezone_default_for_language();
    mem_diag_checkpoint("after timezone default");
    esp_err_t ota_ret = ota_update_service_init();
    if (ota_ret != ESP_OK) {
        ESP_LOGW(TAG, "ota update service init warning: %s", esp_err_to_name(ota_ret));
    }
    mem_diag_checkpoint("after ota_update_service_init");
    esp_err_t time_ret = time_sync_service_init();
    if (time_ret != ESP_OK) {
        ESP_LOGW(TAG, "time sync service init warning: %s", esp_err_to_name(time_ret));
    }
    mem_diag_checkpoint("after time_sync_service_init");

    /* ── 显示初始化 ── */
    lvgl_port_cfg.task_stack = 12288;
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = lvgl_port_cfg,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    assert(disp);
    mem_diag_checkpoint("after bsp_display_start");
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    mem_diag_checkpoint("after display rotation");
    ESP_ERROR_CHECK(bsp_display_brightness_set(0));
    mem_diag_checkpoint("after startup backlight off");

    if (!bsp_display_lock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock during splash");
        return;
    }
    show_startup_splash();
    lv_refr_now(NULL);
    ESP_ERROR_CHECK(app_settings_apply_brightness());
    bsp_display_unlock();
    mem_diag_checkpoint("after boot splash");

    esp_err_t kb_probe_ret = tab5_keyboard_probe_presence();
    ESP_LOGI(TAG, "startup keyboard probe result: %s", esp_err_to_name(kb_probe_ret));
    tab5_keyboard_dump_state("startup probe");
    mem_diag_checkpoint("after keyboard probe");
    if (kb_probe_ret == ESP_OK) {
        esp_err_t kb_led_ret = app_settings_apply_keyboard_indicator_brightness();
        if (kb_led_ret != ESP_OK) {
            ESP_LOGW(TAG, "apply startup keyboard indicator brightness failed: %s",
                     esp_err_to_name(kb_led_ret));
        }
    }
    mem_diag_checkpoint("after keyboard led apply");

    /* ── LVGL 默认字体设为中文字体 ── */
    if (!bsp_display_lock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock during startup");
        return;
    }
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(lv_scr_act(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(lv_scr_act(), &APP_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── 启动主界面；首次刷机未选择语言时先进入语言选择页 ── */
    if (app_i18n_language_is_selected()) {
        ui_server_list_create(lv_scr_act());
        ui_audio_background_init();
    } else {
        ui_language_select_create(lv_scr_act());
    }
    lv_refr_now(NULL);
    bsp_display_unlock();
    mem_diag_checkpoint("after initial ui");

    bool boot_auto_charge = app_settings_get_boot_auto_charge_enabled();
    bool charge_fast = app_settings_get_charge_fast_enabled();
    esp_err_t charge_ret =
        power_monitor_apply_boot_policy_async(boot_auto_charge, charge_fast, 5000);
    if (charge_ret != ESP_OK) {
        ESP_LOGW(TAG, "apply boot charge setting failed: %s", esp_err_to_name(charge_ret));
    } else {
        ESP_LOGI(TAG,
                 "boot auto charge %s, speed=%s",
                 boot_auto_charge ? "enabled" : "disabled",
                 charge_fast ? "fast" : "standard");
    }
    mem_diag_checkpoint("after boot charge policy queued");

    esp_err_t screensaver_ret = screensaver_service_init();
    if (screensaver_ret != ESP_OK) {
        ESP_LOGW(TAG, "screensaver service start failed: %s", esp_err_to_name(screensaver_ret));
    }
    mem_diag_checkpoint("after screensaver_service_init");

    esp_err_t bg_ret = start_boot_background_services();
    if (bg_ret != ESP_OK) {
        ESP_LOGW(TAG, "boot background services start failed: %s", esp_err_to_name(bg_ret));
    }
    mem_diag_checkpoint("after boot background queued");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
