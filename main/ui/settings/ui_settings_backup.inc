typedef enum {
    CONFIG_PASSWORD_ACTION_EXPORT = 1,
    CONFIG_PASSWORD_ACTION_IMPORT = 2,
} config_password_action_t;

static void close_config_password_dialog(void);
static void submit_config_password_dialog(void);

static bool config_migration_enabled(void)
{
    return s_config_migration_checkbox &&
           lv_obj_has_state(s_config_migration_checkbox, LV_STATE_CHECKED);
}

static bool config_backup_password_text_valid(const char *text)
{
    size_t len;

    if (!text) {
        return false;
    }
    len = strlen(text);
    if (len < NVS_CONFIG_BACKUP_PASSWORD_MIN_LEN ||
        len > NVS_CONFIG_BACKUP_PASSWORD_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) {
            return false;
        }
    }
    return true;
}

static const char *config_backup_security_hint(void)
{
    if (config_migration_enabled()) {
        return TRS("加密备份（支持跨设备迁移）：SSH/WiFi 密码只写入备份密码加密的数据，导入后会重新使用当前设备密钥保存。");
    }
    return TRS("非加密备份：会导出本机密码密钥和加密后的 SSH/WiFi 密码，适合本机 0x0 全擦后恢复；备份文件请勿外传。");
}

static void set_config_backup_status_with_hint(const char *message, lv_color_t color)
{
    char text[420];
    const char *hint = config_backup_security_hint();

    if (message && message[0]) {
        snprintf(text, sizeof(text), "%s\n%s", message, hint);
    } else {
        snprintf(text, sizeof(text), "%s", hint);
    }
    set_config_backup_status(text, color);
}

static void refresh_config_backup_hint(void)
{
    set_config_backup_status_with_hint(NULL, THEME_TEXT_TERTIARY);
}

static void on_config_migration_changed(lv_event_t *e)
{
    (void)e;
    refresh_config_backup_hint();
}

static const char *config_password_default_hint(void)
{
    if (s_config_password_action == CONFIG_PASSWORD_ACTION_IMPORT) {
        return TRS("请输入加密备份的备份密码；密码错误时不会修改当前配置。");
    }
    return TRS("请输入 4-64 位英文或数字；加密备份会使用该密码保护服务器和 WiFi 密码。");
}

static void set_config_password_hint(const char *message, lv_color_t color)
{
    if (!s_config_password_hint_label) {
        return;
    }
    lv_label_set_text(s_config_password_hint_label,
                      message && message[0] ? TRS(message) : config_password_default_hint());
    lv_obj_set_style_text_color(s_config_password_hint_label, color, 0);
}

static void reset_config_password_hint(void)
{
    set_config_password_hint(config_password_default_hint(), THEME_TEXT_SECONDARY);
}

static void config_password_insert_text(const char *text, size_t len)
{
    if (!s_config_password_ta || !text) {
        return;
    }
    for (size_t i = 0; i < len && text[i]; ++i) {
        char c = text[i];
        if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z')) {
            lv_textarea_add_char(s_config_password_ta, (uint32_t)c);
        }
    }
}

static void on_config_password_hw_timer(lv_timer_t *timer)
{
    (void)timer;
    tab5_keyboard_event_t event;

    if (!s_config_password_ta) {
        return;
    }
    if (tab5_keyboard_read_event(&event) != ESP_OK || event.length == 0) {
        return;
    }

    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;
    if (ui_hw_textarea_parse_key(event.chars, event.length, &key)) {
        if (key == UI_HW_TEXTAREA_KEY_ENTER) {
            submit_config_password_dialog();
            return;
        }
        if (key == UI_HW_TEXTAREA_KEY_ESCAPE) {
            close_config_password_dialog();
            return;
        }
        if (key == UI_HW_TEXTAREA_KEY_SPACE) {
            return;
        }
        (void)ui_hw_textarea_apply_edit_key(s_config_password_ta, key);
        return;
    }

    if (ui_hw_textarea_modifier_blocks_text(event.modifier)) {
        return;
    }
    config_password_insert_text(event.chars, event.length);
}

static void run_config_backup_action(const char *password)
{
    char message[256];
    esp_err_t err;

    if (!config_backup_password_text_valid(password)) {
        set_config_password_hint(TRS("备份密码需为 4-64 位英文或数字。"), THEME_DANGER_LIGHT);
        return;
    }
    if (!tf_card_is_mounted()) {
        set_config_password_hint(TRS("未检测到 TF 卡，无法操作配置备份。"), THEME_DANGER_LIGHT);
        return;
    }

    if (s_config_password_action == CONFIG_PASSWORD_ACTION_EXPORT) {
        err = nvs_config_backup_export_with_password(password, message, sizeof(message));
    } else {
        err = nvs_config_backup_import_with_password(password, message, sizeof(message));
    }
    if (err != ESP_OK) {
        set_config_password_hint(message, THEME_DANGER_LIGHT);
        set_config_backup_status_with_hint(message, THEME_WARNING_LIGHT);
        return;
    }

    set_config_backup_status_with_hint(message, THEME_SUCCESS_LIGHT);
    if (s_config_password_action == CONFIG_PASSWORD_ACTION_IMPORT) {
        (void)app_settings_init();
        app_settings_ensure_timezone_default_for_language();
        time_sync_service_apply_timezone();
        screenshot_service_refresh_enabled();
        ui_server_list_refresh();
    }
    close_config_password_dialog();
}

static void submit_config_password_dialog(void)
{
    const char *password = s_config_password_ta ? lv_textarea_get_text(s_config_password_ta) : NULL;
    run_config_backup_action(password);
}

static void on_config_password_confirm(lv_event_t *e)
{
    (void)e;
    submit_config_password_dialog();
}

static void on_config_password_cancel(lv_event_t *e)
{
    (void)e;
    close_config_password_dialog();
}

static void on_config_password_keyboard(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        submit_config_password_dialog();
    } else if (code == LV_EVENT_CANCEL) {
        close_config_password_dialog();
    }
}

static void on_config_password_text_changed(lv_event_t *e)
{
    (void)e;
    reset_config_password_hint();
}

static void close_config_password_dialog(void)
{
    if (s_config_password_hw_timer) {
        lv_timer_delete(s_config_password_hw_timer);
        s_config_password_hw_timer = NULL;
        tab5_keyboard_deinit();
    }
    if (s_config_password_overlay) {
        lv_obj_del(s_config_password_overlay);
    }
    s_config_password_overlay = NULL;
    s_config_password_ta = NULL;
    s_config_password_hint_label = NULL;
    s_config_password_keyboard = NULL;
    s_config_password_action = 0;
}

static void show_config_password_dialog(config_password_action_t action)
{
    if (s_config_password_overlay) {
        close_config_password_dialog();
    }

    s_config_password_action = action;
    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();
    s_config_password_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_config_password_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_config_password_overlay, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(s_config_password_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_config_password_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_config_password_overlay, 0, 0);
    lv_obj_clear_flag(s_config_password_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_config_password_overlay, on_config_password_cancel, LV_EVENT_CLICKED, NULL);

    esp_err_t keyboard_init_err = tab5_keyboard_init();
    bool has_hw_keyboard = (keyboard_init_err == ESP_OK) &&
                           (tab5_keyboard_is_present() || tab5_keyboard_was_detected());
    if (!has_hw_keyboard && keyboard_init_err == ESP_OK) {
        tab5_keyboard_deinit();
    }

    lv_obj_t *panel = lv_obj_create(s_config_password_overlay);
    apply_surface(panel, THEME_BG_CARD, THEME_BORDER_DIVIDER, 12);
    lv_obj_set_size(panel, 520, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, on_panel_click, LV_EVENT_CLICKED, NULL);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, has_hw_keyboard ? -34 : -190);

    const char *title = action == CONFIG_PASSWORD_ACTION_EXPORT ? TRS("导出加密备份") : TRS("导入加密备份");
    create_label(panel, title, THEME_TEXT_PRIMARY);
    s_config_password_hint_label = create_wrap_label(panel, config_password_default_hint(), THEME_TEXT_SECONDARY);
    lv_obj_null_on_delete(&s_config_password_hint_label);

    s_config_password_ta = lv_textarea_create(panel);
    lv_obj_set_width(s_config_password_ta, LV_PCT(100));
    lv_obj_set_height(s_config_password_ta, 52);
    lv_textarea_set_one_line(s_config_password_ta, true);
    lv_textarea_set_password_mode(s_config_password_ta, true);
    lv_textarea_set_max_length(s_config_password_ta, NVS_CONFIG_BACKUP_PASSWORD_MAX_LEN);
    lv_textarea_set_accepted_chars(s_config_password_ta,
                                   "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    lv_textarea_set_placeholder_text(s_config_password_ta, "Backup password");
    lv_obj_set_style_text_font(s_config_password_ta, &APP_FONT, 0);
    lv_obj_set_style_bg_color(s_config_password_ta, THEME_BG_PRIMARY, 0);
    lv_obj_set_style_text_color(s_config_password_ta, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(s_config_password_ta, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_radius(s_config_password_ta, 8, 0);
    lv_obj_add_state(s_config_password_ta, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_config_password_ta, on_config_password_text_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);
    create_settings_action_button(buttons, "取消", THEME_BG_HOVER, on_config_password_cancel);
    create_settings_action_button(buttons, "确定", THEME_PRIMARY, on_config_password_confirm);

    if (has_hw_keyboard) {
        s_config_password_hw_timer = lv_timer_create(on_config_password_hw_timer, 20, NULL);
    } else {
        s_config_password_keyboard = lv_keyboard_create(s_config_password_overlay);
        lv_keyboard_set_mode(s_config_password_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_config_password_keyboard, s_config_password_ta);
        lv_obj_set_style_text_font(s_config_password_keyboard, &APP_FONT, 0);
        lv_obj_set_style_bg_color(s_config_password_keyboard, THEME_BG_CARD, 0);
        lv_obj_add_event_cb(s_config_password_keyboard, on_config_password_keyboard, LV_EVENT_ALL, NULL);
        lv_obj_align(s_config_password_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static void on_config_export_click(lv_event_t *e)
{
    (void)e;
    char message[192];
    if (!tf_card_is_mounted()) {
        set_config_backup_status_with_hint(TRS("未检测到 TF 卡，无法导出备份。"), THEME_WARNING_LIGHT);
        return;
    }
    if (config_migration_enabled()) {
        show_config_password_dialog(CONFIG_PASSWORD_ACTION_EXPORT);
        return;
    }
    esp_err_t err = nvs_config_backup_export(message, sizeof(message));
    set_config_backup_status_with_hint(message, err == ESP_OK ? THEME_SUCCESS_LIGHT : THEME_WARNING_LIGHT);
}

static void on_config_import_click(lv_event_t *e)
{
    (void)e;
    char message[192];
    if (!tf_card_is_mounted()) {
        set_config_backup_status_with_hint(TRS("未检测到 TF 卡，无法导入备份。"), THEME_WARNING_LIGHT);
        return;
    }

    bool has_migration = false;
    esp_err_t probe_err = nvs_config_backup_has_migration_payload(&has_migration, message, sizeof(message));
    if (probe_err != ESP_OK) {
        set_config_backup_status_with_hint(message, THEME_WARNING_LIGHT);
        return;
    }
    if (has_migration || config_migration_enabled()) {
        if (has_migration && !config_migration_enabled()) {
            set_config_backup_status(TRS("检测到加密备份（支持跨设备迁移）：请输入备份密码后导入；导入后 SSH/WiFi 密码会按当前设备密钥保存。"), THEME_TEXT_TERTIARY);
        }
        show_config_password_dialog(CONFIG_PASSWORD_ACTION_IMPORT);
        return;
    }
    esp_err_t err = nvs_config_backup_import(message, sizeof(message));
    set_config_backup_status_with_hint(message, err == ESP_OK ? THEME_SUCCESS_LIGHT : THEME_WARNING_LIGHT);
    if (err == ESP_OK) {
        (void)app_settings_init();
        app_settings_ensure_timezone_default_for_language();
        time_sync_service_apply_timezone();
        screenshot_service_refresh_enabled();
        ui_server_list_refresh();
    }
}
