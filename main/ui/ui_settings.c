#include "ui_settings.h"

#include "settings/app_settings.h"
#include "esp_system.h"
#include "theme.hpp"

#include "bsp/m5stack_tab5.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "file_manager/ui_file_manager.h"
#include "file_manager/web_file_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw/tab5_keyboard.h"
#include "ota_update/ota_update_service.h"
#include "sdkconfig.h"
#include "screenshot/screenshot_service.h"
#include "screensaver/screensaver_service.h"
#include "storage/nvs_config_backup.h"
#include "storage/tf_card.h"
#include "storage/tf_usb_disk_mode.h"
#include "storage/usb_storage.h"
#include "time_sync/time_sync_service.h"
#include "ui_audio_background.h"
#include "ui_hw_textarea_input.h"
#include "ui_number_input.h"
#include "ui_server_list.h"
#include "wifi_manager.h"

#include <inttypes.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "ui_settings";

enum {
    SETTINGS_PANEL_W = 980,
    SETTINGS_PANEL_H = 620,
    SETTINGS_LEFT_W = 270,
    SETTINGS_HEADER_H = 58,
    SETTINGS_PAD = 18,
    SETTINGS_GAP = 14,
    SETTINGS_MENU_ITEM_H = 74,
    SETTINGS_SCREENSAVER_DIR_PICKER_MAX = 96,
};

typedef struct {
    uint32_t cpu_mhz;
    uint32_t flash_mb;
    uint32_t psram_physical_mb;
    uint32_t psram_free_mb;
    const char *panel_ic;
    bool touch_ready;
    bool keyboard_ready;
} system_info_t;

typedef struct {
    const char *device;
    const char *chip;
    const char *address;
    const char *description;
} board_i2c_device_t;

typedef void (*settings_render_cb_t)(lv_obj_t *parent);

typedef struct {
    const char *title;
    const char *icon;
    settings_render_cb_t render;
} settings_page_def_t;

typedef char settings_screensaver_dir_entry_t[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN];

static void render_system_settings_page(lv_obj_t *parent);
static void render_other_settings_page(lv_obj_t *parent);
static void render_file_manager_page(lv_obj_t *parent);
static void render_screensaver_settings_page(lv_obj_t *parent);
static void render_firmware_update_page(lv_obj_t *parent);
static void render_system_info_page(lv_obj_t *parent);
static void update_system_info_time_label(void);
static int settings_parse_int_text(const char *text, bool *ok);
static lv_obj_t *create_number_input_row(lv_obj_t *parent,
                                         const char *name,
                                         int value,
                                         lv_obj_t **out_ta);
static void close_screensaver_dir_picker(void);
static void screensaver_color_close_dialog(void);
static void render_screensaver_settings_card(lv_obj_t *parent);

static const settings_page_def_t s_pages[] = {
    {"系统设置", LV_SYMBOL_SETTINGS, render_system_settings_page},
    {"其他设置", LV_SYMBOL_EDIT, render_other_settings_page},
    {"文件管理器", LV_SYMBOL_DIRECTORY, render_file_manager_page},
    {"待机屏保", LV_SYMBOL_IMAGE, render_screensaver_settings_page},
    {"固件升级", LV_SYMBOL_UPLOAD, render_firmware_update_page},
    {"系统信息", LV_SYMBOL_LIST, render_system_info_page},
};

static const board_i2c_device_t s_board_i2c_devices[] = {
    {"触摸控制器", "GT911", "0x14", "电容触摸屏（旧版 Tab5）"},
    {"触摸控制器", "ST7123", "0x55", "电容触摸屏（新版 Tab5 V2，TDDI 集成触控）"},
    {"6轴传感器", "BMI270", "0x68", "加速度计 + 陀螺仪，支持中断唤醒"},
    {"实时时钟", "RX8130CE", "0x32", "RTC 时钟，支持定时中断唤醒"},
    {"电流监测", "INA226", "0x41", "电池电压 / 电流 / 功率实时监测"},
    {"音频 DAC", "ES8388", "0x10", "HiFi 2 通道音频编解码（播放 + 录音）"},
    {"音频 ADC", "ES7210", "0x40", "4 通道 24 位 ADC，AEC 回声消除前端"},
    {"IO扩展器①", "PI4IOE5V6408", "0x43", "WiFi 天线切换、喇叭使能、LCD 复位、触摸复位、摄像头复位、耳机检测"},
    {"IO扩展器②", "PI4IOE5V6408", "0x44", "WiFi 电源、USB 5V 输出、关机脉冲、快充使能、充电状态、充电使能"},
};

#define SETTINGS_PAGE_COUNT ((int)(sizeof(s_pages) / sizeof(s_pages[0])))

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_panel = NULL;
static lv_obj_t *s_content = NULL;
static lv_obj_t *s_menu_buttons[SETTINGS_PAGE_COUNT] = {NULL};
static lv_obj_t *s_menu_icons[SETTINGS_PAGE_COUNT] = {NULL};
static lv_obj_t *s_menu_titles[SETTINGS_PAGE_COUNT] = {NULL};
static lv_obj_t *s_page_containers[SETTINGS_PAGE_COUNT] = {NULL};
static bool s_page_rendered[SETTINGS_PAGE_COUNT] = {false};
static int s_selected_page = 0;
static lv_obj_t *s_ime_input_checks[2] = {NULL};
static lv_obj_t *s_file_web_button = NULL;
static lv_obj_t *s_file_web_button_label = NULL;
static lv_obj_t *s_file_web_status = NULL;
static lv_timer_t *s_file_web_status_timer = NULL;
static lv_obj_t *s_usb_storage_value_label = NULL;
static lv_obj_t *s_usb_storage_button = NULL;
static lv_obj_t *s_usb_storage_button_label = NULL;
static lv_obj_t *s_usb_format_button = NULL;
static lv_obj_t *s_usb_format_button_label = NULL;
static lv_obj_t *s_usb_format_overlay = NULL;
static lv_obj_t *s_usb_storage_status_label = NULL;
static lv_timer_t *s_usb_storage_status_timer = NULL;
static lv_obj_t *s_tf_usb_disk_status_label = NULL;
static lv_obj_t *s_tf_usb_disk_button = NULL;
static lv_obj_t *s_tf_usb_disk_button_label = NULL;
static lv_timer_t *s_tf_usb_disk_status_timer = NULL;
static lv_obj_t *s_ota_status_label = NULL;
static lv_obj_t *s_ota_current_label = NULL;
static lv_obj_t *s_ota_latest_label = NULL;
static lv_obj_t *s_ota_launcher_hint_label = NULL;
static lv_obj_t *s_ota_updater_section = NULL;
static lv_obj_t *s_ota_updater_label = NULL;
static lv_obj_t *s_ota_progress_bar = NULL;
static lv_obj_t *s_ota_progress_label = NULL;
static lv_obj_t *s_ota_changelog_label = NULL;
static lv_obj_t *s_ota_check_button = NULL;
static lv_obj_t *s_ota_check_button_label = NULL;
static lv_obj_t *s_ota_update_button = NULL;
static lv_obj_t *s_ota_update_button_label = NULL;
static lv_obj_t *s_ota_updater_button = NULL;
static lv_obj_t *s_ota_updater_button_label = NULL;
static lv_obj_t *s_ota_confirm_overlay = NULL;
static lv_obj_t *s_ota_updater_confirm_overlay = NULL;
static lv_obj_t *s_ota_wait_overlay = NULL;
static bool s_ota_confirm_shown = false;
static lv_timer_t *s_ota_status_timer = NULL;
static ota_update_status_t s_ota_status_cache;
static lv_obj_t *s_system_info_time_label = NULL;
static lv_timer_t *s_system_info_time_timer = NULL;
static lv_obj_t *s_config_backup_status_label = NULL;
static lv_obj_t *s_config_migration_checkbox = NULL;
static lv_obj_t *s_config_password_overlay = NULL;
static lv_obj_t *s_config_password_ta = NULL;
static lv_obj_t *s_config_password_hint_label = NULL;
static lv_obj_t *s_config_password_keyboard = NULL;
static lv_timer_t *s_config_password_hw_timer = NULL;
static int s_config_password_action = 0;
static lv_obj_t *s_audio_bar_width_ta = NULL;
static lv_obj_t *s_audio_bar_x_ta = NULL;
static lv_obj_t *s_audio_bar_range_label = NULL;
static lv_obj_t *s_audio_bar_status_label = NULL;
static lv_obj_t *s_screensaver_idle_ta = NULL;
static lv_obj_t *s_screensaver_switch_ta = NULL;
static lv_obj_t *s_screensaver_dir_value_label = NULL;
static lv_obj_t *s_screensaver_dir_picker_overlay = NULL;
static lv_obj_t *s_screensaver_dir_picker_path_label = NULL;
static lv_obj_t *s_screensaver_dir_picker_list = NULL;
static lv_obj_t *s_screensaver_dir_picker_status_label = NULL;
static lv_obj_t *s_screensaver_dir_picker_up_button = NULL;
static lv_obj_t *s_screensaver_mode_buttons[3] = {NULL};
static lv_obj_t *s_screensaver_order_buttons[2] = {NULL};
static lv_obj_t *s_screensaver_status_label = NULL;
static lv_obj_t *s_screensaver_pos_tas[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT][2] = {{NULL}};
static lv_obj_t *s_screensaver_font_dropdowns[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT] = {NULL};
static lv_obj_t *s_screensaver_visible_checkboxes[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT] = {NULL};
static lv_obj_t *s_screensaver_component_name_buttons[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT] = {NULL};
static lv_obj_t *s_screensaver_component_name_labels[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT] = {NULL};
static lv_obj_t *s_screensaver_disable_ssh_checkbox = NULL;
static lv_obj_t *s_screensaver_shadow_checkbox = NULL;
static lv_obj_t *s_screensaver_shadow_length_ta = NULL;
static lv_obj_t *s_screensaver_color_overlay = NULL;
static lv_obj_t *s_screensaver_color_preview = NULL;
static lv_obj_t *s_screensaver_color_r_ta = NULL;
static lv_obj_t *s_screensaver_color_g_ta = NULL;
static lv_obj_t *s_screensaver_color_b_ta = NULL;
static app_screensaver_component_t s_screensaver_color_component =
    APP_SETTINGS_SCREENSAVER_COMPONENT_TIME;
static uint32_t s_screensaver_color_pending = 0xFFFFFF;
static char s_screensaver_dir_pending[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN] =
    APP_SETTINGS_DEFAULT_SCREENSAVER_DIR;
static char s_screensaver_dir_picker_path[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN] =
    APP_SETTINGS_DEFAULT_SCREENSAVER_DIR;
static settings_screensaver_dir_entry_t *s_screensaver_dir_picker_entries;
static app_screensaver_mode_t s_screensaver_mode_pending =
    APP_SETTINGS_SCREENSAVER_MODE_OFF;
static app_screensaver_order_t s_screensaver_order_pending =
    APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
static lv_obj_t *s_timezone_status_label = NULL;
static lv_obj_t *s_timezone_buttons[APP_SETTINGS_TIMEZONE_COUNT] = {NULL};
static lv_obj_t *s_timezone_button_labels[APP_SETTINGS_TIMEZONE_COUNT] = {NULL};
static lv_obj_t *s_timezone_dialog_overlay = NULL;
static lv_obj_t *s_timezone_dialog_value_label = NULL;
static int s_timezone_dialog_offset_minutes = 0;

static void close_config_password_dialog(void);
static void close_usb_format_dialog(void);
static void close_timezone_dialog(void);

static bool settings_ensure_screensaver_dir_entries(void)
{
    if (s_screensaver_dir_picker_entries) {
        return true;
    }
    s_screensaver_dir_picker_entries =
        (settings_screensaver_dir_entry_t *)heap_caps_calloc(
            SETTINGS_SCREENSAVER_DIR_PICKER_MAX,
            sizeof(s_screensaver_dir_picker_entries[0]),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_screensaver_dir_picker_entries) {
        ESP_LOGI(TAG,
                 "screensaver dir picker entries allocated in PSRAM: %u bytes",
                 (unsigned)(SETTINGS_SCREENSAVER_DIR_PICKER_MAX *
                            sizeof(s_screensaver_dir_picker_entries[0])));
        return true;
    }
    s_screensaver_dir_picker_entries =
        (settings_screensaver_dir_entry_t *)heap_caps_calloc(
            SETTINGS_SCREENSAVER_DIR_PICKER_MAX,
            sizeof(s_screensaver_dir_picker_entries[0]),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_screensaver_dir_picker_entries) {
        ESP_LOGW(TAG,
                 "screensaver dir picker entries allocated in internal RAM fallback: %u bytes",
                 (unsigned)(SETTINGS_SCREENSAVER_DIR_PICKER_MAX *
                            sizeof(s_screensaver_dir_picker_entries[0])));
        return true;
    }
    ESP_LOGE(TAG,
             "screensaver dir picker entries allocation failed: %u bytes",
             (unsigned)(SETTINGS_SCREENSAVER_DIR_PICKER_MAX *
                        sizeof(s_screensaver_dir_picker_entries[0])));
    return false;
}

/* Private implementation shards. Keep this file as the public UI entry point. */
#include "settings/ui_settings_widgets.inc"
#include "settings/ui_settings_navigation.inc"
#include "settings/ui_settings_backup.inc"
#include "settings/ui_settings_screensaver.inc"
#include "settings/ui_settings_other.inc"
#include "settings/ui_settings_pages.inc"
#include "settings/ui_settings_file_manager.inc"
#include "settings/ui_settings_create.inc"
