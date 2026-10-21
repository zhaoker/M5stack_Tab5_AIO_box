static void relayout_form_dialog(bool keyboard_visible)
{
    int32_t screen_w;
    int32_t screen_h;
    lv_coord_t dialog_w;
    lv_coord_t keyboard_w;
    lv_coord_t keyboard_h;
    lv_coord_t cand_h;
    lv_coord_t top_margin;
    lv_coord_t bottom_margin;
    lv_coord_t available_h;
    lv_coord_t dialog_inner_h;
    lv_coord_t dialog_body_h;
    lv_coord_t dialog_h;

    if (!s_form.overlay || !s_form.dialog || !s_form.body) {
        return;
    }

    screen_w = lv_display_get_horizontal_resolution(NULL);
    screen_h = lv_display_get_vertical_resolution(NULL);
    dialog_w = screen_w > 820 ? 760 : (screen_w - (MODAL_MARGIN * 2));
    keyboard_w = screen_w;
    keyboard_h = 0;
    cand_h = 0;

    if (keyboard_visible && s_form.keyboard && !lv_obj_has_flag(s_form.keyboard, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_update_layout(s_form.overlay);
        lv_obj_update_layout(s_form.keyboard);
        keyboard_w = lv_obj_get_width(s_form.keyboard);
        keyboard_h = lv_obj_get_height(s_form.keyboard);
        if (keyboard_h < 220) {
            keyboard_h = 260;
        }
#if LV_USE_IME_PINYIN != 0
        if (s_form.pinyin_cand_panel && !lv_obj_has_flag(s_form.pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN)) {
            cand_h = lv_obj_get_height(s_form.pinyin_cand_panel);
            if (cand_h < 48) {
                cand_h = 56;
            }
        }
#endif
    }

    top_margin = MODAL_MARGIN;
    bottom_margin = keyboard_visible ? (keyboard_h + cand_h + MODAL_MARGIN) : MODAL_MARGIN;
    available_h = screen_h - top_margin - bottom_margin;
    if (available_h < 340) {
        available_h = 340;
    }

    lv_obj_set_size(s_form.dialog, dialog_w, available_h);
    dialog_inner_h = available_h
                     - lv_obj_get_style_pad_top(s_form.dialog, 0)
                     - lv_obj_get_style_pad_bottom(s_form.dialog, 0)
                     - (lv_obj_get_style_pad_row(s_form.dialog, 0) * 2);
    dialog_body_h = dialog_inner_h - FORM_HEADER_H - FORM_FOOTER_H;
    if (dialog_body_h < 180) {
        dialog_body_h = 180;
    }
    lv_obj_set_height(s_form.body, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_form.dialog);
    if (lv_obj_get_height(s_form.body) > dialog_body_h) {
        lv_obj_set_height(s_form.body, dialog_body_h);
    }

    lv_obj_set_height(s_form.dialog, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_form.dialog);
    dialog_h = lv_obj_get_height(s_form.dialog);
    if (dialog_h > available_h) {
        lv_obj_set_height(s_form.dialog, available_h);
        lv_obj_set_height(s_form.body, dialog_body_h);
    }

    if (keyboard_visible) {
        lv_obj_align(s_form.dialog, LV_ALIGN_TOP_MID, 0, top_margin);
    } else {
        lv_obj_align(s_form.dialog, LV_ALIGN_CENTER, 0, 0);
    }

#if LV_USE_IME_PINYIN != 0
    if (s_form.pinyin_cand_panel && s_form.keyboard) {
        lv_obj_set_size(s_form.pinyin_cand_panel, keyboard_w, 52);
        lv_obj_align_to(s_form.pinyin_cand_panel, s_form.keyboard, LV_ALIGN_OUT_TOP_MID, 0, 0);
    }
#endif
    ui_hw_pinyin_input_reposition(s_form.hw_pinyin);
}

static void hide_form_keyboard(void)
{
    if (s_form.keyboard) {
        lv_keyboard_set_textarea(s_form.keyboard, NULL);
        lv_obj_add_flag(s_form.keyboard, LV_OBJ_FLAG_HIDDEN);
    }

#if LV_USE_IME_PINYIN != 0
    if (s_form.pinyin_cand_panel) {
        lv_obj_add_flag(s_form.pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ui_pinyin_ime_ext_reset(s_form.pinyin_ext);
#endif
    s_form.keyboard_visible = false;
    relayout_form_dialog(false);
}

static void on_form_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_form.overlay) {
        return;
    }

    if (s_form.keyboard_visible) {
        hide_form_keyboard();
    } else {
        close_server_form();
    }
}

static void on_form_keyboard_ready(lv_event_t *e)
{
    (void)e;
    hide_form_keyboard();
}

static void reset_textarea_view(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }

    lv_textarea_set_cursor_pos(ta, 0);
    lv_obj_scroll_to_x(ta, 0, LV_ANIM_OFF);
}

static void set_textarea_font(lv_obj_t *ta, const lv_font_t *font)
{
    if (!ta || !font) {
        return;
    }

    lv_obj_set_style_text_font(ta, font, 0);
}

static void format_host_port(char *out, size_t out_len, const char *host, uint16_t port)
{
    const char *safe_host = (host && host[0]) ? host : "";
    uint16_t safe_port = port ? port : 22;

    if (strchr(safe_host, ':')) {
        snprintf(out, out_len, "[%s]:%u", safe_host, (unsigned)safe_port);
    } else {
        snprintf(out, out_len, "%s:%u", safe_host, (unsigned)safe_port);
    }
}

static void sync_form_hw_pinyin_target(void)
{
    if (!s_form.hw_pinyin) {
        return;
    }

    if (app_i18n_default_chinese_input() && s_form.active_ta == s_form.name_ta) {
        ui_hw_pinyin_input_set_target(s_form.hw_pinyin, s_form.name_ta, s_form.name_ta);
        ui_hw_pinyin_input_reposition(s_form.hw_pinyin);
    } else {
        ui_hw_pinyin_input_set_target(s_form.hw_pinyin, NULL, NULL);
    }
}

static void on_form_textarea_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);

    s_form.active_ta = ta;
    sync_form_hw_pinyin_target();

    if (s_form.hw_keyboard_present) {
        hide_form_keyboard();
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
        return;
    }

    if (!s_form.keyboard) {
        return;
    }

    lv_keyboard_set_textarea(s_form.keyboard, ta);
    if (ta == s_form.port_ta) {
        lv_keyboard_set_mode(s_form.keyboard, LV_KEYBOARD_MODE_NUMBER);
#if LV_USE_IME_PINYIN != 0
        if (s_form.pinyin_cand_panel) {
            lv_obj_add_flag(s_form.pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    } else {
        lv_keyboard_set_mode(s_form.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
#if LV_USE_IME_PINYIN != 0
        if (app_i18n_default_chinese_input() && s_form.pinyin_cand_panel) {
            lv_obj_align_to(s_form.pinyin_cand_panel, s_form.keyboard, LV_ALIGN_OUT_TOP_MID, 0, 0);
        } else if (s_form.pinyin_cand_panel) {
            lv_obj_add_flag(s_form.pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    }

    lv_obj_remove_flag(s_form.keyboard, LV_OBJ_FLAG_HIDDEN);
    s_form.keyboard_visible = true;
    relayout_form_dialog(true);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static lv_obj_t *next_form_textarea(lv_obj_t *ta)
{
    if (ta == s_form.name_ta) return s_form.host_ta;
    if (ta == s_form.host_ta) return s_form.port_ta;
    if (ta == s_form.port_ta) return s_form.user_ta;
    if (ta == s_form.user_ta) return s_form.pass_ta;
    if (ta == s_form.pass_ta) return s_form.cmd_ta;
    return s_form.name_ta ? s_form.name_ta : ta;
}

static lv_obj_t *prev_form_textarea(lv_obj_t *ta)
{
    if (ta == s_form.cmd_ta) return s_form.pass_ta;
    if (ta == s_form.pass_ta) return s_form.user_ta;
    if (ta == s_form.user_ta) return s_form.port_ta;
    if (ta == s_form.port_ta) return s_form.host_ta;
    if (ta == s_form.host_ta) return s_form.name_ta;
    return s_form.cmd_ta ? s_form.cmd_ta : ta;
}

static void hw_keyboard_clear_form_textarea_focus(void)
{
    ui_hw_pinyin_input_reset(s_form.hw_pinyin);
    if (s_form.active_ta) {
        lv_obj_remove_state(s_form.active_ta, LV_STATE_FOCUSED);
    }
    s_form.active_ta = NULL;
    sync_form_hw_pinyin_target();
}

static void hw_keyboard_focus_textarea(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }
    if (s_form.active_ta && s_form.active_ta != ta) {
        lv_obj_remove_state(s_form.active_ta, LV_STATE_FOCUSED);
    }
    s_form.active_ta = ta;
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    sync_form_hw_pinyin_target();
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static bool hw_keyboard_handle_form_key(ui_hw_textarea_key_t key)
{
    if (!s_form.active_ta) {
        return false;
    }

    switch (key) {
    case UI_HW_TEXTAREA_KEY_ENTER:
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        hw_keyboard_clear_form_textarea_focus();
        return true;
    case UI_HW_TEXTAREA_KEY_TAB:
    case UI_HW_TEXTAREA_KEY_DOWN:
        hw_keyboard_focus_textarea(next_form_textarea(s_form.active_ta));
        return true;
    case UI_HW_TEXTAREA_KEY_UP:
        hw_keyboard_focus_textarea(prev_form_textarea(s_form.active_ta));
        return true;
    case UI_HW_TEXTAREA_KEY_SPACE:
        if (s_form.active_ta == s_form.port_ta) {
            return true;
        }
        return ui_hw_textarea_apply_edit_key(s_form.active_ta, key);
    default:
        return ui_hw_textarea_apply_edit_key(s_form.active_ta, key);
    }
}

static void hw_keyboard_insert_form_text(const char *txt, size_t len, uint8_t modifier)
{
    if (!s_form.active_ta || !txt || len == 0) {
        return;
    }

    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;
    if (app_i18n_default_chinese_input() && s_form.active_ta == s_form.name_ta &&
        ui_hw_pinyin_input_handle_event(s_form.hw_pinyin, txt, len, modifier)) {
        return;
    }

    if (ui_hw_textarea_parse_key(txt, len, &key) && hw_keyboard_handle_form_key(key)) {
        return;
    }

    if (ui_hw_textarea_modifier_blocks_text(modifier)) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = txt[i];
        if (ch == '\r' || ch == '\n') {
            hw_keyboard_clear_form_textarea_focus();
        } else if (ch == '\t') {
            hw_keyboard_focus_textarea(next_form_textarea(s_form.active_ta));
        } else if (ch == '\b' || (uint8_t)ch == 0x7f) {
            lv_textarea_delete_char(s_form.active_ta);
        } else if ((uint8_t)ch >= 0x20) {
            if (s_form.active_ta == s_form.port_ta && (ch < '0' || ch > '9')) {
                continue;
            }
            lv_textarea_add_char(s_form.active_ta, (uint32_t)(uint8_t)ch);
        }
    }
}

static void hw_keyboard_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    static uint32_t s_last_probe = 0;

    if (!s_form.hw_keyboard_present) {
        uint32_t now = lv_tick_get();
        if (s_last_probe == 0 || now - s_last_probe >= 1000) {
            s_last_probe = now;
            if (tab5_keyboard_init() == ESP_OK) {
                s_form.hw_keyboard_present = true;
                hide_form_keyboard();
                ESP_LOGI(TAG, "Physical keyboard detected; server form soft keyboard disabled");
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
            s_form.hw_keyboard_present = false;
            tab5_keyboard_deinit();
            break;
        }
        hw_keyboard_insert_form_text(evt.chars, evt.length, evt.modifier);
    }
}

static void close_icon_picker(void)
{
    if (s_icon_picker_overlay) {
        lv_obj_del(s_icon_picker_overlay);
    }
    s_icon_picker_overlay = NULL;
    s_icon_picker_panel = NULL;
    s_icon_picker_preview = NULL;
    s_icon_picker_button = NULL;
}

static void on_icon_picker_overlay_click(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_icon_picker_overlay) {
        close_icon_picker();
    }
}

static void on_icon_pick(lv_event_t *e)
{
    uint8_t icon_id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    icon_picker_apply_selection(icon_id);
    close_icon_picker();
}

static void on_icon_picker_close(lv_event_t *e)
{
    (void)e;
    close_icon_picker();
}

static void show_icon_picker(lv_obj_t *button)
{
    lv_obj_t *grid;
    uint8_t i;

    if (!button) {
        return;
    }

    close_icon_picker();
    s_icon_picker_button = button;
    s_icon_picker_overlay = create_modal_overlay(on_icon_picker_overlay_click);
    lv_obj_set_style_bg_opa(s_icon_picker_overlay, 160, 0);

    s_icon_picker_panel = create_modal_panel(s_icon_picker_overlay, 700, 600);
    lv_obj_set_flex_flow(s_icon_picker_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_icon_picker_panel, 14, 0);
    lv_obj_set_style_pad_row(s_icon_picker_panel, 10, 0);

    lv_obj_t *title = lv_label_create(s_icon_picker_panel);
    lv_label_set_text(title, TRS("选择服务器图标"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    grid = lv_obj_create(s_icon_picker_panel);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);

    for (i = 0; i < SERVER_ICON_COUNT; ++i) {
        lv_obj_t *btn = lv_button_create(grid);
        lv_obj_t *label = lv_label_create(btn);

        lv_obj_set_size(btn, 64, 64);
        lv_obj_add_style(btn, &s_style_icon_picker_btn, 0);
        lv_obj_add_style(btn, &s_style_icon_picker_btn_pressed, LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, on_icon_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_set_style_bg_color(btn, get_server_icon_color(i), 0);
        lv_obj_set_style_border_color(btn, get_server_icon_color(i), 0);

        lv_label_set_text(label, s_server_icons[i].glyph);
        lv_obj_set_style_text_font(label, &APP_FONT, 0);
        lv_obj_set_style_text_color(label, THEME_TEXT_PRIMARY, 0);
        lv_obj_center(label);
    }

    lv_obj_t *footer = lv_obj_create(s_icon_picker_panel);
    lv_obj_set_size(footer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_style_pad_top(footer, 8, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *close_btn = create_button(footer, "关闭", &s_style_secondary_btn, &s_style_secondary_btn_pressed);
    lv_obj_set_size(close_btn, 160, FORM_ACTION_BTN_H);
    lv_obj_center(close_btn);
    lv_obj_add_event_cb(close_btn, on_icon_picker_close, LV_EVENT_CLICKED, NULL);

    lv_obj_center(s_icon_picker_panel);
    lv_obj_move_foreground(s_icon_picker_panel);
}

static lv_obj_t *create_form_field(lv_obj_t *parent, const char *label_txt,
                                   const char *placeholder, bool is_password,
                                   bool allow_ipv6)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_t *label;
    lv_obj_t *ta;

    lv_obj_set_width(wrap, LV_PCT(100));
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_row(wrap, FORM_FIELD_GAP, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(wrap);
    lv_label_set_text(label, TRS(label_txt));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_SECONDARY, 0);

    ta = lv_textarea_create(wrap);
    lv_obj_set_size(ta, LV_PCT(100), FORM_FIELD_H);
    lv_obj_add_style(ta, &s_style_textarea, 0);
    lv_obj_add_style(ta, &s_style_textarea_focused, LV_STATE_FOCUSED);
    lv_obj_set_style_text_font(ta, &APP_FONT, 0);
    lv_obj_set_style_text_color(ta, THEME_INPUT_TEXT, 0);
    lv_obj_set_style_text_color(ta, THEME_INPUT_PLACEHOLDER, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(ta, &APP_FONT, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, THEME_PRIMARY_GLOW, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_text_color(ta, THEME_BG_PRIMARY, LV_PART_CURSOR);
    lv_obj_set_style_text_font(ta, &APP_FONT, LV_PART_CURSOR);
    lv_obj_set_style_width(ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_pad_left(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_pad_right(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_outline_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(ta, 400, LV_PART_CURSOR);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, is_password);
    lv_textarea_set_placeholder_text(ta, TRS(placeholder));
    if (allow_ipv6) {
        lv_textarea_set_accepted_chars(ta, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:[]%");
    }
    lv_obj_add_event_cb(ta, on_form_textarea_focus, LV_EVENT_FOCUSED, NULL);

    return ta;
}

static lv_obj_t *create_icon_preview(lv_obj_t *parent, lv_obj_t **out_label)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(box, 74, 74);
    apply_surface_style(box, THEME_BG_HOVER, THEME_BORDER_SECONDARY, 16);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(box);
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_PRIMARY, 0);
    lv_obj_center(label);

    if (out_label) {
        *out_label = label;
    }
    return box;
}

static void on_icon_preview_click(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (btn) {
        show_icon_picker(btn);
    }
}

static void on_form_cancel(lv_event_t *e)
{
    (void)e;
    close_server_form();
}

static void on_form_save(lv_event_t *e)
{
    server_entry_t entry;
    esp_err_t err;

    (void)e;

    memset(&entry, 0, sizeof(entry));
    strlcpy(entry.name, lv_textarea_get_text(s_form.name_ta), sizeof(entry.name));
    strlcpy(entry.host, lv_textarea_get_text(s_form.host_ta), sizeof(entry.host));
    strlcpy(entry.username, lv_textarea_get_text(s_form.user_ta), sizeof(entry.username));
    strlcpy(entry.password, lv_textarea_get_text(s_form.pass_ta), sizeof(entry.password));
    strlcpy(entry.auto_cmd, lv_textarea_get_text(s_form.cmd_ta), sizeof(entry.auto_cmd));
    entry.port = (uint16_t)atoi(lv_textarea_get_text(s_form.port_ta));
    entry.icon_id = s_selected_icon_id;
    if (entry.port == 0) {
        entry.port = 22;
    }

    if (!entry.host[0] || !entry.username[0]) {
        show_info_dialog("信息不完整", "请至少填写主机/IP 和用户名。");
        return;
    }

    if (s_form.edit_index < 0) {
        err = server_config_append(&entry);
    } else {
        err = server_config_save(s_form.edit_index, &entry);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save server failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_NO_MEM) {
            show_info_dialog("服务器已满", "当前最多支持 10 台服务器，请先删除一台后再添加。");
        } else {
            show_info_dialog("保存失败", "写入服务器配置失败，请稍后再试。");
        }
        return;
    }

    close_server_form();
    refresh_list();
}

static void show_server_form(int edit_index)
{
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *row;
    lv_obj_t *field_name;
    lv_obj_t *field_icon;
    lv_obj_t *field_host;
    lv_obj_t *field_port;
    lv_obj_t *field_user;
    lv_obj_t *field_pass;
    lv_obj_t *field_cmd;
    lv_obj_t *footer;
    lv_obj_t *btn_cancel;
    lv_obj_t *btn_save;

    close_transient_overlays();
    close_server_form();

    s_form.overlay = create_modal_overlay(on_form_overlay_click);
    s_form.edit_index = edit_index;
    s_selected_icon_id = 0;
    lv_obj_set_style_bg_opa(s_form.overlay, 150, 0);

    s_form.dialog = lv_obj_create(s_form.overlay);
    lv_obj_add_style(s_form.dialog, &s_style_modal_panel, 0);
    lv_obj_set_style_pad_all(s_form.dialog, FORM_DIALOG_PAD, 0);
    lv_obj_set_style_pad_row(s_form.dialog, FORM_DIALOG_GAP, 0);
    lv_obj_set_flex_flow(s_form.dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_form.dialog, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(s_form.dialog);
    lv_obj_set_size(header, LV_PCT(100), FORM_HEADER_H);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_row(header, FORM_FIELD_GAP, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(header);
    lv_label_set_text(title, edit_index < 0 ? TRS("添加服务器") : TRS("编辑服务器"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);

    s_form.body = lv_obj_create(s_form.dialog);
    lv_obj_set_width(s_form.body, LV_PCT(100));
    lv_obj_set_style_bg_opa(s_form.body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_form.body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(s_form.body, 0, 0);
    lv_obj_set_style_pad_right(s_form.body, 0, 0);
    lv_obj_set_style_pad_top(s_form.body, 0, 0);
    lv_obj_set_style_pad_bottom(s_form.body, FORM_FIELD_GAP, 0);
    lv_obj_set_style_pad_row(s_form.body, FORM_ROW_GAP, 0);
    lv_obj_set_scroll_dir(s_form.body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_form.body, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_flex_flow(s_form.body, LV_FLEX_FLOW_COLUMN);

    row = create_form_row(s_form.body);

    field_icon = lv_obj_create(row);
    lv_obj_set_size(field_icon, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_icon, 0, 0);
    lv_obj_set_flex_flow(field_icon, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_icon, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(field_icon);
    lv_label_set_text(title, TRS("图标"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_SECONDARY, 0);

    s_icon_picker_preview = create_icon_preview(field_icon, NULL);
    lv_obj_add_flag(s_icon_picker_preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_picker_preview, on_icon_preview_click, LV_EVENT_CLICKED, NULL);
    icon_picker_apply_selection(s_selected_icon_id);

    field_name = lv_obj_create(row);
    lv_obj_set_flex_grow(field_name, 3);
    lv_obj_set_height(field_name, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_name, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_name, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_name, 0, 0);
    lv_obj_set_flex_flow(field_name, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_name, LV_OBJ_FLAG_SCROLLABLE);
    s_form.name_ta = create_form_field(field_name, "名称", "例如：腾讯云", false, false);

    field_host = lv_obj_create(row);
    lv_obj_set_flex_grow(field_host, 4);
    lv_obj_set_height(field_host, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_host, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_host, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_host, 0, 0);
    lv_obj_set_flex_flow(field_host, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_host, LV_OBJ_FLAG_SCROLLABLE);
    s_form.host_ta = create_form_field(field_host, "主机 / IP", "支持域名或IP地址", false, true);

    field_port = lv_obj_create(row);
    lv_obj_set_size(field_port, 126, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_port, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_port, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_port, 0, 0);
    lv_obj_set_flex_flow(field_port, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_port, LV_OBJ_FLAG_SCROLLABLE);
    s_form.port_ta = create_form_field(field_port, "端口", "22", false, false);

    row = create_form_row(s_form.body);

    field_user = lv_obj_create(row);
    lv_obj_set_flex_grow(field_user, 1);
    lv_obj_set_height(field_user, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_user, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_user, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_user, 0, 0);
    lv_obj_set_flex_flow(field_user, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_user, LV_OBJ_FLAG_SCROLLABLE);
    s_form.user_ta = create_form_field(field_user, "用户名", "root", false, false);

    field_pass = lv_obj_create(row);
    lv_obj_set_flex_grow(field_pass, 1);
    lv_obj_set_height(field_pass, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_pass, 0, 0);
    lv_obj_set_flex_flow(field_pass, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_pass, LV_OBJ_FLAG_SCROLLABLE);
    s_form.pass_ta = create_form_field(field_pass, "密码", "留空则按空密码连接", true, false);

    row = create_form_row(s_form.body);

    field_cmd = lv_obj_create(row);
    lv_obj_set_width(field_cmd, LV_PCT(100));
    lv_obj_set_height(field_cmd, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field_cmd, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(field_cmd, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(field_cmd, 0, 0);
    lv_obj_set_flex_flow(field_cmd, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(field_cmd, LV_OBJ_FLAG_SCROLLABLE);
    s_form.cmd_ta = create_form_field(field_cmd, "登录后自动运行命令", "例如：btop", false, false);
    lv_textarea_set_accepted_chars(s_form.port_ta, "0123456789");
    set_textarea_font(s_form.name_ta, &APP_FONT);
    set_textarea_font(s_form.host_ta, &APP_FONT);
    set_textarea_font(s_form.port_ta, &APP_FONT);
    set_textarea_font(s_form.user_ta, &APP_FONT);
    set_textarea_font(s_form.pass_ta, &APP_FONT);
    set_textarea_font(s_form.cmd_ta, &APP_FONT);

    footer = lv_obj_create(s_form.dialog);
    lv_obj_set_size(footer, LV_PCT(100), FORM_FOOTER_H);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_style_pad_column(footer, FORM_ROW_GAP, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    btn_cancel = create_button(footer, "取消", &s_style_secondary_btn, &s_style_secondary_btn_pressed);
    lv_obj_set_size(btn_cancel, 150, FORM_ACTION_BTN_H);
    lv_obj_add_event_cb(btn_cancel, on_form_cancel, LV_EVENT_CLICKED, NULL);

    btn_save = create_button(footer, "保存", &s_style_primary_btn, &s_style_primary_btn_pressed);
    lv_obj_set_size(btn_save, 150, FORM_ACTION_BTN_H);
    lv_obj_add_event_cb(btn_save, on_form_save, LV_EVENT_CLICKED, NULL);

    s_form.keyboard = lv_keyboard_create(s_form.overlay);
    lv_obj_align(s_form.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_form.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_popovers(s_form.keyboard, true);
    lv_obj_set_style_bg_color(s_form.keyboard, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_form.keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_form.keyboard, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(s_form.keyboard, 1, 0);
    lv_obj_set_style_bg_color(s_form.keyboard, THEME_BG_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_form.keyboard, THEME_BG_HOVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_form.keyboard, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_form.keyboard, APP_KEYBOARD_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_form.keyboard, APP_KEYBOARD_FONT, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_form.keyboard, on_form_keyboard_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_form.keyboard, on_form_keyboard_ready, LV_EVENT_CANCEL, NULL);

    ui_hw_pinyin_input_config_t hw_pinyin_cfg = {
        .parent = s_form.overlay,
        .target_ta = s_form.name_ta,
        .anchor = s_form.name_ta,
        .width = 0,
        .height = 52,
    };
    s_form.hw_pinyin = ui_hw_pinyin_input_create(&hw_pinyin_cfg);
    sync_form_hw_pinyin_target();

    s_form.hw_keyboard_present = (tab5_keyboard_init() == ESP_OK);
    if (s_form.hw_keyboard_present) {
        lv_obj_add_flag(s_form.keyboard, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Physical keyboard detected; server form soft keyboard disabled");
    }
    s_form.hw_keyboard_timer = lv_timer_create(hw_keyboard_timer_cb, HW_KEYBOARD_POLL_MS, NULL);

#if LV_USE_IME_PINYIN != 0
    if (app_i18n_default_chinese_input()) {
        s_form.pinyin_ime = lv_ime_pinyin_create(s_form.overlay);
        lv_obj_set_style_text_font(s_form.pinyin_ime, APP_KEYBOARD_FONT, 0);
        lv_ime_pinyin_set_keyboard(s_form.pinyin_ime, s_form.keyboard);
        lv_ime_pinyin_set_dict(s_form.pinyin_ime, g_app_pinyin_dict);
        lv_ime_pinyin_set_mode(s_form.pinyin_ime, LV_IME_PINYIN_MODE_K26);

        s_form.pinyin_cand_panel = lv_ime_pinyin_get_cand_panel(s_form.pinyin_ime);
        lv_obj_set_style_bg_color(s_form.pinyin_cand_panel, THEME_BG_PANEL, 0);
        lv_obj_set_style_bg_opa(s_form.pinyin_cand_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_form.pinyin_cand_panel, 1, 0);
        lv_obj_set_style_border_color(s_form.pinyin_cand_panel, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_radius(s_form.pinyin_cand_panel, 0, 0);
        lv_obj_set_style_pad_all(s_form.pinyin_cand_panel, 2, 0);
        lv_obj_set_style_pad_gap(s_form.pinyin_cand_panel, 0, 0);
        lv_obj_set_style_text_font(s_form.pinyin_cand_panel, &APP_IME_CAND_FONT, 0);
        lv_obj_set_style_text_font(s_form.pinyin_cand_panel, &APP_IME_CAND_FONT, LV_PART_ITEMS);
        lv_obj_set_style_text_color(s_form.pinyin_cand_panel, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(s_form.pinyin_cand_panel, THEME_BG_CARD, LV_PART_ITEMS);
        lv_obj_set_style_radius(s_form.pinyin_cand_panel, 0, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(s_form.pinyin_cand_panel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(s_form.pinyin_cand_panel, THEME_BG_HOVER, LV_PART_ITEMS | LV_STATE_PRESSED);
        s_form.pinyin_ext = ui_pinyin_ime_ext_create(s_form.pinyin_ime, s_form.keyboard, s_form.pinyin_cand_panel);
    }
#endif

    if (edit_index >= 0) {
        server_entry_t entry = {0};

        esp_err_t load_err = server_config_load(edit_index, &entry);
        if (load_err != ESP_OK && server_config_load_public(edit_index, &entry) == ESP_OK) {
            entry.password[0] = '\0';
        }
        if (entry.host[0] || entry.username[0] || entry.name[0]) {
            char port_str[8];
            snprintf(port_str, sizeof(port_str), "%u", entry.port ? entry.port : 22);
            lv_textarea_set_text(s_form.name_ta, entry.name);
            lv_textarea_set_text(s_form.host_ta, entry.host);
            lv_textarea_set_text(s_form.port_ta, port_str);
            lv_textarea_set_text(s_form.user_ta, entry.username);
            lv_textarea_set_text(s_form.pass_ta, entry.password);
            lv_textarea_set_text(s_form.cmd_ta, entry.auto_cmd);
            icon_picker_apply_selection(entry.icon_id);
            reset_textarea_view(s_form.name_ta);
            reset_textarea_view(s_form.host_ta);
            reset_textarea_view(s_form.port_ta);
            reset_textarea_view(s_form.user_ta);
            reset_textarea_view(s_form.pass_ta);
            reset_textarea_view(s_form.cmd_ta);
        }
    }

    relayout_form_dialog(false);
}
