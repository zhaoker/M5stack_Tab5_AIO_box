static void on_close_click(lv_event_t *e)
{
    (void)e;
    destroy_overlay();
}

static void refresh_state_timer(lv_timer_t *timer)
{
    (void)timer;
    update_state_label();
}

static void update_state_label(void)
{
    wifi_state_t st;
    char current_ssid[WIFI_MAX_SSID_LEN] = {0};
    char ipv4[16] = {0};
    char ipv6[48] = {0};
    char ipv4_text[32];
    char ipv6_text[64];
    char text[256];
    const char *state_text;
    lv_color_t color;
    bool has_current;

    if (!s_lbl_state) {
        return;
    }

    st = wifi_manager_get_state();
    has_current = (st == WIFI_STATE_CONNECTED &&
                   wifi_manager_get_current_ssid(current_ssid, sizeof(current_ssid)) == ESP_OK);

    switch (st) {
        case WIFI_STATE_CONNECTED:
            state_text = TRS("已连接");
            color = COLOR_SUCCESS;
            break;
        case WIFI_STATE_CONNECTING:
            state_text = TRS("连接中");
            color = COLOR_WARNING;
            break;
        case WIFI_STATE_FAILED:
            state_text = TRS("连接失败");
            color = COLOR_DANGER;
            break;
        case WIFI_STATE_DISCONNECTED:
            state_text = TRS("未连接");
            color = COLOR_MUTED;
            break;
        default:
            state_text = TRS("空闲");
            color = COLOR_TEXT_SUB;
            break;
    }

    snprintf(text,
             sizeof(text),
             "%s: %s\n%s: %s\n%s: %u",
             TRS("状态"),
             state_text,
             TRS("当前网络"),
             has_current ? current_ssid : TRS("无"),
             TRS("已保存网络"),
             (unsigned)s_saved_count);

    lv_label_set_text(s_lbl_state, text);
    lv_obj_set_style_text_color(s_lbl_state, color, 0);

    if (!s_lbl_ipv4 || !s_lbl_ipv6) {
        return;
    }
    if (has_current) {
        wifi_manager_get_current_ip_text(ipv4, sizeof(ipv4), ipv6, sizeof(ipv6));
        snprintf(ipv4_text, sizeof(ipv4_text), "IPv4: %s", ipv4[0] ? ipv4 : "-");
        snprintf(ipv6_text, sizeof(ipv6_text), "IPv6: %s", ipv6[0] ? ipv6 : "-");
        lv_label_set_text(s_lbl_ipv4, ipv4_text);
        lv_label_set_text(s_lbl_ipv6, ipv6_text);
        lv_obj_set_style_text_color(s_lbl_ipv4, COLOR_TEXT_SUB, 0);
        lv_obj_set_style_text_color(s_lbl_ipv6, COLOR_TEXT_SUB, 0);
    } else {
        lv_label_set_text(s_lbl_ipv4, "");
        lv_label_set_text(s_lbl_ipv6, "");
    }
}

static void on_connect_click(lv_event_t *e)
{
    const char *ssid;
    const char *pass;
    esp_err_t err;

    (void)e;

    if (!s_ta_ssid || !s_ta_pass) {
        return;
    }

    ssid = lv_textarea_get_text(s_ta_ssid);
    pass = lv_textarea_get_text(s_ta_pass);
    if (!ssid || !ssid[0]) {
        set_hint(TRS("请先选择一个 WiFi 或手动输入 SSID。"), COLOR_WARNING);
        return;
    }

    hide_wifi_keyboard();

    err = wifi_manager_connect(ssid, pass, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect failed: %s", esp_err_to_name(err));
        set_hint(TRS("发起连接失败，请检查密码或稍后重试。"), COLOR_DANGER);
        return;
    }

    set_hint(TRS("已发起连接，并保存到已保存网络。"), COLOR_SUCCESS);
    refresh_saved_networks();
    rebuild_saved_networks_list();
    update_state_label();
    request_scan();
}

static void on_disconnect_click(lv_event_t *e)
{
    esp_err_t err;

    (void)e;
    err = wifi_manager_disconnect();
    if (err != ESP_OK) {
        set_hint(TRS("断开连接失败。"), COLOR_DANGER);
        return;
    }

    set_hint(TRS("已断开当前 WiFi 连接。"), COLOR_TEXT_SUB);
    update_state_label();
}

static void on_saved_connect(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err;

    if (index >= s_saved_count) {
        return;
    }

    err = wifi_manager_connect_saved(s_saved_networks[index].ssid);
    if (err != ESP_OK) {
        set_hint(TRS("连接已保存网络失败。"), COLOR_DANGER);
        return;
    }

    set_hint(TRS("正在连接已保存网络..."), COLOR_SUCCESS);
    update_state_label();
}

static void on_saved_delete(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err;

    if (index >= s_saved_count) {
        return;
    }

    err = wifi_manager_delete_network(s_saved_networks[index].ssid);
    if (err != ESP_OK) {
        set_hint(TRS("删除已保存网络失败。"), COLOR_DANGER);
        return;
    }

    set_hint(TRS("已删除该 WiFi。"), COLOR_TEXT_SUB);
    refresh_saved_networks();
    rebuild_saved_networks_list();
    rebuild_scan_list();
    update_state_label();
}

static void on_scan_row_action(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err;

    if (index >= s_scan_count) {
        return;
    }

    if (s_scan_results[index].saved) {
        err = wifi_manager_connect_saved(s_scan_results[index].ssid);
        if (err != ESP_OK) {
            set_hint(TRS("连接已保存网络失败。"), COLOR_DANGER);
            return;
        }
        set_hint(TRS("正在连接已保存网络..."), COLOR_SUCCESS);
        update_state_label();
        return;
    }

    lv_textarea_set_text(s_ta_ssid, s_scan_results[index].ssid);

    if (s_scan_results[index].authmode == WIFI_AUTH_OPEN) {
        lv_textarea_set_text(s_ta_pass, "");
        err = wifi_manager_connect(s_scan_results[index].ssid, "", true);
        if (err != ESP_OK) {
            set_hint(TRS("连接开放网络失败。"), COLOR_DANGER);
            return;
        }
        set_hint(TRS("正在连接开放网络，并已保存。"), COLOR_SUCCESS);
        refresh_saved_networks();
        update_state_label();
        return;
    }

    lv_textarea_set_text(s_ta_pass, "");
    set_hint(TRS("已填入 SSID，请输入密码后点击“保存并连接”。"), COLOR_WARNING);
}

static void on_scan_saved_delete(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err;

    if (index >= s_scan_count || !s_scan_results[index].saved) {
        return;
    }

    err = wifi_manager_delete_network(s_scan_results[index].ssid);
    if (err != ESP_OK) {
        set_hint(TRS("删除已保存网络失败。"), COLOR_DANGER);
        return;
    }

    set_hint(TRS("已删除该已保存 WiFi。"), COLOR_TEXT_SUB);
    refresh_saved_networks();
    rebuild_saved_networks_list();
    rebuild_scan_list();
    update_state_label();
}

static void on_scan_refresh(lv_event_t *e)
{
    (void)e;
    request_scan();
}

static void on_scan_timer(lv_timer_t *timer)
{
    (void)timer;
    if (s_scan_timer) {
        lv_timer_del(s_scan_timer);
        s_scan_timer = NULL;
    }

    scan_and_refresh_networks();
}
