static ssh_event_info_t *alloc_ssh_event_info(ssh_event_t event,
                                              terminal_session_t *session,
                                              const char *reason)
{
    if (session && !terminal_session_retain(session)) {
        return NULL;
    }

    ssh_event_info_t *info = calloc(1, sizeof(*info));
    if (!info) {
        terminal_session_release(session);
        return NULL;
    }
    info->event = event;
    info->session = session;
    if (reason) {
        snprintf(info->reason, sizeof(info->reason), "%s", reason);
    }
    return info;
}

static void free_ssh_event_info(ssh_event_info_t *info)
{
    terminal_session_t *session;

    if (!info) {
        return;
    }

    session = info->session;
    free(info);
    terminal_session_release(session);
}

static void post_ssh_event(lv_async_cb_t cb,
                           ssh_event_t event,
                           terminal_session_t *session,
                           const char *reason)
{
    ssh_event_info_t *info = alloc_ssh_event_info(event, session, reason);
    if (!info) {
        if (event == SSH_EVT_DISCONNECTED) {
            terminal_session_consume_ssh_runtime_ref(session);
        }
        return;
    }

    if (lv_async_call(cb, info) != LV_RESULT_OK) {
        if (event == SSH_EVT_DISCONNECTED) {
            terminal_session_consume_ssh_runtime_ref(session);
        }
        free_ssh_event_info(info);
    }
}

static void show_terminal_msgbox(const char *title, const char *text)
{
    if (!s_screen || !title || !text) return;
    if (s_msgbox) {
        lv_obj_del(s_msgbox);
        s_msgbox = NULL;
    }
    s_msgbox = lv_msgbox_create(s_screen);
    if (!s_msgbox) return;
    lv_msgbox_add_title(s_msgbox, TRS(title));
    lv_msgbox_add_text(s_msgbox, TRS(text));
    lv_obj_clear_flag(s_msgbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(s_msgbox, &APP_FONT, 0);
    lv_coord_t scr_w = lv_display_get_horizontal_resolution(lv_display_get_default());
    lv_coord_t max_w = (scr_w > 160) ? (scr_w - 160) : scr_w;
    lv_coord_t width = 220 + (lv_coord_t)strlen(text) * 6;
    if (width < 300) width = 300;
    if (max_w > 0 && width > max_w) width = max_w;
    lv_obj_set_width(s_msgbox, width);
    lv_obj_set_style_max_width(s_msgbox, max_w, 0);
    lv_obj_set_style_pad_all(s_msgbox, 18, 0);
    lv_obj_t *ok_btn = lv_msgbox_add_footer_button(s_msgbox, TRS("确定"));
    if (ok_btn) {
        lv_obj_set_size(ok_btn, 144, 52);
        lv_obj_set_style_text_font(ok_btn, &APP_FONT, 0);
        lv_obj_add_event_cb(ok_btn, on_terminal_msgbox_ok, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *footer = lv_msgbox_get_footer(s_msgbox);
    if (footer) {
        lv_obj_set_style_pad_all(footer, 10, 0);
        lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    lv_obj_center(s_msgbox);
    lv_obj_move_foreground(s_msgbox);
}

static void on_terminal_msgbox_ok(lv_event_t *e)
{
    (void)e;
    ui_terminal_close();
}

static void delete_ime_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ime_toast_timer) {
        lv_timer_del(s_ime_toast_timer);
        s_ime_toast_timer = NULL;
    }
    if (s_ime_toast) {
        lv_obj_del(s_ime_toast);
        s_ime_toast = NULL;
    }
}

static void position_ime_mode_toast(void)
{
    if (!s_ime_toast) return;
    if (s_hw_keyboard_present) {
        lv_obj_center(s_ime_toast);
    } else {
        lv_obj_align(s_ime_toast, LV_ALIGN_TOP_MID, 0, IME_TOAST_TOP_Y);
    }
}

static void show_ime_mode_toast(void)
{
#if LV_USE_IME_PINYIN != 0
    if (!s_screen) return;

    if (s_ime_toast_timer) {
        lv_timer_del(s_ime_toast_timer);
        s_ime_toast_timer = NULL;
    }
    if (s_ime_toast) {
        lv_obj_del(s_ime_toast);
        s_ime_toast = NULL;
    }

    s_ime_toast = lv_obj_create(s_screen);
    if (!s_ime_toast) return;
    lv_obj_clear_flag(s_ime_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_ime_toast, 168, 92);
    lv_obj_set_style_bg_color(s_ime_toast, lv_color_hex(0x211F1D), 0);
    lv_obj_set_style_bg_opa(s_ime_toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_ime_toast, THEME_PRIMARY_LIGHT, 0);
    lv_obj_set_style_border_width(s_ime_toast, 2, 0);
    lv_obj_set_style_radius(s_ime_toast, 18, 0);
    lv_obj_set_style_shadow_width(s_ime_toast, 18, 0);
    lv_obj_set_style_shadow_opa(s_ime_toast, 70, 0);
    lv_obj_set_style_shadow_color(s_ime_toast, lv_color_hex(0x000000), 0);
    lv_obj_set_style_opa(s_ime_toast, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(s_ime_toast);
    lv_label_set_text(label, s_pinyin_enabled ? "CN" : "EN");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_PRIMARY, 0);
    lv_obj_center(label);

    position_ime_mode_toast();
    lv_obj_move_foreground(s_ime_toast);
    lv_obj_fade_out(s_ime_toast, IME_TOAST_FADE_MS, IME_TOAST_HIDE_MS);

    s_ime_toast_timer = lv_timer_create(delete_ime_toast_timer_cb,
                                        IME_TOAST_HIDE_MS + IME_TOAST_FADE_MS + 50,
                                        NULL);
    if (s_ime_toast_timer) {
        lv_timer_set_repeat_count(s_ime_toast_timer, 1);
    }
#endif
}

static void toggle_ime_mode(void)
{
#if LV_USE_IME_PINYIN != 0
    s_pinyin_enabled = !s_pinyin_enabled;
    pinyin_ime_reset_composition();
    if (s_keyboard) {
        lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    }
    update_ime_mode_label();
    show_ime_mode_toast();
    reposition_elements();
    ESP_LOGI(TAG, "IME mode switched to %s", s_pinyin_enabled ? "pinyin" : "english");
#endif
}

static bool           s_kb_visible = false;
static bool           s_follow_output = true;
static bool           s_statusbar_visible = false;
static uint32_t       s_last_hotzone_tick = 0;

/* ─── SSH 数据接收回调 ─── */

static void on_ssh_data(const char *data, size_t len, void *user_data)
{
    terminal_session_t *session = (terminal_session_t *)user_data;
    /* 在 SSH 任务上下文中调用，需要加锁保护 term_state */
    static uint32_t s_total_bytes = 0;
    if (!session || session->destroy_requested) {
        return;
    }
    s_total_bytes += len;
    if (s_total_bytes <= 4096 || (s_total_bytes % 8192) < (uint32_t)len) {
        ESP_LOGI(TAG, "SSH data: +%d bytes (total=%lu)", (int)len, (unsigned long)s_total_bytes);
    }
    if (xSemaphoreTake(session->term_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ansi_parser_feed(&session->term, data, len);
        xSemaphoreGive(session->term_mutex);
    } else {
        ESP_LOGW(TAG, "on_ssh_data: mutex timeout, dropped %d bytes", (int)len);
    }
}

/* ─── SSH 事件回调（在 SSH 任务上下文；通过 lv_async_call 更新 UI） ─── */

static void do_ssh_connected(void *arg)
{
    ssh_event_info_t *info = (ssh_event_info_t *)arg;
    terminal_session_t *session = info ? info->session : NULL;
    if (!session || session->destroy_requested) {
        free_ssh_event_info(info);
        return;
    }
    session->connected = true;
    session->failed = false;
    if (session == s_active_session && s_msgbox) {
        lv_obj_del(s_msgbox);
        s_msgbox = NULL;
    }
    if (session == s_active_session && s_lbl_status) {
        char txt[160];
        snprintf(txt, sizeof(txt), " %s@%s  connected", session->server.username, session->server.host);
        lv_label_set_text(s_lbl_status, txt);
    }

    /* 注入测试文本：验证渲染管道是否正常工作 */
    if (!session->banner_injected &&
        session->term_mutex &&
        xSemaphoreTake(session->term_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const char *banner = "[SSH Terminal Ready - waiting for server output...]\r\n";
        ansi_parser_feed(&session->term, banner, strlen(banner));
        xSemaphoreGive(session->term_mutex);
        session->banner_injected = true;
        ESP_LOGI(TAG, "Test banner injected into terminal");
    }
    free_ssh_event_info(info);
}

static void do_ssh_failed(void *arg)
{
    ssh_event_info_t *info = (ssh_event_info_t *)arg;
    terminal_session_t *session = info ? info->session : NULL;
    if (!session || session->destroy_requested) {
        free_ssh_event_info(info);
        return;
    }
    session->connected = false;
    session->failed = true;
    const char *reason = (info && info->reason[0]) ? info->reason : NULL;
    if (session == s_active_session && s_lbl_status) {
        lv_label_set_text(s_lbl_status, " SSH connection failed");
    }
    if (session == s_active_session && strcmp(reason ? reason : "", "closed") != 0) {
        show_terminal_msgbox(TRS("连接失败"), reason ? reason : TRS("SSH 连接失败"));
    }
    if (session->term_mutex && xSemaphoreTake(session->term_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        char errmsg[96];
        snprintf(errmsg, sizeof(errmsg), "\r\n\033[31m[%s] %s\033[0m\r\n", TRS("错误"), TRS("SSH 连接失败"));
        ansi_parser_feed(&session->term, errmsg, strlen(errmsg));
        xSemaphoreGive(session->term_mutex);
    }
    free_ssh_event_info(info);
}

static void do_ssh_disconnected(void *arg)
{
    ssh_event_info_t *info = (ssh_event_info_t *)arg;
    terminal_session_t *session = info ? info->session : NULL;
    if (!session || session->destroy_requested) {
        if (session) {
            terminal_session_consume_ssh_runtime_ref(session);
        }
        free_ssh_event_info(info);
        return;
    }
    session->connected = false;
    const char *reason = (info && info->reason[0]) ? info->reason : NULL;
    if (session == s_active_session && strcmp(reason ? reason : "", "closed") != 0) {
        show_terminal_msgbox(TRS("已断开连接"), reason ? reason : TRS("SSH 已断开"));
    }
    terminal_session_consume_ssh_runtime_ref(session);
    free_ssh_event_info(info);
}

static void on_ssh_event(ssh_event_t event, const char *reason, void *user_data)
{
    terminal_session_t *session = (terminal_session_t *)user_data;
    if (event == SSH_EVT_CONNECTED) {
        terminal_session_set_ssh_runtime_ref(session, true);
        post_ssh_event(do_ssh_connected, event, session, NULL);
    } else if (event == SSH_EVT_DISCONNECTED) {
        ESP_LOGW(TAG, "SSH event: %d", event);
        post_ssh_event(do_ssh_disconnected, event, session, reason);
    } else if (event == SSH_EVT_AUTH_FAILED || event == SSH_EVT_ERROR) {
        ESP_LOGW(TAG, "SSH event: %d", event);
        post_ssh_event(do_ssh_failed, event, session, reason);
    }
}

/* ─── 渲染定时器（30ms，LVGL 任务上下文） ─── */

static void render_timer_cb(lv_timer_t *t)
{
    if (!s_canvas || !s_term_mutex) return;

    bool need_render = false;

    if (xSemaphoreTake(s_term_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        /* 检查是否有脏数据 */
        for (int r = 0; r < TERM_ROWS && !need_render; r++) {
            if (s_term.dirty_rows[r]) need_render = true;
        }
        if (s_term.dirty_cursor) need_render = true;

        if (need_render) {
            reposition_elements();
            static uint32_t s_render_cnt = 0;
            s_render_cnt++;
            if (s_render_cnt <= 5 || (s_render_cnt % 30) == 0) {
                ESP_LOGI(TAG, "render #%lu", (unsigned long)s_render_cnt);
            }
            term_renderer_refresh(s_canvas, &s_term);
            if (s_viewport && s_follow_output) {
                lv_obj_update_layout(s_viewport);
                lv_obj_scroll_to_y(s_viewport,
                                   lv_obj_get_scroll_top(s_viewport) + lv_obj_get_scroll_bottom(s_viewport),
                                   LV_ANIM_OFF);
            }
        }
        xSemaphoreGive(s_term_mutex);
    }
}

/* ─── 键盘输入回调 ─── */

/* Enter 键（✓）：向 SSH 发送回车，不关闭键盘 */
static void on_kb_enter(lv_event_t *e)
{
#if LV_USE_IME_PINYIN != 0
    if (pinyin_ime_has_composition()) {
        s_pinyin_committed_in_ready = pinyin_commit_raw_input();
        reposition_elements();
        return;
    }
#endif
    if (ssh_client_is_connected()) {
        ssh_client_write("\r", 1);
    }
}

/* 退格键拦截（VALUE_CHANGED on keyboard）：直接向 SSH 发送 DEL(0x7F)
 * 背景：虚拟 textarea 每次字符输入后立即清空，导致退格时 textarea 已为空，
 * lv_textarea_delete_char 无效，VALUE_CHANGED 不触发。
 * 解决方案：在 keyboard 的 VALUE_CHANGED 回调中检测退格按钮并直接发送。 */
static void on_keyboard_backspace(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint32_t btn = lv_buttonmatrix_get_selected_button(kb);
    if (btn == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *txt = lv_buttonmatrix_get_button_text(kb, btn);
    if (txt && strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if (s_kb_value_is_pinyin_backspace) {
            reposition_elements();
            return;
        }
        if (ssh_client_is_connected()) {
            ssh_client_write("\x7f", 1);
        }
        reposition_elements();
    }
}
