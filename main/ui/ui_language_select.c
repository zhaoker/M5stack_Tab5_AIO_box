#include "ui_language_select.h"

#include <stdint.h>

#include "esp_log.h"
#include "i18n/app_i18n.h"
#include "settings/app_settings.h"
#include "theme.hpp"
#include "time_sync/time_sync_service.h"
#include "ui_audio_background.h"
#include "ui_server_list.h"

static const char *TAG = "ui_lang";
static bool s_language_selection_done;

typedef struct {
    app_language_t language;
    const char *title;
    const char *subtitle;
} language_option_t;

static const language_option_t s_language_options[] = {
    {APP_LANGUAGE_ZH_CN, "简体中文", "中文输入优先"},
    {APP_LANGUAGE_ZH_TW, "繁體中文", "中文輸入優先"},
    {APP_LANGUAGE_EN, "English", "English input first"},
};

static void finish_language_selection_async(void *user_data)
{
    app_language_t language = (app_language_t)(uintptr_t)user_data;

    esp_err_t err = app_i18n_set_language(language);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save language failed: %s", esp_err_to_name(err));
        s_language_selection_done = false;
        return;
    }

    ESP_LOGI(TAG, "language selected: %s", app_i18n_language_name(language));
    app_settings_ensure_timezone_default_for_language();
    time_sync_service_apply_timezone();
    lv_obj_clean(lv_scr_act());
    ui_server_list_create(lv_scr_act());
    ui_audio_background_init();
}

static void finish_language_selection(app_language_t language)
{
    if (s_language_selection_done) {
        return;
    }
    s_language_selection_done = true;

    if (lv_async_call(finish_language_selection_async, (void *)(uintptr_t)language) != LV_RESULT_OK) {
        ESP_LOGW(TAG, "queue language selection failed");
        s_language_selection_done = false;
    }
}

static void on_language_clicked(lv_event_t *e)
{
    const language_option_t *option = (const language_option_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (option && (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED)) {
        finish_language_selection(option->language);
    }
}

static lv_obj_t *create_text(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font ? font : &APP_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void ui_language_select_create(lv_obj_t *parent)
{
    s_language_selection_done = false;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_set_size(panel, 620, 410);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_pad_all(panel, 28, 0);
    lv_obj_set_style_pad_row(panel, 18, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    create_text(panel, "选择语言", THEME_TEXT_PRIMARY, &APP_FONT);
    create_text(panel, "Select Language", THEME_TEXT_SECONDARY, &APP_FONT);

    for (size_t i = 0; i < sizeof(s_language_options) / sizeof(s_language_options[0]); i++) {
        const language_option_t *option = &s_language_options[i];
        lv_obj_t *btn = lv_button_create(panel);
        lv_obj_set_width(btn, 420);
        lv_obj_set_height(btn, 72);
        lv_obj_set_style_bg_color(btn, THEME_BG_CARD, 0);
        lv_obj_set_style_bg_color(btn, THEME_PRIMARY, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);
        lv_obj_set_style_pad_row(btn, 4, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, on_language_clicked, LV_EVENT_RELEASED, (void *)option);
        lv_obj_add_event_cb(btn, on_language_clicked, LV_EVENT_CLICKED, (void *)option);

        lv_obj_t *title = create_text(btn, option->title, THEME_TEXT_PRIMARY, &APP_FONT);
        lv_obj_t *subtitle = create_text(btn, option->subtitle, THEME_TEXT_SECONDARY, &APP_FONT);
        lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(subtitle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(title, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(subtitle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(title, on_language_clicked, LV_EVENT_RELEASED, (void *)option);
        lv_obj_add_event_cb(title, on_language_clicked, LV_EVENT_CLICKED, (void *)option);
        lv_obj_add_event_cb(subtitle, on_language_clicked, LV_EVENT_RELEASED, (void *)option);
        lv_obj_add_event_cb(subtitle, on_language_clicked, LV_EVENT_CLICKED, (void *)option);
    }
}
