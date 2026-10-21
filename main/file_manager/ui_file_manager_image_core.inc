static int32_t fm_point_distance(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    int32_t dx = (int32_t)x0 - (int32_t)x1;
    int32_t dy = (int32_t)y0 - (int32_t)y1;
    return (int32_t)sqrt((double)dx * dx + (double)dy * dy);
}

static uint32_t fm_clamp_image_scale(uint32_t scale)
{
    if (scale < FM_IMAGE_SCALE_MIN) {
        return FM_IMAGE_SCALE_MIN;
    }
    if (scale > FM_IMAGE_SCALE_MAX) {
        return FM_IMAGE_SCALE_MAX;
    }
    return scale;
}

static uint32_t fm_fit_scale_for_image(uint32_t image_w, uint32_t image_h, int32_t area_w, int32_t area_h)
{
    uint32_t scale_w;
    uint32_t scale_h;
    uint32_t scale;

    if (image_w == 0 || image_h == 0 || area_w <= 0 || area_h <= 0) {
        return 256;
    }

    scale_w = (uint32_t)(((uint64_t)area_w * 256U) / image_w);
    scale_h = (uint32_t)(((uint64_t)area_h * 256U) / image_h);
    scale = scale_w < scale_h ? scale_w : scale_h;
    return fm_clamp_image_scale(scale ? scale : 1);
}

static void fm_log_heap(const char *where)
{
    ESP_LOGI(TAG,
             "%s heap: free=%lu min=%lu dma_free=%lu dma_largest=%lu internal_free=%lu internal_largest=%lu psram_free=%lu psram_largest=%lu",
             where ? where : "fm",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    mem_diag_checkpoint(where);
}

static void fm_release_viewer_image(void)
{
    if (s_viewer_gif) {
        lv_obj_del(s_viewer_gif);
        s_viewer_gif = NULL;
    }
    s_viewer_gif_paused_for_controls = false;
    if (s_viewer_image.dsc.data) {
        ESP_LOGI(TAG,
                 "viewer release image: dsc=%lux%lu data=%p size=%lu",
                 (unsigned long)s_viewer_image.dsc.header.w,
                 (unsigned long)s_viewer_image.dsc.header.h,
                 s_viewer_image.dsc.data,
                 (unsigned long)s_viewer_image.dsc.data_size);
    }
    if (s_viewer_img) {
        lv_obj_add_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
        if (s_viewer_image.dsc.data) {
            lv_image_cache_drop(&s_viewer_image.dsc);
        }
    } else if (s_viewer_image.dsc.data) {
        lv_image_cache_drop(&s_viewer_image.dsc);
    }
    file_manager_media_release_image(&s_viewer_image);
}

static lv_obj_t *fm_viewer_display_obj(void)
{
    return s_viewer_gif ? s_viewer_gif : s_viewer_img;
}

static void fm_viewer_pause_gif_for_controls(void)
{
    if (!s_viewer_gif || s_viewer_gif_paused_for_controls) {
        return;
    }
    lv_gif_pause(s_viewer_gif);
    s_viewer_gif_paused_for_controls = true;
}

static void fm_viewer_resume_gif_after_controls(void)
{
    if (!s_viewer_gif || !s_viewer_gif_paused_for_controls) {
        return;
    }
    lv_gif_resume(s_viewer_gif);
    s_viewer_gif_paused_for_controls = false;
}

static bool fm_viewer_touch_is_active(void)
{
    esp_lcd_touch_handle_t tp = bsp_display_get_touch_handle();
    esp_lcd_touch_point_data_t point = {0};
    uint8_t point_count = 0;

    if (!tp) {
        return false;
    }
    return esp_lcd_touch_read_data(tp) == ESP_OK &&
           esp_lcd_touch_get_data(tp, &point, &point_count, 1) == ESP_OK &&
           point_count > 0;
}

static bool fm_viewer_gif_should_skip_frame(lv_obj_t *obj, void *user_data)
{
    (void)obj;
    (void)user_data;

    if (!s_viewer || !s_viewer_gif) {
        return false;
    }
    if (s_viewer_gif_paused_for_controls) {
        return true;
    }
    if (!fm_viewer_touch_is_active()) {
        return false;
    }
    s_viewer_controls_last_touch = lv_tick_get();
    fm_viewer_pause_gif_for_controls();
    if (!s_viewer_controls_visible) {
        fm_viewer_set_controls_visible(true);
    }
    return true;
}

static bool fm_read_gif_size(const char *path, uint32_t *out_w, uint32_t *out_h)
{
    uint8_t header[10];
    FILE *fp;

    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    if (!path) {
        return false;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    bool ok = fread(header, 1, sizeof(header), fp) == sizeof(header);
    fclose(fp);
    ok = ok && (memcmp(header, "GIF87a", 6) == 0 || memcmp(header, "GIF89a", 6) == 0);
    if (!ok) {
        return false;
    }
    uint32_t w = (uint32_t)header[6] | ((uint32_t)header[7] << 8);
    uint32_t h = (uint32_t)header[8] | ((uint32_t)header[9] << 8);
    if (w == 0 || h == 0) {
        return false;
    }
    if (out_w) {
        *out_w = w;
    }
    if (out_h) {
        *out_h = h;
    }
    return true;
}

static bool fm_gif_size_allowed(uint32_t w, uint32_t h)
{
    return w > 0 && h > 0 &&
           w <= FM_GIF_MAX_W &&
           h <= FM_GIF_MAX_H &&
           (uint64_t)w * h <= FM_GIF_MAX_PIXELS;
}

static bool fm_find_static_preview_for_gif(const char *gif_path, char *out, size_t out_len)
{
    static const char *const preview_exts[] = {"bmp", "png", "jpg", "jpeg"};
    char base[FM_PATH_MAX];
    char *dot;
    struct stat st;

    if (!gif_path || !out || out_len == 0) {
        return false;
    }
    strlcpy(base, gif_path, sizeof(base));
    dot = strrchr(base, '.');
    if (!dot) {
        return false;
    }
    *dot = '\0';
    for (size_t i = 0; i < sizeof(preview_exts) / sizeof(preview_exts[0]); i++) {
        if (!fm_snprintf_ok(snprintf(out, out_len, "%s.%s", base, preview_exts[i]), out_len)) {
            continue;
        }
        if (strcmp(out, gif_path) != 0 && stat(out, &st) == 0 && S_ISREG(st.st_mode)) {
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

static uint32_t fm_current_image_width(void)
{
    if (s_viewer_image.source_w) {
        return s_viewer_image.source_w;
    }
    if (s_viewer_image.dsc.header.w) {
        return s_viewer_image.dsc.header.w;
    }
    lv_obj_t *display = fm_viewer_display_obj();
    if (display) {
        lv_image_header_t header;
        const void *src = lv_image_get_src(display);
        if (src && lv_image_decoder_get_info(src, &header) == LV_RESULT_OK) {
            return header.w;
        }
    }
    return 0;
}

static uint32_t fm_current_image_height(void)
{
    if (s_viewer_image.source_h) {
        return s_viewer_image.source_h;
    }
    if (s_viewer_image.dsc.header.h) {
        return s_viewer_image.dsc.header.h;
    }
    lv_obj_t *display = fm_viewer_display_obj();
    if (display) {
        lv_image_header_t header;
        const void *src = lv_image_get_src(display);
        if (src && lv_image_decoder_get_info(src, &header) == LV_RESULT_OK) {
            return header.h;
        }
    }
    return 0;
}

static void fm_center_viewer_image(void)
{
    lv_obj_t *display = fm_viewer_display_obj();
    if (!display || !s_viewer_canvas) {
        return;
    }
    lv_obj_update_layout(s_viewer_canvas);
    lv_obj_center(display);
}

static void fm_viewer_style_overlay_button(lv_obj_t *btn, lv_opa_t opa, lv_coord_t radius)
{
    if (!btn) {
        return;
    }
    lv_obj_set_style_bg_opa(btn, opa, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_radius(btn, radius, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }
}

static void fm_viewer_realign_prev_button(void)
{
    if (!s_viewer_prev_btn) {
        return;
    }
    lv_obj_update_layout(s_viewer);
    lv_coord_t viewer_h = s_viewer ? lv_obj_get_height(s_viewer) : lv_display_get_vertical_resolution(NULL);
    lv_coord_t btn_h = lv_obj_get_height(s_viewer_prev_btn);
    lv_coord_t default_y = (viewer_h - btn_h) / 2;
    if (default_y < 18) {
        default_y = 18;
    }
    if (!s_viewer_exif_panel || !s_viewer_exif_has_content ||
        lv_obj_has_flag(s_viewer_exif_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_pos(s_viewer_prev_btn, 18, default_y);
        return;
    }
    lv_obj_update_layout(s_viewer_exif_panel);
    lv_coord_t exif_top = lv_obj_get_y(s_viewer_exif_panel);
    lv_obj_set_pos(s_viewer_prev_btn, 18, default_y);
    if (default_y + btn_h + 20 <= exif_top) {
        return;
    }
    lv_coord_t target_y = exif_top - btn_h - 20;
    if (target_y < 18) {
        target_y = 18;
    }
    lv_obj_set_pos(s_viewer_prev_btn, 18, target_y);
    ESP_LOGI(TAG,
             "viewer prev avoid exif: viewer_h=%ld exif_top=%ld btn_h=%ld default_y=%ld target_y=%ld",
             (long)viewer_h,
             (long)exif_top,
             (long)btn_h,
             (long)default_y,
             (long)target_y);
}

static void fm_viewer_set_controls_visible(bool visible)
{
    lv_obj_t *controls[] = {
        s_viewer_info_panel,
        s_viewer_exif_top_panel,
        s_viewer_exif_panel,
        s_viewer_prev_btn,
        s_viewer_next_btn,
        s_viewer_rotate_btn,
        s_viewer_close_btn,
    };

    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        if (!controls[i]) {
            continue;
        }
        if (controls[i] == s_viewer_exif_top_panel && !s_viewer_exif_title_has_content) {
            lv_obj_add_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (controls[i] == s_viewer_exif_panel && !s_viewer_exif_has_content) {
            lv_obj_add_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (visible) {
            lv_obj_remove_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(controls[i]);
        } else {
            lv_obj_add_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (visible) {
        fm_viewer_pause_gif_for_controls();
        fm_viewer_realign_prev_button();
    } else {
        fm_viewer_resume_gif_after_controls();
    }
    s_viewer_controls_visible = visible;
}

static void fm_viewer_touch_wake_controls(void)
{
    s_viewer_controls_last_touch = lv_tick_get();
    fm_viewer_pause_gif_for_controls();
    if (!s_viewer_controls_visible) {
        fm_viewer_set_controls_visible(true);
    }
}

static void fm_viewer_set_info_compact(bool compact)
{
    if (!s_viewer_info_panel) {
        return;
    }
    if (compact) {
        lv_obj_set_size(s_viewer_info_panel, LV_SIZE_CONTENT, 48);
        lv_obj_set_style_pad_left(s_viewer_info_panel, 22, 0);
        lv_obj_set_style_pad_right(s_viewer_info_panel, 22, 0);
        if (s_viewer_status) {
            lv_obj_set_width(s_viewer_status, LV_SIZE_CONTENT);
        }
    } else {
        lv_obj_set_size(s_viewer_info_panel, LV_PCT(86), 58);
        lv_obj_set_style_pad_left(s_viewer_info_panel, 18, 0);
        lv_obj_set_style_pad_right(s_viewer_info_panel, 18, 0);
        if (s_viewer_status) {
            lv_obj_set_width(s_viewer_status, LV_PCT(100));
        }
    }
    lv_obj_align(s_viewer_info_panel, LV_ALIGN_BOTTOM_MID, 0, -18);
}

static void fm_viewer_show_loading_status(void)
{
    if (s_viewer_status) {
        fm_viewer_set_info_compact(true);
        lv_label_set_long_mode(s_viewer_status, LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_viewer_status, TRS("正在加载图片..."));
        lv_obj_set_style_text_color(s_viewer_status, THEME_WARNING_LIGHT, 0);
        lv_obj_center(s_viewer_status);
    }
    fm_viewer_touch_wake_controls();
    lv_refr_now(NULL);
}

static void fm_viewer_resize_exif_detail_panel(void)
{
    if (!s_viewer_exif_panel || !s_viewer_exif_label) {
        return;
    }
    lv_coord_t viewer_w = s_viewer ? lv_obj_get_width(s_viewer) : lv_display_get_horizontal_resolution(NULL);
    lv_coord_t max_w = viewer_w - 36;
    if (max_w < 360) {
        max_w = viewer_w > 36 ? viewer_w - 36 : 320;
    }
    if (max_w > 1040) {
        max_w = 1040;
    }
    lv_obj_set_width(s_viewer_exif_label, LV_SIZE_CONTENT);
    lv_obj_set_width(s_viewer_exif_panel, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_viewer_exif_panel);
    if (lv_obj_get_width(s_viewer_exif_panel) > max_w) {
        lv_obj_set_width(s_viewer_exif_panel, max_w);
        lv_obj_set_width(s_viewer_exif_label, max_w - 28);
    }
    lv_obj_align(s_viewer_exif_panel, LV_ALIGN_BOTTOM_LEFT, 18, -86);
    lv_obj_update_layout(s_viewer_exif_panel);
}

static void fm_viewer_resize_exif_title_panel(void)
{
    if (!s_viewer_exif_top_panel || !s_viewer_exif_top_label) {
        return;
    }
    lv_coord_t viewer_w = s_viewer ? lv_obj_get_width(s_viewer) : lv_display_get_horizontal_resolution(NULL);
    lv_coord_t max_w = viewer_w - 284;
    if (max_w > 620) {
        max_w = 620;
    }
    if (max_w < 180) {
        max_w = 180;
    }
    lv_obj_set_width(s_viewer_exif_top_label, LV_SIZE_CONTENT);
    lv_obj_set_width(s_viewer_exif_top_panel, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_viewer_exif_top_panel);
    if (lv_obj_get_width(s_viewer_exif_top_panel) > max_w) {
        lv_obj_set_width(s_viewer_exif_top_panel, max_w);
        lv_obj_set_width(s_viewer_exif_top_label, max_w - 36);
    }
    lv_obj_align(s_viewer_exif_top_panel, LV_ALIGN_TOP_LEFT, 18, 18);
}

static void fm_viewer_update_exif_panel(const char *path)
{
    if (!s_viewer_exif_panel || !s_viewer_exif_label ||
        !s_viewer_exif_top_panel || !s_viewer_exif_top_label) {
        return;
    }
    if (!app_settings_get_image_exif_enabled()) {
        s_viewer_exif_title_has_content = false;
        s_viewer_exif_has_content = false;
        lv_obj_add_flag(s_viewer_exif_top_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_viewer_exif_panel, LV_OBJ_FLAG_HIDDEN);
        fm_viewer_realign_prev_button();
        return;
    }

    file_manager_exif_info_t info;
    char lines[512];
    esp_err_t err = file_manager_exif_read(path, &info);
    bool has_lines = err == ESP_OK && file_manager_exif_format_lines(&info, lines, sizeof(lines));
    bool has_title = err == ESP_OK && info.title[0] != '\0';
    s_viewer_exif_title_has_content = has_title;
    s_viewer_exif_has_content = has_lines;

    if (has_title) {
        lv_label_set_text(s_viewer_exif_top_label, info.title);
        lv_obj_remove_flag(s_viewer_exif_top_panel, LV_OBJ_FLAG_HIDDEN);
        fm_viewer_resize_exif_title_panel();
        lv_obj_move_foreground(s_viewer_exif_top_panel);
    } else {
        lv_obj_add_flag(s_viewer_exif_top_panel, LV_OBJ_FLAG_HIDDEN);
    }

    if (!has_lines) {
        s_viewer_exif_has_content = false;
        lv_obj_add_flag(s_viewer_exif_panel, LV_OBJ_FLAG_HIDDEN);
        fm_viewer_realign_prev_button();
        if (!has_title) {
            ESP_LOGI(TAG, "viewer exif unavailable: %s err=%s", path ? path : "", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "viewer exif title loaded without detail lines: %s title=%s", path ? path : "", info.title);
        }
        return;
    }
    lv_obj_remove_flag(s_viewer_exif_panel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_viewer_exif_label, lines);
    fm_viewer_resize_exif_detail_panel();
    lv_obj_move_foreground(s_viewer_exif_panel);
    lv_obj_update_layout(s_viewer_exif_panel);
    fm_viewer_realign_prev_button();
    ESP_LOGI(TAG, "viewer exif loaded: %s title=%s detail=%d", path, has_title ? info.title : "", has_lines);
}

static void fm_update_viewer_image(void)
{
    lv_obj_t *display = fm_viewer_display_obj();
    if (!display) {
        return;
    }
    lv_image_set_scale(display, s_image_scale);
    lv_image_set_rotation(display, s_image_rotation);
    fm_center_viewer_image();
    if (s_viewer_status) {
        char text[96];
        fm_viewer_set_info_compact(false);
        lv_label_set_long_mode(s_viewer_status, LV_LABEL_LONG_DOT);
        if (s_viewer_image_count > 1) {
            snprintf(text, sizeof(text), "%u/%u  %.1fx",
                     (unsigned)(s_viewer_image_pos + 1),
                     (unsigned)s_viewer_image_count,
                     (double)s_image_scale / 256.0);
        } else {
            snprintf(text, sizeof(text), "%.1fx", (double)s_image_scale / 256.0);
        }
        lv_label_set_text(s_viewer_status, text);
        {
            char info[224];
            const char *name = s_active_name[0] ? s_active_name : TRS("图片");
            size_t total = s_viewer_image_count ? s_viewer_image_count : 1;
            size_t pos = s_viewer_image_count ? (s_viewer_image_pos + 1) : 1;

            snprintf(info, sizeof(info), "%s   %u/%u   %.1fx",
                     name,
                     (unsigned)pos,
                     (unsigned)total,
                     (double)s_image_scale / 256.0);
            lv_label_set_text(s_viewer_status, info);
            lv_obj_set_style_text_color(s_viewer_status, lv_color_white(), 0);
        }
    }
}

static void fm_on_viewer_close(lv_event_t *e)
{
    (void)e;
    fm_viewer_touch_wake_controls();
    fm_close_viewer();
}

static void fm_apply_fit_scale(void)
{
    uint32_t image_w = fm_current_image_width();
    uint32_t image_h = fm_current_image_height();
    int32_t area_w = s_viewer_canvas ? lv_obj_get_width(s_viewer_canvas) : lv_display_get_horizontal_resolution(NULL);
    int32_t area_h = s_viewer_canvas ? lv_obj_get_height(s_viewer_canvas)
                                     : lv_display_get_vertical_resolution(NULL);
    int32_t fallback_w = lv_display_get_horizontal_resolution(NULL);
    int32_t fallback_h = lv_display_get_vertical_resolution(NULL);

    if (area_w <= 0 || area_h <= 0) {
        if (s_viewer) {
            lv_obj_update_layout(s_viewer);
        }
        if (s_viewer_canvas) {
            lv_obj_update_layout(s_viewer_canvas);
            area_w = lv_obj_get_width(s_viewer_canvas);
            area_h = lv_obj_get_height(s_viewer_canvas);
        }
    }
    if (area_w <= 0) {
        area_w = fallback_w > 0 ? fallback_w : 1280;
    }
    if (area_h <= 0) {
        area_h = fallback_h > 0 ? fallback_h : 720;
    }

    s_image_fit_scale = fm_fit_scale_for_image(image_w, image_h, area_w, area_h);
    if (s_viewer_gif && s_image_fit_scale > 256) {
        s_image_fit_scale = 256;
    }
    s_image_scale = s_image_fit_scale;
    ESP_LOGI(TAG,
             "image fit scale: img=%lux%lu area=%ldx%ld scale=%lu (%.2fx)",
             (unsigned long)image_w,
             (unsigned long)image_h,
             (long)area_w,
             (long)area_h,
             (unsigned long)s_image_scale,
             (double)s_image_scale / 256.0);
    fm_update_viewer_image();
}
