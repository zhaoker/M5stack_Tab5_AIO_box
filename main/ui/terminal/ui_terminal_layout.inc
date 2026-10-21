static void apply_hotzone_style(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static bool get_active_touch_point(lv_point_t *p)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !p) return false;
    lv_indev_get_point(indev, p);
    return true;
}

static lv_coord_t get_right_hotzone_x(lv_coord_t screen_w)
{
    return screen_w > RIGHT_HOTZONE_W ? (screen_w - RIGHT_HOTZONE_W) : 0;
}

static lv_coord_t get_side_hotzone_h(lv_coord_t screen_h)
{
    return screen_h > STATUS_HOTZONE_H ? (screen_h - STATUS_HOTZONE_H) : 0;
}

static lv_coord_t get_current_side_hotzone_bottom_y(void)
{
    if (s_left_hotzone && lv_obj_get_height(s_left_hotzone) > 0) {
        return lv_obj_get_y(s_left_hotzone) + lv_obj_get_height(s_left_hotzone);
    }
    if (s_right_hotzone && lv_obj_get_height(s_right_hotzone) > 0) {
        return lv_obj_get_y(s_right_hotzone) + lv_obj_get_height(s_right_hotzone);
    }
    return lv_display_get_vertical_resolution(NULL);
}

static bool point_in_left_hotzone(const lv_point_t *p)
{
    lv_coord_t bottom_y = get_current_side_hotzone_bottom_y();
    return p &&
           p->x >= 0 &&
           p->x < LEFT_HOTZONE_W &&
           p->y >= STATUS_HOTZONE_H &&
           p->y < bottom_y;
}

static bool point_in_right_hotzone(const lv_point_t *p)
{
    if (!p) return false;
    lv_coord_t screen_w = lv_display_get_horizontal_resolution(NULL);
    lv_coord_t bottom_y = get_current_side_hotzone_bottom_y();
    lv_coord_t right_x = get_right_hotzone_x(screen_w);
    return p->x >= right_x &&
           p->x < screen_w &&
           p->y >= STATUS_HOTZONE_H &&
           p->y < bottom_y;
}

static void log_hotzone_press(const char *zone, const char *source, const lv_point_t *p)
{
    if (p) {
        ESP_LOGI(TAG, "Hotzone %s press via %s at (%d,%d)", zone, source, (int)p->x, (int)p->y);
    } else {
        ESP_LOGI(TAG, "Hotzone %s press via %s", zone, source);
    }
}

static bool handle_side_hotzone_press(const char *source)
{
    uint32_t now = lv_tick_get();
    if (now - s_last_hotzone_tick < 200) {
        return false;
    }

    lv_point_t p;
    if (!get_active_touch_point(&p)) {
        ESP_LOGW(TAG, "Hotzone %s: no active touch point", source ? source : "unknown");
        return false;
    }

    if (point_in_left_hotzone(&p)) {
        s_last_hotzone_tick = now;
        log_hotzone_press("left", source ? source : "fallback", &p);
        toggle_control_bar_visible();
        return true;
    }

    if (point_in_right_hotzone(&p)) {
        s_last_hotzone_tick = now;
        log_hotzone_press("right", source ? source : "fallback", &p);
        toggle_keyboard_visible();
        return true;
    }

    if (p.x >= 0 && p.x < LEFT_HOTZONE_W) {
        lv_coord_t bottom_y = get_current_side_hotzone_bottom_y();
        ESP_LOGI(TAG, "Hotzone left miss via %s at (%d,%d), expected x=0..%d y=%d..%d",
                 source ? source : "fallback", (int)p.x, (int)p.y,
                 (int)(LEFT_HOTZONE_W - 1), (int)STATUS_HOTZONE_H, (int)(bottom_y - 1));
    } else {
        lv_coord_t screen_w = lv_display_get_horizontal_resolution(NULL);
        lv_coord_t bottom_y = get_current_side_hotzone_bottom_y();
        lv_coord_t right_x = get_right_hotzone_x(screen_w);
        if (p.x >= right_x && p.x < screen_w) {
            ESP_LOGI(TAG, "Hotzone right miss via %s at (%d,%d), expected x=%d..%d y=%d..%d",
                     source ? source : "fallback", (int)p.x, (int)p.y,
                     (int)right_x, (int)(screen_w - 1), (int)STATUS_HOTZONE_H, (int)(bottom_y - 1));
        }
    }

    return false;
}

#if LV_USE_IME_PINYIN != 0
static const char *pinyin_ime_input_text(void)
{
    if (!s_pinyin_ime) return "";
    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    return ime->input_char;
}

static void update_ime_mode_label(void)
{
    if (!s_lbl_ime_mode) return;
    lv_label_set_text(s_lbl_ime_mode, s_pinyin_enabled ? TRS("拼") : "EN");
    if (s_btn_ime_mode) {
        lv_obj_set_style_bg_color(s_btn_ime_mode,
                                  s_pinyin_enabled ? THEME_PRIMARY_ACTIVE : THEME_BG_HOVER,
                                  0);
    }
}

static void update_pinyin_input_bar(void)
{
    if (!s_pinyin_input_bar || !s_pinyin_input_label) return;

    const char *input = pinyin_ime_input_text();
    bool show = s_pinyin_enabled &&
                pinyin_ime_has_composition() &&
                input &&
                input[0] != '\0';

    if (show) {
        lv_label_set_text(s_pinyin_input_label, input);
        lv_obj_remove_flag(s_pinyin_input_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_pinyin_input_label, "");
        lv_obj_add_flag(s_pinyin_input_bar, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

static void reposition_elements(void)
{
    if (!s_screen || !s_viewport) return;

    /* 强制布局更新以获取最新位置/尺寸 */
    lv_obj_update_layout(s_screen);

    lv_coord_t scr_w = lv_obj_get_width(s_screen);
    if (scr_w <= 0) {
        scr_w = lv_display_get_horizontal_resolution(NULL);
    }
    lv_coord_t scr_h = lv_obj_get_height(s_screen);
    if (scr_h <= 0) {
        scr_h = lv_display_get_vertical_resolution(NULL);
    }
    lv_coord_t kb_h = 0;
    lv_coord_t cb_h = 0;
    lv_coord_t cand_h = 0;
    lv_coord_t input_h = 0;
    bool keyboard_visible = s_kb_visible &&
                            s_keyboard &&
                            !lv_obj_has_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    bool control_visible = s_control_bar &&
                           !lv_obj_has_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN);

    if (keyboard_visible) {
        lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_update_layout(s_keyboard);
        kb_h = lv_obj_get_height(s_keyboard);
        if (kb_h <= 0) kb_h = KEYBOARD_FALLBACK_H;
    }

    if (control_visible) {
        cb_h = lv_obj_get_height(s_control_bar);
        if (cb_h <= 0) cb_h = CONTROL_BAR_H;

        if (keyboard_visible) {
            lv_obj_align_to(s_control_bar, s_keyboard, LV_ALIGN_OUT_TOP_MID, 0, 0);
        } else {
            lv_obj_align(s_control_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
        lv_obj_update_layout(s_control_bar);
    }

#if LV_USE_IME_PINYIN != 0
    update_pinyin_input_bar();
    update_ime_mode_label();

    lv_coord_t ime_base_y = scr_h - kb_h - cb_h;
    if (ime_base_y < 0) ime_base_y = 0;

    if (s_pinyin_cand_panel && s_pinyin_enabled) {
        lv_obj_set_size(s_pinyin_cand_panel, scr_w, IME_CAND_PANEL_H);
        if (!lv_obj_has_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN)) {
            cand_h = lv_obj_get_height(s_pinyin_cand_panel);
            if (cand_h <= 0) cand_h = IME_CAND_PANEL_H;
            lv_obj_set_pos(s_pinyin_cand_panel, 0, ime_base_y - cand_h);
            if (s_hw_pinyin_cand_map_active && s_pinyin_ime) {
                pinyin_position_hardware_digit_labels(
                    pinyin_candidate_count_on_page((lv_ime_pinyin_t *)s_pinyin_ime));
            } else {
                pinyin_hide_hardware_digit_labels();
            }
        } else {
            lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
            pinyin_hide_hardware_digit_labels();
        }
    } else if (s_pinyin_cand_panel) {
        lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
        pinyin_hide_hardware_digit_labels();
    }

    if (s_pinyin_input_bar && !lv_obj_has_flag(s_pinyin_input_bar, LV_OBJ_FLAG_HIDDEN)) {
        input_h = lv_obj_get_height(s_pinyin_input_bar);
        if (input_h <= 0) input_h = IME_INPUT_BAR_H;
        lv_obj_set_size(s_pinyin_input_bar, scr_w, IME_INPUT_BAR_H);
        lv_obj_set_pos(s_pinyin_input_bar, 0, ime_base_y - cand_h - input_h);
    }
#endif

    lv_obj_update_layout(s_screen);

    lv_coord_t total_bottom_h = kb_h + cb_h + cand_h + input_h;
    if (total_bottom_h > 0) {
        total_bottom_h += TERMINAL_BOTTOM_GAP;
    }
    int top_h = s_statusbar_visible ? STATUSBAR_H : 0;
    lv_coord_t hotzone_bottom_y = scr_h - total_bottom_h;
    if (hotzone_bottom_y < STATUS_HOTZONE_H) {
        hotzone_bottom_y = STATUS_HOTZONE_H;
    }
    lv_coord_t side_h = hotzone_bottom_y - STATUS_HOTZONE_H;

    if (s_status_hotzone) {
        lv_obj_set_size(s_status_hotzone, scr_w, STATUS_HOTZONE_H);
        lv_obj_set_pos(s_status_hotzone, 0, 0);
    }
    if (s_left_hotzone) {
        lv_obj_set_size(s_left_hotzone, LEFT_HOTZONE_W, side_h);
        lv_obj_set_pos(s_left_hotzone, 0, STATUS_HOTZONE_H);
    }
    if (s_right_hotzone) {
        lv_coord_t right_x = get_right_hotzone_x(scr_w);
        lv_obj_set_size(s_right_hotzone, RIGHT_HOTZONE_W, side_h);
        lv_obj_set_pos(s_right_hotzone, right_x, STATUS_HOTZONE_H);
    }

    if (total_bottom_h > 0) {
        lv_coord_t usable_bottom_y = scr_h - total_bottom_h - TERMINAL_CURSOR_SAFE_OFFSET;
        if (usable_bottom_y > scr_h) {
            usable_bottom_y = scr_h;
        }
        if (usable_bottom_y < top_h + TERM_CELL_H) {
            usable_bottom_y = top_h + TERM_CELL_H;
        }
        int new_y = (int)usable_bottom_y - (s_term.cursor_row + 1) * TERM_CELL_H;
        if (new_y > top_h) new_y = top_h;
        lv_obj_set_pos(s_viewport, 0, new_y);
        lv_obj_set_height(s_viewport, usable_bottom_y - new_y);
    } else {
        lv_obj_set_pos(s_viewport, 0, top_h);
        lv_obj_set_height(s_viewport, scr_h - top_h);
    }
    lv_obj_set_width(s_viewport, LV_PCT(100));

    if (s_statusbar) {
        if (s_statusbar_visible) {
            lv_obj_remove_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_statusbar, 0, 0);
            lv_obj_move_foreground(s_statusbar);
        } else {
            lv_obj_add_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ui_audio_background_set_terminal_status_visible(s_statusbar_visible);
    if (s_status_hotzone) {
        if (s_statusbar_visible && s_statusbar) {
            lv_obj_move_foreground(s_statusbar);
        } else {
            lv_obj_move_foreground(s_status_hotzone);
        }
    }
    if (s_left_hotzone) {
        lv_obj_move_foreground(s_left_hotzone);
    }
    if (s_right_hotzone) {
        lv_obj_move_foreground(s_right_hotzone);
    }
    if (s_control_bar && !lv_obj_has_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(s_control_bar);
    }
    if (s_keyboard && !lv_obj_has_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(s_keyboard);
    }
#if LV_USE_IME_PINYIN != 0
    if (s_pinyin_cand_panel && !lv_obj_has_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(s_pinyin_cand_panel);
        if (s_hw_pinyin_cand_map_active && s_pinyin_ime) {
            pinyin_position_hardware_digit_labels(
                pinyin_candidate_count_on_page((lv_ime_pinyin_t *)s_pinyin_ime));
        }
    }
    if (s_pinyin_input_bar && !lv_obj_has_flag(s_pinyin_input_bar, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(s_pinyin_input_bar);
    }
#endif
    if (s_ime_toast) {
        position_ime_mode_toast();
        lv_obj_move_foreground(s_ime_toast);
    }
}

static void on_control_key_click(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) return;

#if LV_USE_IME_PINYIN != 0
    if (strcmp(key, "IME") == 0) {
        toggle_ime_mode();
        return;
    }
    if (pinyin_handle_page_key_text(key, 0)) {
        return;
    }
#endif

    if (!ssh_client_is_connected()) return;

    if (send_terminal_key_text(key, 0)) {
        return;
    } else if (strcmp(key, "CTRL") == 0) {
        s_ctrl_active = !s_ctrl_active;
        if (s_btn_ctrl) {
            if (s_ctrl_active) {
                lv_obj_set_style_bg_color(s_btn_ctrl, THEME_PRIMARY_ACTIVE, 0);
            } else {
                lv_obj_set_style_bg_color(s_btn_ctrl, THEME_BG_CARD, 0);
            }
        }
    } else if (strcmp(key, "ALT") == 0) {
        s_alt_active = !s_alt_active;
        if (s_btn_alt) {
            if (s_alt_active) {
                lv_obj_set_style_bg_color(s_btn_alt, THEME_PRIMARY_ACTIVE, 0);
            } else {
                lv_obj_set_style_bg_color(s_btn_alt, THEME_BG_CARD, 0);
            }
        }
    }
}

static void create_control_bar(lv_obj_t *parent)
{
    if (s_control_bar) return;

    s_control_bar = lv_obj_create(parent);
    lv_obj_set_size(s_control_bar, LV_PCT(100), CONTROL_BAR_H);
    lv_obj_set_style_bg_color(s_control_bar, THEME_BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_control_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_control_bar, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(s_control_bar, 1, 0);
    lv_obj_set_style_radius(s_control_bar, 0, 0);
    lv_obj_set_style_pad_all(s_control_bar, 4, 0);
    lv_obj_set_style_pad_column(s_control_bar, 4, 0);
    lv_obj_set_flex_flow(s_control_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_control_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_control_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN);

    static const struct {
        const char *label;
        const char *user_data;
    } keys[] = {
#if LV_USE_IME_PINYIN != 0
        {"EN", "IME"},
#endif
        {"ESC", "ESC"},
        {"TAB", "TAB"},
        {"CTRL", "CTRL"},
        {"ALT", "ALT"},
        {"HOME", "HOME"},
        {"END", "END"},
        {"PgUp", "PGUP"},
        {"PgDn", "PGDN"},
        {LV_SYMBOL_LEFT, "LEFT"},
        {LV_SYMBOL_DOWN, "DOWN"},
        {LV_SYMBOL_UP, "UP"},
        {LV_SYMBOL_RIGHT, "RIGHT"},
        {"Enter", "ENTER"}
    };

    int num_keys = sizeof(keys) / sizeof(keys[0]);
    for (int i = 0; i < num_keys; i++) {
        lv_obj_t *btn = lv_button_create(s_control_bar);
        lv_obj_set_height(btn, CONTROL_BAR_H > 8 ? (CONTROL_BAR_H - 8) : CONTROL_BAR_H);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_bg_color(btn, THEME_BG_CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, on_control_key_click, LV_EVENT_CLICKED, (void *)keys[i].user_data);

        if (strcmp(keys[i].user_data, "CTRL") == 0) {
            s_btn_ctrl = btn;
        } else if (strcmp(keys[i].user_data, "ALT") == 0) {
            s_btn_alt = btn;
#if LV_USE_IME_PINYIN != 0
        } else if (strcmp(keys[i].user_data, "IME") == 0) {
            s_btn_ime_mode = btn;
#endif
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, keys[i].label);
        lv_obj_set_style_text_font(lbl, &APP_FONT, 0);
        lv_obj_set_style_text_color(lbl, THEME_TEXT_PRIMARY, 0);
        lv_obj_center(lbl);
#if LV_USE_IME_PINYIN != 0
        if (strcmp(keys[i].user_data, "IME") == 0) {
            s_lbl_ime_mode = lbl;
            update_ime_mode_label();
        }
#endif
    }
}

/* Cancel/关闭键：隐藏键盘，恢复 canvas 位置 */
static void on_kb_cancel(lv_event_t *e)
{
    if (!s_keyboard) return;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    s_kb_visible = false;
#if LV_USE_IME_PINYIN != 0
    pinyin_ime_reset_composition();
#endif
    reposition_elements();
}

/* 触摸 canvas 显示/隐藏键盘及控制按键行 */
static void on_viewport_scroll(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    s_follow_output = lv_obj_get_scroll_bottom(obj) <= TERM_CELL_H;
}

static void on_terminal_hotzone_pressed(lv_event_t *e)
{
    const char *source = (const char *)lv_event_get_user_data(e);
    handle_side_hotzone_press(source ? source : "pressed");
}

static void hide_statusbar_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_statusbar_timer) {
        lv_timer_del(s_statusbar_timer);
        s_statusbar_timer = NULL;
    }
    s_statusbar_visible = false;
    reposition_elements();
}

static void toggle_statusbar_visible(void)
{
    s_statusbar_visible = !s_statusbar_visible;
    ESP_LOGI(TAG, "Hotzone top: statusbar %s", s_statusbar_visible ? "show" : "hide");
    if (s_statusbar_timer) {
        lv_timer_del(s_statusbar_timer);
        s_statusbar_timer = NULL;
    }
    if (s_statusbar_visible) {
        s_statusbar_timer = lv_timer_create(hide_statusbar_timer_cb, 3500, NULL);
        if (s_statusbar_timer) {
            lv_timer_set_repeat_count(s_statusbar_timer, 1);
        }
    }
    reposition_elements();
}

static void toggle_control_bar_visible(void)
{
    if (!s_control_bar) return;

    if (lv_obj_has_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN);
        s_ctrl_bar_visible = true;
    } else {
        lv_obj_add_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN);
        s_ctrl_bar_visible = false;
    }
    ESP_LOGI(TAG, "Hotzone left: control bar %s", s_ctrl_bar_visible ? "show" : "hide");
    reposition_elements();
}

static void toggle_keyboard_visible(void)
{
    if (!s_keyboard) return;

    if (s_hw_keyboard_present) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        s_kb_visible = false;
        ESP_LOGI(TAG, "Hotzone right: physical keyboard present, soft keyboard suppressed");
        reposition_elements();
        return;
    }

    if (s_kb_visible) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        s_kb_visible = false;
#if LV_USE_IME_PINYIN != 0
        pinyin_ime_reset_composition();
#endif
    } else {
        lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        s_kb_visible = true;
        if (s_control_bar && lv_obj_has_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(s_control_bar, LV_OBJ_FLAG_HIDDEN);
            s_ctrl_bar_visible = true;
        }
        lv_obj_move_foreground(s_keyboard);
    }
    ESP_LOGI(TAG, "Hotzone right: keyboard %s", s_kb_visible ? "show" : "hide");
    reposition_elements();
}

static void on_status_hotzone_click(lv_event_t *e)
{
    lv_point_t p;
    if (!get_active_touch_point(&p) || p.y >= STATUS_HOTZONE_H) {
        return;
    }
    log_hotzone_press("top", "statusbar", &p);
    toggle_statusbar_visible();
}

static void on_left_hotzone_click(lv_event_t *e)
{
    (void)e;
    lv_point_t p;
    if (!get_active_touch_point(&p) || !point_in_left_hotzone(&p)) {
        return;
    }
    log_hotzone_press("left", "control", &p);
    toggle_control_bar_visible();
}

static void on_right_hotzone_click(lv_event_t *e)
{
    (void)e;
    lv_point_t p;
    if (!get_active_touch_point(&p) || !point_in_right_hotzone(&p)) {
        return;
    }
    log_hotzone_press("right", "keyboard", &p);
    toggle_keyboard_visible();
}
