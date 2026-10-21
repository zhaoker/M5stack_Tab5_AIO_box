static bool fm_collect_viewer_images(const char *path)
{
    s_viewer_image_count = 0;
    s_viewer_image_pos = 0;

    if (!path || !path[0]) {
        return false;
    }
    for (size_t i = 0; i < s_entry_count && s_viewer_image_count < FM_MAX_ENTRIES; i++) {
        if (!s_entries[i].is_dir && s_entries[i].type == FM_TYPE_IMAGE) {
            s_viewer_image_indices[s_viewer_image_count] = i;
            if (strcmp(s_entries[i].name, s_active_name) == 0) {
                s_viewer_image_pos = s_viewer_image_count;
            }
            s_viewer_image_count++;
        }
    }
    return s_viewer_image_count > 0;
}

static esp_err_t fm_viewer_load_path(const char *path, const char *name, char *backend_status, size_t status_len)
{
    char lv_path[FM_PATH_MAX + 4];
    esp_err_t image_backend;
    bool is_gif = fm_has_single_ext(path, "gif");

    if (!path || !path[0] || !s_viewer_img) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "viewer load image: path=%s name=%s pos=%u/%u",
             path,
             name ? name : "",
             (unsigned)(s_viewer_image_pos + 1),
             (unsigned)s_viewer_image_count);
    fm_viewer_show_loading_status();
    fm_log_heap("viewer load before");
    fm_release_viewer_image();
    fm_log_heap("viewer after release previous");
    backend_status[0] = '\0';
    if (is_gif) {
        image_backend = ESP_ERR_NOT_SUPPORTED;
        snprintf(backend_status, status_len, "%s", "GIF preview uses LVGL");
    } else {
        image_backend = file_manager_media_load_image(path,
                                                      &s_viewer_image,
                                                      backend_status,
                                                      status_len);
    }
    ESP_LOGI(TAG,
             "viewer image backend done: err=%s dsc=%lux%lu data=%p size=%lu status=%s",
             esp_err_to_name(image_backend),
             (unsigned long)s_viewer_image.dsc.header.w,
             (unsigned long)s_viewer_image.dsc.header.h,
             s_viewer_image.dsc.data,
             (unsigned long)s_viewer_image.dsc.data_size,
             backend_status ? backend_status : "");
    snprintf(lv_path, sizeof(lv_path), "A:%s", path);
    ESP_LOGI(TAG, "viewer set LVGL image src begin");
    if (is_gif) {
        uint32_t gif_w = 0;
        uint32_t gif_h = 0;

        if (!fm_read_gif_size(path, &gif_w, &gif_h)) {
            ESP_LOGW(TAG, "viewer gif header invalid: %s", path);
            snprintf(backend_status, status_len, "%s", "Invalid GIF header");
            if (s_viewer_status) {
                lv_label_set_text(s_viewer_status, backend_status);
                lv_obj_set_style_text_color(s_viewer_status, THEME_DANGER_LIGHT, 0);
            }
            fm_log_heap("viewer gif invalid");
            return ESP_ERR_INVALID_RESPONSE;
        }
        ESP_LOGI(TAG,
                 "viewer gif info: %s %lux%lu pixels=%lu limit=%ux%u/%u",
                 path,
                 (unsigned long)gif_w,
                 (unsigned long)gif_h,
                 (unsigned long)((uint64_t)gif_w * gif_h),
                 (unsigned)FM_GIF_MAX_W,
                 (unsigned)FM_GIF_MAX_H,
                 (unsigned)FM_GIF_MAX_PIXELS);
        if (!fm_gif_size_allowed(gif_w, gif_h)) {
            char preview_path[FM_PATH_MAX];
            ESP_LOGW(TAG,
                     "viewer gif too large, skip animation decode: %s %lux%lu",
                     path,
                     (unsigned long)gif_w,
                     (unsigned long)gif_h);
            if (fm_find_static_preview_for_gif(path, preview_path, sizeof(preview_path))) {
                ESP_LOGI(TAG, "viewer gif static preview: %s -> %s", path, preview_path);
                esp_err_t preview_err = file_manager_media_load_image(preview_path,
                                                                      &s_viewer_image,
                                                                      backend_status,
                                                                      status_len);
                if (preview_err == ESP_OK) {
                    lv_obj_remove_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
                    lv_image_cache_drop(&s_viewer_image.dsc);
                    lv_image_set_src(s_viewer_img, (const void *)&s_viewer_image.dsc);
                    snprintf(backend_status,
                             status_len,
                             "GIF static preview: %lux%lu",
                             (unsigned long)gif_w,
                             (unsigned long)gif_h);
                    image_backend = ESP_OK;
                    goto viewer_image_src_set;
                }
                ESP_LOGW(TAG,
                         "viewer gif static preview failed: %s err=%s",
                         preview_path,
                         esp_err_to_name(preview_err));
            }
            snprintf(backend_status,
                     status_len,
                      "GIF too large: %lux%lu, animation skipped",
                     (unsigned long)gif_w,
                     (unsigned long)gif_h);
            if (s_viewer_status) {
                lv_label_set_text(s_viewer_status, backend_status);
                lv_obj_set_style_text_color(s_viewer_status, THEME_WARNING_LIGHT, 0);
            }
            if (s_viewer_info_panel) {
                fm_viewer_set_info_compact(true);
                lv_obj_remove_flag(s_viewer_info_panel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s_viewer_info_panel);
            }
            fm_viewer_set_controls_visible(true);
            fm_log_heap("viewer gif skipped");
            return ESP_ERR_NOT_SUPPORTED;
        }
        lv_obj_add_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
        s_viewer_gif = lv_gif_create(s_viewer_canvas);
        if (!s_viewer_gif) {
            ESP_LOGW(TAG, "viewer gif create failed");
            snprintf(backend_status, status_len, "%s", "GIF create failed");
            if (s_viewer_status) {
                lv_label_set_text(s_viewer_status, backend_status);
                lv_obj_set_style_text_color(s_viewer_status, THEME_DANGER_LIGHT, 0);
            }
            fm_log_heap("viewer gif create failed");
            return ESP_ERR_NO_MEM;
        } else {
            lv_obj_null_on_delete(&s_viewer_gif);
            lv_gif_set_frame_skip_cb(s_viewer_gif, fm_viewer_gif_should_skip_frame, NULL);
            lv_gif_set_src(s_viewer_gif, (const void *)lv_path);
            if (!lv_gif_is_loaded(s_viewer_gif)) {
                ESP_LOGW(TAG, "viewer gif load failed: %s", lv_path);
                lv_obj_del(s_viewer_gif);
                s_viewer_gif = NULL;
                lv_obj_add_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
                snprintf(backend_status, status_len, "%s", "GIF decode failed");
                if (s_viewer_status) {
                    lv_label_set_text(s_viewer_status, backend_status);
                    lv_obj_set_style_text_color(s_viewer_status, THEME_DANGER_LIGHT, 0);
                }
                fm_log_heap("viewer gif load failed");
                return ESP_FAIL;
            } else {
                lv_obj_remove_flag(s_viewer_gif, LV_OBJ_FLAG_HIDDEN);
                fm_viewer_set_controls_visible(true);
                snprintf(backend_status, status_len, "%s", "GIF decode failed");
                image_backend = ESP_OK;
            }
        }
    } else if (image_backend == ESP_OK) {
        lv_obj_remove_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
        lv_image_cache_drop(&s_viewer_image.dsc);
        lv_image_set_src(s_viewer_img, (const void *)&s_viewer_image.dsc);
    } else if (image_backend == ESP_ERR_NOT_SUPPORTED) {
        lv_obj_remove_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
        lv_image_cache_drop(lv_path);
        lv_image_set_src(s_viewer_img, (const void *)lv_path);
    } else {
        ESP_LOGW(TAG, "viewer image load failed; skip LVGL fallback for safety: %s", esp_err_to_name(image_backend));
        lv_obj_add_flag(s_viewer_img, LV_OBJ_FLAG_HIDDEN);
        if (s_viewer_exif_top_panel) {
            s_viewer_exif_title_has_content = false;
            lv_obj_add_flag(s_viewer_exif_top_panel, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_viewer_exif_panel) {
            s_viewer_exif_has_content = false;
            lv_obj_add_flag(s_viewer_exif_panel, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_viewer_status) {
            lv_label_set_text_fmt(s_viewer_status, TRS("加载失败：%s"), backend_status[0] ? backend_status : esp_err_to_name(image_backend));
            lv_obj_set_style_text_color(s_viewer_status, THEME_DANGER_LIGHT, 0);
        }
        fm_log_heap("viewer load failed");
        return image_backend;
    }
viewer_image_src_set:
    ESP_LOGI(TAG, "viewer set LVGL image src done");
    if (s_viewer) {
        lv_obj_update_layout(s_viewer);
    }
    if (s_viewer_canvas) {
        lv_obj_update_layout(s_viewer_canvas);
    }
    strlcpy(s_viewer_path, path, sizeof(s_viewer_path));
    if (name) {
        strlcpy(s_active_name, name, sizeof(s_active_name));
    }
    if (s_viewer_status && backend_status[0]) {
        lv_label_set_text(s_viewer_status, backend_status);
        lv_obj_set_style_text_color(s_viewer_status, THEME_TEXT_TERTIARY, 0);
    }
    s_image_rotation = 0;
    fm_apply_fit_scale();
    fm_viewer_update_exif_panel(path);
    fm_log_heap("viewer load after");
    return image_backend;
}

static void fm_show_viewer_image_at(size_t pos)
{
    char path[FM_PATH_MAX];
    char backend_status[128];
    size_t entry_index;

    if (s_viewer_image_count == 0 || pos >= s_viewer_image_count) {
        return;
    }
    entry_index = s_viewer_image_indices[pos];
    if (entry_index >= s_entry_count) {
        return;
    }
    if (!fm_join_path(path, sizeof(path), s_current_path, s_entries[entry_index].name)) {
        return;
    }
    ESP_LOGI(TAG, "viewer switch image: %u -> %u/%u %s",
             (unsigned)(s_viewer_image_pos + 1),
             (unsigned)(pos + 1),
             (unsigned)s_viewer_image_count,
             s_entries[entry_index].name);
    s_viewer_image_pos = pos;
    (void)fm_viewer_load_path(path, s_entries[entry_index].name, backend_status, sizeof(backend_status));
}

static void fm_on_viewer_prev(lv_event_t *e)
{
    (void)e;
    fm_viewer_touch_wake_controls();
    if (s_viewer_image_count <= 1) {
        return;
    }
    if (s_viewer_image_pos == 0) {
        fm_show_viewer_image_at(s_viewer_image_count - 1);
    } else {
        fm_show_viewer_image_at(s_viewer_image_pos - 1);
    }
}

static void fm_on_viewer_next(lv_event_t *e)
{
    (void)e;
    fm_viewer_touch_wake_controls();
    if (s_viewer_image_count <= 1) {
        return;
    }
    fm_show_viewer_image_at((s_viewer_image_pos + 1) % s_viewer_image_count);
}

static void fm_on_viewer_rotate(lv_event_t *e)
{
    (void)e;
    fm_viewer_touch_wake_controls();
    s_image_rotation = (s_image_rotation + 900) % 3600;
    fm_update_viewer_image();
}

static void fm_on_viewer_control_pressed(lv_event_t *e)
{
    (void)e;
    fm_viewer_touch_wake_controls();
}

static void fm_viewer_touch_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    esp_lcd_touch_handle_t tp = bsp_display_get_touch_handle();
    esp_lcd_touch_point_data_t points[2] = {0};
    uint8_t point_count = 0;
    uint32_t now;
    bool touched = false;

    if (!tp || !s_viewer || !s_viewer_img) {
        return;
    }
    now = lv_tick_get();
    if (esp_lcd_touch_read_data(tp) == ESP_OK &&
        esp_lcd_touch_get_data(tp, points, &point_count, 2) == ESP_OK &&
        point_count > 0) {
        touched = true;
        s_viewer_controls_last_touch = now;
        fm_viewer_pause_gif_for_controls();
        if (!s_viewer_controls_visible) {
            fm_viewer_set_controls_visible(true);
        }
    } else if (s_viewer_controls_visible &&
               s_viewer_controls_last_touch != 0 &&
               lv_tick_elaps(s_viewer_controls_last_touch) >= FM_VIEWER_CONTROLS_HIDE_MS) {
        fm_viewer_set_controls_visible(false);
    } else if (s_viewer_controls_visible &&
               s_viewer_gif_paused_for_controls &&
               s_viewer_controls_last_touch != 0 &&
               lv_tick_elaps(s_viewer_controls_last_touch) >= FM_GIF_INTERACTION_RESUME_MS) {
        fm_viewer_resume_gif_after_controls();
    }

    if (!touched || point_count < 2) {
        s_pinch_active = false;
        return;
    }

    int32_t distance = fm_point_distance(points[0].x, points[0].y, points[1].x, points[1].y);
    if (distance <= 0) {
        return;
    }

    if (!s_pinch_active) {
        s_pinch_active = true;
        s_pinch_start_distance = distance;
        s_pinch_start_scale = s_image_scale;
        return;
    }

    if (s_pinch_start_distance <= 0) {
        return;
    }
    uint32_t next_scale = (uint32_t)(((uint64_t)s_pinch_start_scale * (uint32_t)distance) /
                                     (uint32_t)s_pinch_start_distance);
    next_scale = fm_clamp_image_scale(next_scale);
    if (next_scale != s_image_scale) {
        s_image_scale = next_scale;
        fm_update_viewer_image();
    }
}

