static const lv_font_t *net_addr_font(void)
{
#if LV_FONT_MONTSERRAT_30
    return &lv_font_montserrat_30;
#elif LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#elif LV_FONT_MONTSERRAT_28
    return &lv_font_montserrat_28;
#else
    return LV_FONT_DEFAULT;
#endif
}

static void apply_surface_style(lv_obj_t *obj, lv_color_t bg, lv_color_t border, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text,
                               lv_style_t *base_style, lv_style_t *pressed_style)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_style(btn, base_style, 0);
    lv_obj_add_style(btn, pressed_style, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, TRS(text));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_PRIMARY, 0);
    lv_obj_center(label);
    return btn;
}

static const server_icon_def_t *get_server_icon(uint8_t icon_id)
{
    if (icon_id >= SERVER_ICON_COUNT) {
        icon_id = 0;
    }
    return &s_server_icons[icon_id];
}

static lv_color_t get_server_icon_color(uint8_t icon_id)
{
    return lv_color_hex(get_server_icon(icon_id)->color_hex);
}

static void set_icon_preview(lv_obj_t *obj, uint8_t icon_id)
{
    const server_icon_def_t *def = get_server_icon(icon_id);
    lv_color_t color = get_server_icon_color(icon_id);

    if (!obj || !def) {
        return;
    }

    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    if (lv_obj_get_child_count(obj) > 0) {
        lv_label_set_text(lv_obj_get_child(obj, 0), def->glyph);
    }
}

static void icon_picker_apply_selection(uint8_t icon_id)
{
    s_selected_icon_id = (icon_id < SERVER_ICON_COUNT) ? icon_id : 0;
    if (s_icon_picker_preview) {
        set_icon_preview(s_icon_picker_preview, s_selected_icon_id);
    }
    if (s_icon_picker_button) {
        set_icon_preview(s_icon_picker_button, s_selected_icon_id);
    }
}

static lv_obj_t *create_status_chip(lv_obj_t *parent, const char *text, lv_obj_t **out_label)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(chip, 116, 46);
    apply_surface_style(chip, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(chip);
    lv_label_set_text(label, TRS(text));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_SECONDARY, 0);
    lv_obj_center(label);

    if (out_label) {
        *out_label = label;
        lv_obj_null_on_delete(out_label);
    }

    return chip;
}

static lv_obj_t *create_form_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);

    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, FORM_ROW_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    return row;
}

static void init_styles(void)
{
    if (s_styles_init) {
        return;
    }
    s_styles_init = true;

    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, THEME_BG_CARD);
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_style_card, 16);
    lv_style_set_border_width(&s_style_card, 1);
    lv_style_set_border_color(&s_style_card, THEME_BORDER_SECONDARY);
    lv_style_set_pad_all(&s_style_card, THEME_SPACE_LG);
    lv_style_set_shadow_width(&s_style_card, 0);
    lv_style_set_text_color(&s_style_card, THEME_TEXT_PRIMARY);
    lv_style_set_text_font(&s_style_card, &APP_FONT);

    lv_style_init(&s_style_card_pressed);
    lv_style_set_bg_color(&s_style_card_pressed, THEME_BG_HOVER);
    lv_style_set_bg_opa(&s_style_card_pressed, LV_OPA_COVER);

    lv_style_init(&s_style_modal_panel);
    lv_style_set_bg_color(&s_style_modal_panel, THEME_BG_PANEL);
    lv_style_set_bg_opa(&s_style_modal_panel, LV_OPA_COVER);
    lv_style_set_radius(&s_style_modal_panel, 18);
    lv_style_set_border_width(&s_style_modal_panel, 1);
    lv_style_set_border_color(&s_style_modal_panel, THEME_BORDER_SECONDARY);
    lv_style_set_pad_all(&s_style_modal_panel, THEME_SPACE_LG);
    lv_style_set_shadow_width(&s_style_modal_panel, 0);

    lv_style_init(&s_style_primary_btn);
    lv_style_set_bg_color(&s_style_primary_btn, THEME_PRIMARY);
    lv_style_set_bg_opa(&s_style_primary_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_style_primary_btn, 14);
    lv_style_set_border_width(&s_style_primary_btn, 0);
    lv_style_set_pad_all(&s_style_primary_btn, 12);
    lv_style_set_text_color(&s_style_primary_btn, THEME_TEXT_PRIMARY);
    lv_style_set_text_font(&s_style_primary_btn, &APP_FONT);

    lv_style_init(&s_style_primary_btn_pressed);
    lv_style_set_bg_color(&s_style_primary_btn_pressed, THEME_PRIMARY_ACTIVE);
    lv_style_set_bg_opa(&s_style_primary_btn_pressed, LV_OPA_COVER);

    lv_style_init(&s_style_secondary_btn);
    lv_style_set_bg_color(&s_style_secondary_btn, THEME_BG_HOVER);
    lv_style_set_bg_opa(&s_style_secondary_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_style_secondary_btn, 14);
    lv_style_set_border_width(&s_style_secondary_btn, 1);
    lv_style_set_border_color(&s_style_secondary_btn, THEME_BORDER_SECONDARY);
    lv_style_set_pad_all(&s_style_secondary_btn, 12);
    lv_style_set_text_color(&s_style_secondary_btn, THEME_TEXT_PRIMARY);
    lv_style_set_text_font(&s_style_secondary_btn, &APP_FONT);

    lv_style_init(&s_style_secondary_btn_pressed);
    lv_style_set_bg_color(&s_style_secondary_btn_pressed, THEME_BG_PANEL);
    lv_style_set_bg_opa(&s_style_secondary_btn_pressed, LV_OPA_COVER);

    lv_style_init(&s_style_danger_btn);
    lv_style_set_bg_color(&s_style_danger_btn, THEME_DANGER);
    lv_style_set_bg_opa(&s_style_danger_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_style_danger_btn, 14);
    lv_style_set_border_width(&s_style_danger_btn, 0);
    lv_style_set_pad_all(&s_style_danger_btn, 12);
    lv_style_set_text_color(&s_style_danger_btn, THEME_TEXT_PRIMARY);
    lv_style_set_text_font(&s_style_danger_btn, &APP_FONT);

    lv_style_init(&s_style_danger_btn_pressed);
    lv_style_set_bg_color(&s_style_danger_btn_pressed, lv_color_hex(0xDC2626));
    lv_style_set_bg_opa(&s_style_danger_btn_pressed, LV_OPA_COVER);

    lv_style_init(&s_style_textarea);
    lv_style_set_bg_color(&s_style_textarea, THEME_INPUT_BG);
    lv_style_set_bg_opa(&s_style_textarea, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_textarea, 2);
    lv_style_set_border_color(&s_style_textarea, THEME_INPUT_BORDER);
    lv_style_set_radius(&s_style_textarea, THEME_RADIUS_MEDIUM);
    lv_style_set_pad_left(&s_style_textarea, 16);
    lv_style_set_pad_right(&s_style_textarea, 16);
    lv_style_set_pad_top(&s_style_textarea, 10);
    lv_style_set_pad_bottom(&s_style_textarea, 10);
    lv_style_set_text_color(&s_style_textarea, THEME_INPUT_TEXT);
    lv_style_set_text_font(&s_style_textarea, &APP_FONT);

    lv_style_init(&s_style_textarea_focused);
    lv_style_set_border_color(&s_style_textarea_focused, THEME_INPUT_FOCUS);
    lv_style_set_shadow_width(&s_style_textarea_focused, 0);

    lv_style_init(&s_style_icon_picker_btn);
    lv_style_set_bg_opa(&s_style_icon_picker_btn, LV_OPA_60);
    lv_style_set_border_width(&s_style_icon_picker_btn, 1);
    lv_style_set_radius(&s_style_icon_picker_btn, 16);
    lv_style_set_pad_all(&s_style_icon_picker_btn, 0);
    lv_style_set_shadow_width(&s_style_icon_picker_btn, 0);

    lv_style_init(&s_style_icon_picker_btn_pressed);
    lv_style_set_bg_opa(&s_style_icon_picker_btn_pressed, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_icon_picker_btn_pressed, THEME_TEXT_PRIMARY);
}

static void update_layout_metrics(void)
{
    int32_t scroll_w = lv_display_get_horizontal_resolution(NULL) - (SIDE_MARGIN * 2);
    s_card_w = (scroll_w - ((GRID_COLUMNS - 1) * GRID_GAP)) / GRID_COLUMNS;
    if (s_card_w < 300) {
        s_card_w = 300;
    }
    if (s_card_w > 360) {
        s_card_w = 360;
    }
    s_card_h = (s_card_w * CARD_RATIO_H) / CARD_RATIO_W;
    if (s_card_h < 180) {
        s_card_h = 180;
    }
}

static void align_power_hotzone(void)
{
    if (!s_power_hotzone || !s_power_chip) {
        return;
    }

    lv_obj_set_size(s_power_hotzone, 180, TOPBAR_HEIGHT);
    lv_obj_align_to(s_power_hotzone, s_power_chip, LV_ALIGN_LEFT_MID, -20, 0);
    lv_obj_move_foreground(s_power_hotzone);
}

static void layout_main_screen(void)
{
    int32_t screen_w;
    int32_t screen_h;
    lv_coord_t scroll_y;
    lv_coord_t scroll_h;

    if (!s_root || !s_list_scroll || !s_list_grid) {
        return;
    }

    screen_w = lv_display_get_horizontal_resolution(NULL);
    screen_h = lv_display_get_vertical_resolution(NULL);
    scroll_y = TOPBAR_HEIGHT + CONTENT_TOP_GAP;
    scroll_h = screen_h - scroll_y - TOPBAR_HEIGHT - CONTENT_BOTTOM_GAP;
    if (scroll_h < 180) {
        scroll_h = 180;
    }

    update_layout_metrics();

    lv_obj_set_size(s_root, screen_w, screen_h);
    if (s_statusbar_bottom) {
        lv_obj_set_size(s_statusbar_bottom, LV_PCT(100), TOPBAR_HEIGHT);
        lv_obj_set_pos(s_statusbar_bottom, 0, screen_h - TOPBAR_HEIGHT);
    }
    align_power_hotzone();
    lv_obj_set_size(s_list_scroll, screen_w - (SIDE_MARGIN * 2), scroll_h);
    lv_obj_set_pos(s_list_scroll, SIDE_MARGIN, scroll_y);
    lv_obj_set_width(s_list_grid, LV_PCT(100));
}

static void update_summary_label(int count)
{
    char text[64];

    if (!s_summary_label) {
        return;
    }

    if (count <= 0) {
        snprintf(text, sizeof(text), TRS("点击右上角添加服务器"));
    } else {
        snprintf(text, sizeof(text), TRS("已保存 %d 台服务器"), count);
    }
    lv_label_set_text(s_summary_label, text);
}

static void update_ip_chip(lv_obj_t *chip, lv_obj_t *label, const char *text, bool active)
{
    if (!chip || !label) {
        return;
    }

    lv_label_set_text(label, text);
    if (active) {
        lv_obj_set_style_text_color(label, THEME_SUCCESS_LIGHT, 0);
        lv_obj_set_style_bg_color(chip, THEME_SUCCESS_BG, 0);
        lv_obj_set_style_border_color(chip, THEME_SUCCESS, 0);
    } else {
        lv_obj_set_style_text_color(label, THEME_TEXT_SECONDARY, 0);
        lv_obj_set_style_bg_color(chip, THEME_BG_PANEL, 0);
        lv_obj_set_style_border_color(chip, THEME_BORDER_SECONDARY, 0);
    }
}

static bool char_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static bool parse_ipv4_candidate(const char *start, size_t len, char *out, size_t out_len)
{
    int dots = 0;
    int part = 0;
    int part_digits = 0;

    if (!start || !out || out_len < 16 || len < 7 || len > 15) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = start[i];
        if (char_is_digit(ch)) {
            part = part * 10 + (ch - '0');
            part_digits++;
            if (part_digits > 3 || part > 255) {
                return false;
            }
        } else if (ch == '.') {
            if (part_digits == 0 || dots >= 3) {
                return false;
            }
            dots++;
            part = 0;
            part_digits = 0;
        } else {
            return false;
        }
    }

    if (dots != 3 || part_digits == 0) {
        return false;
    }

    snprintf(out, out_len, "%.*s", (int)len, start);
    return true;
}

static bool extract_ipv4_from_text(const char *text, char *out, size_t out_len)
{
    if (!text || !out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (!char_is_digit(text[i])) {
            continue;
        }
        size_t j = i;
        while (text[j] && (char_is_digit(text[j]) || text[j] == '.')) {
            j++;
        }
        if (parse_ipv4_candidate(text + i, j - i, out, out_len)) {
            return true;
        }
        i = j;
    }
    return false;
}

static esp_err_t fetch_public_ipv4_from_node(const public_ipv4_node_t *node,
                                             char *ip_buf,
                                             size_t ip_len,
                                             char *raw_buf,
                                             size_t raw_len)
{
    esp_http_client_config_t config = {
        .url = node ? node->url : NULL,
        .user_agent = "M5Tab-SSH-Client/1.0",
        .timeout_ms = 3500,
        .buffer_size = 512,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int status;
    int read_len;

    if (!node || !ip_buf || ip_len == 0 || !raw_buf || raw_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ip_buf[0] = '\0';
    raw_buf[0] = '\0';

    client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    (void)esp_http_client_fetch_headers(client);
    read_len = esp_http_client_read_response(client, raw_buf, (int)raw_len - 1);
    status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    raw_buf[read_len] = '\0';
    if (status < 200 || status >= 300) {
        return ESP_FAIL;
    }
    return extract_ipv4_from_text(raw_buf, ip_buf, ip_len) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void on_public_ipv4_result(void *arg)
{
    public_ipv4_result_t *result = (public_ipv4_result_t *)arg;
    if (!result) {
        return;
    }

    if (result->generation == s_net_info_generation && s_net_info_public_ipv4_label) {
        lv_label_set_text(s_net_info_public_ipv4_label,
                          result->public_ipv4[0] ? result->public_ipv4 : "Public IP:    -");
    }
    free(result);
}

static void public_ipv4_query_task(void *arg)
{
    public_ipv4_task_arg_t *task_arg = (public_ipv4_task_arg_t *)arg;
    uint32_t generation = task_arg ? task_arg->generation : 0;
    public_ipv4_result_t *result = calloc(1, sizeof(*result));
    char ip[16];
    char raw[256];
    bool found = false;

    free(task_arg);

    if (!result) {
        vTaskDelete(NULL);
        return;
    }

    result->generation = generation;

    for (size_t i = 0; i < sizeof(s_public_ipv4_nodes) / sizeof(s_public_ipv4_nodes[0]); i++) {
        esp_err_t err = fetch_public_ipv4_from_node(&s_public_ipv4_nodes[i],
                                                    ip, sizeof(ip),
                                                    raw, sizeof(raw));
        if (err == ESP_OK) {
            snprintf(result->public_ipv4, sizeof(result->public_ipv4), "Public IP:    %s", ip);
            found = true;
            break;
        }
    }

    if (!found) {
        snprintf(result->public_ipv4, sizeof(result->public_ipv4), "Public IP:    -");
    }

    lv_async_call(on_public_ipv4_result, result);
    vTaskDelete(NULL);
}
