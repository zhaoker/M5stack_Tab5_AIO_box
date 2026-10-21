static lv_color_t power_color_for_status(const power_monitor_status_t *status)
{
    if (!status || !status->ina226_present) {
        return THEME_TEXT_SECONDARY;
    }
    if (!status->battery_present) {
        return status->usb_c_present ? THEME_ACCENT_CYAN : THEME_TEXT_SECONDARY;
    }
    if (status->battery_percent <= 15.0f) {
        return THEME_DANGER_LIGHT;
    }
    if (status->battery_percent <= 35.0f) {
        return THEME_WARNING_LIGHT;
    }
    if (status->charging) {
        return THEME_ACCENT_CYAN;
    }
    return THEME_SUCCESS_LIGHT;
}

static void update_power_chip(void)
{
    power_monitor_status_t status;
    esp_err_t err;
    char percent_text[8];
    int percent_value;
    lv_color_t color;

    if (!s_power_chip || !s_power_body || !s_power_cap || !s_power_percent) {
        return;
    }

    err = power_monitor_get_cached_status(&status) ? status.last_error : ESP_ERR_INVALID_STATE;
    color = power_color_for_status(&status);

    if (err != ESP_OK && !status.usb_c_present) {
        lv_label_set_text(s_power_percent, "--");
        lv_obj_set_style_border_color(s_power_body, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_bg_opa(s_power_body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_power_body, THEME_BG_PANEL, 0);
    } else if (!status.battery_present) {
        lv_label_set_text(s_power_percent, status.usb_c_present ? "USB" : "--");
        lv_obj_set_style_border_color(s_power_body, status.usb_c_present ? THEME_ACCENT_CYAN : THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_bg_opa(s_power_body, status.usb_c_present ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_power_body, THEME_ACCENT_CYAN, 0);
    } else {
        percent_value = (int)(status.battery_percent + 0.5f);
        if (percent_value < 0) {
            percent_value = 0;
        } else if (percent_value > 100) {
            percent_value = 100;
        }
        if (status.charging && percent_value < 100) {
            lv_label_set_text(s_power_percent, LV_SYMBOL_CHARGE);
        } else {
            snprintf(percent_text, sizeof(percent_text), "%d", percent_value);
            lv_label_set_text(s_power_percent, percent_text);
        }
        lv_obj_set_style_border_color(s_power_body, color, 0);
        lv_obj_set_style_bg_opa(s_power_body, LV_OPA_30, 0);
        lv_obj_set_style_bg_color(s_power_body, color, 0);
    }

    lv_obj_set_style_border_color(s_power_cap, color, 0);
    lv_obj_set_style_bg_color(s_power_cap, color, 0);
    lv_obj_set_style_text_color(s_power_percent, color, 0);
    lv_obj_center(s_power_percent);
}

static void close_info_overlay(void)
{
    s_net_info_generation++;
    s_net_info_public_ipv4_label = NULL;
    if (s_info_overlay) {
        lv_obj_del(s_info_overlay);
        s_info_overlay = NULL;
    }
}

static void close_power_overlay(void)
{
    if (s_power_detail_timer) {
        lv_timer_del(s_power_detail_timer);
        s_power_detail_timer = NULL;
    }
    memset(s_power_detail_values, 0, sizeof(s_power_detail_values));
    s_boot_auto_charge_slider = NULL;
    s_boot_auto_charge_off_label = NULL;
    s_boot_auto_charge_on_label = NULL;
    s_charge_mode_slider = NULL;
    s_charge_standard_label = NULL;
    s_charge_fast_label = NULL;
    if (s_power_overlay) {
        lv_obj_del(s_power_overlay);
        s_power_overlay = NULL;
    }
}

static void close_delete_overlay(void)
{
    if (s_delete_overlay) {
        lv_obj_del(s_delete_overlay);
        s_delete_overlay = NULL;
    }
}

static void close_action_overlay(void)
{
    if (s_action_overlay) {
        lv_obj_del(s_action_overlay);
        s_action_overlay = NULL;
    }
}

static void close_transient_overlays(void)
{
    close_action_overlay();
    close_delete_overlay();
    close_info_overlay();
    close_power_overlay();
}

static void reset_form_state(void)
{
    memset(&s_form, 0, sizeof(s_form));
    s_form.edit_index = -1;
}

static void close_server_form(void)
{
    close_icon_picker();
    if (s_form.hw_keyboard_timer) {
        lv_timer_del(s_form.hw_keyboard_timer);
        s_form.hw_keyboard_timer = NULL;
    }
    tab5_keyboard_deinit();
    ui_hw_pinyin_input_destroy(&s_form.hw_pinyin);
    ui_pinyin_ime_ext_forget_objects(s_form.pinyin_ext);
    if (s_form.overlay) {
        lv_obj_del(s_form.overlay);
    }
    ui_pinyin_ime_ext_destroy(&s_form.pinyin_ext);
    reset_form_state();
}

static lv_obj_t *create_modal_overlay(lv_event_cb_t on_click)
{
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, THEME_OVERLAY_OPA, 0);
    lv_obj_set_style_border_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    if (on_click) {
        lv_obj_add_event_cb(overlay, on_click, LV_EVENT_CLICKED, NULL);
    }
    return overlay;
}

static lv_obj_t *create_modal_panel(lv_obj_t *overlay, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, width, height);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(panel, &s_style_modal_panel, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *create_power_detail_row(lv_obj_t *parent, const char *name, int value_index)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, TRS(name));
    lv_obj_set_width(name_label, 210);
    lv_obj_set_style_text_font(name_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(name_label, THEME_TEXT_SECONDARY, 0);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, "--");
    lv_obj_set_width(value_label, 310);
    lv_obj_set_style_text_font(value_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(value_label, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);

    if (value_index >= 0 && value_index < POWER_DETAIL_COUNT) {
        s_power_detail_values[value_index] = value_label;
        lv_obj_null_on_delete(&s_power_detail_values[value_index]);
    }

    return row;
}

static void set_power_detail_value(int index, const char *text, lv_color_t color)
{
    if (index < 0 || index >= POWER_DETAIL_COUNT || !s_power_detail_values[index]) {
        return;
    }
    lv_label_set_text(s_power_detail_values[index], text ? text : "--");
    lv_obj_set_style_text_color(s_power_detail_values[index], color, 0);
}

static void update_boot_auto_charge_controls(bool enabled)
{
    if (s_boot_auto_charge_slider) {
        lv_slider_set_value(s_boot_auto_charge_slider, enabled ? 1 : 0, LV_ANIM_OFF);
    }
    if (s_boot_auto_charge_off_label) {
        lv_obj_set_style_text_color(s_boot_auto_charge_off_label,
                                    enabled ? THEME_TEXT_TERTIARY : THEME_TEXT_PRIMARY, 0);
    }
    if (s_boot_auto_charge_on_label) {
        lv_obj_set_style_text_color(s_boot_auto_charge_on_label,
                                    enabled ? THEME_ACCENT_CYAN : THEME_TEXT_TERTIARY, 0);
    }
}

static void update_charge_mode_controls(bool fast_charge_enabled)
{
    if (s_charge_mode_slider) {
        lv_slider_set_value(s_charge_mode_slider, fast_charge_enabled ? 1 : 0, LV_ANIM_OFF);
    }
    if (s_charge_standard_label) {
        lv_obj_set_style_text_color(s_charge_standard_label,
                                    fast_charge_enabled ? THEME_TEXT_TERTIARY : THEME_TEXT_PRIMARY, 0);
    }
    if (s_charge_fast_label) {
        lv_obj_set_style_text_color(s_charge_fast_label,
                                    fast_charge_enabled ? THEME_ACCENT_CYAN : THEME_TEXT_TERTIARY, 0);
    }
}

static void update_power_detail_values(void)
{
    power_monitor_status_t status;
    memset(&status, 0, sizeof(status));
    (void)power_monitor_get_cached_status(&status);
    char text[96];
    lv_color_t state_color = power_color_for_status(&status);

    if (!s_power_overlay) {
        return;
    }

    set_power_detail_value(POWER_DETAIL_BATTERY,
                           status.battery_present ? TRS("已检测到") : TRS("未检测到"),
                           status.battery_present ? THEME_SUCCESS_LIGHT : THEME_TEXT_SECONDARY);

    snprintf(text, sizeof(text), "%.2f V", status.bus_voltage_v);
    set_power_detail_value(POWER_DETAIL_VOLTAGE, text, THEME_TEXT_PRIMARY);

    snprintf(text, sizeof(text), "%.2f A (%s)",
             status.display_current_a,
             status.charging ? TRS("充电") : (status.discharging ? TRS("放电") : TRS("待机")));
    set_power_detail_value(POWER_DETAIL_CURRENT, text, state_color);

    snprintf(text, sizeof(text), "%.2f W", status.power_w);
    set_power_detail_value(POWER_DETAIL_POWER, text, THEME_TEXT_PRIMARY);

    if (!status.battery_present) {
        set_power_detail_value(POWER_DETAIL_VOLTAGE, "-", THEME_TEXT_SECONDARY);
        set_power_detail_value(POWER_DETAIL_CURRENT, "-", THEME_TEXT_SECONDARY);
        set_power_detail_value(POWER_DETAIL_POWER, "-", THEME_TEXT_SECONDARY);
    }

    snprintf(text, sizeof(text), "%.0f%%", status.battery_percent);
    set_power_detail_value(POWER_DETAIL_PERCENT, text, state_color);

    if (status.power_only_usb) {
        set_power_detail_value(POWER_DETAIL_SOURCE, TRS("USB-C 外部供电"), THEME_ACCENT_CYAN);
    } else if (status.usb_c_present && status.battery_present) {
        set_power_detail_value(POWER_DETAIL_SOURCE,
                               status.charging ? TRS("USB-C + 电池充电") : TRS("USB-C + 电池待机"),
                               THEME_ACCENT_CYAN);
    } else if (status.battery_present) {
        set_power_detail_value(POWER_DETAIL_SOURCE, TRS("电池"), state_color);
    } else {
        set_power_detail_value(POWER_DETAIL_SOURCE, TRS("未知"), THEME_TEXT_SECONDARY);
    }

    set_power_detail_value(POWER_DETAIL_USB_C,
                           status.usb_c_present ? TRS("已插入") : TRS("未插入"),
                           status.usb_c_present ? THEME_ACCENT_CYAN : THEME_TEXT_SECONDARY);

    update_boot_auto_charge_controls(app_settings_get_boot_auto_charge_enabled());
    update_charge_mode_controls(app_settings_get_charge_fast_enabled());
}

static void on_boot_auto_charge_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bool enabled = lv_slider_get_value(slider) > 0;
    bool fast_charge_enabled = app_settings_get_charge_fast_enabled();
    esp_err_t save_err = app_settings_set_boot_auto_charge_enabled(enabled);
    esp_err_t apply_err = power_monitor_set_charge_enabled(enabled, fast_charge_enabled);

    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "save boot auto charge failed: %s", esp_err_to_name(save_err));
    }
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "apply boot auto charge failed: %s", esp_err_to_name(apply_err));
    }

    update_boot_auto_charge_controls(app_settings_get_boot_auto_charge_enabled());
    update_charge_mode_controls(app_settings_get_charge_fast_enabled());
    update_power_chip();
    update_power_detail_values();
}

static void on_charge_mode_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bool fast_charge_enabled = lv_slider_get_value(slider) > 0;
    bool charge_enabled = app_settings_get_boot_auto_charge_enabled();
    esp_err_t save_err = app_settings_set_charge_fast_enabled(fast_charge_enabled);
    esp_err_t apply_err = power_monitor_set_charge_enabled(charge_enabled, fast_charge_enabled);

    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "save charge speed failed: %s", esp_err_to_name(save_err));
    }
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "set charge speed failed: %s", esp_err_to_name(apply_err));
    }

    update_boot_auto_charge_controls(app_settings_get_boot_auto_charge_enabled());
    update_charge_mode_controls(app_settings_get_charge_fast_enabled());
    update_power_chip();
    update_power_detail_values();
}

static void on_power_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_power_overlay) {
        close_power_overlay();
    }
}

static void on_power_close(lv_event_t *e)
{
    (void)e;
    close_power_overlay();
}

static void show_power_detail(void)
{
    lv_obj_t *panel;
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *close_btn;
    lv_obj_t *close_label;
    lv_obj_t *body;
    lv_obj_t *detail_list;
    lv_obj_t *charge_card;
    lv_obj_t *charge_name;
    lv_obj_t *charge_controls;

    close_power_overlay();

    s_power_overlay = create_modal_overlay(on_power_overlay_click);
    lv_obj_null_on_delete(&s_power_overlay);

    panel = create_modal_panel(s_power_overlay, POWER_PANEL_W, POWER_PANEL_H);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    header = lv_obj_create(panel);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 48);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(header);
    lv_label_set_text_fmt(title, LV_SYMBOL_BATTERY_FULL " %s", TRS("电源监测"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 46, 46);
    apply_surface_style(close_btn, THEME_BG_HOVER, THEME_BORDER_SECONDARY, 12);
    lv_obj_add_event_cb(close_btn, on_power_close, LV_EVENT_CLICKED, NULL);
    close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(close_label, THEME_TEXT_PRIMARY, 0);
    lv_obj_center(close_label);

    body = lv_obj_create(panel);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    apply_surface_style(body, THEME_BG_PRIMARY, THEME_BORDER_PRIMARY, 12);
    lv_obj_set_style_pad_all(body, 16, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    detail_list = lv_obj_create(body);
    lv_obj_set_width(detail_list, LV_PCT(100));
    lv_obj_set_flex_grow(detail_list, 1);
    lv_obj_set_style_bg_opa(detail_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(detail_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(detail_list, 0, 0);
    lv_obj_set_style_pad_row(detail_list, 8, 0);
    lv_obj_set_flex_flow(detail_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(detail_list, LV_OBJ_FLAG_SCROLLABLE);

    create_power_detail_row(detail_list, "电池", POWER_DETAIL_BATTERY);
    create_power_detail_row(detail_list, "电池电压", POWER_DETAIL_VOLTAGE);
    create_power_detail_row(detail_list, "电池电流", POWER_DETAIL_CURRENT);
    create_power_detail_row(detail_list, "电池功率", POWER_DETAIL_POWER);
    create_power_detail_row(detail_list, "电量估算", POWER_DETAIL_PERCENT);
    create_power_detail_row(detail_list, "供电来源", POWER_DETAIL_SOURCE);
    create_power_detail_row(detail_list, "USB-C", POWER_DETAIL_USB_C);

    /* 充电速度栏布局参数：现场微调时优先改这里。
     * 坐标均以 charge_controls 左上角为基准；X 变小=往左，X 变大=往右。
     * 如果左右文字被边框/旋钮挡住，先调 PAD，再调各控件的 X/W。
     */
    enum {
        CHARGE_CARD_H = 62,              /* 整条充电速度卡片高度 */
        CHARGE_CARD_PAD_X = 14,          /* 卡片左右内边距，增大可让内容离边框更远 */
        CHARGE_CARD_PAD_Y = 10,          /* 卡片上下内边距 */
        CHARGE_CARD_GAP = 8,             /* “充电速度”和右侧控件组之间的间距 */
        CHARGE_NAME_W = 120,             /* “充电速度”文字宽度 */
        CHARGE_CONTROLS_W = 300,         /* 右侧 0.5A/滑块/1A 整组宽度 */
        CHARGE_CONTROLS_H = 40,          /* 右侧控件组高度 */
        CHARGE_CONTROLS_SHIFT_X = 10,    /* 右侧控件组整体左右偏移，负数=整体往左 */
        CHARGE_CONTROLS_PAD_L = 8,       /* 右侧控件组左 padding */
        CHARGE_CONTROLS_PAD_R = 8,       /* 右侧控件组右 padding */
        CHARGE_STD_X = 20,                /* 0.5A 的 X 位置 */
        CHARGE_STD_W = 60,               /* 0.5A 的文字宽度 */
        CHARGE_SLIDER_X = 105,            /* 滑块的 X 位置 */
        CHARGE_SLIDER_W = 90,            /* 滑块宽度 */
        CHARGE_SLIDER_H = 22,            /* 滑块高度 */
        CHARGE_FAST_X = 210,             /* 1A 的 X 位置 */
        CHARGE_FAST_W = 46,              /* 1A 的文字宽度 */
    };

    charge_card = lv_obj_create(body);
    lv_obj_set_width(charge_card, LV_PCT(100));
    lv_obj_set_height(charge_card, CHARGE_CARD_H);
    apply_surface_style(charge_card, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_style_pad_left(charge_card, CHARGE_CARD_PAD_X, 0);
    lv_obj_set_style_pad_right(charge_card, CHARGE_CARD_PAD_X, 0);
    lv_obj_set_style_pad_top(charge_card, CHARGE_CARD_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(charge_card, CHARGE_CARD_PAD_Y, 0);
    lv_obj_set_style_pad_column(charge_card, CHARGE_CARD_GAP, 0);
    lv_obj_set_flex_flow(charge_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(charge_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(charge_card, LV_OBJ_FLAG_SCROLLABLE);

    charge_name = lv_label_create(charge_card);
    lv_label_set_text(charge_name, TRS("开机自动充电"));
    lv_obj_set_width(charge_name, CHARGE_NAME_W);
    lv_obj_set_style_text_font(charge_name, &APP_FONT, 0);
    lv_obj_set_style_text_color(charge_name, THEME_TEXT_SECONDARY, 0);

    charge_controls = lv_obj_create(charge_card);
    lv_obj_set_size(charge_controls, CHARGE_CONTROLS_W, CHARGE_CONTROLS_H);
    lv_obj_set_style_bg_opa(charge_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(charge_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(charge_controls, CHARGE_CONTROLS_PAD_L, 0);
    lv_obj_set_style_pad_right(charge_controls, CHARGE_CONTROLS_PAD_R, 0);
    lv_obj_set_style_pad_top(charge_controls, 0, 0);
    lv_obj_set_style_pad_bottom(charge_controls, 0, 0);
    lv_obj_clear_flag(charge_controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_translate_x(charge_controls, CHARGE_CONTROLS_SHIFT_X, 0);

    s_boot_auto_charge_off_label = lv_label_create(charge_controls);
    lv_obj_null_on_delete(&s_boot_auto_charge_off_label);
    lv_label_set_text(s_boot_auto_charge_off_label, TRS("关"));
    lv_obj_set_width(s_boot_auto_charge_off_label, CHARGE_STD_W);
    lv_obj_set_style_text_font(s_boot_auto_charge_off_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_boot_auto_charge_off_label, THEME_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_align(s_boot_auto_charge_off_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_boot_auto_charge_off_label, LV_ALIGN_LEFT_MID, CHARGE_STD_X, 0);

    s_boot_auto_charge_slider = lv_slider_create(charge_controls);
    lv_obj_null_on_delete(&s_boot_auto_charge_slider);
    lv_obj_set_width(s_boot_auto_charge_slider, CHARGE_SLIDER_W);
    lv_obj_set_height(s_boot_auto_charge_slider, CHARGE_SLIDER_H);
    lv_slider_set_range(s_boot_auto_charge_slider, 0, 1);
    lv_slider_set_value(s_boot_auto_charge_slider,
                        app_settings_get_boot_auto_charge_enabled() ? 1 : 0,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_boot_auto_charge_slider, THEME_BG_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_boot_auto_charge_slider, THEME_ACCENT_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_boot_auto_charge_slider, THEME_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_add_event_cb(s_boot_auto_charge_slider, on_boot_auto_charge_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(s_boot_auto_charge_slider, LV_ALIGN_LEFT_MID, CHARGE_SLIDER_X, 0);

    s_boot_auto_charge_on_label = lv_label_create(charge_controls);
    lv_obj_null_on_delete(&s_boot_auto_charge_on_label);
    lv_label_set_text(s_boot_auto_charge_on_label, TRS("开"));
    lv_obj_set_width(s_boot_auto_charge_on_label, CHARGE_FAST_W);
    lv_obj_set_style_text_font(s_boot_auto_charge_on_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_boot_auto_charge_on_label, THEME_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_align(s_boot_auto_charge_on_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_boot_auto_charge_on_label, LV_ALIGN_LEFT_MID, CHARGE_FAST_X, 0);

    charge_card = lv_obj_create(body);
    lv_obj_set_width(charge_card, LV_PCT(100));
    lv_obj_set_height(charge_card, CHARGE_CARD_H);
    apply_surface_style(charge_card, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_style_pad_left(charge_card, CHARGE_CARD_PAD_X, 0);
    lv_obj_set_style_pad_right(charge_card, CHARGE_CARD_PAD_X, 0);
    lv_obj_set_style_pad_top(charge_card, CHARGE_CARD_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(charge_card, CHARGE_CARD_PAD_Y, 0);
    lv_obj_set_style_pad_column(charge_card, CHARGE_CARD_GAP, 0);
    lv_obj_set_flex_flow(charge_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(charge_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(charge_card, LV_OBJ_FLAG_SCROLLABLE);

    charge_name = lv_label_create(charge_card);
    lv_label_set_text(charge_name, TRS("充电速度"));
    lv_obj_set_width(charge_name, CHARGE_NAME_W);
    lv_obj_set_style_text_font(charge_name, &APP_FONT, 0);
    lv_obj_set_style_text_color(charge_name, THEME_TEXT_SECONDARY, 0);

    charge_controls = lv_obj_create(charge_card);
    lv_obj_set_size(charge_controls, CHARGE_CONTROLS_W, CHARGE_CONTROLS_H);
    lv_obj_set_style_bg_opa(charge_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(charge_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(charge_controls, CHARGE_CONTROLS_PAD_L, 0);
    lv_obj_set_style_pad_right(charge_controls, CHARGE_CONTROLS_PAD_R, 0);
    lv_obj_set_style_pad_top(charge_controls, 0, 0);
    lv_obj_set_style_pad_bottom(charge_controls, 0, 0);
    lv_obj_clear_flag(charge_controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_translate_x(charge_controls, CHARGE_CONTROLS_SHIFT_X, 0);

    s_charge_standard_label = lv_label_create(charge_controls);
    lv_obj_null_on_delete(&s_charge_standard_label);
    lv_label_set_text(s_charge_standard_label, "0.5A");
    lv_obj_set_width(s_charge_standard_label, CHARGE_STD_W);
    lv_obj_set_style_text_font(s_charge_standard_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_charge_standard_label, THEME_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_align(s_charge_standard_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_charge_standard_label, LV_ALIGN_LEFT_MID, CHARGE_STD_X, 0);

    s_charge_mode_slider = lv_slider_create(charge_controls);
    lv_obj_null_on_delete(&s_charge_mode_slider);
    lv_obj_set_width(s_charge_mode_slider, CHARGE_SLIDER_W);
    lv_obj_set_height(s_charge_mode_slider, CHARGE_SLIDER_H);
    lv_slider_set_range(s_charge_mode_slider, 0, 1);
    lv_slider_set_value(s_charge_mode_slider, app_settings_get_charge_fast_enabled() ? 1 : 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_charge_mode_slider, THEME_BG_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_charge_mode_slider, THEME_ACCENT_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_charge_mode_slider, THEME_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_add_event_cb(s_charge_mode_slider, on_charge_mode_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(s_charge_mode_slider, LV_ALIGN_LEFT_MID, CHARGE_SLIDER_X, 0);

    s_charge_fast_label = lv_label_create(charge_controls);
    lv_obj_null_on_delete(&s_charge_fast_label);
    lv_label_set_text(s_charge_fast_label, "1A");
    lv_obj_set_width(s_charge_fast_label, CHARGE_FAST_W);
    lv_obj_set_style_text_font(s_charge_fast_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_charge_fast_label, THEME_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_align(s_charge_fast_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_charge_fast_label, LV_ALIGN_LEFT_MID, CHARGE_FAST_X, 0);

    update_power_detail_values();
    s_power_detail_timer = lv_timer_create(power_detail_timer_cb, 1500, NULL);
}

static void on_info_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_info_overlay) {
        close_info_overlay();
    }
}

static void on_info_close(lv_event_t *e)
{
    (void)e;
    close_info_overlay();
}

static void show_info_dialog(const char *title, const char *message)
{
    lv_obj_t *panel;
    lv_obj_t *title_label;
    lv_obj_t *msg;
    lv_obj_t *btn;

    close_info_overlay();

    s_info_overlay = create_modal_overlay(on_info_overlay_click);
    lv_obj_null_on_delete(&s_info_overlay);

    panel = create_modal_panel(s_info_overlay, INFO_PANEL_W, INFO_PANEL_H);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    title_label = lv_label_create(panel);
    lv_label_set_text(title_label, TRS(title));
    lv_obj_set_style_text_font(title_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(title_label, THEME_TEXT_PRIMARY, 0);

    msg = lv_label_create(panel);
    lv_obj_set_width(msg, LV_PCT(100));
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msg, TRS(message));
    lv_obj_set_style_text_font(msg, &APP_FONT, 0);
    lv_obj_set_style_text_color(msg, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);

    btn = create_button(panel, "知道了", &s_style_primary_btn, &s_style_primary_btn_pressed);
    lv_obj_set_size(btn, 160, 52);
    lv_obj_add_event_cb(btn, on_info_close, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *create_net_info_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label;

    label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, NET_INFO_ROW_H);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text ? text : "-");
    lv_obj_set_style_text_font(label, net_addr_font(), 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);

    return label;
}

static lv_obj_t *show_net_info_dialog_base(const char *title, lv_coord_t panel_w, size_t row_count)
{
    lv_obj_t *panel;
    lv_obj_t *title_label;
    lv_obj_t *body;
    lv_obj_t *btn;
    lv_coord_t panel_h;

    if (row_count == 0) {
        row_count = 1;
    }

    close_info_overlay();

    s_info_overlay = create_modal_overlay(on_info_overlay_click);
    lv_obj_null_on_delete(&s_info_overlay);

    panel_h = 190 + ((lv_coord_t)row_count * NET_INFO_ROW_H);
    if (panel_h < NET_INFO_PANEL_MIN_H) {
        panel_h = NET_INFO_PANEL_MIN_H;
    }
    if (panel_h > NET_INFO_PANEL_MAX_H) {
        panel_h = NET_INFO_PANEL_MAX_H;
    }

    panel = create_modal_panel(s_info_overlay, panel_w, panel_h);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    title_label = lv_label_create(panel);
    lv_label_set_text(title_label, TRS(title));
    lv_obj_set_style_text_font(title_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(title_label, THEME_TEXT_PRIMARY, 0);

    body = lv_obj_create(panel);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, 16, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    btn = create_button(panel, "确定", &s_style_primary_btn, &s_style_primary_btn_pressed);
    lv_obj_set_size(btn, 160, 52);
    lv_obj_add_event_cb(btn, on_info_close, LV_EVENT_CLICKED, NULL);

    return body;
}

static void show_ipv4_info_dialog(void)
{
    char ipv4[16] = {0};
    char ipv6_unused[WIFI_IPV6_TEXT_LEN] = {0};
    char local_line[32];
    public_ipv4_task_arg_t *arg;
    lv_obj_t *body;

    (void)wifi_manager_get_current_ip_text(ipv4, sizeof(ipv4),
                                           ipv6_unused, sizeof(ipv6_unused));
    snprintf(local_line, sizeof(local_line), "Local IP:    %s", ipv4[0] ? ipv4 : "-");

    body = show_net_info_dialog_base("IPv4 Address", NET_INFO_IPV4_PANEL_W, 2);
    create_net_info_row(body, local_line);
    s_net_info_public_ipv4_label = create_net_info_row(body, "Public IP:    querying...");
    lv_obj_null_on_delete(&s_net_info_public_ipv4_label);

    arg = calloc(1, sizeof(*arg));
    if (!arg) {
        if (s_net_info_public_ipv4_label) {
            lv_label_set_text(s_net_info_public_ipv4_label, "Public IP:    -");
        }
        return;
    }

    arg->generation = s_net_info_generation;
    if (xTaskCreate(public_ipv4_query_task, "pub_ipv4",
                    PUBLIC_IPV4_TASK_STACK, arg, 3, NULL) != pdPASS) {
        free(arg);
        if (s_net_info_public_ipv4_label) {
            lv_label_set_text(s_net_info_public_ipv4_label, "Public IP:    -");
        }
    }
}

static void show_ipv6_info_dialog(void)
{
    char addrs[WIFI_MAX_IPV6_ADDRS][WIFI_IPV6_TEXT_LEN];
    size_t count = 0;
    lv_obj_t *body;

    if (wifi_manager_get_all_ipv6_text(addrs, WIFI_MAX_IPV6_ADDRS, &count) == ESP_OK && count > 0) {
        body = show_net_info_dialog_base("IPv6 Address", NET_INFO_IPV6_PANEL_W, count);
        for (size_t i = 0; i < count; i++) {
            create_net_info_row(body, addrs[i]);
        }
    } else {
        body = show_net_info_dialog_base("IPv6 Address", NET_INFO_IPV6_PANEL_W, 1);
        create_net_info_row(body, "IPv6 -");
    }
}
