static void fm_go_parent(void)
{
    char *slash;
    if (s_storage_picker_active) {
        return;
    }
    if (fm_is_root()) {
        fm_leave_to_storage_picker();
        return;
    }
    slash = strrchr(s_current_path, '/');
    if (!slash || slash <= s_current_path + strlen(s_root_path)) {
        strlcpy(s_current_path, s_root_path, sizeof(s_current_path));
    } else {
        *slash = '\0';
    }
    s_page = 0;
    s_selected_index = 0;
    s_all_active = false;
    s_selection_status_active = false;
    fm_render();
}

static void fm_close_viewer(void)
{
    if (s_viewer_touch_timer) {
        lv_timer_del(s_viewer_touch_timer);
        s_viewer_touch_timer = NULL;
    }
    s_viewer_canvas = NULL;
    s_viewer_img = NULL;
    s_viewer_gif = NULL;
    s_viewer_info_panel = NULL;
    s_viewer_exif_top_panel = NULL;
    s_viewer_exif_top_label = NULL;
    s_viewer_exif_panel = NULL;
    s_viewer_exif_label = NULL;
    s_viewer_status = NULL;
    s_viewer_prev_btn = NULL;
    s_viewer_next_btn = NULL;
    s_viewer_rotate_btn = NULL;
    s_viewer_close_btn = NULL;
    s_viewer_path[0] = '\0';
    s_viewer_image_count = 0;
    s_viewer_image_pos = 0;
    s_viewer_controls_last_touch = 0;
    s_pinch_active = false;
    s_viewer_controls_visible = false;
    s_viewer_gif_paused_for_controls = false;
    s_viewer_exif_title_has_content = false;
    s_viewer_exif_has_content = false;
    if (s_viewer) {
        lv_obj_del(s_viewer);
        s_viewer = NULL;
    }
    fm_release_viewer_image();
}

static void fm_close_text_viewer(void)
{
    lv_obj_t *viewer = s_text_viewer;

    s_text_bar = NULL;
    s_text_title = NULL;
    s_text_theme_label = NULL;
    s_text_textarea = NULL;
    s_text_status = NULL;
    s_text_raw_len = 0;
    strlcpy(s_text_encoding, "unknown", sizeof(s_text_encoding));
    if (viewer) {
        lv_obj_del(viewer);
    }
    s_text_viewer = NULL;
}

static char *fm_read_text_file(const char *path,
                               size_t max_bytes,
                               size_t *out_len,
                               bool *out_truncated,
                               size_t *out_raw_len,
                               const char **out_encoding)
{
    FILE *fp;
    uint8_t *buf;
    char *text;
    size_t got;

    if (out_len) {
        *out_len = 0;
    }
    if (out_truncated) {
        *out_truncated = false;
    }
    if (out_raw_len) {
        *out_raw_len = 0;
    }
    if (!path || max_bytes == 0) {
        return NULL;
    }
    if (out_encoding) {
        *out_encoding = "unknown";
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    buf = (uint8_t *)malloc(max_bytes);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, max_bytes, fp);
    if (ferror(fp)) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    if (out_truncated) {
        int extra = fgetc(fp);
        *out_truncated = (extra != EOF);
    }
    fclose(fp);

    if (out_raw_len) {
        *out_raw_len = got;
    }
    text = file_manager_text_decode_to_utf8(buf, got, out_len, out_encoding);
    free(buf);
    return text;
}

static void fm_on_text_close(lv_event_t *e)
{
    (void)e;
    fm_close_text_viewer();
}

static void fm_text_layout(void)
{
    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    int32_t edit_h;

    if (!s_text_viewer) {
        return;
    }
    if (screen_w <= 0) {
        screen_w = 720;
    }
    if (screen_h <= 0) {
        screen_h = 1280;
    }

    if (s_text_bar) {
        lv_obj_set_size(s_text_bar, screen_w, FM_TOPBAR_H);
        lv_obj_set_pos(s_text_bar, 0, 0);
    }
    if (s_text_status) {
        lv_obj_set_size(s_text_status, screen_w, FM_TEXT_STATUS_H);
        lv_obj_align(s_text_status, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (s_text_textarea) {
        edit_h = screen_h - FM_TOPBAR_H - FM_TEXT_STATUS_H;
        if (edit_h < 120) {
            edit_h = 120;
        }
        lv_obj_set_size(s_text_textarea, screen_w, edit_h);
        lv_obj_set_pos(s_text_textarea, 0, FM_TOPBAR_H);
    }
}

static uint32_t fm_text_progress_percent(void)
{
    if (!s_text_textarea) {
        return 0;
    }

    lv_obj_update_layout(s_text_textarea);
    lv_obj_t *label = lv_textarea_get_label(s_text_textarea);
    int32_t scroll_y = lv_obj_get_scroll_y(s_text_textarea);
    int32_t scroll_bottom = lv_obj_get_scroll_bottom(s_text_textarea);
    int32_t line_h = lv_font_get_line_height(&APP_FONT) + lv_obj_get_style_text_line_space(s_text_textarea, 0);

    if (line_h <= 0) {
        line_h = 1;
    }
    if (scroll_y < 0) {
        scroll_y = 0;
    }

    int32_t current_first_line = scroll_y / line_h;
    int32_t last_page_first_line = (scroll_y + scroll_bottom) / line_h;
    if (last_page_first_line <= 0 && label) {
        int32_t content_h = lv_obj_get_height(label);
        int32_t viewport_h = lv_obj_get_height(s_text_textarea);
        int32_t max_scroll = content_h > viewport_h ? content_h - viewport_h : 0;
        last_page_first_line = max_scroll / line_h;
    }
    if (last_page_first_line <= 0) {
        return 100;
    }
    if (current_first_line > last_page_first_line) {
        current_first_line = last_page_first_line;
    }
    return (uint32_t)(((uint64_t)current_first_line * 100 + (uint32_t)last_page_first_line / 2) /
                      (uint32_t)last_page_first_line);
}

static void fm_text_update_status(void)
{
    if (!s_text_status) {
        return;
    }
    lv_label_set_text_fmt(s_text_status,
                          TRS("字节 %u   编码 %s   进度 %u%%"),
                          (unsigned)s_text_raw_len,
                          s_text_encoding,
                          (unsigned)fm_text_progress_percent());
}

static void fm_on_text_scroll(lv_event_t *e)
{
    (void)e;
    fm_text_update_status();
}

static void fm_text_theme_colors(lv_color_t *bg,
                                 lv_color_t *bar,
                                 lv_color_t *textarea_bg,
                                 lv_color_t *textarea_text,
                                 lv_color_t *border,
                                 lv_color_t *muted)
{
    switch (s_text_theme) {
    case FM_TEXT_THEME_LIGHT:
        *bg = lv_color_hex(APP_TEXT_THEME_LIGHT_BG);
        *bar = lv_color_hex(APP_TEXT_THEME_LIGHT_BAR);
        *textarea_bg = lv_color_hex(APP_TEXT_THEME_LIGHT_EDITOR_BG);
        *textarea_text = lv_color_hex(APP_TEXT_THEME_LIGHT_TEXT);
        *border = lv_color_hex(APP_TEXT_THEME_LIGHT_BORDER);
        *muted = lv_color_hex(APP_TEXT_THEME_LIGHT_MUTED);
        break;
    case FM_TEXT_THEME_EYE:
        *bg = lv_color_hex(APP_TEXT_THEME_EYE_BG);
        *bar = lv_color_hex(APP_TEXT_THEME_EYE_BAR);
        *textarea_bg = lv_color_hex(APP_TEXT_THEME_EYE_EDITOR_BG);
        *textarea_text = lv_color_hex(APP_TEXT_THEME_EYE_TEXT);
        *border = lv_color_hex(APP_TEXT_THEME_EYE_BORDER);
        *muted = lv_color_hex(APP_TEXT_THEME_EYE_MUTED);
        break;
    case FM_TEXT_THEME_DARK:
    default:
        *bg = lv_color_hex(APP_TEXT_THEME_DARK_BG);
        *bar = lv_color_hex(APP_TEXT_THEME_DARK_BAR);
        *textarea_bg = lv_color_hex(APP_TEXT_THEME_DARK_EDITOR_BG);
        *textarea_text = lv_color_hex(APP_TEXT_THEME_DARK_TEXT);
        *border = lv_color_hex(APP_TEXT_THEME_DARK_BORDER);
        *muted = lv_color_hex(APP_TEXT_THEME_DARK_MUTED);
        break;
    }
}

static void fm_text_apply_theme(void)
{
    lv_color_t bg;
    lv_color_t bar;
    lv_color_t textarea_bg;
    lv_color_t textarea_text;
    lv_color_t border;
    lv_color_t muted;

    fm_text_theme_colors(&bg, &bar, &textarea_bg, &textarea_text, &border, &muted);

    if (s_text_viewer) {
        lv_obj_set_style_bg_color(s_text_viewer, bg, 0);
    }
    if (s_text_bar) {
        lv_obj_set_style_bg_color(s_text_bar, bar, 0);
        lv_obj_set_style_border_color(s_text_bar, bar, 0);
    }
    if (s_text_title) {
        lv_obj_set_style_text_color(s_text_title, textarea_text, 0);
    }
    if (s_text_theme_label) {
        const char *theme_text = TRS("暗色");
        if (s_text_theme == FM_TEXT_THEME_LIGHT) {
            theme_text = TRS("亮色");
        } else if (s_text_theme == FM_TEXT_THEME_EYE) {
            theme_text = TRS("护眼");
        }
        lv_label_set_text(s_text_theme_label, theme_text);
    }
    if (s_text_textarea) {
        lv_obj_set_style_bg_color(s_text_textarea, textarea_bg, 0);
        lv_obj_set_style_border_color(s_text_textarea, border, 0);
        lv_obj_set_style_text_color(s_text_textarea, textarea_text, 0);
        lv_obj_set_style_text_color(s_text_textarea, muted, LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_font(s_text_textarea, &APP_FONT, 0);
        lv_obj_set_style_text_font(s_text_textarea, &APP_FONT, LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_bg_opa(s_text_textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    }
    if (s_text_status) {
        lv_obj_set_style_bg_color(s_text_status, bar, 0);
        lv_obj_set_style_border_color(s_text_status, border, 0);
    }
}

static void fm_on_text_theme(lv_event_t *e)
{
    (void)e;
    if (s_text_theme == FM_TEXT_THEME_EYE) {
        s_text_theme = FM_TEXT_THEME_DARK;
    } else if (s_text_theme == FM_TEXT_THEME_DARK) {
        s_text_theme = FM_TEXT_THEME_LIGHT;
    } else {
        s_text_theme = FM_TEXT_THEME_EYE;
    }
    fm_text_apply_theme();
}

static void fm_show_text_viewer(const char *path, const char *name)
{
    char *text;
    char status[96];
    size_t text_len = 0;
    size_t raw_len = 0;
    bool truncated = false;
    const char *encoding = "unknown";
    lv_obj_t *btn;

    if (!path || !path[0]) {
        return;
    }

    ESP_LOGI(TAG, "text viewer open: path=%s", path);
    fm_log_heap("text open before");
    text = fm_read_text_file(path, FM_TEXT_MAX_BYTES, &text_len, &truncated, &raw_len, &encoding);
    if (!text) {
        ESP_LOGW(TAG, "text viewer read failed: path=%s errno=%d", path, errno);
        fm_show_info_dialog("打开失败", "无法读取此文本文件。");
        return;
    }
    ESP_LOGI(TAG,
             "text file read: path=%s len=%u limit=%u truncated=%d",
             path,
             (unsigned)raw_len,
             (unsigned)FM_TEXT_MAX_BYTES,
             truncated ? 1 : 0);
    ESP_LOGI(TAG, "text file encoding: %s", encoding ? encoding : "unknown");

    fm_close_viewer();
    fm_close_text_viewer();
    s_text_raw_len = raw_len;
    strlcpy(s_text_encoding, encoding ? encoding : "unknown", sizeof(s_text_encoding));

    s_text_viewer = lv_obj_create(lv_scr_act());
    lv_obj_null_on_delete(&s_text_viewer);
    lv_obj_set_size(s_text_viewer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_text_viewer, 0, 0);
    lv_obj_set_style_bg_color(s_text_viewer, THEME_BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_text_viewer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_text_viewer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_text_viewer, 0, 0);
    lv_obj_set_style_pad_all(s_text_viewer, 0, 0);
    lv_obj_clear_flag(s_text_viewer, LV_OBJ_FLAG_SCROLLABLE);

    s_text_bar = lv_obj_create(s_text_viewer);
    lv_obj_null_on_delete(&s_text_bar);
    fm_style_surface(s_text_bar, THEME_BG_PANEL, THEME_BG_PANEL, 0);
    lv_obj_set_style_pad_left(s_text_bar, FM_PAD, 0);
    lv_obj_set_style_pad_right(s_text_bar, FM_PAD, 0);
    lv_obj_set_style_pad_column(s_text_bar, 10, 0);
    lv_obj_set_flex_flow(s_text_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_text_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_text_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_text_title = fm_label(s_text_bar, name ? name : "文本", THEME_TEXT_PRIMARY);
    lv_obj_null_on_delete(&s_text_title);
    lv_obj_set_flex_grow(s_text_title, 1);
    lv_label_set_long_mode(s_text_title, LV_LABEL_LONG_DOT);

    btn = fm_button(s_text_bar, TRS("主题"), THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 90, 44);
    s_text_theme_label = lv_obj_get_child(btn, 0);
    lv_obj_null_on_delete(&s_text_theme_label);
    lv_obj_add_event_cb(btn, fm_on_text_theme, LV_EVENT_CLICKED, NULL);

    btn = fm_button(s_text_bar, LV_SYMBOL_CLOSE, THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, 52, 44);
    lv_obj_add_event_cb(btn, fm_on_text_close, LV_EVENT_CLICKED, NULL);

    s_text_textarea = lv_textarea_create(s_text_viewer);
    lv_obj_null_on_delete(&s_text_textarea);
    lv_obj_set_style_radius(s_text_textarea, 0, 0);
    lv_obj_set_style_border_width(s_text_textarea, 0, 0);
    lv_obj_set_style_pad_left(s_text_textarea, FM_PAD, 0);
    lv_obj_set_style_pad_right(s_text_textarea, FM_PAD, 0);
    lv_obj_set_style_pad_top(s_text_textarea, FM_PAD, 0);
    lv_obj_set_style_pad_bottom(s_text_textarea, FM_PAD, 0);
    lv_obj_set_style_text_font(s_text_textarea, &APP_FONT, 0);
    lv_textarea_set_one_line(s_text_textarea, false);
    lv_textarea_set_cursor_click_pos(s_text_textarea, false);
    lv_textarea_set_max_length(s_text_textarea, text_len + 1);
    ESP_LOGI(TAG, "text textarea label set begin: len=%u", (unsigned)text_len);
    lv_textarea_clear_selection(s_text_textarea);
    lv_label_set_text(lv_textarea_get_label(s_text_textarea), text);
    lv_textarea_set_cursor_pos(s_text_textarea, 0);
    ESP_LOGI(TAG, "text textarea label set done");
    lv_obj_set_scrollbar_mode(s_text_textarea, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_remove_flag(s_text_textarea, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(s_text_textarea, fm_on_text_scroll, LV_EVENT_SCROLL, NULL);

    snprintf(status,
             sizeof(status),
             TRS("字节 %u   编码 %s   进度 0%%"),
             (unsigned)raw_len,
             s_text_encoding);
    s_text_status = fm_label(s_text_viewer, status, THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_text_status);
    lv_obj_set_style_bg_opa(s_text_status, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_text_status, 1, 0);
    lv_obj_set_style_pad_left(s_text_status, FM_PAD, 0);
    lv_obj_set_style_pad_right(s_text_status, FM_PAD, 0);
    lv_obj_set_style_text_align(s_text_status, LV_TEXT_ALIGN_CENTER, 0);

    fm_text_apply_theme();
    fm_text_layout();
    fm_text_update_status();
    free(text);
    fm_log_heap("text open after");
    ESP_LOGI(TAG, "text viewer open done: path=%s", path);
}
