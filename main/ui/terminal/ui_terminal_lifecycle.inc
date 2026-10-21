static const char *terminal_session_name(const terminal_session_t *session)
{
    if (!session) {
        return "";
    }
    if (session->server.name[0]) {
        return session->server.name;
    }
    return session->server.host;
}

static bool terminal_session_is_listed(const terminal_session_t *session)
{
    return session && !session->destroy_requested;
}

static void terminal_session_free_final(terminal_session_t *session)
{
    if (!session) {
        return;
    }

    if (session == s_active_session) {
        terminal_destroy_view();
        s_active_session = NULL;
    }
    if (session->ssh) {
        ssh_client_destroy(session->ssh);
        session->ssh = NULL;
    }
    ansi_parser_deinit(&session->term);
    if (session->term_mutex) {
        vSemaphoreDelete(session->term_mutex);
        session->term_mutex = NULL;
    }
    free(session);
}

static bool terminal_session_retain(terminal_session_t *session)
{
    bool retained = false;

    if (!session) {
        return false;
    }

    portENTER_CRITICAL(&s_session_ref_mux);
    if (session->ref_count > 0) {
        session->ref_count++;
        retained = true;
    }
    portEXIT_CRITICAL(&s_session_ref_mux);
    return retained;
}

static void terminal_session_release(terminal_session_t *session)
{
    bool should_free = false;

    if (!session) {
        return;
    }

    portENTER_CRITICAL(&s_session_ref_mux);
    if (session->ref_count > 0) {
        session->ref_count--;
        should_free = (session->ref_count == 0);
    }
    portEXIT_CRITICAL(&s_session_ref_mux);

    if (should_free) {
        terminal_session_free_final(session);
    }
}

static void terminal_session_set_ssh_runtime_ref(terminal_session_t *session, bool active)
{
    bool retained = false;

    if (!session) {
        return;
    }

    portENTER_CRITICAL(&s_session_ref_mux);
    if (active) {
        if (session->ssh_runtime_ref) {
            retained = true;
        } else if (session->ref_count > 0) {
            session->ref_count++;
            session->ssh_runtime_ref = true;
            retained = true;
        }
    } else if (session->ssh_runtime_ref) {
        session->ssh_runtime_ref = false;
    }
    portEXIT_CRITICAL(&s_session_ref_mux);

    if (active && !retained) {
        ESP_LOGW(TAG, "Failed to retain SSH runtime session");
    }
}

static bool terminal_session_consume_ssh_runtime_ref(terminal_session_t *session)
{
    bool consumed = false;
    bool should_free = false;

    if (!session) {
        return false;
    }

    portENTER_CRITICAL(&s_session_ref_mux);
    if (session->ssh_runtime_ref && session->ref_count > 0) {
        session->ssh_runtime_ref = false;
        session->ref_count--;
        consumed = true;
        should_free = (session->ref_count == 0);
    }
    portEXIT_CRITICAL(&s_session_ref_mux);

    if (should_free) {
        terminal_session_free_final(session);
    }
    return consumed;
}

static bool terminal_active_ssh_is_connected(void)
{
    return s_active_session &&
           ssh_client_is_connected_handle(s_active_session->ssh);
}

static int terminal_active_ssh_write(const char *data, size_t len)
{
    if (!s_active_session) {
        return -1;
    }
    return ssh_client_write_handle(s_active_session->ssh, data, len);
}

static terminal_session_t *terminal_session_find(uint32_t id)
{
    for (terminal_session_t *it = s_sessions; it; it = it->next) {
        if (it->id == id && terminal_session_is_listed(it)) {
            return it;
        }
    }
    return NULL;
}

static terminal_session_t *terminal_session_create(const server_entry_t *server)
{
    terminal_session_t *session;

    if (!server) {
        return NULL;
    }

    session = calloc(1, sizeof(*session));
    if (!session) {
        return NULL;
    }

    session->id = s_next_session_id++;
    session->ref_count = 1;
    memcpy(&session->server, server, sizeof(session->server));
    session->term_mutex = xSemaphoreCreateMutex();
    session->ssh = ssh_client_create();
    if (!session->term_mutex || !session->ssh) {
        if (session->term_mutex) {
            vSemaphoreDelete(session->term_mutex);
        }
        if (session->ssh) {
            ssh_client_destroy(session->ssh);
        }
        free(session);
        return NULL;
    }

    ansi_parser_init(&session->term);
    session->next = s_sessions;
    s_sessions = session;
    return session;
}

static void terminal_session_unlink(terminal_session_t *session)
{
    terminal_session_t **link = &s_sessions;

    while (*link) {
        if (*link == session) {
            *link = session->next;
            session->next = NULL;
            return;
        }
        link = &(*link)->next;
    }
}

static void terminal_destroy_view(void)
{
    if (s_render_timer) {
        lv_timer_del(s_render_timer);
        s_render_timer = NULL;
    }
    if (s_statusbar_timer) {
        lv_timer_del(s_statusbar_timer);
        s_statusbar_timer = NULL;
    }
    if (s_hw_keyboard_timer) {
        lv_timer_del(s_hw_keyboard_timer);
        s_hw_keyboard_timer = NULL;
    }
    if (s_ime_toast_timer) {
        lv_timer_del(s_ime_toast_timer);
        s_ime_toast_timer = NULL;
    }
    tab5_keyboard_deinit();

    lv_obj_t *screen_to_del = s_screen;
    s_screen = NULL;
    s_viewport = NULL;
    s_canvas = NULL;
    s_statusbar = NULL;
    s_status_hotzone = NULL;
    s_left_hotzone = NULL;
    s_right_hotzone = NULL;
    s_lbl_status = NULL;
    s_keyboard = NULL;
    s_ime_toast = NULL;
#if LV_USE_IME_PINYIN != 0
    s_pinyin_ime = NULL;
    s_pinyin_cand_panel = NULL;
    s_pinyin_input_bar = NULL;
    s_pinyin_input_label = NULL;
    memset(s_hw_pinyin_digit_labels, 0, sizeof(s_hw_pinyin_digit_labels));
    s_pinyin_default_cand_map = NULL;
#endif
    s_control_bar = NULL;
    s_btn_ctrl = NULL;
    s_btn_alt = NULL;
    s_btn_ime_mode = NULL;
    s_lbl_ime_mode = NULL;
    s_vta = NULL;
    s_kb_visible = false;
    s_follow_output = true;
    s_statusbar_visible = false;
    ui_audio_background_set_terminal_status_visible(true);
    if (s_msgbox) {
        lv_obj_del(s_msgbox);
        s_msgbox = NULL;
    }
    s_ctrl_bar_visible = false;
    s_ctrl_active = false;
    s_alt_active = false;
    s_kb_value_is_pinyin_comp = false;
    s_kb_value_is_pinyin_backspace = false;
    s_vta_internal_update = false;
    s_hw_keyboard_present = false;
#if LV_USE_IME_PINYIN != 0
    s_pinyin_enabled = app_i18n_default_chinese_input();
    s_pinyin_committed_in_ready = false;
    s_pinyin_phrase_count = 0;
    memset(s_pinyin_phrase_words, 0, sizeof(s_pinyin_phrase_words));
    s_pinyin_cand_map_kind = PINYIN_CAND_MAP_DEFAULT;
    s_hw_pinyin_composing = false;
    s_hw_pinyin_cand_map_active = false;
#endif

    if (screen_to_del) {
        lv_obj_del(screen_to_del);
    }
}

static void terminal_return_to_server_list(void)
{
    if (s_active_session) {
        s_active_session->minimized = true;
    }
    terminal_destroy_view();
    lv_obj_clean(lv_scr_act());
    ui_server_list_create(lv_scr_act());
}

static void terminal_session_destroy(terminal_session_t *session)
{
    if (!session) {
        return;
    }

    if (session == s_active_session) {
        terminal_destroy_view();
        s_active_session = NULL;
    }

    session->destroy_requested = true;
    terminal_session_unlink(session);
    if (session->ssh) {
        ssh_client_disconnect_handle(session->ssh);
    }
    terminal_session_release(session);
}

static void ssh_connect_task(void *param)
{
    terminal_session_t *session = (terminal_session_t *)param;
    if (!session || !session->ssh) {
        terminal_session_release(session);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    esp_err_t err = ssh_client_connect_handle(session->ssh,
                                             &session->server,
                                             TERM_COLS,
                                             TERM_ROWS,
                                             on_ssh_data,
                                             on_ssh_event,
                                             session);
    if (err != ESP_OK) {
        terminal_session_consume_ssh_runtime_ref(session);
    }
    if (err != ESP_OK && !session->destroy_requested) {
        ESP_LOGE(TAG, "SSH connect failed: %s", esp_err_to_name(err));
    }
    session->connect_task = NULL;
    terminal_session_release(session);
    vTaskDeleteWithCaps(NULL);
}

static void terminal_session_start_connect(terminal_session_t *session)
{
    if (!session || session->connect_started) {
        return;
    }
    session->connect_started = true;
    if (!terminal_session_retain(session)) {
        session->failed = true;
        ESP_LOGE(TAG, "Failed to retain terminal session for SSH connect task");
        return;
    }
    terminal_session_set_ssh_runtime_ref(session, true);
    if (xTaskCreateWithCaps(ssh_connect_task, "ssh_conn", SSH_CONN_TASK_STACK,
                            session, SSH_CONN_TASK_PRIO, &session->connect_task,
                            SSH_CONN_TASK_STACK_CAPS) != pdPASS) {
        session->connect_task = NULL;
        session->failed = true;
        terminal_session_consume_ssh_runtime_ref(session);
        terminal_session_release(session);
        ESP_LOGE(TAG,
                 "Failed to create SSH connect task stack=%u caps=0x%x internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 (unsigned)SSH_CONN_TASK_STACK,
                 (unsigned)SSH_CONN_TASK_STACK_CAPS,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
}

static void terminal_update_status_label(void)
{
    if (!s_active_session || !s_lbl_status) {
        return;
    }

    char status_txt[160];
    const char *state = "connecting...";
    if (s_active_session->connected ||
        ssh_client_is_connected_handle(s_active_session->ssh)) {
        state = "connected";
    } else if (s_active_session->failed) {
        state = "failed";
    }
    snprintf(status_txt, sizeof(status_txt), " %s@%s  %s",
             s_active_session->server.username,
             s_active_session->server.host,
             state);
    lv_label_set_text(s_lbl_status, status_txt);
}

static void terminal_show_session(terminal_session_t *session)
{
    if (!session) {
        return;
    }

    terminal_destroy_view();
    lv_obj_clean(lv_scr_act());
    s_active_session = session;
    session->minimized = false;

    s_screen = lv_obj_create(lv_scr_act());
    s_follow_output = true;
    s_statusbar_visible = false;
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_terminal_hotzone_pressed, LV_EVENT_PRESSED, "screen");

    s_statusbar = lv_obj_create(s_screen);
    lv_obj_set_size(s_statusbar, LV_PCT(100), STATUSBAR_H);
    lv_obj_set_pos(s_statusbar, 0, 0);
    lv_obj_set_style_bg_color(s_statusbar, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(s_statusbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_statusbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_statusbar, 0, 0);
    lv_obj_set_style_pad_left(s_statusbar, 0, 0);
    lv_obj_set_style_pad_right(s_statusbar, 0, 0);
    lv_obj_set_style_pad_top(s_statusbar, 0, 0);
    lv_obj_set_style_pad_bottom(s_statusbar, 0, 0);
    lv_obj_clear_flag(s_statusbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_statusbar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_statusbar, on_status_hotzone_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);

    s_lbl_status = lv_label_create(s_statusbar);
    lv_obj_set_width(s_lbl_status, lv_display_get_horizontal_resolution(NULL) - 230);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_lbl_status, APP_TERM_TITLE_FONT, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x88C0D0), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_LEFT_MID, 0, 0);
    terminal_update_status_label();

    lv_obj_t *btn_dc = lv_button_create(s_statusbar);
    lv_obj_set_size(btn_dc, CLOSE_BTN_W, CLOSE_BTN_H);
    lv_obj_align(btn_dc, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_dc, lv_color_hex(0xBF616A), 0);
    lv_obj_set_style_radius(btn_dc, 6, 0);
    lv_obj_set_style_pad_all(btn_dc, 0, 0);
    lv_obj_t *lbl_dc = lv_label_create(btn_dc);
    lv_label_set_text(lbl_dc, "X");
    lv_obj_set_style_text_font(lbl_dc, APP_TERM_TITLE_FONT, 0);
    lv_obj_center(lbl_dc);
    lv_obj_add_event_cb(btn_dc, on_disconnect_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_min = lv_button_create(s_statusbar);
    lv_obj_set_size(btn_min, CLOSE_BTN_W, CLOSE_BTN_H);
    lv_obj_align_to(btn_min, btn_dc, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_set_style_bg_color(btn_min, lv_color_hex(0x3B475E), 0);
    lv_obj_set_style_radius(btn_min, 6, 0);
    lv_obj_set_style_pad_all(btn_min, 0, 0);
    lv_obj_t *lbl_min = lv_label_create(btn_min);
    lv_label_set_text(lbl_min, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(lbl_min, APP_TERM_TITLE_FONT, 0);
    lv_obj_center(lbl_min);
    lv_obj_add_event_cb(btn_min, on_minimize_click, LV_EVENT_CLICKED, NULL);

    s_viewport = lv_obj_create(s_screen);
    lv_obj_set_size(s_viewport, LV_PCT(100), lv_display_get_vertical_resolution(NULL));
    lv_obj_set_pos(s_viewport, 0, 0);
    lv_obj_set_style_bg_color(s_viewport, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewport, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_viewport, 0, 0);
    lv_obj_set_style_pad_all(s_viewport, 0, 0);
    lv_obj_set_scroll_dir(s_viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_viewport, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(s_viewport, on_viewport_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_viewport, on_terminal_hotzone_pressed, LV_EVENT_PRESSED, "viewport");

    s_canvas = term_renderer_create(s_viewport);
    if (s_canvas) {
        lv_obj_set_pos(s_canvas, 0, 0);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_canvas, on_terminal_hotzone_pressed, LV_EVENT_PRESSED, "canvas");
    }

    lv_coord_t scr_w = lv_display_get_horizontal_resolution(NULL);
    lv_coord_t scr_h = lv_display_get_vertical_resolution(NULL);
    lv_coord_t side_h = get_side_hotzone_h(scr_h);

    s_status_hotzone = lv_obj_create(s_screen);
    lv_obj_set_size(s_status_hotzone, scr_w, STATUS_HOTZONE_H);
    lv_obj_set_pos(s_status_hotzone, 0, 0);
    apply_hotzone_style(s_status_hotzone);
    lv_obj_add_event_cb(s_status_hotzone, on_status_hotzone_click, LV_EVENT_CLICKED, NULL);

    s_left_hotzone = lv_obj_create(s_screen);
    lv_obj_set_size(s_left_hotzone, LEFT_HOTZONE_W, side_h);
    lv_obj_set_pos(s_left_hotzone, 0, STATUS_HOTZONE_H);
    apply_hotzone_style(s_left_hotzone);
    lv_obj_add_event_cb(s_left_hotzone, on_left_hotzone_click, LV_EVENT_CLICKED, NULL);

    s_right_hotzone = lv_obj_create(s_screen);
    lv_coord_t right_x = get_right_hotzone_x(scr_w);
    lv_obj_set_size(s_right_hotzone, RIGHT_HOTZONE_W, side_h);
    lv_obj_set_pos(s_right_hotzone, right_x, STATUS_HOTZONE_H);
    apply_hotzone_style(s_right_hotzone);
    lv_obj_add_event_cb(s_right_hotzone, on_right_hotzone_click, LV_EVENT_CLICKED, NULL);

    s_vta = lv_textarea_create(s_screen);
    lv_obj_set_size(s_vta, 1, 1);
    lv_obj_set_pos(s_vta, -10, -10);
    lv_textarea_set_one_line(s_vta, true);
    lv_obj_add_event_cb(s_vta, on_vta_changed, LV_EVENT_VALUE_CHANGED, NULL);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_keyboard_set_textarea(s_keyboard, s_vta);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_keyboard, APP_KEYBOARD_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_keyboard, APP_KEYBOARD_FONT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_keyboard, THEME_BG_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_keyboard, THEME_BORDER_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_keyboard, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_keyboard, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_keyboard, THEME_BG_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keyboard, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_keyboard, THEME_BORDER_SECONDARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_keyboard, THEME_PRIMARY_ACTIVE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_keyboard, THEME_TEXT_PRIMARY, LV_PART_ITEMS | LV_STATE_PRESSED);

#if LV_USE_IME_PINYIN != 0
    s_pinyin_ime = lv_ime_pinyin_create(s_screen);
    lv_obj_set_style_text_font(s_pinyin_ime, APP_KEYBOARD_FONT, 0);
    lv_ime_pinyin_set_keyboard(s_pinyin_ime, s_keyboard);
    lv_ime_pinyin_set_dict(s_pinyin_ime, g_app_pinyin_dict);
    lv_ime_pinyin_set_mode(s_pinyin_ime, LV_IME_PINYIN_MODE_K26);

    s_pinyin_cand_panel = lv_ime_pinyin_get_cand_panel(s_pinyin_ime);
    s_pinyin_default_cand_map = lv_buttonmatrix_get_map(s_pinyin_cand_panel);
    lv_obj_set_style_bg_color(s_pinyin_cand_panel, THEME_BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_pinyin_cand_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pinyin_cand_panel, 1, 0);
    lv_obj_set_style_border_color(s_pinyin_cand_panel, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_radius(s_pinyin_cand_panel, 0, 0);
    lv_obj_set_style_pad_all(s_pinyin_cand_panel, 4, 0);
    lv_obj_set_style_pad_gap(s_pinyin_cand_panel, 4, 0);
    lv_obj_set_style_text_font(s_pinyin_cand_panel, &APP_IME_CAND_FONT, 0);
    lv_obj_set_style_text_font(s_pinyin_cand_panel, &APP_IME_CAND_FONT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_pinyin_cand_panel, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_pinyin_cand_panel, THEME_BG_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_pinyin_cand_panel, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_pinyin_cand_panel, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_pinyin_cand_panel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_pinyin_cand_panel, THEME_PRIMARY_ACTIVE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_pinyin_cand_panel, THEME_TEXT_PRIMARY, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_pinyin_cand_panel, on_pinyin_cand_panel_preprocess,
                        LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS, NULL);
    lv_obj_add_event_cb(s_pinyin_cand_panel, on_keyboard_value_postprocess, LV_EVENT_VALUE_CHANGED, NULL);
    pinyin_create_hardware_digit_labels();

    s_pinyin_input_bar = lv_obj_create(s_screen);
    lv_obj_set_size(s_pinyin_input_bar, LV_PCT(100), IME_INPUT_BAR_H);
    lv_obj_set_style_bg_color(s_pinyin_input_bar, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_pinyin_input_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pinyin_input_bar, 1, 0);
    lv_obj_set_style_border_color(s_pinyin_input_bar, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_radius(s_pinyin_input_bar, 0, 0);
    lv_obj_set_style_pad_left(s_pinyin_input_bar, 12, 0);
    lv_obj_set_style_pad_right(s_pinyin_input_bar, 12, 0);
    lv_obj_set_style_pad_top(s_pinyin_input_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_pinyin_input_bar, 0, 0);
    lv_obj_clear_flag(s_pinyin_input_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pinyin_input_bar, LV_OBJ_FLAG_HIDDEN);

    s_pinyin_input_label = lv_label_create(s_pinyin_input_bar);
    lv_label_set_text(s_pinyin_input_label, "");
    lv_obj_set_style_text_font(s_pinyin_input_label, APP_KEYBOARD_FONT, 0);
    lv_obj_set_style_text_color(s_pinyin_input_label, THEME_PRIMARY_LIGHT, 0);
    lv_obj_align(s_pinyin_input_label, LV_ALIGN_LEFT_MID, 0, 0);
#endif
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, on_keyboard_value_preprocess,
                        LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS, NULL);
    lv_obj_add_event_cb(s_keyboard, on_kb_enter, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, on_kb_cancel, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(s_keyboard, on_keyboard_backspace, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_keyboard, on_keyboard_value_postprocess, LV_EVENT_VALUE_CHANGED, NULL);
    s_kb_visible = false;

    create_control_bar(s_screen);

    if (session->term_mutex && xSemaphoreTake(session->term_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ansi_parser_mark_all_dirty(&session->term);
        xSemaphoreGive(session->term_mutex);
    }

    reposition_elements();
    s_render_timer = lv_timer_create(render_timer_cb, 30, NULL);
    s_hw_keyboard_present = (tab5_keyboard_init() == ESP_OK);
    if (s_hw_keyboard_present) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        s_kb_visible = false;
        ESP_LOGI(TAG, "Physical keyboard detected; soft keyboard disabled");
    }
    s_hw_keyboard_timer = lv_timer_create(hw_keyboard_timer_cb, HW_KEYBOARD_POLL_MS, NULL);

    terminal_session_start_connect(session);
    ESP_LOGI(TAG, "Terminal UI opened for %s@%s", session->server.username, session->server.host);
}

void ui_terminal_open(const server_entry_t *server)
{
    terminal_session_t *session = terminal_session_create(server);
    if (!session) {
        ESP_LOGE(TAG, "create terminal session failed");
        return;
    }
    terminal_show_session(session);
}

void ui_terminal_resume_session(uint32_t session_id)
{
    terminal_session_t *session = terminal_session_find(session_id);
    if (!session) {
        ESP_LOGW(TAG, "resume missing terminal session %" PRIu32, session_id);
        return;
    }
    terminal_show_session(session);
}

void ui_terminal_close(void)
{
    terminal_session_t *session = s_active_session;
    ESP_LOGI(TAG, "Closing terminal UI");
    terminal_session_destroy(session);
    lv_obj_clean(lv_scr_act());
    ui_server_list_create(lv_scr_act());
}

int ui_terminal_session_count(void)
{
    int count = 0;
    for (terminal_session_t *it = s_sessions; it; it = it->next) {
        if (terminal_session_is_listed(it)) {
            count++;
        }
    }
    return count;
}

bool ui_terminal_has_active_ssh_session(void)
{
    for (terminal_session_t *it = s_sessions; it; it = it->next) {
        if (!terminal_session_is_listed(it) || it->failed) {
            continue;
        }
        if (it->connected ||
            it->connect_task ||
            ssh_client_is_connected_handle(it->ssh)) {
            return true;
        }
    }
    return false;
}

bool ui_terminal_session_get_info(int index, ui_terminal_session_info_t *out)
{
    int pos = 0;
    if (!out || index < 0) {
        return false;
    }
    for (terminal_session_t *it = s_sessions; it; it = it->next) {
        if (!terminal_session_is_listed(it)) {
            continue;
        }
        if (pos == index) {
            memset(out, 0, sizeof(*out));
            out->id = it->id;
            out->icon_id = it->server.icon_id;
            out->connected = it->connected || ssh_client_is_connected_handle(it->ssh);
            out->failed = it->failed;
            strncpy(out->name, terminal_session_name(it), sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = '\0';
            return true;
        }
        pos++;
    }
    return false;
}
