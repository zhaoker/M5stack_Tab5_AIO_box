static uint8_t screensaver_rgb_r(uint32_t rgb)
{
    return (uint8_t)((rgb >> 16) & 0xFF);
}

static uint8_t screensaver_rgb_g(uint32_t rgb)
{
    return (uint8_t)((rgb >> 8) & 0xFF);
}

static uint8_t screensaver_rgb_b(uint32_t rgb)
{
    return (uint8_t)(rgb & 0xFF);
}

static bool screensaver_color_is_light(uint32_t rgb)
{
    uint32_t r = screensaver_rgb_r(rgb);
    uint32_t g = screensaver_rgb_g(rgb);
    uint32_t b = screensaver_rgb_b(rgb);

    return r * 299U + g * 587U + b * 114U >= 128000U;
}

static void screensaver_color_set_textarea(lv_obj_t *ta, uint8_t value)
{
    char text[8];

    if (!ta) {
        return;
    }
    snprintf(text, sizeof(text), "%u", value);
    lv_textarea_set_text(ta, text);
}

static void screensaver_color_update_preview(void)
{
    if (!s_screensaver_color_preview) {
        return;
    }
    lv_obj_set_style_bg_color(s_screensaver_color_preview,
                              lv_color_hex(s_screensaver_color_pending & 0xFFFFFF),
                              0);
}

static void screensaver_color_set_pending(uint32_t rgb)
{
    s_screensaver_color_pending = rgb & 0xFFFFFF;
    screensaver_color_set_textarea(s_screensaver_color_r_ta,
                                   screensaver_rgb_r(s_screensaver_color_pending));
    screensaver_color_set_textarea(s_screensaver_color_g_ta,
                                   screensaver_rgb_g(s_screensaver_color_pending));
    screensaver_color_set_textarea(s_screensaver_color_b_ta,
                                   screensaver_rgb_b(s_screensaver_color_pending));
    screensaver_color_update_preview();
}

static lv_color_t screensaver_name_bg_for_color(uint32_t rgb)
{
    return screensaver_color_is_light(rgb) ? lv_color_black() : lv_color_white();
}

static void screensaver_apply_component_name_style(app_screensaver_component_t component)
{
    uint32_t rgb;
    lv_obj_t *btn;
    lv_obj_t *label;

    if ((int)component < 0 || component >= APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT) {
        return;
    }
    btn = s_screensaver_component_name_buttons[component];
    label = s_screensaver_component_name_labels[component];
    rgb = app_settings_get_screensaver_component_color(component) & 0xFFFFFF;

    if (btn) {
        lv_obj_set_style_bg_color(btn, screensaver_name_bg_for_color(rgb), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(rgb), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_90, 0);
    }
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    }
}

static void screensaver_color_close_dialog(void)
{
    ui_number_input_close();
    if (s_screensaver_color_overlay) {
        lv_obj_del(s_screensaver_color_overlay);
    }
    s_screensaver_color_overlay = NULL;
    s_screensaver_color_preview = NULL;
    s_screensaver_color_r_ta = NULL;
    s_screensaver_color_g_ta = NULL;
    s_screensaver_color_b_ta = NULL;
}

static void on_screensaver_color_cancel_click(lv_event_t *e)
{
    (void)e;
    screensaver_color_close_dialog();
}

static bool screensaver_parse_color_value(lv_obj_t *ta, uint8_t *out_value)
{
    bool ok = false;
    int value;

    if (!ta || !out_value) {
        return false;
    }
    value = settings_parse_int_text(lv_textarea_get_text(ta), &ok);
    if (!ok || value < 0 || value > 255) {
        return false;
    }
    *out_value = (uint8_t)value;
    return true;
}

static void on_screensaver_color_save_click(lv_event_t *e)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t rgb;
    esp_err_t err;

    (void)e;
    if (!screensaver_parse_color_value(s_screensaver_color_r_ta, &r) ||
        !screensaver_parse_color_value(s_screensaver_color_g_ta, &g) ||
        !screensaver_parse_color_value(s_screensaver_color_b_ta, &b)) {
        set_screensaver_status("RGB颜色数值有效范围：0-255。", THEME_DANGER_LIGHT);
        return;
    }
    rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    err = app_settings_set_screensaver_component_color(s_screensaver_color_component, rgb);
    if (err != ESP_OK) {
        set_screensaver_status("组件颜色保存失败。", THEME_DANGER_LIGHT);
        return;
    }
    screensaver_apply_component_name_style(s_screensaver_color_component);
    screensaver_service_reload_settings();
    screensaver_color_close_dialog();
    set_screensaver_status("组件颜色已保存。", THEME_SUCCESS_LIGHT);
}

static uint32_t screensaver_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v)
{
    uint8_t region = h / 43;
    uint8_t remainder = (uint8_t)((h - (region * 43)) * 6);
    uint8_t p = (uint8_t)(((uint16_t)v * (255 - s)) >> 8);
    uint8_t q = (uint8_t)(((uint16_t)v * (255 - (((uint16_t)s * remainder) >> 8))) >> 8);
    uint8_t t = (uint8_t)(((uint16_t)v * (255 - (((uint16_t)s * (255 - remainder)) >> 8))) >> 8);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (region) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    default:
        r = v;
        g = p;
        b = q;
        break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void on_screensaver_color_swatch_click(lv_event_t *e)
{
    uint32_t rgb = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    screensaver_color_set_pending(rgb);
}

static lv_obj_t *screensaver_create_color_value_input(lv_obj_t *parent, uint8_t value)
{
    lv_obj_t *ta = lv_textarea_create(parent);

    lv_obj_set_size(ta, 74, 42);
    lv_obj_set_style_text_font(ta, &APP_FONT, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 3);
    screensaver_color_set_textarea(ta, value);
    ui_number_input_attach(ta, s_screensaver_color_overlay ? s_screensaver_color_overlay : s_overlay);
    return ta;
}

static void screensaver_add_color_swatch(lv_obj_t *board, uint32_t rgb)
{
    lv_obj_t *btn = lv_button_create(board);

    lv_obj_set_size(btn, 46, 38);
    lv_obj_set_style_bg_color(btn, lv_color_hex(rgb & 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn,
                                  screensaver_color_is_light(rgb) ? lv_color_black() : lv_color_white(),
                                  0);
    lv_obj_set_style_border_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_add_event_cb(btn,
                        on_screensaver_color_swatch_click,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)(rgb & 0xFFFFFF));
}

static void screensaver_create_color_board(lv_obj_t *parent)
{
    lv_obj_t *board = lv_obj_create(parent);

    lv_obj_set_size(board, 690, 430);
    lv_obj_set_style_bg_color(board, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_40, 0);
    lv_obj_set_style_border_color(board, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(board, 1, 0);
    lv_obj_set_style_radius(board, 8, 0);
    lv_obj_set_style_pad_all(board, 8, 0);
    lv_obj_set_style_pad_row(board, 8, 0);
    lv_obj_set_style_pad_column(board, 8, 0);
    lv_obj_set_flex_flow(board, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t y = 0; y < 7; y++) {
        uint8_t s = (uint8_t)(255 - y * 30);
        uint8_t v = (uint8_t)(255 - y * 8);

        for (uint8_t x = 0; x < 12; x++) {
            screensaver_add_color_swatch(board, screensaver_hsv_to_rgb(x * 21, s, v));
        }
    }
    for (uint8_t x = 0; x < 12; x++) {
        uint8_t gray = (uint8_t)(x * 23);
        uint32_t rgb = ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | gray;
        screensaver_add_color_swatch(board, rgb);
    }
}

static void screensaver_create_color_side(lv_obj_t *parent)
{
    lv_obj_t *side = lv_obj_create(parent);
    lv_obj_t *row;

    lv_obj_set_size(side, 250, 430);
    lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(side, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(side, 0, 0);
    lv_obj_set_style_pad_row(side, 12, 0);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    create_label(side, "颜色预览", THEME_TEXT_SECONDARY);
    s_screensaver_color_preview = lv_obj_create(side);
    lv_obj_set_size(s_screensaver_color_preview, 230, 82);
    lv_obj_set_style_border_color(s_screensaver_color_preview, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(s_screensaver_color_preview, 1, 0);
    lv_obj_set_style_radius(s_screensaver_color_preview, 8, 0);
    lv_obj_clear_flag(s_screensaver_color_preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_null_on_delete(&s_screensaver_color_preview);

    row = lv_obj_create(side);
    lv_obj_set_size(row, 230, 174);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_row(row, 14, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *channel_label = create_label(row, "红", THEME_TEXT_TERTIARY);
    lv_obj_set_width(channel_label, 34);
    s_screensaver_color_r_ta =
        screensaver_create_color_value_input(row, screensaver_rgb_r(s_screensaver_color_pending));
    lv_obj_null_on_delete(&s_screensaver_color_r_ta);
    channel_label = create_label(row, "绿", THEME_TEXT_TERTIARY);
    lv_obj_set_width(channel_label, 34);
    s_screensaver_color_g_ta =
        screensaver_create_color_value_input(row, screensaver_rgb_g(s_screensaver_color_pending));
    lv_obj_null_on_delete(&s_screensaver_color_g_ta);
    channel_label = create_label(row, "蓝", THEME_TEXT_TERTIARY);
    lv_obj_set_width(channel_label, 34);
    s_screensaver_color_b_ta =
        screensaver_create_color_value_input(row, screensaver_rgb_b(s_screensaver_color_pending));
    lv_obj_null_on_delete(&s_screensaver_color_b_ta);

    row = lv_obj_create(side);
    lv_obj_set_size(row, 230, 48);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cancel_btn =
        screensaver_create_small_button(row, "取消", THEME_BG_HOVER, on_screensaver_color_cancel_click, NULL);
    lv_obj_t *save_btn =
        screensaver_create_small_button(row, "保存颜色", THEME_PRIMARY, on_screensaver_color_save_click, NULL);
    lv_obj_set_width(cancel_btn, 96);
    lv_obj_set_width(save_btn, 120);
    screensaver_color_update_preview();
}

static void screensaver_open_color_dialog(app_screensaver_component_t component)
{
    lv_obj_t *panel;
    lv_obj_t *body;

    screensaver_color_close_dialog();
    s_screensaver_color_component = component;
    s_screensaver_color_pending =
        app_settings_get_screensaver_component_color(component) & 0xFFFFFF;

    s_screensaver_color_overlay = lv_obj_create(lv_layer_top());
    lv_obj_null_on_delete(&s_screensaver_color_overlay);
    lv_obj_set_size(s_screensaver_color_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screensaver_color_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screensaver_color_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_opa(s_screensaver_color_overlay, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_screensaver_color_overlay, LV_OBJ_FLAG_SCROLLABLE);

    panel = lv_obj_create(s_screensaver_color_overlay);
    lv_obj_set_size(panel, 1000, 600);
    lv_obj_center(panel);
    apply_surface(panel, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    create_label(panel, "选择组件颜色", THEME_PRIMARY_LIGHT);
    body = lv_obj_create(panel);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 500);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_column(body, 24, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    screensaver_create_color_board(body);
    screensaver_create_color_side(body);
    screensaver_color_set_pending(s_screensaver_color_pending);
}

static void on_screensaver_component_name_click(lv_event_t *e)
{
    app_screensaver_component_t component =
        (app_screensaver_component_t)(uintptr_t)lv_event_get_user_data(e);

    screensaver_open_color_dialog(component);
}

static void screensaver_create_visible_checkbox(lv_obj_t *parent,
                                                app_screensaver_component_t component)
{
    lv_obj_t *cb = lv_checkbox_create(parent);

    lv_checkbox_set_text(cb, "");
    lv_obj_set_size(cb, 34, 42);
    lv_obj_set_style_pad_all(cb, 0, 0);
    lv_obj_set_style_border_color(cb, THEME_BORDER_SECONDARY, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(cb, 1, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(cb, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(cb, THEME_PRIMARY_LIGHT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (app_settings_get_screensaver_component_visible(component)) {
        lv_obj_add_state(cb, LV_STATE_CHECKED);
    }
    s_screensaver_visible_checkboxes[component] = cb;
    lv_obj_null_on_delete(&s_screensaver_visible_checkboxes[component]);
}

static void screensaver_create_component_name_button(lv_obj_t *parent,
                                                     app_screensaver_component_t component)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(btn,
                    component == APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE ||
                            component == APP_SETTINGS_SCREENSAVER_COMPONENT_DATE
                        ? 144
                        : 132,
                    component == APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE ||
                            component == APP_SETTINGS_SCREENSAVER_COMPONENT_DATE
                        ? 48
                        : 42);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_pad_left(btn, 8, 0);
    lv_obj_set_style_pad_right(btn, 8, 0);
    lv_obj_add_event_cb(btn,
                        on_screensaver_component_name_click,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)component);

    label = create_label(btn, screensaver_component_label(component), lv_color_white());
    lv_obj_set_width(label,
                     component == APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE ||
                             component == APP_SETTINGS_SCREENSAVER_COMPONENT_DATE
                         ? 128
                         : 116);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);

    s_screensaver_component_name_buttons[component] = btn;
    s_screensaver_component_name_labels[component] = label;
    lv_obj_null_on_delete(&s_screensaver_component_name_buttons[component]);
    lv_obj_null_on_delete(&s_screensaver_component_name_labels[component]);
    screensaver_apply_component_name_style(component);
}

static void screensaver_refresh_component_controls(void)
{
    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        if (s_screensaver_visible_checkboxes[i]) {
            if (app_settings_get_screensaver_component_visible((app_screensaver_component_t)i)) {
                lv_obj_add_state(s_screensaver_visible_checkboxes[i], LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(s_screensaver_visible_checkboxes[i], LV_STATE_CHECKED);
            }
        }
        screensaver_apply_component_name_style((app_screensaver_component_t)i);
    }
}

static esp_err_t screensaver_save_component_controls(void)
{
    esp_err_t err = ESP_OK;

    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        if (!s_screensaver_visible_checkboxes[i]) {
            continue;
        }
        bool visible = !s_screensaver_visible_checkboxes[i] ||
                       lv_obj_has_state(s_screensaver_visible_checkboxes[i], LV_STATE_CHECKED);
        esp_err_t step = app_settings_set_screensaver_component_visible(
            (app_screensaver_component_t)i,
            visible);

        if (step != ESP_OK) {
            err = step;
        }
    }
    return err;
}
