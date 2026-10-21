void ui_wifi_create(lv_obj_t *parent)
{
    lv_coord_t screen_w;
    lv_coord_t panel_w;
    lv_obj_t *header;
    lv_obj_t *body;
    lv_obj_t *left_col;
    lv_obj_t *right_col;
    lv_obj_t *scan_tools;
    lv_obj_t *title;
    lv_obj_t *btn_close;
    lv_obj_t *status_card;
    lv_obj_t *manual_card;
    lv_obj_t *row_ssid;
    lv_obj_t *row_pass;
    lv_obj_t *btn_row;
    lv_obj_t *btn_refresh;
    lv_obj_t *btn_disconnect;
    lv_obj_t *btn_connect;
    lv_obj_t *scan_section;

    if (s_overlay) {
        destroy_overlay();
    }

    refresh_saved_networks();
    screen_w = lv_display_get_horizontal_resolution(NULL);
    panel_w = screen_w - 40;
    if (panel_w < WIFI_PANEL_MIN_W) {
        panel_w = WIFI_PANEL_MIN_W;
    }

    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, panel_w, WIFI_PANEL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    apply_card_style(s_panel, COLOR_BG);
    lv_obj_set_style_pad_all(s_panel, WIFI_SIDE_PAD, 0);
    lv_obj_set_style_pad_row(s_panel, WIFI_GAP, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(s_panel);
    lv_obj_set_size(header, LV_PCT(100), WIFI_TITLE_H);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(header);
    lv_label_set_text_fmt(title, LV_SYMBOL_WIFI "  %s", TRS("WiFi 管理"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    btn_close = create_text_button(header, LV_SYMBOL_CLOSE, COLOR_DANGER, on_close_click, NULL);
    lv_obj_set_size(btn_close, 42, 42);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, 0, 0);

    body = lv_obj_create(s_panel);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_column(body, WIFI_GAP, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    left_col = lv_obj_create(body);
    lv_obj_set_size(left_col, WIFI_LEFT_COL_W, LV_PCT(100));
    lv_obj_set_flex_grow(left_col, 0);
    lv_obj_set_style_bg_opa(left_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(left_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(left_col, 0, 0);
    lv_obj_set_style_pad_row(left_col, WIFI_GAP, 0);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(left_col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(left_col, LV_SCROLLBAR_MODE_ACTIVE);

    right_col = lv_obj_create(body);
    lv_obj_set_flex_grow(right_col, 1);
    lv_obj_set_height(right_col, LV_PCT(100));
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(right_col, 0, 0);
    lv_obj_set_style_pad_row(right_col, WIFI_GAP, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);

    status_card = lv_obj_create(left_col);
    s_status_card = status_card;
    lv_obj_set_width(status_card, LV_PCT(100));
    lv_obj_set_height(status_card, LV_SIZE_CONTENT);
    apply_card_style(status_card, COLOR_PANEL);
    lv_obj_set_style_pad_all(status_card, 14, 0);
    lv_obj_set_style_pad_row(status_card, 8, 0);
    lv_obj_set_flex_flow(status_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_state = lv_label_create(status_card);
    lv_obj_set_style_text_font(s_lbl_state, &APP_FONT, 0);
    lv_label_set_text(s_lbl_state, "");

    s_lbl_ipv4 = lv_label_create(status_card);
    lv_obj_set_width(s_lbl_ipv4, LV_PCT(100));
    lv_obj_set_style_text_font(s_lbl_ipv4, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_lbl_ipv4, COLOR_TEXT_SUB, 0);
    lv_label_set_long_mode(s_lbl_ipv4, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_lbl_ipv4, "");

    s_lbl_ipv6 = lv_label_create(status_card);
    lv_obj_set_width(s_lbl_ipv6, LV_PCT(100));
    lv_obj_set_style_text_font(s_lbl_ipv6, wifi_ipv6_font(), 0);
    lv_obj_set_style_text_color(s_lbl_ipv6, COLOR_TEXT_SUB, 0);
    lv_label_set_long_mode(s_lbl_ipv6, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_lbl_ipv6, "");

    manual_card = lv_obj_create(left_col);
    lv_obj_set_width(manual_card, LV_PCT(100));
    lv_obj_set_height(manual_card, LV_SIZE_CONTENT);
    apply_card_style(manual_card, COLOR_PANEL);
    lv_obj_set_style_pad_all(manual_card, WIFI_MANUAL_PAD, 0);
    lv_obj_set_style_pad_row(manual_card, WIFI_SMALL_GAP, 0);
    lv_obj_set_flex_flow(manual_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(manual_card, LV_OBJ_FLAG_SCROLLABLE);

    s_manual_title = lv_label_create(manual_card);
    lv_label_set_text(s_manual_title, TRS("手动连接 / 新增 WiFi"));
    lv_obj_set_style_text_font(s_manual_title, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_manual_title, COLOR_TEXT, 0);

    row_ssid = lv_obj_create(manual_card);
    lv_obj_set_width(row_ssid, LV_PCT(100));
    lv_obj_set_height(row_ssid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row_ssid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row_ssid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row_ssid, 0, 0);
    lv_obj_set_style_pad_column(row_ssid, WIFI_FIELD_GAP, 0);
    lv_obj_set_flex_flow(row_ssid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_ssid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row_ssid, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(row_ssid);
    lv_label_set_text(title, "SSID");
    lv_obj_set_width(title, WIFI_FIELD_LABEL_W);
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SUB, 0);

    s_ta_ssid = lv_textarea_create(row_ssid);
    lv_obj_set_size(s_ta_ssid, WIFI_FIELD_INPUT_W, WIFI_INPUT_H);
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_textarea_set_placeholder_text(s_ta_ssid, TRS("输入名称或从扫描列表连接"));
    lv_obj_set_style_text_font(s_ta_ssid, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_ta_ssid, COLOR_TEXT, 0);
    lv_obj_set_style_text_color(s_ta_ssid, COLOR_TEXT_SUB, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_ta_ssid, COLOR_INPUT, 0);
    lv_obj_set_style_bg_opa(s_ta_ssid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ta_ssid, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_ta_ssid, 1, 0);
    lv_obj_set_style_radius(s_ta_ssid, 12, 0);
    apply_wifi_textarea_cursor_style(s_ta_ssid);

    row_pass = lv_obj_create(manual_card);
    lv_obj_set_width(row_pass, LV_PCT(100));
    lv_obj_set_height(row_pass, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row_pass, 0, 0);
    lv_obj_set_style_pad_column(row_pass, WIFI_FIELD_GAP, 0);
    lv_obj_set_flex_flow(row_pass, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_pass, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row_pass, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(row_pass);
    lv_label_set_text(title, TRS("密码"));
    lv_obj_set_width(title, WIFI_FIELD_LABEL_W);
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SUB, 0);

    s_ta_pass = lv_textarea_create(row_pass);
    lv_obj_set_size(s_ta_pass, WIFI_FIELD_INPUT_W, WIFI_INPUT_H);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_placeholder_text(s_ta_pass, TRS("开放网络可留空"));
    lv_obj_set_style_text_font(s_ta_pass, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_ta_pass, COLOR_TEXT, 0);
    lv_obj_set_style_text_color(s_ta_pass, COLOR_TEXT_SUB, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_ta_pass, COLOR_INPUT, 0);
    lv_obj_set_style_bg_opa(s_ta_pass, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ta_pass, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_ta_pass, 1, 0);
    lv_obj_set_style_radius(s_ta_pass, 12, 0);
    apply_wifi_textarea_cursor_style(s_ta_pass);

    lv_obj_add_event_cb(s_ta_ssid, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_pass, on_ta_focus, LV_EVENT_FOCUSED, NULL);

    btn_row = lv_obj_create(manual_card);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    btn_disconnect = create_text_button(btn_row, "断开", COLOR_DANGER, on_disconnect_click, NULL);
    lv_obj_set_width(btn_disconnect, app_i18n_get_language() == APP_LANGUAGE_EN ? 142 : 96);

    btn_connect = create_text_button(btn_row, "保存并连接", COLOR_PRIMARY, on_connect_click, NULL);
    lv_obj_set_width(btn_connect, app_i18n_get_language() == APP_LANGUAGE_EN ? 160 : 148);

    s_saved_list = NULL;

    scan_tools = lv_obj_create(right_col);
    lv_obj_set_width(scan_tools, LV_PCT(100));
    lv_obj_set_height(scan_tools, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(scan_tools, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(scan_tools, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(scan_tools, 0, 0);
    lv_obj_set_style_pad_column(scan_tools, WIFI_GAP, 0);
    lv_obj_set_flex_flow(scan_tools, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_tools, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(scan_tools, LV_OBJ_FLAG_SCROLLABLE);

    s_scan_hint = lv_label_create(scan_tools);
    lv_label_set_text(s_scan_hint, TRS("扫描中......"));
    lv_obj_set_style_text_font(s_scan_hint, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_scan_hint, COLOR_TEXT_SUB, 0);
    lv_obj_set_flex_grow(s_scan_hint, 1);

    btn_refresh = create_text_button(scan_tools, "扫描 / 刷新", COLOR_MUTED, on_scan_refresh, NULL);
    lv_obj_set_width(btn_refresh, 200);

    scan_section = create_section(right_col, "WiFi 列表", 0, &s_scan_list);
    (void)scan_section;

    s_kb = lv_keyboard_create(s_overlay);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(s_kb, APP_KEYBOARD_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_kb, APP_KEYBOARD_FONT, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_CANCEL, NULL);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    s_pinyin_kb = lv_keyboard_create(s_overlay);
    lv_obj_add_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(s_pinyin_kb, APP_KEYBOARD_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_pinyin_kb, APP_KEYBOARD_FONT, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_pinyin_kb, on_kb_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_pinyin_kb, on_kb_ready, LV_EVENT_CANCEL, NULL);
    lv_obj_align(s_pinyin_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    ui_hw_pinyin_input_config_t pinyin_cfg = {
        .parent = s_overlay,
        .target_ta = s_ta_ssid,
        .anchor = s_ta_ssid,
        .width = 0,
        .height = 52,
    };
    s_hw_pinyin = ui_hw_pinyin_input_create(&pinyin_cfg);
    sync_wifi_hw_pinyin_target();

    s_hw_keyboard_present = (tab5_keyboard_init() == ESP_OK);
    if (s_hw_keyboard_present) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Physical keyboard detected; WiFi soft keyboard disabled");
    }
    s_hw_keyboard_timer = lv_timer_create(hw_keyboard_timer_cb, HW_KEYBOARD_POLL_MS, NULL);

#if LV_USE_IME_PINYIN != 0
    if (app_i18n_default_chinese_input()) {
        s_pinyin_ime = lv_ime_pinyin_create(s_overlay);
        lv_obj_set_style_text_font(s_pinyin_ime, APP_KEYBOARD_FONT, 0);
        lv_ime_pinyin_set_keyboard(s_pinyin_ime, s_pinyin_kb);
        lv_ime_pinyin_set_dict(s_pinyin_ime, g_app_pinyin_dict);
        lv_ime_pinyin_set_mode(s_pinyin_ime, LV_IME_PINYIN_MODE_K26);

        s_pinyin_cand_panel = lv_ime_pinyin_get_cand_panel(s_pinyin_ime);
        lv_obj_set_size(s_pinyin_cand_panel, lv_display_get_horizontal_resolution(NULL), 52);
        lv_obj_align_to(s_pinyin_cand_panel, s_pinyin_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(s_pinyin_cand_panel, COLOR_PANEL, 0);
        lv_obj_set_style_bg_opa(s_pinyin_cand_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_pinyin_cand_panel, 1, 0);
        lv_obj_set_style_border_color(s_pinyin_cand_panel, COLOR_BORDER, 0);
        lv_obj_set_style_radius(s_pinyin_cand_panel, 0, 0);
        lv_obj_set_style_pad_all(s_pinyin_cand_panel, 2, 0);
        lv_obj_set_style_pad_gap(s_pinyin_cand_panel, 0, 0);
        lv_obj_set_style_text_font(s_pinyin_cand_panel, &APP_IME_CAND_FONT, 0);
        lv_obj_set_style_text_font(s_pinyin_cand_panel, &APP_IME_CAND_FONT, LV_PART_ITEMS);
        lv_obj_set_style_text_color(s_pinyin_cand_panel, COLOR_TEXT, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(s_pinyin_cand_panel, COLOR_SURFACE, LV_PART_ITEMS);
        lv_obj_set_style_radius(s_pinyin_cand_panel, 0, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(s_pinyin_cand_panel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(s_pinyin_cand_panel, COLOR_PRIMARY_DARK, LV_PART_ITEMS | LV_STATE_PRESSED);
        s_pinyin_ext = ui_pinyin_ime_ext_create(s_pinyin_ime, s_pinyin_kb, s_pinyin_cand_panel);
    }
#endif

    rebuild_saved_networks_list();
    rebuild_scan_list();
    update_state_label();
    request_scan();

    s_refresh_timer = lv_timer_create(refresh_state_timer, 1500, NULL);
}
