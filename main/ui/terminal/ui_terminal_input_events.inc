static void ssh_send_text_with_modifier(const char *txt, size_t len, uint8_t modifier)
{
    if (!txt || len == 0 || !ssh_client_is_connected()) return;

    bool ctrl = (modifier & 0x01U) != 0U;
    bool alt = (modifier & 0x04U) != 0U;
    if (!ctrl && !alt) {
        ssh_send_terminal_text(txt, len);
        return;
    }

    char ch = txt[0];
    if (ctrl) {
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 1);
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 1);
        }
    }

    if (alt) {
        char seq[2] = {0x1b, ch};
        ssh_client_write(seq, sizeof(seq));
    } else {
        ssh_client_write(&ch, 1);
    }
}

static void hw_keyboard_handle_text(const char *txt, size_t len, uint8_t modifier)
{
    if (!txt || len == 0) return;
    bool has_modifier = (modifier & (HW_MOD_CTRL | HW_MOD_ALT)) != 0U;

#if LV_USE_IME_PINYIN != 0
    if ((modifier & HW_MOD_CTRL) != 0U &&
        (modifier & HW_MOD_ALT) == 0U &&
        len == 1 &&
        txt[0] == ' ') {
        toggle_ime_mode();
        return;
    }
#endif

#if LV_USE_IME_PINYIN != 0
    if (!has_modifier && pinyin_ime_has_composition()) {
        const char *seq = NULL;
        size_t seq_len = 0;
        if (pinyin_handle_page_key_text(txt, len)) {
            return;
        }
        if (terminal_key_text_to_sequence(txt, len, &seq, &seq_len)) {
            if (seq_len == 1 && seq[0] == '\x7f') {
                (void)pinyin_backspace_hardware();
            } else if (seq_len == 1 && seq[0] == '\r') {
                (void)pinyin_commit_raw_input();
            } else {
                pinyin_ime_reset_composition();
            }
            reposition_elements();
            return;
        }
    }
#endif

    if (send_terminal_key_text(txt, len)) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = txt[i];

#if LV_USE_IME_PINYIN != 0
        if (!has_modifier && pinyin_ime_has_composition()) {
            if (pinyin_handle_page_key_text(&ch, 1)) {
                continue;
            }
            if (ch >= '0' && ch <= '9' && pinyin_select_candidate_by_digit(ch)) {
                continue;
            }
            if (ch == ' ' && pinyin_select_candidate_by_digit('1')) {
                continue;
            }
            if (ch == '\b' || ch == 0x7f) {
                (void)pinyin_backspace_hardware();
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                (void)pinyin_commit_raw_input();
                continue;
            }
            if (ch == 0x1b) {
                pinyin_ime_reset_composition();
                reposition_elements();
                continue;
            }
            if (pinyin_handle_hardware_char(ch)) {
                continue;
            }
            pinyin_ime_reset_composition();
        } else if (!has_modifier && pinyin_handle_hardware_char(ch)) {
            continue;
        }
#endif

        if (ch == '\n') {
            ssh_send_text_with_modifier("\r", 1, modifier);
        } else if (ch == '\b' || (uint8_t)ch == 0x7f) {
            ssh_send_text_with_modifier("\x7f", 1, modifier);
        } else {
            ssh_send_text_with_modifier(&ch, 1, modifier);
        }
    }
}

static void hw_keyboard_timer_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t s_last_probe = 0;

    if (!s_hw_keyboard_present) {
        uint32_t now = lv_tick_get();
        if (s_last_probe == 0 || now - s_last_probe >= 1000) {
            s_last_probe = now;
            if (tab5_keyboard_init() == ESP_OK) {
                s_hw_keyboard_present = true;
                if (s_keyboard) {
                    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
                    s_kb_visible = false;
                }
                reposition_elements();
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
#if LV_USE_IME_PINYIN != 0
            pinyin_restore_default_candidate_map();
#endif
            break;
        }

        hw_keyboard_handle_text(evt.chars, evt.length, evt.modifier);
    }
}

static void ssh_send_terminal_text(const char *txt, size_t len)
{
    if (!txt || len == 0 || !ssh_client_is_connected()) return;

    if (s_ctrl_active || s_alt_active) {
        char ch = txt[0];
        if (s_ctrl_active) {
            if (ch >= 'a' && ch <= 'z') {
                ch = ch - 'a' + 1;
            } else if (ch >= 'A' && ch <= 'Z') {
                ch = ch - 'A' + 1;
            } else if (ch == '[') {
                ch = 27;
            } else if ((uint8_t)ch == 0x5c) {
                ch = 28;
            } else if (ch == ']') {
                ch = 29;
            } else if (ch == '^') {
                ch = 30;
            } else if (ch == '_') {
                ch = 31;
            }
            s_ctrl_active = false;
            if (s_btn_ctrl) {
                lv_obj_set_style_bg_color(s_btn_ctrl, THEME_BG_CARD, 0);
            }
            ssh_client_write(&ch, 1);
        } else {
            char alt_seq[2] = {0x1b, ch};
            ssh_client_write(alt_seq, 2);
            s_alt_active = false;
            if (s_btn_alt) {
                lv_obj_set_style_bg_color(s_btn_alt, THEME_BG_CARD, 0);
            }
        }
    } else {
        ssh_client_write(txt, len);
    }
}

static void on_keyboard_value_preprocess(lv_event_t *e)
{
    const char *txt = keyboard_selected_button_text();

    s_kb_value_is_pinyin_comp = false;
    s_kb_value_is_pinyin_backspace = false;

    if (!txt) return;

#if LV_USE_IME_PINYIN != 0
    if (s_pinyin_committed_in_ready) {
        s_pinyin_committed_in_ready = false;
        reposition_elements();
        lv_event_stop_processing(e);
        return;
    }
#endif

    if ((s_ctrl_active || s_alt_active) && txt[0] != '\0' && txt[1] == '\0') {
        ssh_send_terminal_text(txt, 1);
#if LV_USE_IME_PINYIN != 0
        pinyin_ime_reset_composition();
#else
        clear_vta_text();
#endif
        reposition_elements();
        lv_event_stop_processing(e);
        return;
    }

#if LV_USE_IME_PINYIN != 0
    if (!s_pinyin_enabled) {
        if (strcmp(txt, "abc") == 0 || strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        } else if (strcmp(txt, "ABC") == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
        } else if (strcmp(txt, "1#") == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_SPECIAL);
        } else if (strcmp(txt, "123") == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_NUMBER);
        } else if (send_terminal_key_text(txt, 0)) {
            /* Handled above. */
        } else if (strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
            if (s_keyboard) {
                lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
                s_kb_visible = false;
            }
        } else {
            ssh_send_terminal_text(txt, strlen(txt));
        }
        clear_vta_text();
        reposition_elements();
        lv_event_stop_processing(e);
        return;
    }

    if (keyboard_text_is_mode_key(txt)) {
        if (strcmp(txt, "abc") == 0 || strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        } else if (strcmp(txt, "ABC") == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
        } else if (strcmp(txt, "1#") == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_SPECIAL);
        } else if (strcmp(txt, "123") == 0) {
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_NUMBER);
        } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
            if (pinyin_ime_has_composition()) {
                (void)pinyin_commit_raw_input();
            } else if (ssh_client_is_connected()) {
                ssh_client_write("\r", 1);
            }
        }
        if (strcmp(txt, LV_SYMBOL_OK) != 0) {
            pinyin_ime_reset_composition();
        }
        reposition_elements();
        lv_event_stop_processing(e);
        return;
    }
#endif

#if LV_USE_IME_PINYIN != 0
    if (pinyin_ime_has_composition()) {
        if (pinyin_handle_page_key_text(txt, 0)) {
            reposition_elements();
            lv_event_stop_processing(e);
            return;
        }
        if (strcmp(txt, " ") == 0 && pinyin_commit_candidate_index(0)) {
            lv_event_stop_processing(e);
            return;
        }
        if (txt[0] >= '0' && txt[0] <= '9' && txt[1] == '\0' &&
            pinyin_select_candidate_by_digit(txt[0])) {
            lv_event_stop_processing(e);
            return;
        }
        if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
            s_kb_value_is_pinyin_backspace = true;
            reposition_elements();
            return;
        }
        if (strcmp(txt, LV_SYMBOL_NEW_LINE) == 0 ||
            strcmp(txt, "Enter") == 0 ||
            strcmp(txt, LV_SYMBOL_OK) == 0) {
            (void)pinyin_commit_raw_input();
            reposition_elements();
            lv_event_stop_processing(e);
            return;
        }
        if (strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
            pinyin_ime_reset_composition();
            reposition_elements();
            lv_event_stop_processing(e);
            return;
        }
    }
#endif

    if (send_terminal_key_text(txt, 0)) {
#if LV_USE_IME_PINYIN != 0
        pinyin_ime_reset_composition();
#else
        clear_vta_text();
#endif
        reposition_elements();
        lv_event_stop_processing(e);
        return;
    }

#if LV_USE_IME_PINYIN != 0
    s_kb_value_is_pinyin_backspace =
        (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) && pinyin_ime_has_composition();
#endif
    s_kb_value_is_pinyin_comp =
        !keyboard_text_is_direct_ssh_key(txt) &&
        keyboard_value_is_pinyin_comp_key(txt);
}

static void on_keyboard_value_postprocess(lv_event_t *e)
{
    (void)e;
#if LV_USE_IME_PINYIN != 0
    if (pinyin_ime_has_composition()) {
        if (s_hw_keyboard_present ||
            s_hw_pinyin_composing ||
            pinyin_phrase_mode_enabled() ||
            s_pinyin_cand_map_kind != PINYIN_CAND_MAP_DEFAULT) {
            pinyin_refresh_hardware_candidates();
        }
    } else {
        s_hw_pinyin_composing = false;
        if (s_pinyin_cand_map_kind != PINYIN_CAND_MAP_DEFAULT) {
            pinyin_restore_default_candidate_map();
        }
    }
#endif
    reposition_elements();
    s_kb_value_is_pinyin_comp = false;
    s_kb_value_is_pinyin_backspace = false;
}

static void on_vta_changed(lv_event_t *e)
{
    (void)e;
    if (s_vta_internal_update || !ssh_client_is_connected() || !s_vta) return;

    const char *txt = lv_textarea_get_text(s_vta);
    size_t len = txt ? strlen(txt) : 0;
    if (len == 0) {
        reposition_elements();
        return;
    }

#if LV_USE_IME_PINYIN != 0
    /*
     * LVGL pinyin IME keeps the temporary pinyin letters in the textarea.
     * Do not send these composing letters to SSH; send only final committed
     * text, e.g. selected Chinese characters or non-pinyin keyboard modes.
     */
    if ((s_kb_value_is_pinyin_comp || pinyin_ime_has_composition() || pinyin_cand_visible()) &&
        is_ascii_letters_only(txt)) {
        reposition_elements();
        return;
    }
#endif

    ssh_send_terminal_text(txt, len);
    clear_vta_text();
    reposition_elements();
}

static void on_disconnect_click(lv_event_t *e)
{
    (void)e;
    ui_terminal_close();
}

static void on_minimize_click(lv_event_t *e)
{
    (void)e;
    terminal_return_to_server_list();
}

/* ─── SSH 连接任务（独立栈，避免 LVGL 任务栈溢出） ─── */
