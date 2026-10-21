static lv_obj_t *create_menu_button(lv_obj_t *parent, int index)
{
    lv_obj_t *btn = lv_button_create(parent);
    apply_surface(btn, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_size(btn, LV_PCT(100), SETTINGS_MENU_ITEM_H);
    lv_obj_set_style_pad_all(btn, 12, 0);
    lv_obj_set_style_pad_column(btn, 12, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, on_menu_click, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *icon = create_label(btn, s_pages[index].icon, THEME_PRIMARY_LIGHT);
    lv_obj_set_width(icon, 32);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    s_menu_icons[index] = icon;

    lv_obj_t *texts = lv_obj_create(btn);
    lv_obj_set_width(texts, LV_PCT(100));
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(texts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(texts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(texts, 0, 0);
    lv_obj_set_style_pad_row(texts, 3, 0);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(texts, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(texts, TRS(s_pages[index].title), THEME_TEXT_PRIMARY);
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    s_menu_titles[index] = title;

    return btn;
}

void ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *header;
    lv_obj_t *body;
    lv_obj_t *left;
    lv_obj_t *title;
    lv_obj_t *close_btn;
    lv_obj_t *close_label;

    close_settings();

    s_overlay = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, THEME_OVERLAY_OPA, 0);
    lv_obj_set_style_border_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, on_overlay_click, LV_EVENT_CLICKED, NULL);
    lv_obj_null_on_delete(&s_overlay);

    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, SETTINGS_PANEL_W, SETTINGS_PANEL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    apply_surface(s_panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 16);
    lv_obj_set_style_pad_all(s_panel, SETTINGS_PAD, 0);
    lv_obj_set_style_pad_row(s_panel, SETTINGS_GAP, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_panel, on_panel_click, LV_EVENT_CLICKED, NULL);

    header = lv_obj_create(s_panel);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, SETTINGS_HEADER_H);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title = create_label(header, LV_SYMBOL_SETTINGS " 设置", THEME_TEXT_PRIMARY);
    lv_obj_set_style_text_font(title, &APP_FONT, 0);

    close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 48, 48);
    lv_obj_set_style_bg_color(close_btn, THEME_BG_HOVER, 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_radius(close_btn, 12, 0);
    lv_obj_add_event_cb(close_btn, on_close_click, LV_EVENT_CLICKED, NULL);
    close_label = create_label(close_btn, LV_SYMBOL_CLOSE, THEME_TEXT_PRIMARY);
    lv_obj_center(close_label);

    body = lv_obj_create(s_panel);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_column(body, SETTINGS_GAP, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    left = lv_obj_create(body);
    apply_surface(left, THEME_BG_PRIMARY, THEME_BORDER_PRIMARY, 12);
    lv_obj_set_size(left, SETTINGS_LEFT_W, LV_PCT(100));
    lv_obj_set_style_pad_all(left, 12, 0);
    lv_obj_set_style_pad_row(left, 10, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < SETTINGS_PAGE_COUNT; ++i) {
        s_menu_buttons[i] = create_menu_button(left, i);
    }

    s_content = lv_obj_create(body);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_height(s_content, LV_PCT(100));
    apply_surface(s_content, THEME_BG_PRIMARY, THEME_BORDER_PRIMARY, 12);
    lv_obj_set_style_pad_all(s_content, 18, 0);
    lv_obj_set_style_pad_row(s_content, 14, 0);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_null_on_delete(&s_content);

    render_selected_page();
}
