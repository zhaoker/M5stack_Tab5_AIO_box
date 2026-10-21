static void apply_surface(lv_obj_t *obj, lv_color_t bg, lv_color_t border, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, TRS(text));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *create_wrap_label(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *label = create_label(parent, text, color);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t *create_footer_label(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *label = create_label(parent, text, color);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static lv_obj_t *create_row(lv_obj_t *parent, const char *name, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_label = create_label(row, name, THEME_TEXT_SECONDARY);
    lv_obj_set_width(name_label, 220);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

    lv_obj_t *value_label = create_label(row, value, THEME_TEXT_PRIMARY);
    lv_obj_set_width(value_label, 360);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    return row;
}

static uint32_t bytes_to_mb(size_t bytes)
{
    return (uint32_t)((bytes + (1024 * 1024 / 2)) / (1024 * 1024));
}

static void collect_basic_system_info(system_info_t *info)
{
    uint32_t flash_size = 0;

    memset(info, 0, sizeof(*info));
    info->cpu_mhz = 400;
    info->psram_physical_mb = bytes_to_mb(esp_psram_get_size());
    info->psram_free_mb = bytes_to_mb(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    info->panel_ic = bsp_display_get_panel_ic();
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        info->flash_mb = bytes_to_mb(flash_size);
    }
    info->touch_ready = (bsp_display_get_input_dev() != NULL);
    bool keyboard_present = tab5_keyboard_is_present();
    bool keyboard_seen = tab5_keyboard_was_detected();
    info->keyboard_ready = keyboard_seen;
    ESP_LOGI(TAG, "system info keyboard state: present=%d was_detected=%d ready=%d",
             keyboard_present ? 1 : 0,
             keyboard_seen ? 1 : 0,
             info->keyboard_ready ? 1 : 0);
    tab5_keyboard_dump_state("ui_settings system_info");
}

static lv_obj_t *add_info_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *section = lv_obj_create(parent);
    apply_surface(section, THEME_BG_CARD, THEME_BORDER_SECONDARY, 10);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(section, 14, 0);
    lv_obj_set_style_pad_row(section, 8, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    create_label(section, title, THEME_PRIMARY_LIGHT);
    return section;
}

static void add_i2c_device_line_with_color(lv_obj_t *section, unsigned index, const board_i2c_device_t *dev,
                                           lv_color_t line_color, lv_color_t desc_color)
{
    char line[192];
    snprintf(line, sizeof(line), "%u. %s  %s  %s", index, TRS(dev->device), dev->chip, dev->address);
    create_wrap_label(section, line, line_color);
    create_wrap_label(section, dev->description, desc_color);
}

static void add_i2c_device_line(lv_obj_t *section, unsigned index, const board_i2c_device_t *dev)
{
    add_i2c_device_line_with_color(section, index, dev, THEME_TEXT_SECONDARY, THEME_TEXT_TERTIARY);
}

static void add_board_i2c_device_list(lv_obj_t *parent, const char *panel_ic, bool keyboard_ready)
{
    lv_obj_t *section = add_info_section(parent, "Tab5 板载 I2C 设备");
    bool old_touch = panel_ic && (strcmp(panel_ic, "ILI9881C") == 0);
    const board_i2c_device_t keyboard = {
        "实体键盘", "A164", "0x6D", "已连接，I2C: port 1"
    };
    unsigned index = 1;

    add_i2c_device_line(section, index++, old_touch ? &s_board_i2c_devices[0] : &s_board_i2c_devices[1]);
    for (size_t i = 2; i < sizeof(s_board_i2c_devices) / sizeof(s_board_i2c_devices[0]); ++i) {
        add_i2c_device_line(section, index++, &s_board_i2c_devices[i]);
    }
    if (keyboard_ready) {
        add_i2c_device_line_with_color(section, index++, &keyboard, THEME_ACCENT_CYAN, THEME_ACCENT_CYAN);
    }
}

static int64_t elapsed_ms_since(int64_t start_us)
{
    return (esp_timer_get_time() - start_us) / 1000;
}

static lv_obj_t *create_page_container(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_width(page, LV_PCT(100));
    lv_obj_set_height(page, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_pad_row(page, 14, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}
