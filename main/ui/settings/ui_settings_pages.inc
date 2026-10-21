static void render_system_settings_page(lv_obj_t *parent)
{
    int brightness = app_settings_get_brightness();
    int keyboard_indicator_brightness = app_settings_get_keyboard_indicator_brightness();
    bool keyboard_detected = tab5_keyboard_was_detected_at_startup();
    bool tf_mounted = tf_card_is_mounted();
    char value_text[16];

    create_label(parent, "系统设置", THEME_TEXT_PRIMARY);
    render_language_settings_card(parent);

    lv_obj_t *card = lv_obj_create(parent);
    apply_surface(card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(card);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 34);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    create_label(header, "屏幕背光", THEME_TEXT_SECONDARY);
    snprintf(value_text, sizeof(value_text), "%d%%", brightness);
    lv_obj_t *value_label = create_label(header, value_text, THEME_PRIMARY_LIGHT);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_height(slider, 26);
    lv_slider_set_range(slider, APP_SETTINGS_MIN_BRIGHTNESS, APP_SETTINGS_MAX_BRIGHTNESS);
    lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, THEME_BG_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, THEME_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, THEME_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, value_label);
    lv_obj_add_event_cb(slider, on_brightness_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, on_brightness_released, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *screenshot_card = lv_obj_create(parent);
    apply_surface(screenshot_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(screenshot_card, LV_PCT(100));
    lv_obj_set_height(screenshot_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(screenshot_card, 18, 0);
    lv_obj_set_style_pad_row(screenshot_card, 12, 0);
    lv_obj_set_flex_flow(screenshot_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(screenshot_card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(screenshot_card, "截屏", THEME_TEXT_SECONDARY);
    lv_obj_t *screenshot_cb = lv_checkbox_create(screenshot_card);
    lv_checkbox_set_text(screenshot_cb, TRS("开启三指滑动截屏"));
    lv_obj_set_width(screenshot_cb, LV_PCT(100));
    lv_obj_set_height(screenshot_cb, 42);
    lv_obj_set_style_text_font(screenshot_cb, &APP_FONT, 0);
    lv_obj_set_style_text_color(screenshot_cb,
                                tf_mounted ? THEME_TEXT_PRIMARY : THEME_TEXT_SECONDARY,
                                0);
    lv_obj_set_style_bg_opa(screenshot_cb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(screenshot_cb,
                                  THEME_PRIMARY,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(screenshot_cb,
                              THEME_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(screenshot_cb,
                            LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (tf_mounted && app_settings_get_three_finger_screenshot_enabled()) {
        lv_obj_add_state(screenshot_cb, LV_STATE_CHECKED);
    }
    if (!tf_mounted) {
        lv_obj_add_state(screenshot_cb, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(screenshot_cb,
                        on_three_finger_screenshot_changed,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);

    create_wrap_label(screenshot_card,
                      tf_mounted ? "截图将保存到TF卡的“ScreenShots”文件夹，文件名使用当前时间。"
                                 : "未检测到 TF 卡，三指截屏功能不可用。",
                      tf_mounted ? THEME_TEXT_TERTIARY : THEME_WARNING_LIGHT);

    if (keyboard_detected) {
        lv_obj_t *keyboard_card = lv_obj_create(parent);
        apply_surface(keyboard_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
        lv_obj_set_width(keyboard_card, LV_PCT(100));
        lv_obj_set_height(keyboard_card, 150);
        lv_obj_set_style_pad_all(keyboard_card, 18, 0);
        lv_obj_set_style_pad_row(keyboard_card, 18, 0);
        lv_obj_set_flex_flow(keyboard_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(keyboard_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *keyboard_header = lv_obj_create(keyboard_card);
        lv_obj_set_width(keyboard_header, LV_PCT(100));
        lv_obj_set_height(keyboard_header, 34);
        lv_obj_set_style_bg_opa(keyboard_header, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(keyboard_header, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(keyboard_header, 0, 0);
        lv_obj_set_flex_flow(keyboard_header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(keyboard_header,
                              LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(keyboard_header, LV_OBJ_FLAG_SCROLLABLE);

        create_label(keyboard_header, "实体键盘指示灯亮度", THEME_TEXT_SECONDARY);
        snprintf(value_text, sizeof(value_text), "%d%%", keyboard_indicator_brightness);
        lv_obj_t *keyboard_value_label = create_label(keyboard_header, value_text, THEME_PRIMARY_LIGHT);

        lv_obj_t *keyboard_slider = lv_slider_create(keyboard_card);
        lv_obj_set_width(keyboard_slider, LV_PCT(100));
        lv_obj_set_height(keyboard_slider, 26);
        lv_slider_set_range(keyboard_slider,
                            APP_SETTINGS_MIN_KEYBOARD_INDICATOR_BRIGHTNESS,
                            APP_SETTINGS_MAX_KEYBOARD_INDICATOR_BRIGHTNESS);
        lv_slider_set_value(keyboard_slider, keyboard_indicator_brightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(keyboard_slider, THEME_BG_PRIMARY, LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboard_slider, THEME_PRIMARY, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(keyboard_slider, THEME_TEXT_PRIMARY, LV_PART_KNOB);
        lv_obj_add_event_cb(keyboard_slider,
                            on_keyboard_indicator_brightness_changed,
                            LV_EVENT_VALUE_CHANGED,
                            keyboard_value_label);
        lv_obj_add_event_cb(keyboard_slider,
                            on_keyboard_indicator_brightness_released,
                            LV_EVENT_RELEASED,
                            NULL);
        lv_obj_add_event_cb(keyboard_slider,
                            on_keyboard_indicator_brightness_released,
                            LV_EVENT_PRESS_LOST,
                            NULL);
    }

    lv_obj_t *ime_card = lv_obj_create(parent);
    apply_surface(ime_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(ime_card, LV_PCT(100));
    lv_obj_set_height(ime_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ime_card, 18, 0);
    lv_obj_set_style_pad_row(ime_card, 12, 0);
    lv_obj_set_flex_flow(ime_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(ime_card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(ime_card, "输入法方式", THEME_TEXT_SECONDARY);
    lv_obj_t *ime_options = lv_obj_create(ime_card);
    lv_obj_set_width(ime_options, LV_PCT(100));
    lv_obj_set_height(ime_options, 44);
    lv_obj_set_style_bg_opa(ime_options, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(ime_options, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ime_options, 0, 0);
    lv_obj_set_style_pad_column(ime_options, 24, 0);
    lv_obj_set_flex_flow(ime_options, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ime_options,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ime_options, LV_OBJ_FLAG_SCROLLABLE);
    create_ime_input_option(ime_options, "单字输入", APP_SETTINGS_IME_INPUT_MODE_CHARACTER);
    create_ime_input_option(ime_options, "字词输入", APP_SETTINGS_IME_INPUT_MODE_PHRASE);
    update_ime_input_checks(app_settings_get_ime_input_mode());

    lv_obj_t *backup_card = lv_obj_create(parent);
    apply_surface(backup_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(backup_card, LV_PCT(100));
    lv_obj_set_height(backup_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(backup_card, 18, 0);
    lv_obj_set_style_pad_row(backup_card, 12, 0);
    lv_obj_set_flex_flow(backup_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(backup_card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(backup_card, "配置备份", THEME_TEXT_SECONDARY);

    s_config_migration_checkbox = lv_checkbox_create(backup_card);
    lv_checkbox_set_text(s_config_migration_checkbox, TRS("加密备份（支持跨设备迁移）"));
    lv_obj_set_width(s_config_migration_checkbox, LV_PCT(100));
    lv_obj_set_height(s_config_migration_checkbox, 38);
    lv_obj_set_style_text_font(s_config_migration_checkbox, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_config_migration_checkbox, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_opa(s_config_migration_checkbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_config_migration_checkbox, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_config_migration_checkbox, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_config_migration_checkbox, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_config_migration_checkbox, on_config_migration_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_null_on_delete(&s_config_migration_checkbox);

    lv_obj_t *backup_buttons = lv_obj_create(backup_card);
    lv_obj_set_width(backup_buttons, LV_PCT(100));
    lv_obj_set_height(backup_buttons, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(backup_buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(backup_buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(backup_buttons, 0, 0);
    lv_obj_set_style_pad_column(backup_buttons, 12, 0);
    lv_obj_set_style_pad_row(backup_buttons, 12, 0);
    lv_obj_set_flex_flow(backup_buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(backup_buttons, LV_OBJ_FLAG_SCROLLABLE);

    create_settings_action_button(backup_buttons, LV_SYMBOL_UPLOAD " 导出备份", THEME_PRIMARY, on_config_export_click);
    create_settings_action_button(backup_buttons, LV_SYMBOL_DOWNLOAD " 导入备份", THEME_BG_HOVER, on_config_import_click);
    s_config_backup_status_label =
        create_wrap_label(backup_card,
                          TRS("非加密备份：会导出本机密码密钥和加密后的 SSH/WiFi 密码，适合本机 0x0 全擦后恢复；备份文件请勿外传。"),
                          THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_config_backup_status_label);
}

static void format_fixed_2(uint64_t value, uint64_t scale, const char *unit, char *out, size_t out_len)
{
    uint64_t whole;
    uint64_t frac;

    if (!out || out_len == 0) {
        return;
    }
    if (scale == 0) {
        snprintf(out, out_len, "0 %s", unit ? unit : "");
        return;
    }

    whole = value / scale;
    frac = ((value % scale) * 100ULL + scale / 2ULL) / scale;
    if (frac >= 100ULL) {
        whole++;
        frac -= 100ULL;
    }

    snprintf(out,
             out_len,
             "%llu.%02llu %s",
             (unsigned long long)whole,
             (unsigned long long)frac,
             unit ? unit : "");
}

static void format_storage_size(uint64_t bytes, char *out, size_t out_len)
{
    static const char *const units[] = {"KB", "MB", "GB"};
    uint64_t scale = 1024ULL;
    size_t unit = 0;

    while (unit + 1 < sizeof(units) / sizeof(units[0]) && bytes >= scale * 1024ULL) {
        scale *= 1024ULL;
        unit++;
    }
    format_fixed_2(bytes, scale, units[unit], out, out_len);
}

static void format_ota_size(uint64_t bytes, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        format_fixed_2(bytes, 1024ULL * 1024ULL, "MB", out, out_len);
    } else if (bytes >= 1024ULL) {
        format_fixed_2(bytes, 1024ULL, "KB", out, out_len);
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}

static void format_ota_speed(uint32_t bytes_per_sec, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (bytes_per_sec >= 1024U * 1024U) {
        format_fixed_2(bytes_per_sec, 1024ULL * 1024ULL, "MB/s", out, out_len);
    } else if (bytes_per_sec >= 1024U) {
        format_fixed_2(bytes_per_sec, 1024ULL, "KB/s", out, out_len);
    } else {
        snprintf(out, out_len, "%" PRIu32 " B/s", bytes_per_sec);
    }
}

static void close_ota_confirm_dialog(void);
static void show_ota_confirm_dialog(void);
static void close_ota_updater_confirm_dialog(void);
static void show_ota_updater_confirm_dialog(void);
static void close_ota_wait_dialog(void);
static void show_ota_wait_dialog(const char *message);

static const ota_update_status_t *get_ota_status_snapshot(void)
{
    ota_update_get_status(&s_ota_status_cache);
    return &s_ota_status_cache;
}

static void update_ota_button_state(const ota_update_status_t *st)
{
    bool launcher_mode = st && st->launcher_mode;
    bool can_check = st && !st->busy && !st->ready_to_reboot;
    bool can_update = st && !launcher_mode && !st->busy && (st->update_available || st->ready_to_reboot);
    bool show_updater = st && !launcher_mode && (st->updater_update_available || st->updater_busy);
    bool can_update_updater = show_updater && !st->busy && !st->ready_to_reboot && st->updater_update_available;

    if (s_ota_check_button) {
        lv_obj_clear_flag(s_ota_check_button, LV_OBJ_FLAG_HIDDEN);
        if (can_check) {
            lv_obj_remove_state(s_ota_check_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_ota_check_button, LV_STATE_DISABLED);
        }
    }
    if (s_ota_update_button) {
        if (launcher_mode) {
            lv_obj_add_flag(s_ota_update_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_ota_update_button, LV_OBJ_FLAG_HIDDEN);
        }
        if (can_update) {
            lv_obj_remove_state(s_ota_update_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_ota_update_button,
                                      st->ready_to_reboot ? THEME_WARNING : THEME_SUCCESS,
                                      0);
        } else {
            lv_obj_add_state(s_ota_update_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_ota_update_button, THEME_BG_HOVER, 0);
        }
    }
    if (s_ota_update_button_label) {
        lv_label_set_text(s_ota_update_button_label,
                          st && st->ready_to_reboot ? TRS(LV_SYMBOL_OK " 安装并重启")
                                                     : TRS(LV_SYMBOL_DOWNLOAD " 立即更新"));
        lv_obj_center(s_ota_update_button_label);
    }
    if (s_ota_updater_section) {
        if (show_updater) {
            lv_obj_clear_flag(s_ota_updater_section, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ota_updater_section, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ota_updater_button) {
        if (show_updater) {
            lv_obj_clear_flag(s_ota_updater_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ota_updater_button, LV_OBJ_FLAG_HIDDEN);
        }
        if (can_update_updater) {
            lv_obj_remove_state(s_ota_updater_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_ota_updater_button, THEME_WARNING, 0);
        } else {
            lv_obj_add_state(s_ota_updater_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_ota_updater_button, THEME_BG_HOVER, 0);
        }
    }
    if (s_ota_updater_button_label) {
        lv_label_set_text(s_ota_updater_button_label,
                          st && st->updater_busy ? TRS("更新中...") : TRS("更新UPLOAD固件"));
        lv_obj_center(s_ota_updater_button_label);
    }
}

static void update_ota_status_view(void)
{
    const ota_update_status_t *st = get_ota_status_snapshot();
    char text[160];
    char size_text[32];
    char got_text[32];
    char speed_text[32];
    bool show_progress = st->state == OTA_UPDATE_STATE_DOWNLOADING ||
                         st->state == OTA_UPDATE_STATE_READY_TO_REBOOT ||
                         st->downloaded > 0;

    if (s_ota_status_label) {
        lv_label_set_text_fmt(s_ota_status_label,
                              "%s：%s",
                              ota_update_state_text(st->state),
                              st->error[0] ? st->error : st->status);
        lv_obj_set_style_text_color(s_ota_status_label,
                                    st->state == OTA_UPDATE_STATE_ERROR ? THEME_WARNING_LIGHT
                                    : st->state == OTA_UPDATE_STATE_UPDATE_AVAILABLE ? THEME_SUCCESS_LIGHT
                                    : THEME_TEXT_SECONDARY,
                                    0);
    }
    if (s_ota_current_label) {
        lv_label_set_text_fmt(s_ota_current_label,
                              TRS("当前版本：%s"),
                              st->current_version[0] ? st->current_version : TRS("未知"));
    }
    if (s_ota_latest_label) {
        if (st->latest_version[0]) {
            lv_label_set_text_fmt(s_ota_latest_label,
                                  TRS("最新版本：%s  %s"),
                                  st->latest_version,
                                  st->latest_build_time[0] ? st->latest_build_time : "");
        } else {
            lv_label_set_text(s_ota_latest_label, TRS("最新版本：尚未检测"));
        }
    }
    if (s_ota_updater_label) {
        if (!st->launcher_mode && (st->updater_update_available || st->updater_busy)) {
            lv_obj_clear_flag(s_ota_updater_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(s_ota_updater_label,
                                  TRS("UPLOAD 固件\n当前版本：%s\n最新版本：%s  %s"),
                                  st->updater_current_version[0] ? st->updater_current_version : TRS("未知"),
                                  st->updater_latest_version[0] ? st->updater_latest_version : TRS("未知"),
                                  st->updater_build_time[0] ? st->updater_build_time : "");
            lv_obj_set_style_text_color(s_ota_updater_label,
                                        st->updater_update_available ? THEME_WARNING_LIGHT : THEME_TEXT_TERTIARY,
                                        0);
        } else {
            lv_label_set_text(s_ota_updater_label, "");
            lv_obj_add_flag(s_ota_updater_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ota_launcher_hint_label) {
        if (st->launcher_mode) {
            lv_obj_clear_flag(s_ota_launcher_hint_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_ota_launcher_hint_label,
                              TRS("当前app使用Launcher，无法直接OTA升级，请通过 Launcher 更新"));
        } else {
            lv_obj_add_flag(s_ota_launcher_hint_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ota_progress_bar) {
        if (show_progress) {
            lv_obj_clear_flag(s_ota_progress_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ota_progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
        lv_bar_set_value(s_ota_progress_bar, st->progress, LV_ANIM_OFF);
    }
    if (s_ota_progress_label) {
        if (show_progress) {
            lv_obj_clear_flag(s_ota_progress_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ota_progress_label, LV_OBJ_FLAG_HIDDEN);
        }
        format_ota_size(st->downloaded, got_text, sizeof(got_text));
        format_ota_size(st->total, size_text, sizeof(size_text));
        format_ota_speed(st->speed_bps, speed_text, sizeof(speed_text));
        snprintf(text,
                 sizeof(text),
                 "%d%%  %s / %s  %s",
                 st->progress,
                 got_text,
                 st->total ? size_text : "--",
                 st->speed_bps ? speed_text : "");
        lv_label_set_text(s_ota_progress_label, text);
    }
    if (s_ota_changelog_label) {
        lv_label_set_text(s_ota_changelog_label,
                          st->changelog[0] ? st->changelog : TRS("检测到服务器更新日志后将在这里显示。"));
    }
    update_ota_button_state(st);
}

static void on_ota_status_timer(lv_timer_t *timer)
{
    (void)timer;
    update_ota_status_view();
    if (s_ota_status_cache.ready_to_reboot) {
        if (!s_ota_confirm_shown) {
            s_ota_confirm_shown = true;
            show_ota_confirm_dialog();
        }
    } else {
        s_ota_confirm_shown = false;
        close_ota_confirm_dialog();
    }
    if (s_ota_updater_confirm_overlay && s_ota_status_cache.busy) {
        close_ota_updater_confirm_dialog();
    }
}

static void on_ota_check_click(lv_event_t *e)
{
    (void)e;
    esp_err_t err = ota_update_start_check();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "start ota check failed: %s", esp_err_to_name(err));
    }
    update_ota_status_view();
}

static void close_ota_confirm_dialog(void)
{
    if (s_ota_confirm_overlay) {
        lv_obj_del(s_ota_confirm_overlay);
        s_ota_confirm_overlay = NULL;
    }
}

static void close_ota_updater_confirm_dialog(void)
{
    if (s_ota_updater_confirm_overlay) {
        lv_obj_del(s_ota_updater_confirm_overlay);
        s_ota_updater_confirm_overlay = NULL;
    }
}

static void close_ota_wait_dialog(void)
{
    if (s_ota_wait_overlay) {
        lv_obj_del(s_ota_wait_overlay);
        s_ota_wait_overlay = NULL;
    }
}

static void show_ota_wait_dialog(const char *message)
{
    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();

    close_ota_confirm_dialog();
    close_ota_updater_confirm_dialog();
    close_ota_wait_dialog();

    s_ota_wait_overlay = lv_obj_create(parent);
    lv_obj_null_on_delete(&s_ota_wait_overlay);
    lv_obj_set_size(s_ota_wait_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ota_wait_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ota_wait_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_ota_wait_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ota_wait_overlay, 0, 0);
    lv_obj_clear_flag(s_ota_wait_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ota_wait_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_ota_wait_overlay);
    lv_obj_set_size(panel, 300, 120);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    lv_obj_t *label = create_label(panel, message && message[0] ? TRS(message) : TRS("请稍等..."), THEME_TEXT_PRIMARY);
    lv_obj_center(label);

    lv_timer_handler();
    lv_refr_now(NULL);
}

static void on_ota_confirm_cancel(lv_event_t *e)
{
    (void)e;
    close_ota_confirm_dialog();
}

static void on_ota_confirm_install(lv_event_t *e)
{
    (void)e;
    show_ota_wait_dialog("请稍等...");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_err_t err = ota_update_reboot_to_updater();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reboot to updater failed: %s", esp_err_to_name(err));
        close_ota_wait_dialog();
        update_ota_status_view();
    }
}

static void on_ota_updater_confirm_cancel(lv_event_t *e)
{
    (void)e;
    close_ota_updater_confirm_dialog();
}

static void on_ota_updater_confirm_install(lv_event_t *e)
{
    (void)e;
    close_ota_updater_confirm_dialog();
    esp_err_t err = ota_update_start_updater_install();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "start updater install failed: %s", esp_err_to_name(err));
    }
    update_ota_status_view();
}

static lv_obj_t *create_ota_dialog_button(lv_obj_t *parent,
                                          const char *text,
                                          lv_color_t bg,
                                          lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 150, 44);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = create_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    return btn;
}

static void show_ota_confirm_dialog(void)
{
    const ota_update_status_t *st;
    char message[384];

    if (s_ota_confirm_overlay) {
        lv_obj_move_foreground(s_ota_confirm_overlay);
        return;
    }
    st = get_ota_status_snapshot();
    if (!st->ready_to_reboot) {
        return;
    }

    snprintf(message,
             sizeof(message),
             TRS("已下载固件：%.63s\n\n安装会重启到 updater 并刷写主程序。建议安装前保持稳定供电，避免升级过程中断电。"),
             st->latest_version[0] ? st->latest_version : TRS("未知版本"));

    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();
    s_ota_confirm_overlay = lv_obj_create(parent);
    lv_obj_null_on_delete(&s_ota_confirm_overlay);
    lv_obj_set_size(s_ota_confirm_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ota_confirm_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ota_confirm_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_ota_confirm_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ota_confirm_overlay, 0, 0);
    lv_obj_clear_flag(s_ota_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ota_confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_ota_confirm_overlay);
    lv_obj_set_size(panel, 520, LV_SIZE_CONTENT);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    create_label(panel, "确认安装固件", THEME_WARNING_LIGHT);
    create_wrap_label(panel, message, THEME_TEXT_SECONDARY);
    create_wrap_label(panel,
                      TRS("确认后系统会先校验 TF 卡固件，再进入升级。"),
                      THEME_TEXT_TERTIARY);

    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, 48);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    create_ota_dialog_button(buttons, "取消", THEME_BG_HOVER, on_ota_confirm_cancel);
    create_ota_dialog_button(buttons, "确认安装", THEME_WARNING, on_ota_confirm_install);
}

static void show_ota_updater_confirm_dialog(void)
{
    const ota_update_status_t *st;
    char message[384];

    if (s_ota_updater_confirm_overlay) {
        lv_obj_move_foreground(s_ota_updater_confirm_overlay);
        return;
    }
    st = get_ota_status_snapshot();
    if (!st->updater_update_available || st->busy) {
        return;
    }

    snprintf(message,
             sizeof(message),
             TRS("准备更新 UPLOAD 固件：%.63s\n\n更新过程中不得断电，不得进行其他操作。确认后系统会下载、校验并刷写 factory updater 分区。"),
             st->updater_latest_version[0] ? st->updater_latest_version : TRS("未知版本"));

    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();
    s_ota_updater_confirm_overlay = lv_obj_create(parent);
    lv_obj_null_on_delete(&s_ota_updater_confirm_overlay);
    lv_obj_set_size(s_ota_updater_confirm_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ota_updater_confirm_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ota_updater_confirm_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_ota_updater_confirm_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ota_updater_confirm_overlay, 0, 0);
    lv_obj_clear_flag(s_ota_updater_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ota_updater_confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_ota_updater_confirm_overlay);
    lv_obj_set_size(panel, 560, LV_SIZE_CONTENT);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    create_label(panel, "确认更新 UPLOAD 固件", THEME_WARNING_LIGHT);
    create_wrap_label(panel, message, THEME_TEXT_SECONDARY);
    create_wrap_label(panel,
                      TRS("该操作不会更新主程序，但会影响后续主程序 OTA 安装流程。"),
                      THEME_TEXT_TERTIARY);

    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, 48);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    create_ota_dialog_button(buttons, "取消", THEME_BG_HOVER, on_ota_updater_confirm_cancel);
    create_ota_dialog_button(buttons, "确认更新", THEME_WARNING, on_ota_updater_confirm_install);
}

static void on_ota_update_click(lv_event_t *e)
{
    (void)e;
    const ota_update_status_t *st = get_ota_status_snapshot();

    if (st->ready_to_reboot) {
        show_ota_confirm_dialog();
        return;
    }

    esp_err_t err = ota_update_start_download();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "start ota download failed: %s", esp_err_to_name(err));
    }
    update_ota_status_view();
}

static void on_ota_updater_click(lv_event_t *e)
{
    (void)e;
    show_ota_updater_confirm_dialog();
}

static lv_obj_t *create_ota_button(lv_obj_t *parent,
                                   const char *text,
                                   lv_color_t bg,
                                   lv_event_cb_t cb,
                                   lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, 188);
    lv_obj_set_height(btn, 46);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = create_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    if (out_label) {
        *out_label = label;
        lv_obj_null_on_delete(out_label);
    }
    return btn;
}

static void render_firmware_update_page(lv_obj_t *parent)
{
    create_label(parent, "固件升级", THEME_TEXT_PRIMARY);

    lv_obj_t *card = lv_obj_create(parent);
    apply_surface(card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_ota_status_label = create_wrap_label(card, "等待检测更新。", THEME_TEXT_SECONDARY);
    lv_obj_null_on_delete(&s_ota_status_label);
    s_ota_current_label = create_wrap_label(card, "当前版本：--", THEME_TEXT_PRIMARY);
    lv_obj_null_on_delete(&s_ota_current_label);
    s_ota_latest_label = create_wrap_label(card, "最新版本：尚未检测", THEME_TEXT_SECONDARY);
    lv_obj_null_on_delete(&s_ota_latest_label);
    s_ota_launcher_hint_label =
        create_wrap_label(card,
                          TRS("当前app使用Launcher，无法直接OTA升级，请通过 Launcher 更新"),
                          THEME_WARNING_LIGHT);
    lv_obj_null_on_delete(&s_ota_launcher_hint_label);
    lv_obj_add_flag(s_ota_launcher_hint_label, LV_OBJ_FLAG_HIDDEN);

    s_ota_progress_bar = lv_bar_create(card);
    lv_obj_null_on_delete(&s_ota_progress_bar);
    lv_obj_set_width(s_ota_progress_bar, LV_PCT(100));
    lv_obj_set_height(s_ota_progress_bar, 20);
    lv_bar_set_range(s_ota_progress_bar, 0, 100);
    lv_bar_set_value(s_ota_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ota_progress_bar, THEME_BG_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ota_progress_bar, THEME_PRIMARY, LV_PART_INDICATOR);
    lv_obj_add_flag(s_ota_progress_bar, LV_OBJ_FLAG_HIDDEN);

    s_ota_progress_label = create_wrap_label(card, "0%  0 B / --", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_ota_progress_label);
    lv_obj_add_flag(s_ota_progress_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *buttons = lv_obj_create(card);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_style_pad_row(buttons, 12, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    s_ota_check_button = create_ota_button(buttons,
                                           LV_SYMBOL_REFRESH " 检测更新",
                                           THEME_PRIMARY,
                                           on_ota_check_click,
                                           &s_ota_check_button_label);
    lv_obj_null_on_delete(&s_ota_check_button);
    s_ota_update_button = create_ota_button(buttons,
                                            LV_SYMBOL_DOWNLOAD " 立即更新",
                                            THEME_BG_HOVER,
                                            on_ota_update_click,
                                            &s_ota_update_button_label);
    lv_obj_null_on_delete(&s_ota_update_button);
    s_ota_updater_section = lv_obj_create(card);
    lv_obj_null_on_delete(&s_ota_updater_section);
    lv_obj_set_width(s_ota_updater_section, LV_PCT(100));
    lv_obj_set_height(s_ota_updater_section, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_ota_updater_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_ota_updater_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_ota_updater_section, 0, 0);
    lv_obj_set_style_pad_row(s_ota_updater_section, 10, 0);
    lv_obj_set_flex_flow(s_ota_updater_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_ota_updater_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ota_updater_section, LV_OBJ_FLAG_HIDDEN);

    s_ota_updater_label = create_wrap_label(s_ota_updater_section,
                                            TRS("UPLOAD 固件\n当前版本：--\n最新版本：--"),
                                            THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_ota_updater_label);
    lv_obj_add_flag(s_ota_updater_label, LV_OBJ_FLAG_HIDDEN);
    s_ota_updater_button = create_ota_button(s_ota_updater_section,
                                             "更新UPLOAD固件",
                                             THEME_BG_HOVER,
                                             on_ota_updater_click,
                                             &s_ota_updater_button_label);
    lv_obj_null_on_delete(&s_ota_updater_button);
    lv_obj_add_flag(s_ota_updater_button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *log_card = lv_obj_create(parent);
    apply_surface(log_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(log_card, LV_PCT(100));
    lv_obj_set_height(log_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(log_card, 18, 0);
    lv_obj_set_style_pad_row(log_card, 10, 0);
    lv_obj_set_flex_flow(log_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(log_card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(log_card, "更新日志", THEME_TEXT_PRIMARY);
    s_ota_changelog_label = create_wrap_label(log_card, "检测到服务器更新日志后将在这里显示。", THEME_TEXT_SECONDARY);
    lv_obj_null_on_delete(&s_ota_changelog_label);

    update_ota_status_view();
    if (!s_ota_status_timer) {
        s_ota_status_timer = lv_timer_create(on_ota_status_timer, 600, NULL);
    }
    const ota_update_status_t *st = get_ota_status_snapshot();
    if (!st->launcher_mode) {
        (void)ota_update_scan_ready_package();
        st = get_ota_status_snapshot();
    }
    if (st->state == OTA_UPDATE_STATE_IDLE) {
        (void)ota_update_start_check();
    }
}

static void update_system_info_time_label(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    char text[72];
    const char *timezone_label = TRS(app_settings_get_current_timezone_source_label());

    if (!s_system_info_time_label) {
        return;
    }
    if (now <= 0 || !localtime_r(&now, &tm_now)) {
        snprintf(text, sizeof(text), "%s --", timezone_label);
        lv_label_set_text(s_system_info_time_label, text);
        return;
    }
    snprintf(text,
             sizeof(text),
             "%s %04d-%02d-%02d %02d:%02d",
             timezone_label,
             tm_now.tm_year + 1900,
             tm_now.tm_mon + 1,
             tm_now.tm_mday,
             tm_now.tm_hour,
             tm_now.tm_min);
    lv_label_set_text(s_system_info_time_label, text);
}

static void on_system_info_time_timer(lv_timer_t *timer)
{
    (void)timer;
    update_system_info_time_label();
}

static void render_system_info_page(lv_obj_t *parent)
{
    system_info_t info;
    char value[96];
    char used_text[24];
    char total_text[24];

    collect_basic_system_info(&info);

    lv_obj_t *title_row = lv_obj_create(parent);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 34);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    create_label(title_row, "系统信息", THEME_TEXT_PRIMARY);
    s_system_info_time_label = create_label(title_row, "", THEME_TEXT_DISABLED);
    lv_obj_null_on_delete(&s_system_info_time_label);
    lv_obj_set_width(s_system_info_time_label, 360);
    lv_label_set_long_mode(s_system_info_time_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_system_info_time_label, LV_TEXT_ALIGN_RIGHT, 0);
    update_system_info_time_label();
    if (!s_system_info_time_timer) {
        s_system_info_time_timer = lv_timer_create(on_system_info_time_timer, 30000, NULL);
    }

    lv_obj_t *card = lv_obj_create(parent);
    apply_surface(card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    snprintf(value, sizeof(value), "%" PRIu32 "MHz (Dual Core + LP Core)", info.cpu_mhz);
    create_row(card, "CPU 频率", value);
    snprintf(used_text, sizeof(used_text), "%" PRIu32 " MB", info.psram_physical_mb - info.psram_free_mb);
    snprintf(total_text, sizeof(total_text), "%" PRIu32 " MB", info.psram_physical_mb);
    snprintf(value, sizeof(value), TRS("已用 %s / 总量 %s"), used_text, total_text);
    create_row(card, "PSRAM", value);
    snprintf(value, sizeof(value), "%" PRIu32 " MB", info.flash_mb);
    create_row(card, "闪存", value);
    create_row(card, "屏幕方案", info.panel_ic ? info.panel_ic : TRS("未知"));
    create_row(card, "实体键盘", info.keyboard_ready ? TRS("已连接") : TRS("未检测到"));

    add_board_i2c_device_list(parent, info.panel_ic, info.keyboard_ready);

    create_footer_label(parent, TRS("本固件使用了Ewan Parker 的 libssh库（LGPL v2.1）"), THEME_TEXT_TERTIARY);
    create_footer_label(parent, "Powered by Zhaoker", THEME_TEXT_TERTIARY);
}
