static void clear_container_children(lv_obj_t *container)
{
    uint32_t count;

    if (!container) {
        return;
    }

    count = lv_obj_get_child_count(container);
    while (count > 0) {
        lv_obj_del(lv_obj_get_child(container, count - 1));
        count--;
    }
}

static lv_obj_t *create_section(lv_obj_t *parent, const char *title, lv_coord_t height, lv_obj_t **content_out)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_t *label = lv_label_create(section);
    lv_obj_t *content = lv_obj_create(section);

    lv_obj_set_width(section, LV_PCT(100));
    if (height > 0) {
        lv_obj_set_height(section, height);
    } else {
        lv_obj_set_flex_grow(section, 1);
    }
    apply_card_style(section, COLOR_PANEL);
    lv_obj_set_style_pad_all(section, 12, 0);
    lv_obj_set_style_pad_row(section, 10, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_label_set_text(label, TRS(title));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT, 0);

    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);

    if (content_out) {
        *content_out = content;
    }
    return section;
}

static void refresh_saved_networks(void)
{
    memset(s_saved_networks, 0, sizeof(s_saved_networks));
    s_saved_count = wifi_manager_get_saved_networks(s_saved_networks, WIFI_MAX_SAVED_NETWORKS);
}

static void rebuild_saved_networks_list(void)
{
    size_t i;
    bool has_scanned_saved = false;
    bool has_missing_saved = false;

    clear_container_children(s_saved_list);
    if (!s_saved_list) {
        return;
    }

    if (s_saved_count == 0) {
        lv_obj_t *empty = lv_label_create(s_saved_list);
        lv_label_set_text(empty, TRS("还没有已保存 WiFi。连接一个网络后会自动保存到这里。"));
        lv_obj_set_style_text_font(empty, &APP_FONT, 0);
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SUB, 0);
        return;
    }

    for (i = 0; i < s_saved_count; ++i) {
        if (saved_network_is_scanned(s_saved_networks[i].ssid)) {
            has_scanned_saved = true;
        } else {
            has_missing_saved = true;
        }
    }

    if (has_scanned_saved) {
        lv_obj_t *label = lv_label_create(s_saved_list);
        lv_label_set_text(label, TRS("已保存且已扫描"));
        lv_obj_set_style_text_font(label, &APP_FONT, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT_SUB, 0);
    }

    for (i = 0; i < s_saved_count; ++i) {
        lv_obj_t *row;
        lv_obj_t *info;
        lv_obj_t *ssid;
        lv_obj_t *sub;
        lv_obj_t *btns;
        lv_obj_t *btn_connect;
        lv_obj_t *btn_delete;

        if (!saved_network_is_scanned(s_saved_networks[i].ssid)) {
            continue;
        }

        row = lv_obj_create(s_saved_list);
        info = lv_obj_create(row);
        ssid = lv_label_create(info);
        sub = lv_label_create(info);
        btns = lv_obj_create(row);
        btn_connect = NULL;
        btn_delete = NULL;

        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        apply_card_style(row, COLOR_SURFACE);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_set_style_pad_row(info, 4, 0);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

        lv_label_set_text(ssid, s_saved_networks[i].ssid);
        lv_obj_set_style_text_font(ssid, &APP_FONT, 0);
        lv_obj_set_style_text_color(ssid, COLOR_TEXT, 0);

        lv_label_set_text(sub,
                          s_saved_networks[i].password[0] ? TRS("已保存密码") : TRS("开放网络 / 无密码"));
        lv_obj_set_style_text_font(sub, &APP_FONT, 0);
        lv_obj_set_style_text_color(sub, COLOR_TEXT_SUB, 0);

        lv_obj_set_width(btns, app_i18n_get_language() == APP_LANGUAGE_EN ? 236 : 188);
        lv_obj_set_height(btns, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(btns, 0, 0);
        lv_obj_set_style_pad_column(btns, 8, 0);
        lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

        btn_connect = create_text_button(btns, "连接", COLOR_PRIMARY, on_saved_connect, (void *)(uintptr_t)i);
        lv_obj_set_width(btn_connect, app_i18n_get_language() == APP_LANGUAGE_EN ? 112 : 86);

        btn_delete = create_text_button(btns, TRS("删除"), COLOR_DANGER, on_saved_delete, (void *)(uintptr_t)i);
        lv_obj_set_width(btn_delete, app_i18n_get_language() == APP_LANGUAGE_EN ? 108 : 86);
    }

    if (has_missing_saved) {
        lv_obj_t *label = lv_label_create(s_saved_list);
        lv_label_set_text(label, TRS("已保存但未扫描到"));
        lv_obj_set_style_text_font(label, &APP_FONT, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT_SUB, 0);
    }

    for (i = 0; i < s_saved_count; ++i) {
        lv_obj_t *row = lv_obj_create(s_saved_list);
        lv_obj_t *info = lv_obj_create(row);
        lv_obj_t *ssid = lv_label_create(info);
        lv_obj_t *sub = lv_label_create(info);
        lv_obj_t *btns = lv_obj_create(row);
        lv_obj_t *btn_connect;
        lv_obj_t *btn_delete;
        bool scanned = saved_network_is_scanned(s_saved_networks[i].ssid);
        lv_color_t text_color = COLOR_TEXT_SUB;

        if (scanned) {
            continue;
        }

        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        apply_card_style(row, COLOR_PANEL);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_set_style_pad_row(info, 4, 0);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

        lv_label_set_text(ssid, s_saved_networks[i].ssid);
        lv_obj_set_style_text_font(ssid, &APP_FONT, 0);
        lv_obj_set_style_text_color(ssid, text_color, 0);

        lv_label_set_text(sub,
                          s_saved_networks[i].password[0] ? TRS("已保存密码") : TRS("开放网络 / 无密码"));
        lv_obj_set_style_text_font(sub, &APP_FONT, 0);
        lv_obj_set_style_text_color(sub, text_color, 0);

        lv_obj_set_width(btns, app_i18n_get_language() == APP_LANGUAGE_EN ? 236 : 188);
        lv_obj_set_height(btns, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(btns, 0, 0);
        lv_obj_set_style_pad_column(btns, 8, 0);
        lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

        btn_connect = create_text_button(btns, "连接", scanned ? COLOR_PRIMARY : COLOR_MUTED, on_saved_connect, (void *)(uintptr_t)i);
        lv_obj_set_width(btn_connect, app_i18n_get_language() == APP_LANGUAGE_EN ? 112 : 86);

        btn_delete = create_text_button(btns, TRS("删除"), COLOR_DANGER, on_saved_delete, (void *)(uintptr_t)i);
        lv_obj_set_width(btn_delete, app_i18n_get_language() == APP_LANGUAGE_EN ? 108 : 86);
    }
}

static void rebuild_scan_list(void)
{
    size_t i;
    bool has_any = false;
    bool has_missing_saved = false;

    clear_container_children(s_scan_list);
    if (!s_scan_list) {
        return;
    }

    for (int pass = 0; pass < 2; ++pass) {
        for (i = 0; i < s_scan_count; ++i) {
            char sub_text[96];
            bool saved = s_scan_results[i].saved;
            const char *btn_text;
            lv_color_t btn_color;
            lv_obj_t *row;
            lv_obj_t *info;
            lv_obj_t *ssid;
            lv_obj_t *sub;
            lv_obj_t *btns = NULL;
            lv_obj_t *btn;

            if ((pass == 0 && !saved) || (pass == 1 && saved)) {
                continue;
            }

            has_any = true;
            btn_text = saved ? TRS("连接") :
                       ((s_scan_results[i].authmode == WIFI_AUTH_OPEN) ? TRS("直连") : TRS("使用"));
            btn_color = saved ? COLOR_PRIMARY : COLOR_PRIMARY_DARK;

            row = lv_obj_create(s_scan_list);
            info = lv_obj_create(row);
            ssid = lv_label_create(info);
            sub = lv_label_create(info);

            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            apply_card_style(row, saved ? COLOR_SURFACE : COLOR_PANEL);
            lv_obj_set_style_pad_all(row, 10, 0);
            lv_obj_set_style_pad_column(row, 10, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_set_flex_grow(info, 1);
            lv_obj_set_height(info, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_opa(info, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(info, 0, 0);
            lv_obj_set_style_pad_row(info, 4, 0);
            lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
            lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

            lv_label_set_text(ssid, s_scan_results[i].ssid);
            lv_obj_set_style_text_font(ssid, &APP_FONT, 0);
            lv_obj_set_style_text_color(ssid, saved ? COLOR_TEXT : COLOR_TEXT_SUB, 0);

            snprintf(sub_text, sizeof(sub_text), "%s | RSSI %d dBm%s%s",
                     authmode_to_text(s_scan_results[i].authmode),
                     (int)s_scan_results[i].rssi,
                     saved ? TRS(" | 已保存") : "",
                     s_scan_results[i].connected ? TRS(" | 当前连接") : "");
            lv_label_set_text(sub, sub_text);
            lv_obj_set_style_text_font(sub, &APP_FONT, 0);
            lv_obj_set_style_text_color(sub, COLOR_TEXT_SUB, 0);

            if (saved) {
                lv_obj_t *btn_delete;

                btns = lv_obj_create(row);
                lv_obj_set_width(btns, app_i18n_get_language() == APP_LANGUAGE_EN ? 236 : 188);
                lv_obj_set_height(btns, LV_SIZE_CONTENT);
                lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_opa(btns, LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_all(btns, 0, 0);
                lv_obj_set_style_pad_column(btns, 8, 0);
                lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

                btn = create_text_button(btns, btn_text, btn_color, on_scan_row_action, (void *)(uintptr_t)i);
                lv_obj_set_width(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 112 : 86);

                btn_delete = create_text_button(btns, TRS("删除"), COLOR_DANGER, on_scan_saved_delete, (void *)(uintptr_t)i);
                lv_obj_set_width(btn_delete, app_i18n_get_language() == APP_LANGUAGE_EN ? 108 : 86);
            } else {
                btn = create_text_button(row, btn_text, btn_color, on_scan_row_action, (void *)(uintptr_t)i);
                lv_obj_set_width(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 112 : 86);
            }
        }
    }

    for (i = 0; i < s_saved_count; ++i) {
        if (!saved_network_is_scanned(s_saved_networks[i].ssid)) {
            has_missing_saved = true;
            break;
        }
    }

    if (has_missing_saved) {
        lv_obj_t *label = lv_label_create(s_scan_list);
        lv_label_set_text(label, TRS("已保存但未扫描到"));
        lv_obj_set_style_text_font(label, &APP_FONT, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT_SUB, 0);
    }

    for (i = 0; i < s_saved_count; ++i) {
        lv_obj_t *row;
        lv_obj_t *info;
        lv_obj_t *ssid;
        lv_obj_t *sub;
        lv_obj_t *btns;
        lv_obj_t *btn_delete;

        if (saved_network_is_scanned(s_saved_networks[i].ssid)) {
            continue;
        }

        has_any = true;
        row = lv_obj_create(s_scan_list);
        info = lv_obj_create(row);
        ssid = lv_label_create(info);
        sub = lv_label_create(info);
        btns = lv_obj_create(row);

        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        apply_card_style(row, COLOR_PANEL);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_set_style_pad_row(info, 4, 0);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

        lv_label_set_text(ssid, s_saved_networks[i].ssid);
        lv_obj_set_style_text_font(ssid, &APP_FONT, 0);
        lv_obj_set_style_text_color(ssid, COLOR_TEXT_SUB, 0);

        lv_label_set_text(sub,
                          s_saved_networks[i].password[0] ? TRS("已保存密码") : TRS("开放网络 / 无密码"));
        lv_obj_set_style_text_font(sub, &APP_FONT, 0);
        lv_obj_set_style_text_color(sub, COLOR_TEXT_SUB, 0);

        lv_obj_set_width(btns, app_i18n_get_language() == APP_LANGUAGE_EN ? 118 : 90);
        lv_obj_set_height(btns, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(btns, 0, 0);
        lv_obj_set_style_pad_column(btns, 8, 0);
        lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

        btn_delete = create_text_button(btns, TRS("删除"), COLOR_DANGER, on_saved_delete, (void *)(uintptr_t)i);
        lv_obj_set_width(btn_delete, app_i18n_get_language() == APP_LANGUAGE_EN ? 108 : 86);
    }

    if (!has_any) {
        lv_obj_t *empty = lv_label_create(s_scan_list);
        lv_label_set_text(empty, TRS("还没有 WiFi 记录，点击扫描 / 刷新。"));
        lv_obj_set_style_text_font(empty, &APP_FONT, 0);
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SUB, 0);
    }
}

static void scan_and_refresh_networks(void)
{
    esp_err_t err;

    memset(s_scan_results, 0, sizeof(s_scan_results));
    s_scan_count = 0;

    err = wifi_manager_scan_networks(s_scan_results, WIFI_MAX_SCAN_RESULTS, &s_scan_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        set_scan_hint(TRS("扫描失败，请稍后再试。"), COLOR_DANGER);
        refresh_saved_networks();
        rebuild_scan_list();
        return;
    }

    refresh_saved_networks();
    rebuild_scan_list();
    set_scan_hint(TRS("扫描完成"), COLOR_TEXT_SUB);
}

static void request_scan(void)
{
    if (wifi_manager_get_state() == WIFI_STATE_CONNECTING) {
        set_scan_hint(TRS("正在连接，稍后再扫描。"), COLOR_WARNING);
        return;
    }

    set_scan_hint(TRS("扫描中......"), COLOR_WARNING);

    if (s_scan_timer) {
        lv_timer_del(s_scan_timer);
        s_scan_timer = NULL;
    }

    s_scan_timer = lv_timer_create(on_scan_timer, 60, NULL);
    if (s_scan_timer) {
        lv_timer_set_repeat_count(s_scan_timer, 1);
    }
}
