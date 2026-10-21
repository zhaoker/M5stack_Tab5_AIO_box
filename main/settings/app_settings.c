#include "settings/app_settings.h"

#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "hw/tab5_keyboard.h"
#include "i18n/app_i18n.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "app_settings";

#define SETTINGS_NVS_NAMESPACE "app_settings"
#define SETTINGS_NVS_BRIGHTNESS_KEY "brightness"
#define SETTINGS_NVS_KEYBOARD_INDICATOR_BRIGHTNESS_KEY "kb_led"
#define SETTINGS_NVS_IME_INPUT_MODE_KEY "ime_input"
#define SETTINGS_NVS_BOOT_AUTO_CHARGE_KEY "boot_chg"
#define SETTINGS_NVS_CHARGE_FAST_KEY "chg_fast"
#define SETTINGS_NVS_IMAGE_EXIF_KEY "img_exif"
#define SETTINGS_NVS_THREE_FINGER_SCREENSHOT_KEY "ss_3finger"
#define SETTINGS_NVS_AUDIO_VOLUME_KEY "aud_vol"
#define SETTINGS_NVS_AUDIO_BAR_WIDTH_KEY "aud_bar_w"
#define SETTINGS_NVS_AUDIO_BAR_X_KEY "aud_bar_x"
#define SETTINGS_NVS_TIMEZONE_KEY "timezone"
#define SETTINGS_NVS_UTC_OFFSET_KEY "utc_offset"
#define SETTINGS_NVS_SCREENSAVER_MODE_KEY "ss_mode"
#define SETTINGS_NVS_SCREENSAVER_IDLE_KEY "ss_idle"
#define SETTINGS_NVS_SCREENSAVER_DISABLE_SSH_KEY "ss_no_ssh"
#define SETTINGS_NVS_SCREENSAVER_DIR_KEY "ss_dir"
#define SETTINGS_NVS_SCREENSAVER_ORDER_KEY "ss_order"
#define SETTINGS_NVS_SCREENSAVER_SWITCH_KEY "ss_switch"
#define SETTINGS_NVS_SCREENSAVER_EXIF_KEY "ss_exif"
#define SETTINGS_NVS_SCREENSAVER_TITLE_X_KEY "ss_title_x"
#define SETTINGS_NVS_SCREENSAVER_TITLE_Y_KEY "ss_title_y"
#define SETTINGS_NVS_SCREENSAVER_TIME_X_KEY "ss_time_x"
#define SETTINGS_NVS_SCREENSAVER_TIME_Y_KEY "ss_time_y"
#define SETTINGS_NVS_SCREENSAVER_SECOND_X_KEY "ss_sec_x"
#define SETTINGS_NVS_SCREENSAVER_SECOND_Y_KEY "ss_sec_y"
#define SETTINGS_NVS_SCREENSAVER_DATE_X_KEY "ss_date_x"
#define SETTINGS_NVS_SCREENSAVER_DATE_Y_KEY "ss_date_y"
#define SETTINGS_NVS_SCREENSAVER_LUNAR_X_KEY "ss_lunar_x"
#define SETTINGS_NVS_SCREENSAVER_LUNAR_Y_KEY "ss_lunar_y"
#define SETTINGS_NVS_SCREENSAVER_TITLE_FONT_KEY "ss_title_f"
#define SETTINGS_NVS_SCREENSAVER_TIME_FONT_KEY "ss_time_f"
#define SETTINGS_NVS_SCREENSAVER_SECOND_FONT_KEY "ss_sec_f"
#define SETTINGS_NVS_SCREENSAVER_DATE_FONT_KEY "ss_date_f"
#define SETTINGS_NVS_SCREENSAVER_LUNAR_FONT_KEY "ss_lunar_f"
#define SETTINGS_NVS_SCREENSAVER_SHADOW_ENABLED_KEY "ss_shdw_e"
#define SETTINGS_NVS_SCREENSAVER_SHADOW_LENGTH_KEY "ss_shdw_l"

static int s_brightness = APP_SETTINGS_DEFAULT_BRIGHTNESS;
static int s_keyboard_indicator_brightness = APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS;
static int s_audio_volume = APP_SETTINGS_DEFAULT_AUDIO_VOLUME;
static int s_audio_bar_width = APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH;
static int s_audio_bar_x = APP_SETTINGS_DEFAULT_AUDIO_BAR_X;
static app_ime_input_mode_t s_ime_input_mode = APP_SETTINGS_IME_INPUT_MODE_PHRASE;
static app_timezone_t s_timezone = APP_SETTINGS_TIMEZONE_BEIJING;
static int s_utc_offset_minutes = 0;
static bool s_timezone_configured;
static bool s_boot_auto_charge_enabled = APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED;
static bool s_charge_fast_enabled = APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED;
static bool s_image_exif_enabled = APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED;
static bool s_three_finger_screenshot_enabled =
    APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED;
static app_screensaver_mode_t s_screensaver_mode = APP_SETTINGS_SCREENSAVER_MODE_OFF;
static int s_screensaver_idle_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_IDLE_MINUTES;
static char s_screensaver_wallpaper_dir[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN] =
    APP_SETTINGS_DEFAULT_SCREENSAVER_DIR;
static app_screensaver_order_t s_screensaver_order =
    APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
static int s_screensaver_switch_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_SWITCH_MINUTES;
static bool s_screensaver_show_exif_title = true;
static bool s_screensaver_disable_during_ssh =
    APP_SETTINGS_DEFAULT_SCREENSAVER_DISABLE_DURING_SSH;
static int s_screensaver_title_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_X;
static int s_screensaver_title_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_Y;
static int s_screensaver_time_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X;
static int s_screensaver_time_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y;
static int s_screensaver_second_x = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_X;
static int s_screensaver_second_y = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_Y;
static int s_screensaver_date_x = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_X;
static int s_screensaver_date_y = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_Y;
static int s_screensaver_lunar_x = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_X;
static int s_screensaver_lunar_y = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_Y;
static int s_screensaver_title_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_FONT_SIZE;
static int s_screensaver_time_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_FONT_SIZE;
static int s_screensaver_second_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_FONT_SIZE;
static int s_screensaver_date_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_FONT_SIZE;
static int s_screensaver_lunar_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_FONT_SIZE;
static bool s_screensaver_text_shadow_enabled =
    APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_ENABLED;
static int s_screensaver_text_shadow_length =
    APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_LENGTH;
static bool s_screensaver_component_visible[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT] = {
    APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_VISIBLE,
    APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_VISIBLE,
    APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_VISIBLE,
    APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_VISIBLE,
    APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_VISIBLE,
};
static uint32_t s_screensaver_component_color[APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT] = {
    APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_COLOR,
    APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_COLOR,
    APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_COLOR,
    APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_COLOR,
    APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_COLOR,
};

static void app_settings_screensaver_set_default_component_state(void);
static void app_settings_screensaver_load_component_state(nvs_handle_t h);

static int clamp_brightness(int value)
{
    if (value < APP_SETTINGS_MIN_BRIGHTNESS) {
        return APP_SETTINGS_MIN_BRIGHTNESS;
    }
    if (value > APP_SETTINGS_MAX_BRIGHTNESS) {
        return APP_SETTINGS_MAX_BRIGHTNESS;
    }
    return value;
}

static app_ime_input_mode_t clamp_ime_input_mode(uint8_t value)
{
    if (value == APP_SETTINGS_IME_INPUT_MODE_PHRASE) {
        return APP_SETTINGS_IME_INPUT_MODE_PHRASE;
    }
    return APP_SETTINGS_IME_INPUT_MODE_CHARACTER;
}

static app_timezone_t clamp_timezone(uint8_t value)
{
    if (value < APP_SETTINGS_TIMEZONE_COUNT) {
        return (app_timezone_t)value;
    }
    return APP_SETTINGS_TIMEZONE_BEIJING;
}

static int clamp_utc_offset_minutes(int value)
{
    if (value < APP_SETTINGS_UTC_OFFSET_MIN_MINUTES) {
        return APP_SETTINGS_UTC_OFFSET_MIN_MINUTES;
    }
    if (value > APP_SETTINGS_UTC_OFFSET_MAX_MINUTES) {
        return APP_SETTINGS_UTC_OFFSET_MAX_MINUTES;
    }
    return (value / APP_SETTINGS_UTC_OFFSET_STEP_MINUTES) *
           APP_SETTINGS_UTC_OFFSET_STEP_MINUTES;
}

static bool utc_offset_minutes_is_valid(int value)
{
    return value >= APP_SETTINGS_UTC_OFFSET_MIN_MINUTES &&
           value <= APP_SETTINGS_UTC_OFFSET_MAX_MINUTES &&
           (value % APP_SETTINGS_UTC_OFFSET_STEP_MINUTES) == 0;
}

static app_timezone_t default_timezone_for_current_language(void)
{
    return app_i18n_get_language() == APP_LANGUAGE_EN
               ? APP_SETTINGS_TIMEZONE_NEW_YORK
               : APP_SETTINGS_TIMEZONE_BEIJING;
}

static int clamp_keyboard_indicator_brightness(int value)
{
    if (value < APP_SETTINGS_MIN_KEYBOARD_INDICATOR_BRIGHTNESS) {
        return APP_SETTINGS_MIN_KEYBOARD_INDICATOR_BRIGHTNESS;
    }
    if (value > APP_SETTINGS_MAX_KEYBOARD_INDICATOR_BRIGHTNESS) {
        return APP_SETTINGS_MAX_KEYBOARD_INDICATOR_BRIGHTNESS;
    }
    return value;
}

static int clamp_audio_volume(int value)
{
    if (value < APP_SETTINGS_MIN_AUDIO_VOLUME) {
        return APP_SETTINGS_MIN_AUDIO_VOLUME;
    }
    if (value > APP_SETTINGS_MAX_AUDIO_VOLUME) {
        return APP_SETTINGS_MAX_AUDIO_VOLUME;
    }
    return value;
}

static int clamp_audio_bar_width(int value)
{
    if (value < APP_SETTINGS_MIN_AUDIO_BAR_WIDTH) {
        return APP_SETTINGS_MIN_AUDIO_BAR_WIDTH;
    }
    if (value > APP_SETTINGS_MAX_AUDIO_BAR_WIDTH) {
        return APP_SETTINGS_MAX_AUDIO_BAR_WIDTH;
    }
    return value;
}

int app_settings_get_audio_bar_max_x_for_width(int width_px)
{
    int width = clamp_audio_bar_width(width_px);
    int max_x = APP_SETTINGS_AUDIO_BAR_SCREEN_W - width;
    return max_x > 0 ? max_x : 0;
}

static int clamp_audio_bar_x(int value, int width_px)
{
    int max_x = app_settings_get_audio_bar_max_x_for_width(width_px);
    if (value < 0) {
        return 0;
    }
    if (value > max_x) {
        return max_x;
    }
    return value;
}

static app_screensaver_mode_t clamp_screensaver_mode(uint8_t value)
{
    if (value == APP_SETTINGS_SCREENSAVER_MODE_BLANK ||
        value == APP_SETTINGS_SCREENSAVER_MODE_WALLPAPER) {
        return (app_screensaver_mode_t)value;
    }
    return APP_SETTINGS_SCREENSAVER_MODE_OFF;
}

static app_screensaver_order_t clamp_screensaver_order(uint8_t value)
{
    return value == APP_SETTINGS_SCREENSAVER_ORDER_RANDOM
               ? APP_SETTINGS_SCREENSAVER_ORDER_RANDOM
               : APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
}

static int clamp_screensaver_idle_minutes(int value)
{
    if (value < APP_SETTINGS_MIN_SCREENSAVER_IDLE_MINUTES) {
        return APP_SETTINGS_MIN_SCREENSAVER_IDLE_MINUTES;
    }
    if (value > APP_SETTINGS_MAX_SCREENSAVER_IDLE_MINUTES) {
        return APP_SETTINGS_MAX_SCREENSAVER_IDLE_MINUTES;
    }
    return value;
}

static int clamp_screensaver_switch_minutes(int value)
{
    if (value < APP_SETTINGS_MIN_SCREENSAVER_SWITCH_MINUTES) {
        return APP_SETTINGS_MIN_SCREENSAVER_SWITCH_MINUTES;
    }
    if (value > APP_SETTINGS_MAX_SCREENSAVER_SWITCH_MINUTES) {
        return APP_SETTINGS_MAX_SCREENSAVER_SWITCH_MINUTES;
    }
    return value;
}

static int clamp_screensaver_time_x(int value)
{
    if (value < APP_SETTINGS_SCREENSAVER_COORD_MIN) {
        return APP_SETTINGS_SCREENSAVER_COORD_MIN;
    }
    if (value > APP_SETTINGS_SCREENSAVER_COORD_MAX) {
        return APP_SETTINGS_SCREENSAVER_COORD_MAX;
    }
    return value;
}

static int clamp_screensaver_time_y(int value)
{
    if (value < APP_SETTINGS_SCREENSAVER_COORD_MIN) {
        return APP_SETTINGS_SCREENSAVER_COORD_MIN;
    }
    if (value > APP_SETTINGS_SCREENSAVER_COORD_MAX) {
        return APP_SETTINGS_SCREENSAVER_COORD_MAX;
    }
    return value;
}

static int clamp_screensaver_font_size(int value)
{
    switch (value) {
    case APP_SETTINGS_SCREENSAVER_FONT_20:
    case APP_SETTINGS_SCREENSAVER_FONT_40:
    case APP_SETTINGS_SCREENSAVER_FONT_60:
    case APP_SETTINGS_SCREENSAVER_FONT_80:
    case APP_SETTINGS_SCREENSAVER_FONT_120:
    case APP_SETTINGS_SCREENSAVER_FONT_160:
    case APP_SETTINGS_SCREENSAVER_FONT_200:
        return value;
    default:
        return APP_SETTINGS_SCREENSAVER_FONT_80;
    }
}

static bool screensaver_font_size_is_valid(int value)
{
    return clamp_screensaver_font_size(value) == value;
}

static int clamp_screensaver_shadow_length(int value)
{
    if (value < APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MIN) {
        return APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MIN;
    }
    if (value > APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MAX) {
        return APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MAX;
    }
    return value;
}

static void set_default_screensaver_settings(void)
{
    s_screensaver_mode = APP_SETTINGS_SCREENSAVER_MODE_OFF;
    s_screensaver_idle_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_IDLE_MINUTES;
    strlcpy(s_screensaver_wallpaper_dir,
            APP_SETTINGS_DEFAULT_SCREENSAVER_DIR,
            sizeof(s_screensaver_wallpaper_dir));
    s_screensaver_order = APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
    s_screensaver_switch_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_SWITCH_MINUTES;
    s_screensaver_show_exif_title = true;
    s_screensaver_disable_during_ssh =
        APP_SETTINGS_DEFAULT_SCREENSAVER_DISABLE_DURING_SSH;
    s_screensaver_title_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_X;
    s_screensaver_title_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_Y;
    s_screensaver_time_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X;
    s_screensaver_time_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y;
    s_screensaver_second_x = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_X;
    s_screensaver_second_y = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_Y;
    s_screensaver_date_x = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_X;
    s_screensaver_date_y = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_Y;
    s_screensaver_lunar_x = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_X;
    s_screensaver_lunar_y = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_Y;
    s_screensaver_title_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_FONT_SIZE;
    s_screensaver_time_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_FONT_SIZE;
    s_screensaver_second_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_FONT_SIZE;
    s_screensaver_date_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_FONT_SIZE;
    s_screensaver_lunar_font_size = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_FONT_SIZE;
    s_screensaver_text_shadow_enabled =
        APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_ENABLED;
    s_screensaver_text_shadow_length =
        APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_LENGTH;
    app_settings_screensaver_set_default_component_state();
}

static esp_err_t save_u8_setting(const char *key, uint8_t value, const char *name)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for %s save failed: %s", name, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save %s failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t save_i32_setting(const char *key, int32_t value, const char *name)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for %s save failed: %s", name, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_i32(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save %s failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t save_string_setting(const char *key, const char *value, const char *name)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for %s save failed: %s", name, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, key, value ? value : "");
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save %s failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t save_audio_bar_geometry_locked(int width_px, int x_px)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for audio bar geometry save failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_i32(h, SETTINGS_NVS_AUDIO_BAR_WIDTH_KEY, width_px);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, SETTINGS_NVS_AUDIO_BAR_X_KEY, x_px);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save audio bar geometry failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_settings_init(void)
{
    nvs_handle_t h;
    uint8_t saved = APP_SETTINGS_DEFAULT_BRIGHTNESS;
    uint8_t saved_keyboard_indicator_brightness = APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS;
    uint8_t saved_audio_volume = APP_SETTINGS_DEFAULT_AUDIO_VOLUME;
    uint8_t saved_ime_input_mode = APP_SETTINGS_IME_INPUT_MODE_PHRASE;
    uint8_t saved_timezone = APP_SETTINGS_TIMEZONE_BEIJING;
    int32_t saved_utc_offset_minutes = 0;
    uint8_t saved_boot_auto_charge = APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED ? 1 : 0;
    uint8_t saved_charge_fast = APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED ? 1 : 0;
    uint8_t saved_image_exif = APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED ? 1 : 0;
    uint8_t saved_three_finger_screenshot =
        APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED ? 1 : 0;
    uint8_t saved_screensaver_mode = APP_SETTINGS_SCREENSAVER_MODE_OFF;
    uint8_t saved_screensaver_order = APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
    uint8_t saved_screensaver_exif = 1;
    uint8_t saved_screensaver_disable_ssh =
        APP_SETTINGS_DEFAULT_SCREENSAVER_DISABLE_DURING_SSH ? 1 : 0;
    uint8_t saved_screensaver_shadow_enabled =
        APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_ENABLED ? 1 : 0;
    int32_t saved_audio_bar_width = APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH;
    int32_t saved_audio_bar_x = APP_SETTINGS_DEFAULT_AUDIO_BAR_X;
    int32_t saved_screensaver_idle = APP_SETTINGS_DEFAULT_SCREENSAVER_IDLE_MINUTES;
    int32_t saved_screensaver_switch = APP_SETTINGS_DEFAULT_SCREENSAVER_SWITCH_MINUTES;
    int32_t saved_screensaver_title_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_X;
    int32_t saved_screensaver_title_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_Y;
    int32_t saved_screensaver_time_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X;
    int32_t saved_screensaver_time_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y;
    int32_t saved_screensaver_second_x = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_X;
    int32_t saved_screensaver_second_y = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_Y;
    int32_t saved_screensaver_date_x = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_X;
    int32_t saved_screensaver_date_y = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_Y;
    int32_t saved_screensaver_lunar_x = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_X;
    int32_t saved_screensaver_lunar_y = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_Y;
    int32_t saved_screensaver_title_font = APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_FONT_SIZE;
    int32_t saved_screensaver_time_font = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_FONT_SIZE;
    int32_t saved_screensaver_second_font = APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_FONT_SIZE;
    int32_t saved_screensaver_date_font = APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_FONT_SIZE;
    int32_t saved_screensaver_lunar_font = APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_FONT_SIZE;
    int32_t saved_screensaver_shadow_length =
        APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_LENGTH;
    char saved_screensaver_dir[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN] =
        APP_SETTINGS_DEFAULT_SCREENSAVER_DIR;
    size_t saved_screensaver_dir_len = sizeof(saved_screensaver_dir);
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_brightness = APP_SETTINGS_DEFAULT_BRIGHTNESS;
        s_keyboard_indicator_brightness = APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS;
        s_audio_volume = APP_SETTINGS_DEFAULT_AUDIO_VOLUME;
        s_audio_bar_width = APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH;
        s_audio_bar_x = APP_SETTINGS_DEFAULT_AUDIO_BAR_X;
        s_ime_input_mode = APP_SETTINGS_IME_INPUT_MODE_PHRASE;
        s_timezone = APP_SETTINGS_TIMEZONE_BEIJING;
        s_utc_offset_minutes = 0;
        s_timezone_configured = false;
        s_boot_auto_charge_enabled = APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED;
        s_charge_fast_enabled = APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED;
        s_image_exif_enabled = APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED;
        s_three_finger_screenshot_enabled =
            APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED;
        set_default_screensaver_settings();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        s_brightness = APP_SETTINGS_DEFAULT_BRIGHTNESS;
        s_keyboard_indicator_brightness = APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS;
        s_audio_volume = APP_SETTINGS_DEFAULT_AUDIO_VOLUME;
        s_audio_bar_width = APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH;
        s_audio_bar_x = APP_SETTINGS_DEFAULT_AUDIO_BAR_X;
        s_ime_input_mode = APP_SETTINGS_IME_INPUT_MODE_PHRASE;
        s_timezone = APP_SETTINGS_TIMEZONE_BEIJING;
        s_utc_offset_minutes = 0;
        s_timezone_configured = false;
        s_boot_auto_charge_enabled = APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED;
        s_charge_fast_enabled = APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED;
        s_image_exif_enabled = APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED;
        s_three_finger_screenshot_enabled =
            APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED;
        set_default_screensaver_settings();
        return err;
    }

    err = nvs_get_u8(h, SETTINGS_NVS_BRIGHTNESS_KEY, &saved);
    esp_err_t kb_led_err = nvs_get_u8(h,
                                      SETTINGS_NVS_KEYBOARD_INDICATOR_BRIGHTNESS_KEY,
                                      &saved_keyboard_indicator_brightness);
    esp_err_t ime_err = nvs_get_u8(h, SETTINGS_NVS_IME_INPUT_MODE_KEY, &saved_ime_input_mode);
    esp_err_t timezone_err = nvs_get_u8(h, SETTINGS_NVS_TIMEZONE_KEY, &saved_timezone);
    esp_err_t utc_offset_err =
        nvs_get_i32(h, SETTINGS_NVS_UTC_OFFSET_KEY, &saved_utc_offset_minutes);
    esp_err_t audio_volume_err = nvs_get_u8(h, SETTINGS_NVS_AUDIO_VOLUME_KEY, &saved_audio_volume);
    esp_err_t audio_bar_width_err =
        nvs_get_i32(h, SETTINGS_NVS_AUDIO_BAR_WIDTH_KEY, &saved_audio_bar_width);
    esp_err_t audio_bar_x_err = nvs_get_i32(h, SETTINGS_NVS_AUDIO_BAR_X_KEY, &saved_audio_bar_x);
    esp_err_t boot_charge_err = nvs_get_u8(h,
                                           SETTINGS_NVS_BOOT_AUTO_CHARGE_KEY,
                                           &saved_boot_auto_charge);
    esp_err_t charge_fast_err = nvs_get_u8(h, SETTINGS_NVS_CHARGE_FAST_KEY, &saved_charge_fast);
    esp_err_t image_exif_err = nvs_get_u8(h, SETTINGS_NVS_IMAGE_EXIF_KEY, &saved_image_exif);
    esp_err_t three_finger_screenshot_err =
        nvs_get_u8(h,
                   SETTINGS_NVS_THREE_FINGER_SCREENSHOT_KEY,
                   &saved_three_finger_screenshot);
    esp_err_t screensaver_mode_err =
        nvs_get_u8(h, SETTINGS_NVS_SCREENSAVER_MODE_KEY, &saved_screensaver_mode);
    esp_err_t screensaver_idle_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_IDLE_KEY, &saved_screensaver_idle);
    esp_err_t screensaver_dir_err =
        nvs_get_str(h,
                    SETTINGS_NVS_SCREENSAVER_DIR_KEY,
                    saved_screensaver_dir,
                    &saved_screensaver_dir_len);
    esp_err_t screensaver_order_err =
        nvs_get_u8(h, SETTINGS_NVS_SCREENSAVER_ORDER_KEY, &saved_screensaver_order);
    esp_err_t screensaver_switch_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_SWITCH_KEY, &saved_screensaver_switch);
    esp_err_t screensaver_exif_err =
        nvs_get_u8(h, SETTINGS_NVS_SCREENSAVER_EXIF_KEY, &saved_screensaver_exif);
    esp_err_t screensaver_disable_ssh_err =
        nvs_get_u8(h,
                   SETTINGS_NVS_SCREENSAVER_DISABLE_SSH_KEY,
                   &saved_screensaver_disable_ssh);
    esp_err_t screensaver_title_x_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_TITLE_X_KEY, &saved_screensaver_title_x);
    esp_err_t screensaver_title_y_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_TITLE_Y_KEY, &saved_screensaver_title_y);
    esp_err_t screensaver_time_x_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_TIME_X_KEY, &saved_screensaver_time_x);
    esp_err_t screensaver_time_y_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_TIME_Y_KEY, &saved_screensaver_time_y);
    esp_err_t screensaver_second_x_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_SECOND_X_KEY, &saved_screensaver_second_x);
    esp_err_t screensaver_second_y_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_SECOND_Y_KEY, &saved_screensaver_second_y);
    esp_err_t screensaver_date_x_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_DATE_X_KEY, &saved_screensaver_date_x);
    esp_err_t screensaver_date_y_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_DATE_Y_KEY, &saved_screensaver_date_y);
    esp_err_t screensaver_lunar_x_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_LUNAR_X_KEY, &saved_screensaver_lunar_x);
    esp_err_t screensaver_lunar_y_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_LUNAR_Y_KEY, &saved_screensaver_lunar_y);
    esp_err_t screensaver_title_font_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_TITLE_FONT_KEY, &saved_screensaver_title_font);
    esp_err_t screensaver_time_font_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_TIME_FONT_KEY, &saved_screensaver_time_font);
    esp_err_t screensaver_second_font_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_SECOND_FONT_KEY, &saved_screensaver_second_font);
    esp_err_t screensaver_date_font_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_DATE_FONT_KEY, &saved_screensaver_date_font);
    esp_err_t screensaver_lunar_font_err =
        nvs_get_i32(h, SETTINGS_NVS_SCREENSAVER_LUNAR_FONT_KEY, &saved_screensaver_lunar_font);
    esp_err_t screensaver_shadow_enabled_err =
        nvs_get_u8(h,
                   SETTINGS_NVS_SCREENSAVER_SHADOW_ENABLED_KEY,
                   &saved_screensaver_shadow_enabled);
    esp_err_t screensaver_shadow_length_err =
        nvs_get_i32(h,
                    SETTINGS_NVS_SCREENSAVER_SHADOW_LENGTH_KEY,
                    &saved_screensaver_shadow_length);
    app_settings_screensaver_load_component_state(h);
    nvs_close(h);

    if (three_finger_screenshot_err == ESP_OK) {
        s_three_finger_screenshot_enabled = saved_three_finger_screenshot != 0;
    } else if (three_finger_screenshot_err == ESP_ERR_NVS_NOT_FOUND) {
        s_three_finger_screenshot_enabled =
            APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED;
    } else {
        ESP_LOGW(TAG,
                 "load three finger screenshot setting failed: %s",
                 esp_err_to_name(three_finger_screenshot_err));
        s_three_finger_screenshot_enabled =
            APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED;
    }

    if (image_exif_err == ESP_OK) {
        s_image_exif_enabled = saved_image_exif != 0;
    } else if (image_exif_err == ESP_ERR_NVS_NOT_FOUND) {
        s_image_exif_enabled = APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED;
    } else {
        ESP_LOGW(TAG, "load image exif setting failed: %s", esp_err_to_name(image_exif_err));
        s_image_exif_enabled = APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED;
    }

    if (screensaver_mode_err == ESP_OK) {
        s_screensaver_mode = clamp_screensaver_mode(saved_screensaver_mode);
    } else if (screensaver_mode_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_mode = APP_SETTINGS_SCREENSAVER_MODE_OFF;
    } else {
        ESP_LOGW(TAG, "load screensaver mode failed: %s", esp_err_to_name(screensaver_mode_err));
        s_screensaver_mode = APP_SETTINGS_SCREENSAVER_MODE_OFF;
    }
    if (screensaver_idle_err == ESP_OK) {
        s_screensaver_idle_minutes =
            clamp_screensaver_idle_minutes((int)saved_screensaver_idle);
    } else if (screensaver_idle_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_idle_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_IDLE_MINUTES;
    } else {
        ESP_LOGW(TAG, "load screensaver idle failed: %s", esp_err_to_name(screensaver_idle_err));
        s_screensaver_idle_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_IDLE_MINUTES;
    }
    if (screensaver_dir_err == ESP_OK && saved_screensaver_dir[0]) {
        strlcpy(s_screensaver_wallpaper_dir,
                saved_screensaver_dir,
                sizeof(s_screensaver_wallpaper_dir));
    } else if (screensaver_dir_err == ESP_ERR_NVS_NOT_FOUND) {
        strlcpy(s_screensaver_wallpaper_dir,
                APP_SETTINGS_DEFAULT_SCREENSAVER_DIR,
                sizeof(s_screensaver_wallpaper_dir));
    } else {
        ESP_LOGW(TAG, "load screensaver dir failed: %s", esp_err_to_name(screensaver_dir_err));
        strlcpy(s_screensaver_wallpaper_dir,
                APP_SETTINGS_DEFAULT_SCREENSAVER_DIR,
                sizeof(s_screensaver_wallpaper_dir));
    }
    if (screensaver_order_err == ESP_OK) {
        s_screensaver_order = clamp_screensaver_order(saved_screensaver_order);
    } else if (screensaver_order_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_order = APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
    } else {
        ESP_LOGW(TAG, "load screensaver order failed: %s", esp_err_to_name(screensaver_order_err));
        s_screensaver_order = APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
    }
    if (screensaver_switch_err == ESP_OK) {
        s_screensaver_switch_minutes =
            clamp_screensaver_switch_minutes((int)saved_screensaver_switch);
    } else if (screensaver_switch_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_switch_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_SWITCH_MINUTES;
    } else {
        ESP_LOGW(TAG,
                 "load screensaver switch failed: %s",
                 esp_err_to_name(screensaver_switch_err));
        s_screensaver_switch_minutes = APP_SETTINGS_DEFAULT_SCREENSAVER_SWITCH_MINUTES;
    }
    if (screensaver_exif_err == ESP_OK) {
        s_screensaver_show_exif_title = saved_screensaver_exif != 0;
    } else if (screensaver_exif_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_show_exif_title = true;
    } else {
        ESP_LOGW(TAG, "load screensaver exif failed: %s", esp_err_to_name(screensaver_exif_err));
        s_screensaver_show_exif_title = true;
    }
    if (screensaver_disable_ssh_err == ESP_OK) {
        s_screensaver_disable_during_ssh = saved_screensaver_disable_ssh != 0;
    } else if (screensaver_disable_ssh_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_disable_during_ssh =
            APP_SETTINGS_DEFAULT_SCREENSAVER_DISABLE_DURING_SSH;
    } else {
        ESP_LOGW(TAG,
                 "load screensaver ssh disable failed: %s",
                 esp_err_to_name(screensaver_disable_ssh_err));
        s_screensaver_disable_during_ssh =
            APP_SETTINGS_DEFAULT_SCREENSAVER_DISABLE_DURING_SSH;
    }
    s_screensaver_title_x =
        screensaver_title_x_err == ESP_OK
            ? clamp_screensaver_time_x((int)saved_screensaver_title_x)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_X;
    s_screensaver_title_y =
        screensaver_title_y_err == ESP_OK
            ? clamp_screensaver_time_y((int)saved_screensaver_title_y)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_Y;
    if (screensaver_time_x_err == ESP_OK) {
        s_screensaver_time_x = clamp_screensaver_time_x((int)saved_screensaver_time_x);
    } else if (screensaver_time_x_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_time_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X;
    } else {
        ESP_LOGW(TAG,
                 "load screensaver time x failed: %s",
                 esp_err_to_name(screensaver_time_x_err));
        s_screensaver_time_x = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X;
    }
    if (screensaver_time_y_err == ESP_OK) {
        s_screensaver_time_y = clamp_screensaver_time_y((int)saved_screensaver_time_y);
    } else if (screensaver_time_y_err == ESP_ERR_NVS_NOT_FOUND) {
        s_screensaver_time_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y;
    } else {
        ESP_LOGW(TAG,
                 "load screensaver time y failed: %s",
                 esp_err_to_name(screensaver_time_y_err));
        s_screensaver_time_y = APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y;
    }
    s_screensaver_second_x =
        screensaver_second_x_err == ESP_OK
            ? clamp_screensaver_time_x((int)saved_screensaver_second_x)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_X;
    s_screensaver_second_y =
        screensaver_second_y_err == ESP_OK
            ? clamp_screensaver_time_y((int)saved_screensaver_second_y)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_Y;
    s_screensaver_date_x =
        screensaver_date_x_err == ESP_OK
            ? clamp_screensaver_time_x((int)saved_screensaver_date_x)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_X;
    s_screensaver_date_y =
        screensaver_date_y_err == ESP_OK
            ? clamp_screensaver_time_y((int)saved_screensaver_date_y)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_Y;
    s_screensaver_lunar_x =
        screensaver_lunar_x_err == ESP_OK
            ? clamp_screensaver_time_x((int)saved_screensaver_lunar_x)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_X;
    s_screensaver_lunar_y =
        screensaver_lunar_y_err == ESP_OK
            ? clamp_screensaver_time_y((int)saved_screensaver_lunar_y)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_Y;
    s_screensaver_title_font_size =
        screensaver_title_font_err == ESP_OK &&
                screensaver_font_size_is_valid((int)saved_screensaver_title_font)
            ? (int)saved_screensaver_title_font
            : APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_FONT_SIZE;
    s_screensaver_time_font_size =
        screensaver_time_font_err == ESP_OK &&
                screensaver_font_size_is_valid((int)saved_screensaver_time_font)
            ? (int)saved_screensaver_time_font
            : APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_FONT_SIZE;
    s_screensaver_second_font_size =
        screensaver_second_font_err == ESP_OK &&
                screensaver_font_size_is_valid((int)saved_screensaver_second_font)
            ? (int)saved_screensaver_second_font
            : APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_FONT_SIZE;
    s_screensaver_date_font_size =
        screensaver_date_font_err == ESP_OK &&
                screensaver_font_size_is_valid((int)saved_screensaver_date_font)
            ? (int)saved_screensaver_date_font
            : APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_FONT_SIZE;
    s_screensaver_lunar_font_size =
        screensaver_lunar_font_err == ESP_OK &&
                screensaver_font_size_is_valid((int)saved_screensaver_lunar_font)
            ? (int)saved_screensaver_lunar_font
            : APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_FONT_SIZE;
    s_screensaver_text_shadow_enabled =
        screensaver_shadow_enabled_err == ESP_OK
            ? saved_screensaver_shadow_enabled != 0
            : APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_ENABLED;
    s_screensaver_text_shadow_length =
        screensaver_shadow_length_err == ESP_OK
            ? clamp_screensaver_shadow_length((int)saved_screensaver_shadow_length)
            : APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_LENGTH;

    if (boot_charge_err == ESP_OK) {
        s_boot_auto_charge_enabled = saved_boot_auto_charge != 0;
    } else if (boot_charge_err == ESP_ERR_NVS_NOT_FOUND) {
        s_boot_auto_charge_enabled = APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED;
    } else {
        ESP_LOGW(TAG, "load boot auto charge failed: %s", esp_err_to_name(boot_charge_err));
        s_boot_auto_charge_enabled = APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED;
    }

    if (charge_fast_err == ESP_OK) {
        s_charge_fast_enabled = saved_charge_fast != 0;
    } else if (charge_fast_err == ESP_ERR_NVS_NOT_FOUND) {
        s_charge_fast_enabled = APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED;
    } else {
        ESP_LOGW(TAG, "load charge speed failed: %s", esp_err_to_name(charge_fast_err));
        s_charge_fast_enabled = APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED;
    }

    if (ime_err == ESP_OK) {
        s_ime_input_mode = clamp_ime_input_mode(saved_ime_input_mode);
    } else if (ime_err == ESP_ERR_NVS_NOT_FOUND) {
        s_ime_input_mode = APP_SETTINGS_IME_INPUT_MODE_PHRASE;
    } else {
        ESP_LOGW(TAG, "load ime input mode failed: %s", esp_err_to_name(ime_err));
        s_ime_input_mode = APP_SETTINGS_IME_INPUT_MODE_PHRASE;
    }

    if (timezone_err == ESP_OK) {
        s_timezone = clamp_timezone(saved_timezone);
        s_timezone_configured = true;
    } else if (timezone_err == ESP_ERR_NVS_NOT_FOUND) {
        s_timezone = APP_SETTINGS_TIMEZONE_BEIJING;
        s_timezone_configured = false;
    } else {
        ESP_LOGW(TAG, "load timezone failed: %s", esp_err_to_name(timezone_err));
        s_timezone = APP_SETTINGS_TIMEZONE_BEIJING;
        s_timezone_configured = false;
    }
    if (utc_offset_err == ESP_OK) {
        s_utc_offset_minutes = clamp_utc_offset_minutes((int)saved_utc_offset_minutes);
    } else if (utc_offset_err == ESP_ERR_NVS_NOT_FOUND) {
        s_utc_offset_minutes = 0;
    } else {
        ESP_LOGW(TAG, "load UTC offset failed: %s", esp_err_to_name(utc_offset_err));
        s_utc_offset_minutes = 0;
    }

    if (audio_volume_err == ESP_OK) {
        s_audio_volume = clamp_audio_volume((int)saved_audio_volume);
    } else if (audio_volume_err == ESP_ERR_NVS_NOT_FOUND) {
        s_audio_volume = APP_SETTINGS_DEFAULT_AUDIO_VOLUME;
    } else {
        ESP_LOGW(TAG, "load audio volume failed: %s", esp_err_to_name(audio_volume_err));
        s_audio_volume = APP_SETTINGS_DEFAULT_AUDIO_VOLUME;
    }

    if (audio_bar_width_err == ESP_OK) {
        s_audio_bar_width = clamp_audio_bar_width((int)saved_audio_bar_width);
    } else if (audio_bar_width_err == ESP_ERR_NVS_NOT_FOUND) {
        s_audio_bar_width = APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH;
    } else {
        ESP_LOGW(TAG, "load audio bar width failed: %s", esp_err_to_name(audio_bar_width_err));
        s_audio_bar_width = APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH;
    }

    if (audio_bar_x_err == ESP_OK) {
        s_audio_bar_x = clamp_audio_bar_x((int)saved_audio_bar_x, s_audio_bar_width);
    } else if (audio_bar_x_err == ESP_ERR_NVS_NOT_FOUND) {
        s_audio_bar_x = clamp_audio_bar_x(APP_SETTINGS_DEFAULT_AUDIO_BAR_X, s_audio_bar_width);
    } else {
        ESP_LOGW(TAG, "load audio bar x failed: %s", esp_err_to_name(audio_bar_x_err));
        s_audio_bar_x = clamp_audio_bar_x(APP_SETTINGS_DEFAULT_AUDIO_BAR_X, s_audio_bar_width);
    }

    if (kb_led_err == ESP_OK) {
        s_keyboard_indicator_brightness = clamp_keyboard_indicator_brightness(
            (int)saved_keyboard_indicator_brightness);
    } else if (kb_led_err == ESP_ERR_NVS_NOT_FOUND) {
        s_keyboard_indicator_brightness = APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS;
    } else {
        ESP_LOGW(TAG, "load keyboard indicator brightness failed: %s", esp_err_to_name(kb_led_err));
        s_keyboard_indicator_brightness = APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_brightness = APP_SETTINGS_DEFAULT_BRIGHTNESS;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "load brightness failed: %s", esp_err_to_name(err));
        s_brightness = APP_SETTINGS_DEFAULT_BRIGHTNESS;
        return err;
    }

    s_brightness = clamp_brightness((int)saved);
    return ESP_OK;
}

void app_settings_ensure_timezone_default_for_language(void)
{
    if (!s_timezone_configured) {
        s_timezone = default_timezone_for_current_language();
    }
}

int app_settings_get_brightness(void)
{
    return s_brightness;
}

esp_err_t app_settings_preview_brightness(int brightness_percent)
{
    return bsp_display_brightness_set(clamp_brightness(brightness_percent));
}

esp_err_t app_settings_set_brightness(int brightness_percent)
{
    nvs_handle_t h;
    esp_err_t err;

    s_brightness = clamp_brightness(brightness_percent);
    err = bsp_display_brightness_set(s_brightness);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "apply brightness failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for save failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(h, SETTINGS_NVS_BRIGHTNESS_KEY, (uint8_t)s_brightness);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save brightness failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool app_settings_get_boot_auto_charge_enabled(void)
{
    return s_boot_auto_charge_enabled;
}

esp_err_t app_settings_set_boot_auto_charge_enabled(bool enabled)
{
    s_boot_auto_charge_enabled = enabled;
    return save_u8_setting(SETTINGS_NVS_BOOT_AUTO_CHARGE_KEY,
                           enabled ? 1 : 0,
                           "boot auto charge");
}

bool app_settings_get_charge_fast_enabled(void)
{
    return s_charge_fast_enabled;
}

esp_err_t app_settings_set_charge_fast_enabled(bool enabled)
{
    s_charge_fast_enabled = enabled;
    return save_u8_setting(SETTINGS_NVS_CHARGE_FAST_KEY,
                           enabled ? 1 : 0,
                           "charge speed");
}

bool app_settings_get_image_exif_enabled(void)
{
    return s_image_exif_enabled;
}

esp_err_t app_settings_set_image_exif_enabled(bool enabled)
{
    s_image_exif_enabled = enabled;
    return save_u8_setting(SETTINGS_NVS_IMAGE_EXIF_KEY,
                           enabled ? 1 : 0,
                           "image exif");
}

bool app_settings_get_three_finger_screenshot_enabled(void)
{
    return s_three_finger_screenshot_enabled;
}

esp_err_t app_settings_set_three_finger_screenshot_enabled(bool enabled)
{
    s_three_finger_screenshot_enabled = enabled;
    return save_u8_setting(SETTINGS_NVS_THREE_FINGER_SCREENSHOT_KEY,
                           enabled ? 1 : 0,
                           "three finger screenshot");
}

#include "app_settings_screensaver.inc"

esp_err_t app_settings_apply_brightness(void)
{
    return bsp_display_brightness_set(s_brightness);
}

int app_settings_get_keyboard_indicator_brightness(void)
{
    return s_keyboard_indicator_brightness;
}

esp_err_t app_settings_preview_keyboard_indicator_brightness(int brightness_percent)
{
    int clamped = clamp_keyboard_indicator_brightness(brightness_percent);
    return tab5_keyboard_set_indicator_brightness((uint8_t)clamped);
}

esp_err_t app_settings_apply_keyboard_indicator_brightness(void)
{
    return tab5_keyboard_set_indicator_brightness((uint8_t)s_keyboard_indicator_brightness);
}

esp_err_t app_settings_set_keyboard_indicator_brightness(int brightness_percent)
{
    nvs_handle_t h;
    esp_err_t save_err;
    esp_err_t apply_err;

    s_keyboard_indicator_brightness = clamp_keyboard_indicator_brightness(brightness_percent);
    apply_err = app_settings_apply_keyboard_indicator_brightness();
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "apply keyboard indicator brightness failed: %s", esp_err_to_name(apply_err));
    }

    save_err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for keyboard indicator brightness save failed: %s",
                 esp_err_to_name(save_err));
        return save_err;
    }

    save_err = nvs_set_u8(h,
                          SETTINGS_NVS_KEYBOARD_INDICATOR_BRIGHTNESS_KEY,
                          (uint8_t)s_keyboard_indicator_brightness);
    if (save_err == ESP_OK) {
        save_err = nvs_commit(h);
    }
    nvs_close(h);

    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "save keyboard indicator brightness failed: %s", esp_err_to_name(save_err));
        return save_err;
    }
    return apply_err;
}

int app_settings_get_audio_volume(void)
{
    return s_audio_volume;
}

esp_err_t app_settings_set_audio_volume(int volume_percent)
{
    s_audio_volume = clamp_audio_volume(volume_percent);
    return save_u8_setting(SETTINGS_NVS_AUDIO_VOLUME_KEY,
                           (uint8_t)s_audio_volume,
                           "audio volume");
}

int app_settings_get_audio_bar_width(void)
{
    return s_audio_bar_width;
}

int app_settings_get_audio_bar_x(void)
{
    return s_audio_bar_x;
}

esp_err_t app_settings_set_audio_bar_geometry(int width_px, int x_px)
{
    int width = clamp_audio_bar_width(width_px);
    int x = clamp_audio_bar_x(x_px, width);

    if (width != width_px || x != x_px) {
        return ESP_ERR_INVALID_ARG;
    }

    s_audio_bar_width = width;
    s_audio_bar_x = x;
    return save_audio_bar_geometry_locked(s_audio_bar_width, s_audio_bar_x);
}

esp_err_t app_settings_set_audio_bar_width(int width_px)
{
    int width = clamp_audio_bar_width(width_px);
    int x = clamp_audio_bar_x(s_audio_bar_x, width);

    if (width != width_px) {
        return ESP_ERR_INVALID_ARG;
    }

    s_audio_bar_width = width;
    s_audio_bar_x = x;
    return save_audio_bar_geometry_locked(s_audio_bar_width, s_audio_bar_x);
}

esp_err_t app_settings_set_audio_bar_x(int x_px)
{
    int x = clamp_audio_bar_x(x_px, s_audio_bar_width);

    if (x != x_px) {
        return ESP_ERR_INVALID_ARG;
    }

    s_audio_bar_x = x;
    return save_i32_setting(SETTINGS_NVS_AUDIO_BAR_X_KEY, s_audio_bar_x, "audio bar x");
}

app_ime_input_mode_t app_settings_get_ime_input_mode(void)
{
    return s_ime_input_mode;
}

esp_err_t app_settings_set_ime_input_mode(app_ime_input_mode_t mode)
{
    nvs_handle_t h;
    esp_err_t err;

    s_ime_input_mode = clamp_ime_input_mode((uint8_t)mode);

    err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for ime input mode save failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(h, SETTINGS_NVS_IME_INPUT_MODE_KEY, (uint8_t)s_ime_input_mode);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save ime input mode failed: %s", esp_err_to_name(err));
    }
    return err;
}

#include "app_settings_timezone.inc"
