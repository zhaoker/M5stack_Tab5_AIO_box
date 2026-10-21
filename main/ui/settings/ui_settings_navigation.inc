static void ensure_page_rendered(int index)
{
    if (index < 0 || index >= SETTINGS_PAGE_COUNT || !s_content) {
        return;
    }
    if (!s_page_containers[index]) {
        s_page_containers[index] = create_page_container(s_content);
    }
    if (!s_page_rendered[index]) {
        s_pages[index].render(s_page_containers[index]);
        s_page_rendered[index] = true;
    }
}

static void apply_menu_selection(void)
{
    const lv_color_t active_bg = lv_color_hex(0x2F3D55);
    const lv_color_t active_text = lv_color_hex(0xFDE68A);

    for (int i = 0; i < SETTINGS_PAGE_COUNT; ++i) {
        if (!s_menu_buttons[i]) {
            continue;
        }
        if (i == s_selected_page) {
            lv_obj_set_style_bg_color(s_menu_buttons[i], active_bg, 0);
            lv_obj_set_style_border_color(s_menu_buttons[i], THEME_BORDER_DIVIDER, 0);
            if (s_menu_icons[i]) {
                lv_obj_set_style_text_color(s_menu_icons[i], active_text, 0);
            }
            if (s_menu_titles[i]) {
                lv_obj_set_style_text_color(s_menu_titles[i], active_text, 0);
            }
        } else {
            lv_obj_set_style_bg_color(s_menu_buttons[i], THEME_BG_CARD, 0);
            lv_obj_set_style_border_color(s_menu_buttons[i], THEME_BORDER_SECONDARY, 0);
            if (s_menu_icons[i]) {
                lv_obj_set_style_text_color(s_menu_icons[i], THEME_PRIMARY_LIGHT, 0);
            }
            if (s_menu_titles[i]) {
                lv_obj_set_style_text_color(s_menu_titles[i], THEME_TEXT_PRIMARY, 0);
            }
        }
    }
}

static void render_selected_page(void)
{
    int64_t total_start_us = esp_timer_get_time();
    int64_t step_start_us = total_start_us;

    if (s_selected_page < 0 || s_selected_page >= SETTINGS_PAGE_COUNT) {
        s_selected_page = 0;
    }

    apply_menu_selection();
    int64_t menu_ms = elapsed_ms_since(step_start_us);

    step_start_us = esp_timer_get_time();
    ensure_page_rendered(s_selected_page);
    int64_t render_ms = elapsed_ms_since(step_start_us);

    step_start_us = esp_timer_get_time();
    for (int i = 0; i < SETTINGS_PAGE_COUNT; ++i) {
        if (!s_page_containers[i]) {
            continue;
        }
        if (i == s_selected_page) {
            lv_obj_clear_flag(s_page_containers[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_page_containers[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
    int64_t show_ms = elapsed_ms_since(step_start_us);

    ESP_LOGI(TAG, "settings page switch to %s: menu=%lldms render=%lldms show=%lldms total=%lldms",
             s_pages[s_selected_page].title,
             (long long)menu_ms,
             (long long)render_ms,
             (long long)show_ms,
             (long long)elapsed_ms_since(total_start_us));
}

static void on_menu_click(lv_event_t *e)
{
    int64_t click_start_us = esp_timer_get_time();
    int selected_page = (int)(intptr_t)lv_event_get_user_data(e);
    if (selected_page < 0 || selected_page >= SETTINGS_PAGE_COUNT) {
        selected_page = 0;
    }
    if (selected_page == s_selected_page) {
        return;
    }

    s_selected_page = selected_page;
    apply_menu_selection();
    lv_refr_now(NULL);
    ESP_LOGI(TAG, "settings menu visual update to %s: %lldms",
             s_pages[s_selected_page].title,
             (long long)elapsed_ms_since(click_start_us));
    render_selected_page();
}

static void close_settings(void)
{
    close_config_password_dialog();
    close_usb_format_dialog();
    close_timezone_dialog();
    if (s_file_web_status_timer) {
        lv_timer_delete(s_file_web_status_timer);
        s_file_web_status_timer = NULL;
    }
    if (s_usb_storage_status_timer) {
        lv_timer_delete(s_usb_storage_status_timer);
        s_usb_storage_status_timer = NULL;
    }
    if (s_tf_usb_disk_status_timer) {
        lv_timer_delete(s_tf_usb_disk_status_timer);
        s_tf_usb_disk_status_timer = NULL;
    }
    if (s_ota_status_timer) {
        lv_timer_delete(s_ota_status_timer);
        s_ota_status_timer = NULL;
    }
    if (s_system_info_time_timer) {
        lv_timer_delete(s_system_info_time_timer);
        s_system_info_time_timer = NULL;
    }
    ui_number_input_close();
    screensaver_color_close_dialog();
    close_screensaver_dir_picker();
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
        s_panel = NULL;
        s_content = NULL;
        s_file_web_button = NULL;
        s_file_web_button_label = NULL;
        s_file_web_status = NULL;
        s_usb_storage_value_label = NULL;
        s_usb_storage_button = NULL;
        s_usb_storage_button_label = NULL;
        s_usb_format_button = NULL;
        s_usb_format_button_label = NULL;
        s_usb_format_overlay = NULL;
        s_usb_storage_status_label = NULL;
        s_tf_usb_disk_status_label = NULL;
        s_tf_usb_disk_button = NULL;
        s_tf_usb_disk_button_label = NULL;
        s_ota_status_label = NULL;
        s_ota_current_label = NULL;
        s_ota_latest_label = NULL;
        s_ota_updater_section = NULL;
        s_ota_updater_label = NULL;
        s_ota_progress_bar = NULL;
        s_ota_progress_label = NULL;
        s_ota_changelog_label = NULL;
        s_ota_check_button = NULL;
        s_ota_check_button_label = NULL;
        s_ota_update_button = NULL;
        s_ota_update_button_label = NULL;
        s_ota_updater_button = NULL;
        s_ota_updater_button_label = NULL;
        s_ota_confirm_overlay = NULL;
        s_ota_updater_confirm_overlay = NULL;
        s_ota_confirm_shown = false;
        s_system_info_time_label = NULL;
        s_config_backup_status_label = NULL;
        s_config_migration_checkbox = NULL;
        s_audio_bar_width_ta = NULL;
        s_audio_bar_x_ta = NULL;
        s_audio_bar_range_label = NULL;
        s_audio_bar_status_label = NULL;
        s_screensaver_idle_ta = NULL;
        s_screensaver_switch_ta = NULL;
        s_screensaver_dir_value_label = NULL;
        s_screensaver_dir_picker_overlay = NULL;
        s_screensaver_dir_picker_path_label = NULL;
        s_screensaver_dir_picker_list = NULL;
        s_screensaver_dir_picker_status_label = NULL;
        s_screensaver_dir_picker_up_button = NULL;
        s_screensaver_status_label = NULL;
        memset(s_screensaver_pos_tas, 0, sizeof(s_screensaver_pos_tas));
        memset(s_screensaver_font_dropdowns, 0, sizeof(s_screensaver_font_dropdowns));
        memset(s_screensaver_visible_checkboxes, 0, sizeof(s_screensaver_visible_checkboxes));
        memset(s_screensaver_component_name_buttons,
               0,
               sizeof(s_screensaver_component_name_buttons));
        memset(s_screensaver_component_name_labels,
               0,
               sizeof(s_screensaver_component_name_labels));
        s_screensaver_disable_ssh_checkbox = NULL;
        s_screensaver_shadow_checkbox = NULL;
        s_screensaver_shadow_length_ta = NULL;
        s_screensaver_color_overlay = NULL;
        s_screensaver_color_preview = NULL;
        s_screensaver_color_r_ta = NULL;
        s_screensaver_color_g_ta = NULL;
        s_screensaver_color_b_ta = NULL;
        s_screensaver_color_component = APP_SETTINGS_SCREENSAVER_COMPONENT_TIME;
        s_screensaver_color_pending = 0xFFFFFF;
        snprintf(s_screensaver_dir_pending,
                 sizeof(s_screensaver_dir_pending),
                 "%s",
                 APP_SETTINGS_DEFAULT_SCREENSAVER_DIR);
        snprintf(s_screensaver_dir_picker_path,
                 sizeof(s_screensaver_dir_picker_path),
                 "%s",
                 APP_SETTINGS_DEFAULT_SCREENSAVER_DIR);
        s_screensaver_mode_pending = APP_SETTINGS_SCREENSAVER_MODE_OFF;
        s_screensaver_order_pending = APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL;
        memset(s_screensaver_mode_buttons, 0, sizeof(s_screensaver_mode_buttons));
        memset(s_screensaver_order_buttons, 0, sizeof(s_screensaver_order_buttons));
        s_timezone_status_label = NULL;
        s_timezone_dialog_overlay = NULL;
        s_timezone_dialog_value_label = NULL;
        s_timezone_dialog_offset_minutes = 0;
        memset(s_timezone_buttons, 0, sizeof(s_timezone_buttons));
        memset(s_timezone_button_labels, 0, sizeof(s_timezone_button_labels));
        memset(s_menu_buttons, 0, sizeof(s_menu_buttons));
        memset(s_menu_icons, 0, sizeof(s_menu_icons));
        memset(s_menu_titles, 0, sizeof(s_menu_titles));
        memset(s_page_containers, 0, sizeof(s_page_containers));
        memset(s_page_rendered, 0, sizeof(s_page_rendered));
    }
}

static void on_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_overlay) {
        close_settings();
    }
}

static void on_panel_click(lv_event_t *e)
{
    (void)e;
}

static void on_close_click(lv_event_t *e)
{
    (void)e;
    close_settings();
}

static void on_brightness_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int value = (int)lv_slider_get_value(slider);
    char text[16];

    snprintf(text, sizeof(text), "%d%%", value);
    lv_label_set_text(value_label, text);
    app_settings_preview_brightness(value);
}

static void on_brightness_released(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = (int)lv_slider_get_value(slider);
    esp_err_t err = app_settings_set_brightness(value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save brightness failed: %s", esp_err_to_name(err));
    }
}

static void on_keyboard_indicator_brightness_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int value = (int)lv_slider_get_value(slider);
    char text[16];

    snprintf(text, sizeof(text), "%d%%", value);
    lv_label_set_text(value_label, text);
    (void)app_settings_preview_keyboard_indicator_brightness(value);
}

static void on_keyboard_indicator_brightness_released(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = (int)lv_slider_get_value(slider);
    esp_err_t err = app_settings_set_keyboard_indicator_brightness(value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set keyboard indicator brightness failed: %s", esp_err_to_name(err));
    }
}

static void update_ime_input_checks(app_ime_input_mode_t mode)
{
    for (int i = 0; i < 2; ++i) {
        if (!s_ime_input_checks[i]) {
            continue;
        }
        if ((app_ime_input_mode_t)i == mode) {
            lv_obj_add_state(s_ime_input_checks[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_ime_input_checks[i], LV_STATE_CHECKED);
        }
    }
}

static void on_ime_input_mode_changed(lv_event_t *e)
{
    app_ime_input_mode_t mode = (app_ime_input_mode_t)(intptr_t)lv_event_get_user_data(e);
    esp_err_t err;

    err = app_settings_set_ime_input_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save ime input mode failed: %s", esp_err_to_name(err));
    }
    update_ime_input_checks(app_settings_get_ime_input_mode());
}

static void on_image_exif_changed(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    esp_err_t err = app_settings_set_image_exif_enabled(enabled);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save image exif setting failed: %s", esp_err_to_name(err));
    }
}

static void on_three_finger_screenshot_changed(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);

    if (!tf_card_is_mounted()) {
        lv_obj_remove_state(cb, LV_STATE_CHECKED);
        ESP_LOGW(TAG, "three finger screenshot ignored: TF card is not mounted");
        return;
    }

    esp_err_t err = app_settings_set_three_finger_screenshot_enabled(enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save three finger screenshot setting failed: %s", esp_err_to_name(err));
    }
    screenshot_service_refresh_enabled();
}

static lv_obj_t *create_ime_input_option(lv_obj_t *parent,
                                         const char *text,
                                         app_ime_input_mode_t mode)
{
    lv_obj_t *cb = lv_checkbox_create(parent);
    lv_checkbox_set_text(cb, text);
    lv_obj_set_width(cb, 180);
    lv_obj_set_height(cb, 42);
    lv_obj_set_style_text_font(cb, &APP_FONT, 0);
    lv_obj_set_style_text_color(cb, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(cb, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(cb, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(cb, on_ime_input_mode_changed, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)mode);
    s_ime_input_checks[(int)mode] = cb;
    return cb;
}

static lv_obj_t *create_settings_action_button(lv_obj_t *parent,
                                               const char *text,
                                               lv_color_t bg,
                                               lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, 180);
    lv_obj_set_height(btn, 48);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *label = create_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    return btn;
}

static void set_config_backup_status(const char *message, lv_color_t color)
{
    if (!s_config_backup_status_label) {
        return;
    }
    lv_label_set_text(s_config_backup_status_label, message ? TRS(message) : "");
    lv_obj_set_style_text_color(s_config_backup_status_label, color, 0);
}
