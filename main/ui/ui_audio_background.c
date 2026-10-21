#include "ui_audio_background.h"

#include <stdio.h>
#include <string.h>

#include "audio/audio_player_service.h"
#include "file_manager/ui_file_manager.h"
#include "lvgl.h"
#include "settings/app_settings.h"
#include "theme.hpp"

enum {
    /* 后台音乐条高度，决定顶部浮条自身的触摸区域高度。 */
    AUDIO_BG_BAR_H = 50,
    /* 后台音乐条刷新周期，数值越小进度越及时但 LVGL 刷新更频繁。 */
    AUDIO_BG_POLL_MS = 500,
    /* 后台音乐条左上角 Y 坐标；终端状态栏显示时从这个 Y 位置开始绘制。 */
    AUDIO_BG_BAR_Y = 1,
    /* 播放图标固定区域宽度，歌曲名从这个图标右侧开始左对齐。 */
    AUDIO_BG_ICON_W = 28,
    /* 顶部后台音乐条右侧进度条宽度，靠右对齐，剩余空间自动给歌曲名。 */
    AUDIO_BG_PROGRESS_W = 96,
};

static lv_obj_t *s_bar;
static lv_obj_t *s_icon_label;
static lv_obj_t *s_title_label;
static lv_obj_t *s_progress_bar;
static lv_timer_t *s_timer;
static char s_last_title[AUDIO_LIBRARY_NAME_MAX];
static bool s_music_ui_visible;
static bool s_terminal_status_visible = true;

static bool audio_bg_state_should_show(audio_player_state_t state)
{
    return state == AUDIO_PLAYER_STATE_QUEUED ||
           state == AUDIO_PLAYER_STATE_SCANNING ||
           state == AUDIO_PLAYER_STATE_PREPARING ||
           state == AUDIO_PLAYER_STATE_PLAYING ||
           state == AUDIO_PLAYER_STATE_PAUSED;
}

static void audio_bg_set_hidden(bool hidden)
{
    if (!s_bar) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_bar);
    }
}

static lv_obj_t *audio_bg_create_label(lv_obj_t *parent,
                                       const char *text,
                                       int32_t width,
                                       lv_color_t color,
                                       lv_label_long_mode_t long_mode)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, long_mode);
    if (width > 0) {
        lv_obj_set_width(label, width);
    }
    return label;
}

static void audio_bg_apply_geometry(void)
{
    int32_t sw = lv_display_get_horizontal_resolution(NULL);
    int32_t width = app_settings_get_audio_bar_width();
    int32_t x = app_settings_get_audio_bar_x();

    if (!s_bar) {
        return;
    }
    if (sw <= 0) {
        sw = APP_SETTINGS_AUDIO_BAR_SCREEN_W;
    }
    if (width > sw) {
        width = sw;
    }
    if (x + width > sw) {
        x = sw - width;
    }
    if (x < 0) {
        x = 0;
    }
    lv_obj_set_size(s_bar, width, AUDIO_BG_BAR_H);
    lv_obj_set_pos(s_bar, x, AUDIO_BG_BAR_Y);
}

static void audio_bg_update_now(void)
{
    audio_player_status_t status;
    const char *title;
    uint32_t progress = 0;

    if (!s_bar || s_music_ui_visible || !s_terminal_status_visible ||
        !audio_player_service_is_initialized()) {
        audio_bg_set_hidden(true);
        return;
    }
    if (audio_player_get_status(&status) != ESP_OK || !audio_bg_state_should_show(status.state)) {
        audio_bg_set_hidden(true);
        return;
    }

    title = status.title[0] ? status.title : (status.message[0] ? status.message : "音频");
    if (status.duration_ms > 0) {
        uint64_t scaled = ((uint64_t)status.elapsed_ms * 1000ULL) / status.duration_ms;
        progress = scaled > 1000ULL ? 1000U : (uint32_t)scaled;
    }

    if (s_title_label && strcmp(s_last_title, title) != 0) {
        lv_label_set_text(s_title_label, title);
        snprintf(s_last_title, sizeof(s_last_title), "%s", title);
    }
    if (s_progress_bar) {
        lv_bar_set_value(s_progress_bar, (int32_t)progress, LV_ANIM_OFF);
    }
    audio_bg_apply_geometry();
    audio_bg_set_hidden(false);
}

static void audio_bg_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    audio_bg_update_now();
}

static void audio_bg_on_click(lv_event_t *e)
{
    (void)e;
    ui_file_manager_audio_play_reserved(NULL);
}

void ui_audio_background_init(void)
{
    lv_obj_t *layer = lv_layer_top();
    lv_obj_t *title_wrap;

    if (s_bar || !layer) {
        return;
    }

    s_bar = lv_obj_create(layer);
    lv_obj_null_on_delete(&s_bar);
    audio_bg_apply_geometry();
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x141A27), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(0x2A3550), 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_left(s_bar, 12, 0);
    lv_obj_set_style_pad_right(s_bar, 12, 0);
    lv_obj_set_style_pad_top(s_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_bar, 0, 0);
    lv_obj_set_style_pad_column(s_bar, 12, 0);
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_bar, audio_bg_on_click, LV_EVENT_CLICKED, NULL);

    s_icon_label = audio_bg_create_label(s_bar,
                                         LV_SYMBOL_PLAY,
                                         AUDIO_BG_ICON_W,
                                         THEME_TEXT_PRIMARY,
                                         LV_LABEL_LONG_CLIP);
    lv_obj_null_on_delete(&s_icon_label);
    lv_obj_set_style_text_align(s_icon_label, LV_TEXT_ALIGN_LEFT, 0);

    title_wrap = lv_obj_create(s_bar);
    lv_obj_set_width(title_wrap, 1);
    lv_obj_set_height(title_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(title_wrap, 1);
    lv_obj_set_style_bg_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(title_wrap, 0, 0);
    lv_obj_clear_flag(title_wrap, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = audio_bg_create_label(title_wrap,
                                          TRS("音频"),
                                          0,
                                          THEME_TEXT_PRIMARY,
                                          LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_null_on_delete(&s_title_label);
    lv_obj_set_width(s_title_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_progress_bar = lv_bar_create(s_bar);
    lv_obj_null_on_delete(&s_progress_bar);
    lv_obj_set_size(s_progress_bar, AUDIO_BG_PROGRESS_W, 5);
    lv_bar_set_range(s_progress_bar, 0, 1000);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_progress_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x2A3550), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x7C5CFF), LV_PART_INDICATOR);

    audio_bg_set_hidden(true);
    s_timer = lv_timer_create(audio_bg_timer_cb, AUDIO_BG_POLL_MS, NULL);
    if (s_timer) {
        lv_timer_ready(s_timer);
    }
}

void ui_audio_background_set_music_ui_visible(bool visible)
{
    s_music_ui_visible = visible;
    audio_bg_update_now();
}

void ui_audio_background_set_terminal_status_visible(bool visible)
{
    s_terminal_status_visible = visible;
    audio_bg_update_now();
}

void ui_audio_background_refresh_geometry(void)
{
    audio_bg_apply_geometry();
    audio_bg_update_now();
}
