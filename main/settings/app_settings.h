#pragma once

#include "esp_err.h"
#include "screensaver/screensaver_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SETTINGS_DEFAULT_BRIGHTNESS 80
#define APP_SETTINGS_MIN_BRIGHTNESS     5
#define APP_SETTINGS_MAX_BRIGHTNESS     100

#define APP_SETTINGS_DEFAULT_KEYBOARD_INDICATOR_BRIGHTNESS 20
#define APP_SETTINGS_MIN_KEYBOARD_INDICATOR_BRIGHTNESS     0
#define APP_SETTINGS_MAX_KEYBOARD_INDICATOR_BRIGHTNESS     100

/* 音乐播放器默认音量；首次启动或 NVS 中没有历史音量时使用。 */
#define APP_SETTINGS_DEFAULT_AUDIO_VOLUME 80
/* 音乐播放器允许保存的最小音量百分比，防止异常参数写入 NVS。 */
#define APP_SETTINGS_MIN_AUDIO_VOLUME     0
/* 音乐播放器允许保存的最大音量百分比，和播放器 UI 滑条范围保持一致。 */
#define APP_SETTINGS_MAX_AUDIO_VOLUME     100

/* 顶部后台音乐条默认宽度，使用 px 绝对值。 */
#define APP_SETTINGS_DEFAULT_AUDIO_BAR_WIDTH 420
/* 顶部后台音乐条允许设置的最小宽度，过窄会导致歌曲名和进度条挤压。 */
#define APP_SETTINGS_MIN_AUDIO_BAR_WIDTH     100
/* 顶部后台音乐条允许设置的最大宽度，避免覆盖顶部状态区太多内容。 */
#define APP_SETTINGS_MAX_AUDIO_BAR_WIDTH     1000
/* 顶部后台音乐条默认距离屏幕左侧的位置。 */
#define APP_SETTINGS_DEFAULT_AUDIO_BAR_X     350
/* 用于校验顶部后台音乐条位置的屏幕宽度；Tab5 当前横屏宽度为 1280px。 */
#define APP_SETTINGS_AUDIO_BAR_SCREEN_W      1280

#define APP_SETTINGS_DEFAULT_BOOT_AUTO_CHARGE_ENABLED false
#define APP_SETTINGS_DEFAULT_CHARGE_FAST_ENABLED      false
#define APP_SETTINGS_DEFAULT_IMAGE_EXIF_ENABLED       true
#define APP_SETTINGS_DEFAULT_THREE_FINGER_SCREENSHOT_ENABLED true

#define APP_SETTINGS_UTC_OFFSET_MIN_MINUTES  (-12 * 60)
#define APP_SETTINGS_UTC_OFFSET_MAX_MINUTES  (14 * 60)
#define APP_SETTINGS_UTC_OFFSET_STEP_MINUTES 15

#define APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN 160
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DIR "/sdcard"
#define APP_SETTINGS_MIN_SCREENSAVER_IDLE_MINUTES 1
#define APP_SETTINGS_MAX_SCREENSAVER_IDLE_MINUTES 99
#define APP_SETTINGS_DEFAULT_SCREENSAVER_IDLE_MINUTES 1
#define APP_SETTINGS_MIN_SCREENSAVER_SWITCH_MINUTES 1
#define APP_SETTINGS_MAX_SCREENSAVER_SWITCH_MINUTES 999
#define APP_SETTINGS_DEFAULT_SCREENSAVER_SWITCH_MINUTES 1
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DISABLE_DURING_SSH false
#define APP_SETTINGS_SCREENSAVER_SCREEN_W SCREENSAVER_LAYOUT_SCREEN_W
#define APP_SETTINGS_SCREENSAVER_SCREEN_H SCREENSAVER_LAYOUT_SCREEN_H
#define APP_SETTINGS_SCREENSAVER_COORD_MIN (-9999)
#define APP_SETTINGS_SCREENSAVER_COORD_MAX 9999
#define APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MIN 0
#define APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MAX 40
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_ENABLED \
    SCREENSAVER_DEFAULT_TEXT_SHADOW_ENABLED
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_LENGTH \
    SCREENSAVER_DEFAULT_TEXT_SHADOW_LENGTH
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_X SCREENSAVER_DEFAULT_TITLE_X
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_Y SCREENSAVER_DEFAULT_TITLE_Y
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X SCREENSAVER_DEFAULT_TIME_X
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y SCREENSAVER_DEFAULT_TIME_Y
#define APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_X SCREENSAVER_DEFAULT_SECOND_X
#define APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_Y SCREENSAVER_DEFAULT_SECOND_Y
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_X SCREENSAVER_DEFAULT_DATE_X
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_Y SCREENSAVER_DEFAULT_DATE_Y
#define APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_X SCREENSAVER_DEFAULT_LUNAR_X
#define APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_Y SCREENSAVER_DEFAULT_LUNAR_Y
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_FONT_SIZE SCREENSAVER_DEFAULT_TITLE_FONT_SIZE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_FONT_SIZE SCREENSAVER_DEFAULT_TIME_FONT_SIZE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_FONT_SIZE SCREENSAVER_DEFAULT_SECOND_FONT_SIZE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_FONT_SIZE SCREENSAVER_DEFAULT_DATE_FONT_SIZE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_FONT_SIZE SCREENSAVER_DEFAULT_LUNAR_FONT_SIZE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_VISIBLE SCREENSAVER_DEFAULT_TITLE_VISIBLE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_VISIBLE SCREENSAVER_DEFAULT_TIME_VISIBLE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_VISIBLE SCREENSAVER_DEFAULT_SECOND_VISIBLE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_VISIBLE SCREENSAVER_DEFAULT_DATE_VISIBLE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_VISIBLE SCREENSAVER_DEFAULT_LUNAR_VISIBLE
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_COLOR SCREENSAVER_DEFAULT_TITLE_COLOR
#define APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_COLOR SCREENSAVER_DEFAULT_TIME_COLOR
#define APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_COLOR SCREENSAVER_DEFAULT_SECOND_COLOR
#define APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_COLOR SCREENSAVER_DEFAULT_DATE_COLOR
#define APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_COLOR SCREENSAVER_DEFAULT_LUNAR_COLOR

typedef enum {
    APP_SETTINGS_IME_INPUT_MODE_CHARACTER = 0,
    APP_SETTINGS_IME_INPUT_MODE_PHRASE = 1,
} app_ime_input_mode_t;

typedef enum {
    APP_SETTINGS_TIMEZONE_BEIJING = 0,
    APP_SETTINGS_TIMEZONE_NEW_YORK,
    APP_SETTINGS_TIMEZONE_UTC,
    APP_SETTINGS_TIMEZONE_LONDON,
    APP_SETTINGS_TIMEZONE_TOKYO,
    APP_SETTINGS_TIMEZONE_CENTRAL_EUROPE,
    APP_SETTINGS_TIMEZONE_COUNT,
} app_timezone_t;

typedef enum {
    APP_SETTINGS_SCREENSAVER_MODE_OFF = 0,
    APP_SETTINGS_SCREENSAVER_MODE_BLANK,
    APP_SETTINGS_SCREENSAVER_MODE_WALLPAPER,
} app_screensaver_mode_t;

typedef enum {
    APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL = 0,
    APP_SETTINGS_SCREENSAVER_ORDER_RANDOM,
} app_screensaver_order_t;

typedef enum {
    APP_SETTINGS_SCREENSAVER_COMPONENT_TIME = 0,
    APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND,
    APP_SETTINGS_SCREENSAVER_COMPONENT_DATE,
    APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR,
    APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE,
    APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT,
} app_screensaver_component_t;

typedef enum {
    APP_SETTINGS_SCREENSAVER_FONT_20 = 20,
    APP_SETTINGS_SCREENSAVER_FONT_40 = 40,
    APP_SETTINGS_SCREENSAVER_FONT_60 = 60,
    APP_SETTINGS_SCREENSAVER_FONT_80 = 80,
    APP_SETTINGS_SCREENSAVER_FONT_120 = 120,
    APP_SETTINGS_SCREENSAVER_FONT_160 = 160,
    APP_SETTINGS_SCREENSAVER_FONT_200 = 200,
} app_screensaver_font_size_t;

esp_err_t app_settings_init(void);
void app_settings_ensure_timezone_default_for_language(void);
int app_settings_get_brightness(void);
esp_err_t app_settings_preview_brightness(int brightness_percent);
esp_err_t app_settings_set_brightness(int brightness_percent);
esp_err_t app_settings_apply_brightness(void);
int app_settings_get_keyboard_indicator_brightness(void);
esp_err_t app_settings_preview_keyboard_indicator_brightness(int brightness_percent);
esp_err_t app_settings_set_keyboard_indicator_brightness(int brightness_percent);
esp_err_t app_settings_apply_keyboard_indicator_brightness(void);
int app_settings_get_audio_volume(void);
esp_err_t app_settings_set_audio_volume(int volume_percent);
int app_settings_get_audio_bar_width(void);
int app_settings_get_audio_bar_x(void);
int app_settings_get_audio_bar_max_x_for_width(int width_px);
esp_err_t app_settings_set_audio_bar_geometry(int width_px, int x_px);
esp_err_t app_settings_set_audio_bar_width(int width_px);
esp_err_t app_settings_set_audio_bar_x(int x_px);
app_ime_input_mode_t app_settings_get_ime_input_mode(void);
esp_err_t app_settings_set_ime_input_mode(app_ime_input_mode_t mode);
app_timezone_t app_settings_get_timezone(void);
esp_err_t app_settings_set_timezone(app_timezone_t timezone);
int app_settings_get_utc_offset_minutes(void);
esp_err_t app_settings_set_utc_offset_minutes(int offset_minutes);
void app_settings_format_utc_offset(int offset_minutes, char *out, size_t out_len);
const char *app_settings_get_timezone_tz(app_timezone_t timezone);
const char *app_settings_get_timezone_source_label(app_timezone_t timezone);
const char *app_settings_get_current_timezone_tz(void);
const char *app_settings_get_current_timezone_source_label(void);
bool app_settings_get_boot_auto_charge_enabled(void);
esp_err_t app_settings_set_boot_auto_charge_enabled(bool enabled);
bool app_settings_get_charge_fast_enabled(void);
esp_err_t app_settings_set_charge_fast_enabled(bool enabled);
bool app_settings_get_image_exif_enabled(void);
esp_err_t app_settings_set_image_exif_enabled(bool enabled);
bool app_settings_get_three_finger_screenshot_enabled(void);
esp_err_t app_settings_set_three_finger_screenshot_enabled(bool enabled);
app_screensaver_mode_t app_settings_get_screensaver_mode(void);
esp_err_t app_settings_set_screensaver_mode(app_screensaver_mode_t mode);
int app_settings_get_screensaver_idle_minutes(void);
esp_err_t app_settings_set_screensaver_idle_minutes(int minutes);
bool app_settings_get_screensaver_disable_during_ssh(void);
esp_err_t app_settings_set_screensaver_disable_during_ssh(bool enabled);
const char *app_settings_get_screensaver_wallpaper_dir(void);
esp_err_t app_settings_set_screensaver_wallpaper_dir(const char *path);
app_screensaver_order_t app_settings_get_screensaver_order(void);
esp_err_t app_settings_set_screensaver_order(app_screensaver_order_t order);
int app_settings_get_screensaver_switch_minutes(void);
esp_err_t app_settings_set_screensaver_switch_minutes(int minutes);
bool app_settings_get_screensaver_show_exif_title(void);
esp_err_t app_settings_set_screensaver_show_exif_title(bool enabled);
int app_settings_get_screensaver_time_x(void);
int app_settings_get_screensaver_time_y(void);
esp_err_t app_settings_set_screensaver_time_position(int x, int y);
int app_settings_get_screensaver_component_x(app_screensaver_component_t component);
int app_settings_get_screensaver_component_y(app_screensaver_component_t component);
esp_err_t app_settings_set_screensaver_component_position(app_screensaver_component_t component,
                                                          int x,
                                                          int y);
int app_settings_get_screensaver_component_font_size(app_screensaver_component_t component);
esp_err_t app_settings_set_screensaver_component_font_size(app_screensaver_component_t component,
                                                           int font_size);
bool app_settings_get_screensaver_component_visible(app_screensaver_component_t component);
esp_err_t app_settings_set_screensaver_component_visible(app_screensaver_component_t component,
                                                         bool visible);
uint32_t app_settings_get_screensaver_component_color(app_screensaver_component_t component);
esp_err_t app_settings_set_screensaver_component_color(app_screensaver_component_t component,
                                                       uint32_t rgb);
bool app_settings_get_screensaver_text_shadow_enabled(void);
esp_err_t app_settings_set_screensaver_text_shadow_enabled(bool enabled);
int app_settings_get_screensaver_text_shadow_length(void);
esp_err_t app_settings_set_screensaver_text_shadow_length(int length);
esp_err_t app_settings_reset_screensaver_component_positions(void);

#ifdef __cplusplus
}
#endif
