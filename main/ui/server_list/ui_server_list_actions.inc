static void on_delete_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_delete_overlay) {
        close_delete_overlay();
    }
}

static void on_delete_cancel(lv_event_t *e)
{
    (void)e;
    close_delete_overlay();
}

static void on_delete_confirm(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    esp_err_t err = server_config_delete(index);

    close_delete_overlay();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "delete server failed: %s", esp_err_to_name(err));
        show_info_dialog("删除失败", "删除服务器配置失败，请稍后再试。");
        return;
    }

    refresh_list();
}

static void show_delete_confirm(int index)
{
    server_entry_t entry;
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *label;
    lv_obj_t *btn_row;
    lv_obj_t *btn_cancel;
    lv_obj_t *btn_delete;
    char msg[160];

    esp_err_t load_err = server_config_load(index, &entry);
    if (load_err != ESP_OK) {
        memset(&entry, 0, sizeof(entry));
        (void)server_config_load_public(index, &entry);
    }

    close_delete_overlay();
    close_action_overlay();

    s_delete_overlay = create_modal_overlay(on_delete_overlay_click);
    lv_obj_null_on_delete(&s_delete_overlay);

    panel = create_modal_panel(s_delete_overlay, DELETE_PANEL_W, DELETE_PANEL_H);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    title = lv_label_create(panel);
    lv_label_set_text(title, TRS("删除服务器"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    if (load_err == ESP_OK) {
        snprintf(msg, sizeof(msg), TRS("确认删除 \"%s\" ？\n删除后将立即从列表中移除。"),
                 entry.name[0] ? entry.name : entry.host);
    } else {
        snprintf(msg, sizeof(msg), TRS("确认删除这条无法解密的服务器配置？\n错误：%s"),
                 esp_err_to_name(load_err));
    }
    label = lv_label_create(panel);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    btn_row = lv_obj_create(panel);
    lv_obj_set_size(btn_row, LV_PCT(100), 58);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, THEME_SPACE_MD, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    btn_cancel = create_button(btn_row, "取消", &s_style_secondary_btn, &s_style_secondary_btn_pressed);
    lv_obj_set_size(btn_cancel, 160, 52);
    lv_obj_add_event_cb(btn_cancel, on_delete_cancel, LV_EVENT_CLICKED, NULL);

    btn_delete = create_button(btn_row, "删除", &s_style_danger_btn, &s_style_danger_btn_pressed);
    lv_obj_set_size(btn_delete, 160, 52);
    lv_obj_add_event_cb(btn_delete, on_delete_confirm, LV_EVENT_CLICKED, (void *)(intptr_t)index);
}

static void on_action_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_action_overlay) {
        close_action_overlay();
    }
}

static void on_action_cancel(lv_event_t *e)
{
    (void)e;
    close_action_overlay();
}

static void on_action_edit(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    close_action_overlay();
    show_server_form(index);
}

static void on_action_delete(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    close_action_overlay();
    show_delete_confirm(index);
}

static void show_card_actions(int index)
{
    server_entry_t entry;
    lv_obj_t *panel;
    lv_obj_t *info_group;
    lv_obj_t *icon_wrap;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *sub;
    lv_obj_t *btn_group;
    lv_obj_t *btn_row;
    lv_obj_t *btn_edit;
    lv_obj_t *btn_delete;
    lv_obj_t *btn_cancel;
    char host_info[128];

    esp_err_t load_err = server_config_load(index, &entry);

    close_action_overlay();
    close_delete_overlay();

    s_action_overlay = create_modal_overlay(on_action_overlay_click);
    lv_obj_null_on_delete(&s_action_overlay);

    panel = create_modal_panel(s_action_overlay, ACTION_PANEL_W, ACTION_PANEL_H);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, ACTION_SECTION_GAP, 0);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    info_group = lv_obj_create(panel);
    lv_obj_set_size(info_group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(info_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(info_group, 0, 0);
    lv_obj_set_style_pad_row(info_group, ACTION_INFO_GAP, 0);
    lv_obj_set_flex_flow(info_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(info_group, LV_OBJ_FLAG_SCROLLABLE);

    icon_wrap = lv_obj_create(info_group);
    lv_obj_set_size(icon_wrap, 64, 64);
    apply_surface_style(icon_wrap, THEME_BG_HOVER, THEME_BORDER_SECONDARY, THEME_RADIUS_PILL);
    lv_obj_set_style_pad_all(icon_wrap, 0, 0);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_SCROLLABLE);
    icon = lv_label_create(icon_wrap);
    lv_label_set_text(icon, load_err == ESP_OK ? get_server_icon(entry.icon_id)->glyph : LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &APP_FONT, 0);
    lv_obj_set_style_text_color(icon, load_err == ESP_OK ? get_server_icon_color(entry.icon_id) : THEME_WARNING_LIGHT, 0);
    lv_obj_center(icon);

    title = lv_label_create(info_group);
    lv_label_set_text(title,
                      load_err == ESP_OK ? (entry.name[0] ? entry.name : entry.host) :
                                          TRS("服务器配置无法解密"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    if (load_err == ESP_OK) {
        snprintf(host_info, sizeof(host_info), "%s@%s:%u",
                 entry.username[0] ? entry.username : "user",
                 entry.host,
                 (unsigned)(entry.port ? entry.port : 22));
    } else {
        snprintf(host_info, sizeof(host_info), TRS("错误：%s"), esp_err_to_name(load_err));
    }
    sub = lv_label_create(info_group);
    lv_label_set_text(sub, host_info);
    lv_obj_set_style_text_font(sub, &APP_FONT, 0);
    lv_obj_set_style_text_color(sub, THEME_TEXT_TERTIARY, 0);

    btn_group = lv_obj_create(panel);
    lv_obj_set_size(btn_group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_group, 0, 0);
    lv_obj_set_style_pad_row(btn_group, ACTION_CANCEL_TOP_GAP, 0);
    lv_obj_set_flex_flow(btn_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(btn_group, LV_OBJ_FLAG_SCROLLABLE);

    btn_row = lv_obj_create(btn_group);
    lv_obj_set_size(btn_row, LV_PCT(100), 58);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, ACTION_BUTTON_GAP, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    btn_edit = create_button(btn_row, load_err == ESP_OK ? "编辑" : "补密码", &s_style_primary_btn, &s_style_primary_btn_pressed);
    lv_obj_set_height(btn_edit, 52);
    lv_obj_set_flex_grow(btn_edit, 1);
    lv_obj_add_event_cb(btn_edit, on_action_edit, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    btn_delete = create_button(btn_row, "删除", &s_style_danger_btn, &s_style_danger_btn_pressed);
    lv_obj_set_height(btn_delete, 52);
    lv_obj_set_flex_grow(btn_delete, 1);
    lv_obj_add_event_cb(btn_delete, on_action_delete, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    btn_cancel = create_button(btn_group, "取消", &s_style_secondary_btn, &s_style_secondary_btn_pressed);
    lv_obj_set_size(btn_cancel, LV_PCT(100), 50);
    lv_obj_add_event_cb(btn_cancel, on_action_cancel, LV_EVENT_CLICKED, NULL);
}

static void on_card_click(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    server_entry_t entry;

    if (server_config_load(index, &entry) != ESP_OK) {
        show_info_dialog("服务器配置无法解密",
                         "这条配置缺少原来的本机密码密钥。请使用新版本机备份重新导入，或长按删除后重建。");
        return;
    }

    ESP_LOGI(TAG, "Connecting to server[%d]: %s@%s:%u", index,
             entry.username, entry.host, entry.port);
    ui_terminal_open(&entry);
}

static void on_card_long_press(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    show_card_actions(index);
}
