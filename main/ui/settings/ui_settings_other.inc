static int settings_parse_int_text(const char *text, bool *ok)
{
    char *end = NULL;
    long value;

    if (ok) {
        *ok = false;
    }
    if (!text || !text[0]) {
        return 0;
    }
    value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value < INT32_MIN || value > INT32_MAX) {
        return 0;
    }
    if (ok) {
        *ok = true;
    }
    return (int)value;
}

static void update_timezone_option_styles(void);

static void set_audio_bar_status(const char *message, lv_color_t color)
{
    if (!s_audio_bar_status_label) {
        return;
    }
    lv_label_set_text(s_audio_bar_status_label, message ? message : "");
    lv_obj_set_style_text_color(s_audio_bar_status_label, color, 0);
}

static void set_timezone_status(const char *message, lv_color_t color)
{
    if (!s_timezone_status_label) {
        return;
    }
    lv_label_set_text(s_timezone_status_label, message ? TRS(message) : "");
    lv_obj_set_style_text_color(s_timezone_status_label, color, 0);
}

static void close_timezone_dialog(void)
{
    if (s_timezone_dialog_overlay) {
        lv_obj_del(s_timezone_dialog_overlay);
        s_timezone_dialog_overlay = NULL;
        s_timezone_dialog_value_label = NULL;
    }
}

static int timezone_dialog_clamp_offset(int offset)
{
    if (offset < APP_SETTINGS_UTC_OFFSET_MIN_MINUTES) {
        return APP_SETTINGS_UTC_OFFSET_MIN_MINUTES;
    }
    if (offset > APP_SETTINGS_UTC_OFFSET_MAX_MINUTES) {
        return APP_SETTINGS_UTC_OFFSET_MAX_MINUTES;
    }
    return (offset / APP_SETTINGS_UTC_OFFSET_STEP_MINUTES) *
           APP_SETTINGS_UTC_OFFSET_STEP_MINUTES;
}

static void update_timezone_dialog_value(void)
{
    char text[64];
    char offset_text[24];

    if (!s_timezone_dialog_value_label) {
        return;
    }
    s_timezone_dialog_offset_minutes =
        timezone_dialog_clamp_offset(s_timezone_dialog_offset_minutes);
    app_settings_format_utc_offset(s_timezone_dialog_offset_minutes,
                                   offset_text,
                                   sizeof(offset_text));
    snprintf(text, sizeof(text), TRS("当前选择：%s"), offset_text);
    lv_label_set_text(s_timezone_dialog_value_label, text);
}

static lv_obj_t *create_timezone_dialog_button(lv_obj_t *parent,
                                               const char *text,
                                               lv_color_t bg,
                                               lv_coord_t width,
                                               lv_event_cb_t cb,
                                               void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, width);
    lv_obj_set_height(btn, 44);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = create_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    return btn;
}

static void on_timezone_dialog_close(lv_event_t *e)
{
    (void)e;
    close_timezone_dialog();
}

static void on_timezone_dialog_save(lv_event_t *e)
{
    (void)e;

    if (app_settings_set_utc_offset_minutes(s_timezone_dialog_offset_minutes) != ESP_OK) {
        set_timezone_status(TRS("时区保存失败"), THEME_DANGER_LIGHT);
        return;
    }
    time_sync_service_apply_timezone();
    update_timezone_option_styles();
    set_timezone_status(TRS("时区已保存"), THEME_SUCCESS_LIGHT);
    update_system_info_time_label();
    close_timezone_dialog();
}

static void on_timezone_dialog_direction(lv_event_t *e)
{
    int direction = (int)(intptr_t)lv_event_get_user_data(e);
    int abs_minutes = s_timezone_dialog_offset_minutes < 0
                          ? -s_timezone_dialog_offset_minutes
                          : s_timezone_dialog_offset_minutes;

    if (direction == 0) {
        s_timezone_dialog_offset_minutes = 0;
    } else {
        s_timezone_dialog_offset_minutes = abs_minutes * direction;
        if (s_timezone_dialog_offset_minutes == 0) {
            s_timezone_dialog_offset_minutes = direction * 60;
        }
    }
    update_timezone_dialog_value();
}

static void on_timezone_dialog_adjust(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    s_timezone_dialog_offset_minutes =
        timezone_dialog_clamp_offset(s_timezone_dialog_offset_minutes + delta);
    update_timezone_dialog_value();
}

static void show_timezone_dialog(void)
{
    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();

    if (s_timezone_dialog_overlay) {
        lv_obj_move_foreground(s_timezone_dialog_overlay);
        return;
    }

    s_timezone_dialog_offset_minutes = app_settings_get_utc_offset_minutes();
    s_timezone_dialog_overlay = lv_obj_create(parent);
    lv_obj_null_on_delete(&s_timezone_dialog_overlay);
    lv_obj_set_size(s_timezone_dialog_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_timezone_dialog_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_timezone_dialog_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_timezone_dialog_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_timezone_dialog_overlay, 0, 0);
    lv_obj_clear_flag(s_timezone_dialog_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_timezone_dialog_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_timezone_dialog_overlay);
    lv_obj_set_size(panel, 540, LV_SIZE_CONTENT);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    create_label(panel, "选择UTC时区", THEME_TEXT_PRIMARY);
    create_wrap_label(panel,
                      "UTC+ 表示当地时间比 UTC 快；UTC- 表示当地时间比 UTC 慢。",
                      THEME_TEXT_TERTIARY);

    s_timezone_dialog_value_label = create_label(panel, "", THEME_PRIMARY_LIGHT);
    lv_obj_null_on_delete(&s_timezone_dialog_value_label);
    lv_obj_set_width(s_timezone_dialog_value_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_timezone_dialog_value_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *direction_row = lv_obj_create(panel);
    lv_obj_set_width(direction_row, LV_PCT(100));
    lv_obj_set_height(direction_row, 48);
    lv_obj_set_style_bg_opa(direction_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(direction_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(direction_row, 0, 0);
    lv_obj_set_style_pad_column(direction_row, 10, 0);
    lv_obj_set_flex_flow(direction_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(direction_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(direction_row, LV_OBJ_FLAG_SCROLLABLE);

    create_timezone_dialog_button(direction_row,
                                  "UTC-",
                                  THEME_BG_HOVER,
                                  138,
                                  on_timezone_dialog_direction,
                                  (void *)(intptr_t)-1);
    create_timezone_dialog_button(direction_row,
                                  "UTC",
                                  THEME_BG_HOVER,
                                  138,
                                  on_timezone_dialog_direction,
                                  (void *)(intptr_t)0);
    create_timezone_dialog_button(direction_row,
                                  "UTC+",
                                  THEME_BG_HOVER,
                                  138,
                                  on_timezone_dialog_direction,
                                  (void *)(intptr_t)1);

    lv_obj_t *adjust_row = lv_obj_create(panel);
    lv_obj_set_width(adjust_row, LV_PCT(100));
    lv_obj_set_height(adjust_row, 100);
    lv_obj_set_style_bg_opa(adjust_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(adjust_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(adjust_row, 0, 0);
    lv_obj_set_style_pad_column(adjust_row, 12, 0);
    lv_obj_set_flex_flow(adjust_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(adjust_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(adjust_row, LV_OBJ_FLAG_SCROLLABLE);

    create_timezone_dialog_button(adjust_row,
                                  "-1h",
                                  THEME_BG_HOVER,
                                  96,
                                  on_timezone_dialog_adjust,
                                  (void *)(intptr_t)-60);
    create_timezone_dialog_button(adjust_row,
                                  "-15m",
                                  THEME_BG_HOVER,
                                  96,
                                  on_timezone_dialog_adjust,
                                  (void *)(intptr_t)-15);
    create_timezone_dialog_button(adjust_row,
                                  "+15m",
                                  THEME_BG_HOVER,
                                  96,
                                  on_timezone_dialog_adjust,
                                  (void *)(intptr_t)15);
    create_timezone_dialog_button(adjust_row,
                                  "+1h",
                                  THEME_BG_HOVER,
                                  96,
                                  on_timezone_dialog_adjust,
                                  (void *)(intptr_t)60);

    create_wrap_label(panel,
                      "例如北京时间、中国台湾和日本附近常用 UTC+8/UTC+9；纽约标准时间为 UTC-5。",
                      THEME_TEXT_TERTIARY);

    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, 48);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    create_timezone_dialog_button(buttons,
                                  "取消",
                                  THEME_BG_HOVER,
                                  128,
                                  on_timezone_dialog_close,
                                  NULL);
    create_timezone_dialog_button(buttons,
                                  LV_SYMBOL_SAVE " 保存",
                                  THEME_PRIMARY,
                                  150,
                                  on_timezone_dialog_save,
                                  NULL);

    update_timezone_dialog_value();
}

static void update_timezone_option_styles(void)
{
    app_timezone_t current = app_settings_get_timezone();

    for (int i = 0; i < APP_SETTINGS_TIMEZONE_COUNT; ++i) {
        lv_obj_t *btn = s_timezone_buttons[i];
        bool selected = (app_timezone_t)i == current;
        if (!btn) {
            continue;
        }
        lv_obj_set_style_bg_color(btn, selected ? THEME_PRIMARY : THEME_BG_CARD, 0);
        lv_obj_set_style_border_color(btn, selected ? THEME_PRIMARY_LIGHT : THEME_BORDER_SECONDARY, 0);
        if (s_timezone_button_labels[i]) {
            lv_label_set_text(s_timezone_button_labels[i],
                              TRS(app_settings_get_timezone_source_label((app_timezone_t)i)));
        }
    }
}

static void on_timezone_option_click(lv_event_t *e)
{
    app_timezone_t timezone = (app_timezone_t)(uintptr_t)lv_event_get_user_data(e);

    if (timezone == APP_SETTINGS_TIMEZONE_UTC) {
        show_timezone_dialog();
        return;
    }
    if (app_settings_set_timezone(timezone) != ESP_OK) {
        set_timezone_status(TRS("时区保存失败"), THEME_DANGER_LIGHT);
        return;
    }
    time_sync_service_apply_timezone();
    update_timezone_option_styles();
    set_timezone_status(TRS("时区已保存"), THEME_SUCCESS_LIGHT);
    update_system_info_time_label();
}

static void create_timezone_option(lv_obj_t *parent, app_timezone_t timezone)
{
    bool selected = app_settings_get_timezone() == timezone;
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, 168);
    lv_obj_set_height(btn, 46);
    lv_obj_set_style_bg_color(btn, selected ? THEME_PRIMARY : THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, selected ? THEME_PRIMARY_LIGHT : THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, on_timezone_option_click, LV_EVENT_CLICKED, (void *)(uintptr_t)timezone);
    s_timezone_buttons[timezone] = btn;
    lv_obj_null_on_delete(&s_timezone_buttons[timezone]);

    lv_obj_t *label = create_label(btn, app_settings_get_timezone_source_label(timezone), THEME_TEXT_PRIMARY);
    s_timezone_button_labels[timezone] = label;
    lv_obj_null_on_delete(&s_timezone_button_labels[timezone]);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
}

static void update_audio_bar_range_hint(void)
{
    bool width_ok = false;
    int width = settings_parse_int_text(s_audio_bar_width_ta ? lv_textarea_get_text(s_audio_bar_width_ta) : NULL,
                                        &width_ok);
    int max_x;
    char hint[160];

    if (!width_ok ||
        width < APP_SETTINGS_MIN_AUDIO_BAR_WIDTH ||
        width > APP_SETTINGS_MAX_AUDIO_BAR_WIDTH) {
        width = app_settings_get_audio_bar_width();
    }
    max_x = app_settings_get_audio_bar_max_x_for_width(width);
    snprintf(hint,
             sizeof(hint),
             TRS("宽度允许 %d-%d px；当前宽度下左侧允许 0-%d px。"),
             APP_SETTINGS_MIN_AUDIO_BAR_WIDTH,
             APP_SETTINGS_MAX_AUDIO_BAR_WIDTH,
             max_x);
    if (s_audio_bar_range_label) {
        lv_label_set_text(s_audio_bar_range_label, hint);
    }
}

static void on_audio_bar_text_changed(lv_event_t *e)
{
    (void)e;
    update_audio_bar_range_hint();
}

static lv_obj_t *create_number_input_row(lv_obj_t *parent,
                                         const char *name,
                                         int value,
                                         lv_obj_t **out_ta)
{
    char value_text[16];
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = create_label(row, TRS(name), THEME_TEXT_SECONDARY);
    lv_obj_set_width(label, 210);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    lv_obj_t *ta = lv_textarea_create(row);
    lv_obj_set_width(ta, 240);
    lv_obj_set_height(ta, 48);
    lv_obj_set_style_text_font(ta, &APP_FONT, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 4);
    snprintf(value_text, sizeof(value_text), "%d", value);
    lv_textarea_set_text(ta, value_text);
    ui_number_input_attach(ta, s_overlay);
    lv_obj_add_event_cb(ta, on_audio_bar_text_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (out_ta) {
        *out_ta = ta;
        lv_obj_null_on_delete(out_ta);
    }
    return ta;
}

static void on_audio_bar_save_click(lv_event_t *e)
{
    bool width_ok = false;
    bool x_ok = false;
    int width = settings_parse_int_text(s_audio_bar_width_ta ? lv_textarea_get_text(s_audio_bar_width_ta) : NULL,
                                        &width_ok);
    int x = settings_parse_int_text(s_audio_bar_x_ta ? lv_textarea_get_text(s_audio_bar_x_ta) : NULL,
                                    &x_ok);
    int max_x;
    esp_err_t err;

    (void)e;
    if (!width_ok ||
        width < APP_SETTINGS_MIN_AUDIO_BAR_WIDTH ||
        width > APP_SETTINGS_MAX_AUDIO_BAR_WIDTH) {
        set_audio_bar_status(TRS("宽度超出范围，未保存。"), THEME_DANGER_LIGHT);
        return;
    }

    max_x = app_settings_get_audio_bar_max_x_for_width(width);
    if (!x_ok || x < 0 || x > max_x) {
        set_audio_bar_status(TRS("距离左边位置超出当前宽度允许范围，未保存。"), THEME_DANGER_LIGHT);
        return;
    }

    err = app_settings_set_audio_bar_geometry(width, x);
    if (err != ESP_OK) {
        set_audio_bar_status(TRS("保存失败，请检查输入范围。"), THEME_DANGER_LIGHT);
        return;
    }
    ui_audio_background_refresh_geometry();
    update_audio_bar_range_hint();
    set_audio_bar_status(TRS("顶部状态条长度和位置已保存。"), THEME_SUCCESS_LIGHT);
}

static void restart_after_language_switch_cb(lv_timer_t *timer)
{
    (void)timer;
    esp_restart();
}

static void language_switch_backlight_off_cb(lv_timer_t *timer)
{
    (void)timer;
    (void)bsp_display_backlight_off();
}

static void show_language_switching_overlay(void)
{
    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_t *panel;
    lv_obj_t *label;

    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    panel = lv_obj_create(overlay);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_size(panel, 300, 128);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = create_label(panel, "切换中", THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    lv_obj_move_foreground(overlay);
}

static void on_language_option_click(lv_event_t *e)
{
    app_language_t language = (app_language_t)(uintptr_t)lv_event_get_user_data(e);

    if (app_i18n_set_language(language) == ESP_OK) {
        lv_timer_t *off_timer;
        lv_timer_t *restart_timer;

        show_language_switching_overlay();

        off_timer = lv_timer_create(language_switch_backlight_off_cb, 1080, NULL);
        if (off_timer) {
            lv_timer_set_repeat_count(off_timer, 1);
        }

        restart_timer = lv_timer_create(restart_after_language_switch_cb, 1280, NULL);
        if (restart_timer) {
            lv_timer_set_repeat_count(restart_timer, 1);
        } else {
            (void)bsp_display_backlight_off();
            esp_restart();
        }
    } else {
        set_audio_bar_status(TRS("保存语言失败"), THEME_DANGER_LIGHT);
    }
}

static void create_language_option(lv_obj_t *parent,
                                   app_language_t language,
                                   const char *title)
{
    bool selected = app_i18n_get_language() == language;
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, 178);
    lv_obj_set_height(btn, 50);
    lv_obj_set_style_bg_color(btn, selected ? THEME_PRIMARY : THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, selected ? THEME_PRIMARY_LIGHT : THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, on_language_option_click, LV_EVENT_CLICKED, (void *)(uintptr_t)language);

    lv_obj_t *label = create_label(btn, title, THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
}

static void render_language_settings_card(lv_obj_t *parent)
{
    lv_obj_t *language_card = lv_obj_create(parent);
    apply_surface(language_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(language_card, LV_PCT(100));
    lv_obj_set_height(language_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(language_card, 18, 0);
    lv_obj_set_style_pad_row(language_card, 12, 0);
    lv_obj_set_flex_flow(language_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(language_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(language_card, "语言", THEME_TEXT_SECONDARY);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *row = lv_obj_create(language_card);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    create_language_option(row, APP_LANGUAGE_ZH_CN, "简体中文");
    create_language_option(row, APP_LANGUAGE_ZH_TW, "繁體中文");
    create_language_option(row, APP_LANGUAGE_EN, "English");
}

static void render_other_settings_page(lv_obj_t *parent)
{
    create_label(parent, "其他设置", THEME_TEXT_PRIMARY);

    lv_obj_t *timezone_card = lv_obj_create(parent);
    apply_surface(timezone_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(timezone_card, LV_PCT(100));
    lv_obj_set_height(timezone_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(timezone_card, 18, 0);
    lv_obj_set_style_pad_row(timezone_card, 12, 0);
    lv_obj_set_flex_flow(timezone_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(timezone_card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(timezone_card, "当前时区", THEME_TEXT_SECONDARY);
    create_wrap_label(timezone_card,
                      "用于系统时间显示、截图文件名和 NTP 同步后的本地时间。",
                      THEME_TEXT_TERTIARY);

    lv_obj_t *timezone_options = lv_obj_create(timezone_card);
    lv_obj_set_width(timezone_options, LV_PCT(100));
    lv_obj_set_height(timezone_options, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(timezone_options, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(timezone_options, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(timezone_options, 0, 0);
    lv_obj_set_style_pad_column(timezone_options, 12, 0);
    lv_obj_set_style_pad_row(timezone_options, 10, 0);
    lv_obj_set_flex_flow(timezone_options, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(timezone_options, LV_OBJ_FLAG_SCROLLABLE);

    create_timezone_option(timezone_options, APP_SETTINGS_TIMEZONE_BEIJING);
    create_timezone_option(timezone_options, APP_SETTINGS_TIMEZONE_NEW_YORK);
    create_timezone_option(timezone_options, APP_SETTINGS_TIMEZONE_UTC);
    create_timezone_option(timezone_options, APP_SETTINGS_TIMEZONE_LONDON);
    create_timezone_option(timezone_options, APP_SETTINGS_TIMEZONE_TOKYO);
    create_timezone_option(timezone_options, APP_SETTINGS_TIMEZONE_CENTRAL_EUROPE);

    s_timezone_status_label = create_wrap_label(timezone_card, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_timezone_status_label);

    lv_obj_t *card = lv_obj_create(parent);
    apply_surface(card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(card, "顶部后台音乐条", THEME_TEXT_SECONDARY);
    create_number_input_row(card,
                            "长度（px）",
                            app_settings_get_audio_bar_width(),
                            &s_audio_bar_width_ta);
    create_number_input_row(card,
                            "距离左边（px）",
                            app_settings_get_audio_bar_x(),
                            &s_audio_bar_x_ta);

    s_audio_bar_range_label = create_wrap_label(card, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_audio_bar_range_label);
    update_audio_bar_range_hint();

    lv_obj_t *save_btn = lv_button_create(card);
    lv_obj_set_width(save_btn, LV_PCT(100));
    lv_obj_set_height(save_btn, 50);
    lv_obj_set_style_bg_color(save_btn, THEME_PRIMARY, 0);
    lv_obj_set_style_bg_opa(save_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    lv_obj_set_style_radius(save_btn, 12, 0);
    lv_obj_add_event_cb(save_btn, on_audio_bar_save_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_label = create_label(save_btn, LV_SYMBOL_SAVE " 保存顶部状态条设置", THEME_TEXT_PRIMARY);
    lv_obj_center(save_label);

    s_audio_bar_status_label = create_wrap_label(card, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_audio_bar_status_label);
}
