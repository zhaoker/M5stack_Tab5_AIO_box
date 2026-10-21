static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    if (s_kb) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_pinyin_kb) {
        lv_keyboard_set_textarea(s_pinyin_kb, NULL);
        lv_obj_add_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
    }
#if LV_USE_IME_PINYIN != 0
    if (s_pinyin_cand_panel) {
        lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ui_pinyin_ime_ext_reset(s_pinyin_ext);
#endif
    clear_wifi_textarea_focus();
    restore_wifi_input_layout();
}

static void hide_wifi_keyboard(void)
{
    if (s_kb) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_pinyin_kb) {
        lv_keyboard_set_textarea(s_pinyin_kb, NULL);
        lv_obj_add_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
    }
#if LV_USE_IME_PINYIN != 0
    if (s_pinyin_cand_panel) {
        lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ui_pinyin_ime_ext_reset(s_pinyin_ext);
#endif
    ui_hw_pinyin_input_reset(s_hw_pinyin);
    clear_wifi_textarea_focus();
    restore_wifi_input_layout();
}

static void sync_wifi_hw_pinyin_target(void)
{
    if (!s_hw_pinyin) {
        return;
    }

    if (app_i18n_default_chinese_input() && s_hw_active_ta == s_ta_ssid) {
        ui_hw_pinyin_input_set_target(s_hw_pinyin, s_ta_ssid, s_ta_ssid);
        ui_hw_pinyin_input_reposition(s_hw_pinyin);
    } else {
        ui_hw_pinyin_input_set_target(s_hw_pinyin, NULL, NULL);
    }
}

static void on_ta_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);

    s_hw_active_ta = ta;
    set_wifi_textarea_focus(ta);
    sync_wifi_hw_pinyin_target();

    if (s_hw_keyboard_present) {
        if (s_kb) {
            lv_keyboard_set_textarea(s_kb, NULL);
            lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_pinyin_kb) {
            lv_keyboard_set_textarea(s_pinyin_kb, NULL);
            lv_obj_add_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
        }
#if LV_USE_IME_PINYIN != 0
        if (s_pinyin_cand_panel) {
            lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
        restore_wifi_input_layout();
        return;
    }

    if (app_i18n_default_chinese_input() && ta == s_ta_ssid) {
        if (!s_pinyin_kb) {
            return;
        }
        if (s_kb) {
            lv_keyboard_set_textarea(s_kb, NULL);
            lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }
        lv_keyboard_set_textarea(s_pinyin_kb, ta);
        lv_keyboard_set_mode(s_pinyin_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
#if LV_USE_IME_PINYIN != 0
        ui_pinyin_ime_ext_set_enabled(s_pinyin_ext, true);
        if (s_pinyin_cand_panel) {
            lv_obj_align_to(s_pinyin_cand_panel, s_pinyin_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        }
#endif
        lv_obj_remove_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (!s_kb) {
            return;
        }
        if (s_pinyin_kb) {
            lv_keyboard_set_textarea(s_pinyin_kb, NULL);
            lv_obj_add_flag(s_pinyin_kb, LV_OBJ_FLAG_HIDDEN);
        }
        lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_kb, ta);
#if LV_USE_IME_PINYIN != 0
        ui_pinyin_ime_ext_set_enabled(s_pinyin_ext, false);
        if (s_pinyin_cand_panel) {
            lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
    show_wifi_input_layout();
}

static void hw_keyboard_finish_wifi_textarea(void)
{
    ui_hw_pinyin_input_reset(s_hw_pinyin);
    clear_wifi_textarea_focus();
    s_hw_active_ta = NULL;
    sync_wifi_hw_pinyin_target();
    restore_wifi_input_layout();
}

static void hw_keyboard_focus_wifi_textarea(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }
    s_hw_active_ta = ta;
    set_wifi_textarea_focus(ta);
    sync_wifi_hw_pinyin_target();
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static void hw_keyboard_focus_next_wifi_textarea(void)
{
    lv_obj_t *next = (s_hw_active_ta == s_ta_ssid) ? s_ta_pass : s_ta_ssid;
    hw_keyboard_focus_wifi_textarea(next);
}

static bool hw_keyboard_handle_wifi_key(ui_hw_textarea_key_t key)
{
    if (!s_hw_active_ta) {
        return false;
    }

    switch (key) {
    case UI_HW_TEXTAREA_KEY_ENTER:
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        hw_keyboard_finish_wifi_textarea();
        return true;
    case UI_HW_TEXTAREA_KEY_TAB:
    case UI_HW_TEXTAREA_KEY_UP:
    case UI_HW_TEXTAREA_KEY_DOWN:
        hw_keyboard_focus_next_wifi_textarea();
        return true;
    default:
        return ui_hw_textarea_apply_edit_key(s_hw_active_ta, key);
    }
}

static void hw_keyboard_insert_text(const char *txt, size_t len, uint8_t modifier)
{
    if (!s_hw_active_ta || !txt || len == 0) {
        return;
    }

    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;
    if (app_i18n_default_chinese_input() && s_hw_active_ta == s_ta_ssid &&
        ui_hw_pinyin_input_handle_event(s_hw_pinyin, txt, len, modifier)) {
        return;
    }

    if (ui_hw_textarea_parse_key(txt, len, &key) && hw_keyboard_handle_wifi_key(key)) {
        return;
    }

    if (ui_hw_textarea_modifier_blocks_text(modifier)) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = txt[i];
        if (ch == '\r' || ch == '\n') {
            hw_keyboard_finish_wifi_textarea();
        } else if (ch == '\t') {
            hw_keyboard_focus_next_wifi_textarea();
        } else if (ch == '\b' || (uint8_t)ch == 0x7f) {
            lv_textarea_delete_char(s_hw_active_ta);
        } else if ((uint8_t)ch >= 0x20) {
            lv_textarea_add_char(s_hw_active_ta, (uint32_t)(uint8_t)ch);
        }
    }
}

static void hw_keyboard_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    static uint32_t s_last_probe = 0;

    if (!s_hw_keyboard_present) {
        uint32_t now = lv_tick_get();
        if (s_last_probe == 0 || now - s_last_probe >= 1000) {
            s_last_probe = now;
            if (tab5_keyboard_init() == ESP_OK) {
                s_hw_keyboard_present = true;
                hide_wifi_keyboard();
                ESP_LOGI(TAG, "Physical keyboard detected; WiFi soft keyboard disabled");
            }
        }
        return;
    }

    for (int i = 0; i < 8; i++) {
        tab5_keyboard_event_t evt;
        esp_err_t err = tab5_keyboard_read_event(&evt);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "hardware keyboard read failed: %s", esp_err_to_name(err));
            s_hw_keyboard_present = false;
            tab5_keyboard_deinit();
            break;
        }
        hw_keyboard_insert_text(evt.chars, evt.length, evt.modifier);
    }
}

static void destroy_overlay(void)
{
    if (s_refresh_timer) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    if (s_scan_timer) {
        lv_timer_del(s_scan_timer);
        s_scan_timer = NULL;
    }
    if (s_hw_keyboard_timer) {
        lv_timer_del(s_hw_keyboard_timer);
        s_hw_keyboard_timer = NULL;
    }
    tab5_keyboard_deinit();
    ui_hw_pinyin_input_destroy(&s_hw_pinyin);
    ui_pinyin_ime_ext_forget_objects(s_pinyin_ext);
    if (s_overlay) {
        lv_obj_del(s_overlay);
    }
    ui_pinyin_ime_ext_destroy(&s_pinyin_ext);

    s_overlay = NULL;
    s_panel = NULL;
    s_status_card = NULL;
    s_manual_title = NULL;
    s_lbl_state = NULL;
    s_lbl_ipv4 = NULL;
    s_lbl_ipv6 = NULL;
    s_scan_hint = NULL;
    s_ta_ssid = NULL;
    s_ta_pass = NULL;
    s_kb = NULL;
    s_pinyin_kb = NULL;
#if LV_USE_IME_PINYIN != 0
    s_pinyin_ime = NULL;
    s_pinyin_cand_panel = NULL;
#endif
    s_saved_list = NULL;
    s_scan_list = NULL;
    s_hw_active_ta = NULL;
    s_hw_keyboard_present = false;
    s_saved_count = 0;
    s_scan_count = 0;
}
