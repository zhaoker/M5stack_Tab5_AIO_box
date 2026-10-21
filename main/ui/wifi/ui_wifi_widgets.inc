static const lv_font_t *wifi_ipv6_font(void)
{
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return LV_FONT_DEFAULT;
#endif
}

static void apply_wifi_textarea_cursor_style(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }

    lv_obj_set_style_bg_color(ta, lv_color_hex(0xF8FAFC), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_text_color(ta, COLOR_INPUT, LV_PART_CURSOR);
    lv_obj_set_style_width(ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_pad_left(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_pad_right(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_outline_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(ta, 350, LV_PART_CURSOR);
}

static void restore_wifi_input_layout(void)
{
    if (s_status_card) {
        lv_obj_remove_flag(s_status_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_manual_title) {
        lv_obj_remove_flag(s_manual_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_panel) {
        lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    }
}

static void show_wifi_input_layout(void)
{
    if (s_status_card) {
        lv_obj_add_flag(s_status_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_manual_title) {
        lv_obj_add_flag(s_manual_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_panel) {
        lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_update_layout(s_overlay ? s_overlay : lv_scr_act());
}

static void clear_wifi_textarea_focus(void)
{
    if (s_ta_ssid) {
        lv_obj_remove_state(s_ta_ssid, LV_STATE_FOCUSED);
    }
    if (s_ta_pass) {
        lv_obj_remove_state(s_ta_pass, LV_STATE_FOCUSED);
    }
}

static void set_wifi_textarea_focus(lv_obj_t *active_ta)
{
    clear_wifi_textarea_focus();
    if (active_ta) {
        lv_obj_add_state(active_ta, LV_STATE_FOCUSED);
    }
}

static int find_saved_scan_index(const char *ssid)
{
    size_t i;

    if (!ssid || !ssid[0]) {
        return -1;
    }

    for (i = 0; i < s_scan_count; ++i) {
        if (strncmp(s_scan_results[i].ssid, ssid, sizeof(s_scan_results[i].ssid)) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static bool saved_network_is_scanned(const char *ssid)
{
    return find_saved_scan_index(ssid) >= 0;
}

static void set_scan_hint(const char *text, lv_color_t color)
{
    if (!s_scan_hint) {
        return;
    }
    lv_label_set_text(s_scan_hint, TRS(text ? text : ""));
    lv_obj_set_style_text_color(s_scan_hint, color, 0);
}

static void apply_card_style(lv_obj_t *obj, lv_color_t bg)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 14, 0);
}

static lv_obj_t *create_text_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_height(btn, WIFI_BUTTON_H);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_label_set_text(label, TRS(text));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

static void set_hint(const char *text, lv_color_t color)
{
    set_scan_hint(text, color);
}

static const char *authmode_to_text(uint8_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return TRS("开放网络");
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";
        default:
            return TRS("加密网络");
    }
}
