static void create_empty_state(void)
{
    lv_obj_t *panel;
    lv_obj_t *icon_wrap;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *hint;

    lv_obj_set_flex_flow(s_list_grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(s_list_grid, LV_PCT(100));

    panel = lv_obj_create(s_list_grid);
    lv_obj_add_style(panel, &s_style_modal_panel, 0);
    lv_obj_set_size(panel, LV_PCT(100), 280);
    lv_obj_set_style_pad_all(panel, 28, 0);
    lv_obj_set_style_pad_row(panel, THEME_SPACE_MD, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    icon_wrap = lv_obj_create(panel);
    lv_obj_set_size(icon_wrap, 88, 88);
    apply_surface_style(icon_wrap, THEME_BG_HOVER, THEME_BORDER_SECONDARY, THEME_RADIUS_MEDIUM);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_SCROLLABLE);
    icon = lv_label_create(icon_wrap);
    lv_label_set_text(icon, get_server_icon(0)->glyph);
    lv_obj_set_style_text_font(icon, &APP_FONT, 0);
    lv_obj_set_style_text_color(icon, get_server_icon_color(0), 0);
    lv_obj_center(icon);

    title = lv_label_create(panel);
    lv_label_set_text(title, TRS("还没有服务器"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    hint = lv_label_create(panel);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, TRS("点击右上角添加按钮，创建第一台服务器。"));
    lv_obj_set_style_text_font(hint, &APP_FONT, 0);
    lv_obj_set_style_text_color(hint, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_server_card(int index, const server_entry_t *entry)
{
    lv_obj_t *card = lv_obj_create(s_list_grid);
    lv_obj_t *icon_wrap;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *host;
    lv_obj_t *user_chip;
    lv_obj_t *user;
    lv_obj_t *action;
    lv_coord_t action_w = app_i18n_get_language() == APP_LANGUAGE_EN ? 92 : 64;
    lv_coord_t user_chip_w = s_card_w - 86 - action_w;
    char host_text[96];
    char user_text[96];

    lv_obj_set_size(card, s_card_w, s_card_h);
    lv_obj_add_style(card, &s_style_card, 0);
    lv_obj_add_style(card, &s_style_card_pressed, LV_STATE_PRESSED);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, on_card_click, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(card, on_card_long_press, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)index);

    icon_wrap = lv_obj_create(card);
    lv_obj_set_size(icon_wrap, 58, 58);
    apply_surface_style(icon_wrap, THEME_BG_HOVER, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_style_pad_all(icon_wrap, 0, 0);
    lv_obj_align(icon_wrap, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_SCROLLABLE);

    icon = lv_label_create(icon_wrap);
    lv_label_set_text(icon, get_server_icon(entry->icon_id)->glyph);
    lv_obj_set_style_text_font(icon, &APP_FONT, 0);
    lv_obj_set_style_text_color(icon, get_server_icon_color(entry->icon_id), 0);
    lv_obj_center(icon);

    name = lv_label_create(card);
    lv_obj_set_width(name, s_card_w - 150);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entry->name[0] ? entry->name : entry->host);
    lv_obj_set_style_text_font(name, &APP_FONT, 0);
    lv_obj_set_style_text_color(name, THEME_TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 78, 2);

    format_host_port(host_text, sizeof(host_text), entry->host, entry->port);
    host = lv_label_create(card);
    lv_obj_set_width(host, s_card_w - 150);
    lv_label_set_long_mode(host, LV_LABEL_LONG_DOT);
    lv_label_set_text(host, host_text);
    lv_obj_set_style_text_font(host, &APP_FONT, 0);
    lv_obj_set_style_text_color(host, THEME_TEXT_SECONDARY, 0);
    lv_obj_align(host, LV_ALIGN_TOP_LEFT, 78, 42);

    user_chip = lv_obj_create(card);
    if (user_chip_w < 120) {
        user_chip_w = 120;
    }
    lv_obj_set_size(user_chip, user_chip_w, 44);
    apply_surface_style(user_chip, THEME_BG_HOVER, THEME_BORDER_PRIMARY, THEME_RADIUS_MEDIUM);
    lv_obj_align(user_chip, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_pad_left(user_chip, 14, 0);
    lv_obj_set_style_pad_right(user_chip, 14, 0);
    lv_obj_set_style_pad_top(user_chip, 0, 0);
    lv_obj_set_style_pad_bottom(user_chip, 0, 0);
    lv_obj_clear_flag(user_chip, LV_OBJ_FLAG_SCROLLABLE);

    snprintf(user_text, sizeof(user_text), "%s %s",
             LV_SYMBOL_SAVE,
             entry->username[0] ? entry->username : TRS("未设置用户"));
    user = lv_label_create(user_chip);
    lv_label_set_text(user, user_text);
    lv_obj_set_width(user, user_chip_w - 28);
    lv_label_set_long_mode(user, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(user, &APP_FONT, 0);
    lv_obj_set_style_text_color(user, THEME_TEXT_TERTIARY, 0);
    lv_obj_align(user, LV_ALIGN_LEFT_MID, 0, 0);

    action = lv_label_create(card);
    lv_label_set_text_fmt(action, "%s " LV_SYMBOL_RIGHT, TRS("连接"));
    lv_obj_set_style_text_font(action, &APP_FONT, 0);
    lv_obj_set_style_text_color(action, THEME_PRIMARY_LIGHT, 0);
    lv_obj_align(action, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void create_broken_server_card(int index, esp_err_t load_err)
{
    server_entry_t public_entry;
    lv_obj_t *card = lv_obj_create(s_list_grid);
    lv_obj_t *icon_wrap;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *host;
    lv_obj_t *hint;
    char text[96];

    lv_obj_set_size(card, s_card_w, s_card_h);
    lv_obj_add_style(card, &s_style_card, 0);
    lv_obj_add_style(card, &s_style_card_pressed, LV_STATE_PRESSED);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, on_card_click, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(card, on_card_long_press, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)index);
    if (server_config_load_public(index, &public_entry) != ESP_OK) {
        memset(&public_entry, 0, sizeof(public_entry));
    }

    icon_wrap = lv_obj_create(card);
    lv_obj_set_size(icon_wrap, 58, 58);
    apply_surface_style(icon_wrap, THEME_WARNING_BG, THEME_WARNING, 12);
    lv_obj_set_style_pad_all(icon_wrap, 0, 0);
    lv_obj_align(icon_wrap, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_SCROLLABLE);

    icon = lv_label_create(icon_wrap);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &APP_FONT, 0);
    lv_obj_set_style_text_color(icon, THEME_WARNING_LIGHT, 0);
    lv_obj_center(icon);

    snprintf(text,
             sizeof(text),
             "%s",
             public_entry.name[0] ? public_entry.name : (public_entry.host[0] ? public_entry.host : TRS("服务器配置")));
    name = lv_label_create(card);
    lv_obj_set_width(name, s_card_w - 150);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, text);
    lv_obj_set_style_text_font(name, &APP_FONT, 0);
    lv_obj_set_style_text_color(name, THEME_TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 78, 2);

    if (public_entry.host[0]) {
        char host_text[48];
        format_host_port(host_text, sizeof(host_text), public_entry.host, public_entry.port);
        snprintf(text, sizeof(text), TRS("%s  密码无法解密"), host_text);
    } else {
        snprintf(text, sizeof(text), TRS("配置无法解密：%s"), esp_err_to_name(load_err));
    }
    host = lv_label_create(card);
    lv_obj_set_width(host, s_card_w - 150);
    lv_label_set_long_mode(host, LV_LABEL_LONG_DOT);
    lv_label_set_text(host, text);
    lv_obj_set_style_text_font(host, &APP_FONT, 0);
    lv_obj_set_style_text_color(host, THEME_WARNING_LIGHT, 0);
    lv_obj_align(host, LV_ALIGN_TOP_LEFT, 78, 42);

    hint = lv_label_create(card);
    lv_obj_set_width(hint, s_card_w - 20);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, TRS("长按可编辑补密码，或删除后重新导入。"));
    lv_obj_set_style_text_font(hint, &APP_FONT, 0);
    lv_obj_set_style_text_color(hint, THEME_TEXT_TERTIARY, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void refresh_list(void)
{
    int count;
    int i;

    if (!s_list_grid) {
        return;
    }

    close_transient_overlays();
    lv_obj_clean(s_list_grid);

    count = server_config_count();
    update_summary_label(count);

    if (count <= 0) {
        create_empty_state();
        return;
    }

    lv_obj_set_flex_flow(s_list_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_list_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_height(s_list_grid, LV_SIZE_CONTENT);

    for (i = 0; i < count; ++i) {
        server_entry_t entry;
        esp_err_t err = server_config_load(i, &entry);
        if (err != ESP_OK) {
            create_broken_server_card(i, err);
            continue;
        }
        create_server_card(i, &entry);
    }
}

static void on_wifi_chip_click(lv_event_t *e)
{
    (void)e;
    ui_server_list_show_wifi_panel();
}

static void on_ipv4_chip_click(lv_event_t *e)
{
    (void)e;
    show_ipv4_info_dialog();
}

static void on_ipv6_chip_click(lv_event_t *e)
{
    (void)e;
    show_ipv6_info_dialog();
}

static void on_settings_click(lv_event_t *e)
{
    (void)e;
    ui_settings_create(lv_scr_act());
}

static void on_file_manager_click(lv_event_t *e)
{
    (void)e;
    ui_file_manager_create(lv_scr_act());
}

static void ensure_file_manager_button(void)
{
    if (!s_statusbar_bottom || s_file_manager_button || !tf_card_is_mounted()) {
        return;
    }

    s_file_manager_button = create_button(s_statusbar_bottom,
                                          LV_SYMBOL_DIRECTORY,
                                          &s_style_secondary_btn,
                                          &s_style_secondary_btn_pressed);
    lv_obj_null_on_delete(&s_file_manager_button);
    lv_obj_set_size(s_file_manager_button, 54, 48);
    lv_obj_add_event_cb(s_file_manager_button, on_file_manager_click, LV_EVENT_CLICKED, NULL);
    lv_obj_move_to_index(s_file_manager_button, 1);
    ESP_LOGI(TAG, "TF card mounted; file manager shortcut added to status bar");
}

static void on_session_button_click(lv_event_t *e)
{
    uint32_t session_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ui_terminal_resume_session(session_id);
}

static void on_session_button_press_state(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (!label) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_obj_add_state(label, LV_STATE_PRESSED);
    } else {
        lv_obj_remove_state(label, LV_STATE_PRESSED);
    }
}

static void refresh_session_buttons(void)
{
    int count;

    if (!s_session_row) {
        return;
    }

    lv_obj_clean(s_session_row);
    count = ui_terminal_session_count();

    for (int i = 0; i < count; ++i) {
        ui_terminal_session_info_t info;
        const server_icon_def_t *icon;
        lv_obj_t *btn;
        lv_obj_t *label;
        lv_color_t border_color;
        lv_point_t text_size;
        int32_t label_w;
        int32_t line_space;
        bool multiline;
        char text[96];

        if (!ui_terminal_session_get_info(i, &info)) {
            continue;
        }

        icon = get_server_icon(info.icon_id);
        snprintf(text, sizeof(text), "%s %s", icon->glyph, info.name[0] ? info.name : "SSH");

        btn = lv_button_create(s_session_row);
        lv_obj_add_style(btn, &s_style_secondary_btn, 0);
        lv_obj_set_size(btn, SESSION_BUTTON_W, SESSION_BUTTON_H);
        lv_obj_set_flex_grow(btn, 0);
        lv_obj_set_style_pad_left(btn, 10, 0);
        lv_obj_set_style_pad_right(btn, 10, 0);
        lv_obj_set_style_pad_top(btn, 0, 0);
        lv_obj_set_style_pad_bottom(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, on_session_button_click, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)info.id);

        if (info.failed) {
            border_color = THEME_DANGER;
        } else if (info.connected) {
            border_color = THEME_SUCCESS;
        } else {
            border_color = THEME_WARNING;
        }
        lv_obj_set_style_border_color(btn, border_color, 0);
        lv_obj_set_style_border_color(btn, border_color, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 1, LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, THEME_BG_PANEL, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);

        label = lv_label_create(btn);
        label_w = SESSION_BUTTON_W - 22;
        line_space = SESSION_BUTTON_TEXT_H + SESSION_BUTTON_VISUAL_LINE_GAP - (&APP_FONT)->line_height;
        lv_text_get_size(&text_size, text, &APP_FONT, 0, line_space, label_w, LV_TEXT_FLAG_NONE);
        multiline = text_size.y > (&APP_FONT)->line_height;
        lv_obj_set_width(label, label_w);
        lv_obj_set_height(label, multiline ? ((&APP_FONT)->line_height * 2 + line_space)
                                           : (&APP_FONT)->line_height);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, &APP_FONT, 0);
        lv_obj_set_style_text_color(label, info.failed ? THEME_DANGER_LIGHT : THEME_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_color(label, THEME_WARNING_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, line_space, 0);
        lv_label_set_text(label, text);
        if (multiline) {
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
        } else {
            lv_obj_center(label);
        }
        lv_obj_add_event_cb(btn, on_session_button_press_state, LV_EVENT_PRESSED, label);
        lv_obj_add_event_cb(btn, on_session_button_press_state, LV_EVENT_RELEASED, label);
        lv_obj_add_event_cb(btn, on_session_button_press_state, LV_EVENT_PRESS_LOST, label);
    }
}

static void on_power_chip_click(lv_event_t *e)
{
    (void)e;
    ui_server_list_show_power_detail();
}

void ui_server_list_show_wifi_panel(void)
{
    ui_wifi_create(lv_scr_act());
}

void ui_server_list_show_power_detail(void)
{
    show_power_detail();
}

static void update_wifi_label(void)
{
    wifi_state_t state;

    if (!s_wifi_chip || !s_wifi_label) {
        return;
    }

    state = wifi_manager_get_state();
    if (state == WIFI_STATE_CONNECTED) {
        lv_label_set_text(s_wifi_label, TRS(LV_SYMBOL_WIFI " 已连接"));
        lv_obj_set_style_text_color(s_wifi_label, THEME_SUCCESS_LIGHT, 0);
        lv_obj_set_style_bg_color(s_wifi_chip, THEME_SUCCESS_BG, 0);
        lv_obj_set_style_border_color(s_wifi_chip, THEME_SUCCESS, 0);
    } else if (state == WIFI_STATE_CONNECTING) {
        lv_label_set_text(s_wifi_label, TRS(LV_SYMBOL_WIFI " 连接中"));
        lv_obj_set_style_text_color(s_wifi_label, THEME_WARNING_LIGHT, 0);
        lv_obj_set_style_bg_color(s_wifi_chip, THEME_WARNING_BG, 0);
        lv_obj_set_style_border_color(s_wifi_chip, THEME_WARNING, 0);
    } else {
        lv_label_set_text(s_wifi_label, TRS(LV_SYMBOL_WIFI " 未连接"));
        lv_obj_set_style_text_color(s_wifi_label, THEME_TEXT_SECONDARY, 0);
        lv_obj_set_style_bg_color(s_wifi_chip, THEME_BG_PANEL, 0);
        lv_obj_set_style_border_color(s_wifi_chip, THEME_BORDER_SECONDARY, 0);
    }

    update_ip_chip(s_ipv4_chip, s_ipv4_label, "IPv4", wifi_manager_has_ipv4());
    update_ip_chip(s_ipv6_chip, s_ipv6_label, "IPv6", wifi_manager_has_ipv6_global());
}

static void wifi_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    update_wifi_label();
    ensure_file_manager_button();
}

static void power_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_power_chip();
}

static void power_detail_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_power_chip();
    update_power_detail_values();
}

static void on_add_click(lv_event_t *e)
{
    (void)e;
    if (server_config_count() >= MAX_SERVERS) {
        show_info_dialog("服务器已满", "当前最多支持 10 台服务器，请先删除一台后再添加。");
        return;
    }
    show_server_form(-1);
}

void ui_server_list_create(lv_obj_t *parent)
{
    lv_obj_t *topbar;
    lv_obj_t *title_wrap;
    lv_obj_t *title;
    lv_obj_t *btn_add;
    lv_obj_t *btn_settings;
    lv_obj_t *status_spacer;

    init_styles();
    reset_form_state();
    close_transient_overlays();

    s_root = lv_obj_create(parent);
    lv_obj_null_on_delete(&s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, THEME_BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    topbar = lv_obj_create(s_root);
    lv_obj_set_size(topbar, LV_PCT(100), TOPBAR_HEIGHT);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(topbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(topbar, 0, 0);
    lv_obj_set_style_pad_left(topbar, SIDE_MARGIN, 0);
    lv_obj_set_style_pad_right(topbar, SIDE_MARGIN, 0);
    lv_obj_set_style_pad_top(topbar, 14, 0);
    lv_obj_set_style_pad_bottom(topbar, 12, 0);
    lv_obj_set_style_pad_column(topbar, 16, 0);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

    title_wrap = lv_obj_create(topbar);
    lv_obj_set_flex_grow(title_wrap, 1);
    lv_obj_set_height(title_wrap, LV_PCT(100));
    lv_obj_set_style_bg_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(title_wrap, 0, 0);
    lv_obj_set_style_pad_column(title_wrap, 20, 0);
    lv_obj_set_flex_flow(title_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(title_wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(title_wrap, on_title_screensaver_click, LV_EVENT_CLICKED, NULL);

    title = lv_label_create(title_wrap);
    lv_label_set_text(title, TRS("SSH 服务器"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    s_summary_label = lv_label_create(title_wrap);
    lv_obj_null_on_delete(&s_summary_label);
    lv_obj_set_style_text_font(s_summary_label, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_summary_label, THEME_TEXT_TERTIARY, 0);

    s_power_chip = lv_obj_create(topbar);
    lv_obj_null_on_delete(&s_power_chip);
    lv_obj_set_size(s_power_chip, 142, 62);
    lv_obj_set_style_bg_opa(s_power_chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_power_chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_power_chip, 0, 0);
    lv_obj_set_style_shadow_width(s_power_chip, 0, 0);
    lv_obj_clear_flag(s_power_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_power_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_power_chip, on_power_chip_click, LV_EVENT_CLICKED, NULL);

    s_power_body = lv_obj_create(s_power_chip);
    lv_obj_null_on_delete(&s_power_body);
    lv_obj_set_size(s_power_body, 92, 38);
    lv_obj_align(s_power_body, LV_ALIGN_CENTER, -4, 0);
    lv_obj_set_style_bg_opa(s_power_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_power_body, 3, 0);
    lv_obj_set_style_border_color(s_power_body, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_radius(s_power_body, 6, 0);
    lv_obj_set_style_pad_all(s_power_body, 0, 0);
    lv_obj_clear_flag(s_power_body, LV_OBJ_FLAG_SCROLLABLE);

    s_power_cap = lv_obj_create(s_power_chip);
    lv_obj_null_on_delete(&s_power_cap);
    lv_obj_set_size(s_power_cap, 8, 20);
    lv_obj_align_to(s_power_cap, s_power_body, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_set_style_bg_color(s_power_cap, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_power_cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_power_cap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_power_cap, 2, 0);
    lv_obj_clear_flag(s_power_cap, LV_OBJ_FLAG_SCROLLABLE);

    s_power_percent = lv_label_create(s_power_body);
    lv_obj_null_on_delete(&s_power_percent);
    lv_label_set_text(s_power_percent, "--");
    lv_obj_set_style_text_font(s_power_percent, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_power_percent, THEME_TEXT_SECONDARY, 0);
    lv_obj_center(s_power_percent);

    s_power_hotzone = lv_obj_create(topbar);
    lv_obj_null_on_delete(&s_power_hotzone);
    lv_obj_add_flag(s_power_hotzone, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_power_hotzone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_power_hotzone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_power_hotzone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_power_hotzone, 0, 0);
    lv_obj_clear_flag(s_power_hotzone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_power_hotzone, on_power_chip_click, LV_EVENT_CLICKED, NULL);
    align_power_hotzone();

    s_wifi_chip = lv_obj_create(topbar);
    lv_obj_null_on_delete(&s_wifi_chip);
    lv_obj_set_size(s_wifi_chip, 168, 48);
    apply_surface_style(s_wifi_chip, THEME_BG_CARD, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(s_wifi_chip, 0, 0);
    lv_obj_set_style_shadow_width(s_wifi_chip, 0, 0);
    lv_obj_clear_flag(s_wifi_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wifi_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifi_chip, on_wifi_chip_click, LV_EVENT_CLICKED, NULL);

    s_wifi_label = lv_label_create(s_wifi_chip);
    lv_obj_null_on_delete(&s_wifi_label);
    lv_obj_set_style_text_font(s_wifi_label, &APP_FONT, 0);
    lv_obj_center(s_wifi_label);

    btn_add = create_button(topbar, LV_SYMBOL_PLUS " 添加", &s_style_primary_btn, &s_style_primary_btn_pressed);
    lv_obj_set_size(btn_add, 154, 48);
    lv_obj_add_event_cb(btn_add, on_add_click, LV_EVENT_CLICKED, NULL);

    s_statusbar_bottom = lv_obj_create(s_root);
    lv_obj_null_on_delete(&s_statusbar_bottom);
    lv_obj_set_size(s_statusbar_bottom, LV_PCT(100), TOPBAR_HEIGHT);
    lv_obj_set_pos(s_statusbar_bottom, 0, lv_display_get_vertical_resolution(NULL) - TOPBAR_HEIGHT);
    lv_obj_set_style_bg_color(s_statusbar_bottom, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_statusbar_bottom, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_statusbar_bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_statusbar_bottom, 0, 0);
    lv_obj_set_style_pad_left(s_statusbar_bottom, SIDE_MARGIN, 0);
    lv_obj_set_style_pad_right(s_statusbar_bottom, SIDE_MARGIN, 0);
    lv_obj_set_style_pad_top(s_statusbar_bottom, 14, 0);
    lv_obj_set_style_pad_bottom(s_statusbar_bottom, 12, 0);
    lv_obj_set_style_pad_column(s_statusbar_bottom, 16, 0);
    lv_obj_set_flex_flow(s_statusbar_bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_statusbar_bottom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_statusbar_bottom, LV_OBJ_FLAG_SCROLLABLE);

    btn_settings = create_button(s_statusbar_bottom, LV_SYMBOL_SETTINGS,
                                 &s_style_secondary_btn, &s_style_secondary_btn_pressed);
    lv_obj_set_size(btn_settings, 54, 48);
    lv_obj_add_event_cb(btn_settings, on_settings_click, LV_EVENT_CLICKED, NULL);

    s_file_manager_button = NULL;
    ensure_file_manager_button();

    s_session_row = lv_obj_create(s_statusbar_bottom);
    lv_obj_null_on_delete(&s_session_row);
    lv_obj_set_flex_grow(s_session_row, 1);
    lv_obj_set_height(s_session_row, SESSION_BUTTON_H);
    lv_obj_set_style_bg_opa(s_session_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_session_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_session_row, 0, 0);
    lv_obj_set_style_pad_column(s_session_row, 8, 0);
    lv_obj_set_flex_flow(s_session_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_session_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_session_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_session_row, LV_SCROLLBAR_MODE_OFF);

    status_spacer = lv_obj_create(s_statusbar_bottom);
    lv_obj_set_width(status_spacer, 1);
    lv_obj_set_height(status_spacer, 1);
    lv_obj_set_style_bg_opa(status_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(status_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(status_spacer, 0, 0);
    lv_obj_clear_flag(status_spacer, LV_OBJ_FLAG_SCROLLABLE);

    s_ipv4_chip = create_status_chip(s_statusbar_bottom, "IPv4", &s_ipv4_label);
    lv_obj_null_on_delete(&s_ipv4_chip);
    lv_obj_add_flag(s_ipv4_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ipv4_chip, on_ipv4_chip_click, LV_EVENT_CLICKED, NULL);
    s_ipv6_chip = create_status_chip(s_statusbar_bottom, "IPv6", &s_ipv6_label);
    lv_obj_null_on_delete(&s_ipv6_chip);
    lv_obj_add_flag(s_ipv6_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ipv6_chip, on_ipv6_chip_click, LV_EVENT_CLICKED, NULL);

    s_list_scroll = lv_obj_create(s_root);
    lv_obj_null_on_delete(&s_list_scroll);
    lv_obj_set_style_bg_color(s_list_scroll, THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(s_list_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_list_scroll, THEME_BORDER_PRIMARY, 0);
    lv_obj_set_style_border_width(s_list_scroll, 1, 0);
    lv_obj_set_style_radius(s_list_scroll, 18, 0);
    lv_obj_set_style_pad_all(s_list_scroll, THEME_SPACE_LG, 0);
    lv_obj_set_style_shadow_width(s_list_scroll, 0, 0);
    lv_obj_set_scroll_dir(s_list_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_row(s_list_scroll, GRID_GAP, 0);

    s_list_grid = lv_obj_create(s_list_scroll);
    lv_obj_null_on_delete(&s_list_grid);
    lv_obj_set_width(s_list_grid, LV_PCT(100));
    lv_obj_set_height(s_list_grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_list_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_list_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_list_grid, 0, 0);
    lv_obj_set_style_pad_row(s_list_grid, GRID_GAP, 0);
    lv_obj_set_style_pad_column(s_list_grid, GRID_GAP, 0);
    lv_obj_set_flex_flow(s_list_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_list_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_list_grid, LV_OBJ_FLAG_SCROLLABLE);

    layout_main_screen();
    refresh_list();
    refresh_session_buttons();
    update_wifi_label();

    if (!s_wifi_timer) {
        s_wifi_timer = lv_timer_create(wifi_status_timer_cb, 2000, NULL);
    }
    if (!s_power_timer) {
        s_power_timer = lv_timer_create(power_status_timer_cb, 10000, NULL);
    }
    lv_timer_t *power_warmup_timer = lv_timer_create(power_status_timer_cb, 500, NULL);
    if (power_warmup_timer) {
        lv_timer_set_repeat_count(power_warmup_timer, 12);
    }
}

void ui_server_list_refresh(void)
{
    layout_main_screen();
    refresh_list();
    refresh_session_buttons();
    update_wifi_label();
    update_power_chip();
}
