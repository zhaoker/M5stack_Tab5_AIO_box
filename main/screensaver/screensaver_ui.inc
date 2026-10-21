static void ss_release_image(void)
{
    if (s_wallpaper && s_image.dsc.data) {
        lv_image_cache_drop(&s_image.dsc);
    } else if (s_wallpaper && s_lv_image_path[0]) {
        lv_image_cache_drop(s_lv_image_path);
    }
    file_manager_media_release_image(&s_image);
    memset(&s_image, 0, sizeof(s_image));
    s_lv_image_path[0] = '\0';
    s_current_image_w = 0;
    s_current_image_h = 0;
}

typedef struct {
    uint32_t generation;
    esp_err_t err;
    char path[SS_PATH_MAX];
    char title[SS_TITLE_MAX];
    char status[128];
    file_manager_image_t image;
} screensaver_loader_result_t;

static void screensaver_release_async_result(void *data)
{
    screensaver_loader_result_t *result = (screensaver_loader_result_t *)data;

    if (!result) {
        return;
    }
    file_manager_media_release_image(&result->image);
    free(result);
}

static bool screensaver_ensure_loader_queue(void)
{
    if (!s_loader_result_queue) {
        s_loader_result_queue = xQueueCreate(1, sizeof(screensaver_loader_result_t *));
    }
    return s_loader_result_queue != NULL;
}

static void screensaver_apply_loader_result(screensaver_loader_result_t *result)
{
    bool should_start_pending = false;

    if (!result) {
        return;
    }
    if (!s_active || s_blank_mode || result->generation != s_loader_generation || !s_wallpaper) {
        s_loader_async_pending = false;
        s_loader_busy = false;
        s_loader_task = NULL;
        screensaver_release_async_result(result);
        return;
    }

    ss_set_wait_visible(false, NULL);
    if (result->err == ESP_OK) {
        file_manager_image_t old_image = s_image;
        char old_lv_path[sizeof(s_lv_image_path)];

        memcpy(old_lv_path, s_lv_image_path, sizeof(old_lv_path));
        s_image = result->image;
        memset(&result->image, 0, sizeof(result->image));
        s_lv_image_path[0] = '\0';
        s_current_image_w = s_image.source_w ? s_image.source_w : s_image.dsc.header.w;
        s_current_image_h = s_image.source_h ? s_image.source_h : s_image.dsc.header.h;
        lv_image_set_src(s_wallpaper, &s_image.dsc);
        screensaver_fit_image();
        if (old_image.dsc.data) {
            lv_image_cache_drop(&old_image.dsc);
            file_manager_media_release_image(&old_image);
        } else if (old_lv_path[0]) {
            lv_image_cache_drop(old_lv_path);
        }
        if (s_title_label) {
            lv_label_set_text(s_title_label, result->title);
            ss_resize_title_panel();
            ss_set_component_hidden(s_title_panel,
                                    APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE,
                                    false);
        }
    } else {
        ESP_LOGW(TAG,
                 "load screensaver image failed: %s err=%s status=%s",
                 result->path,
                 esp_err_to_name(result->err),
                 result->status);
        if (s_title_label) {
            lv_label_set_text(s_title_label, TRS("图片加载失败"));
            ss_resize_title_panel();
            ss_set_component_hidden(s_title_panel,
                                    APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE,
                                    false);
        }
    }

    should_start_pending = s_loader_pending_next;
    s_loader_pending_next = false;
    s_loader_async_pending = false;
    s_loader_busy = false;
    s_loader_task = NULL;
    free(result);
    screensaver_update_clock_force();
    if (should_start_pending && s_active && !s_blank_mode) {
        ss_select_next_image();
        screensaver_load_current_image(false);
    }
}

static void screensaver_poll_loader_result(void)
{
    screensaver_loader_result_t *result = NULL;

    if (!s_loader_result_queue) {
        return;
    }
    if (xQueueReceive(s_loader_result_queue, &result, 0) == pdTRUE) {
        screensaver_apply_loader_result(result);
    }
}

static void screensaver_loader_task(void *arg)
{
    screensaver_loader_result_t *result = (screensaver_loader_result_t *)arg;
    bool result_queued = false;

    if (!result) {
        s_loader_task_running = false;
        s_loader_async_pending = false;
        s_loader_busy = false;
        s_loader_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (!s_loader_cancel) {
        result->err = file_manager_media_load_image_for_area(result->path,
                                                             APP_SETTINGS_SCREENSAVER_SCREEN_W,
                                                             APP_SETTINGS_SCREENSAVER_SCREEN_H,
                                                             &result->image,
                                                             result->status,
                                                             sizeof(result->status));
        if (result->err == ESP_OK) {
            ss_title_for_image(result->path, result->title, sizeof(result->title));
        } else {
            result->title[0] = '\0';
        }
    } else {
        result->err = ESP_ERR_INVALID_STATE;
    }

    if (s_loader_cancel || result->generation != s_loader_generation) {
        screensaver_release_async_result(result);
        s_loader_task = NULL;
        s_loader_task_running = false;
        s_loader_async_pending = false;
        s_loader_busy = false;
    } else {
        if (!s_loader_cancel &&
            result->generation == s_loader_generation &&
            s_loader_result_queue &&
            xQueueSend(s_loader_result_queue, &result, 0) == pdTRUE) {
            s_loader_task = NULL;
            s_loader_task_running = false;
            s_loader_async_pending = true;
            result_queued = true;
        }
        if (!result_queued) {
            screensaver_release_async_result(result);
            s_loader_task = NULL;
            s_loader_task_running = false;
            s_loader_async_pending = false;
            s_loader_busy = false;
        }
    }
    vTaskDelete(NULL);
}

static bool screensaver_start_image_load(bool show_wait)
{
    screensaver_loader_result_t *result;

    if (s_image_count == 0 || !s_wallpaper) {
        return false;
    }
    if (!screensaver_ensure_loader_queue()) {
        ESP_LOGW(TAG, "screensaver loader queue alloc failed");
        return false;
    }
    if (s_loader_busy) {
        s_loader_pending_next = true;
        return true;
    }
    result = (screensaver_loader_result_t *)calloc(1, sizeof(*result));
    if (!result) {
        ESP_LOGW(TAG, "screensaver loader result alloc failed");
        return false;
    }
    result->generation = s_loader_generation;
    strlcpy(result->path, s_images[s_image_pos].path, sizeof(result->path));
    strlcpy(s_loader_path, result->path, sizeof(s_loader_path));
    if (show_wait) {
        ss_set_wait_visible(true, "进入屏保中...");
    }
    s_loader_cancel = false;
    s_loader_busy = true;
    s_loader_task_running = true;
    s_loader_async_pending = false;
    if (xTaskCreate(screensaver_loader_task,
                    "ss_img_load",
                    SS_LOADER_STACK,
                    result,
                    SS_LOADER_PRIORITY,
                    &s_loader_task) != pdPASS) {
        s_loader_task = NULL;
        s_loader_task_running = false;
        s_loader_async_pending = false;
        s_loader_busy = false;
        screensaver_release_async_result(result);
        ESP_LOGW(TAG, "create screensaver image loader task failed");
        if (show_wait) {
            ss_set_wait_visible(false, NULL);
        }
        return false;
    }
    return true;
}

static void screensaver_cancel_loader(bool wait)
{
    s_loader_generation++;
    s_loader_pending_next = false;
    s_loader_cancel = true;
    while (wait && s_loader_task_running) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_loader_result_queue) {
        screensaver_loader_result_t *queued = NULL;

        while (xQueueReceive(s_loader_result_queue, &queued, 0) == pdTRUE) {
            screensaver_release_async_result(queued);
        }
        s_loader_async_pending = false;
        if (!s_loader_task_running) {
            vQueueDelete(s_loader_result_queue);
            s_loader_result_queue = NULL;
        }
    }
    s_loader_task = NULL;
    s_loader_busy = s_loader_async_pending;
    s_loader_cancel = false;
    s_loader_path[0] = '\0';
}

static void ss_set_wait_visible(bool visible, const char *message)
{
    if (!s_overlay) {
        return;
    }
    if (visible && !s_wait_overlay) {
        lv_obj_t *panel;

        s_wait_overlay = lv_obj_create(s_overlay);
        lv_obj_null_on_delete(&s_wait_overlay);
        lv_obj_set_size(s_wait_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(s_wait_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_wait_overlay, LV_OPA_60, 0);
        lv_obj_set_style_border_opa(s_wait_overlay, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(s_wait_overlay, LV_OBJ_FLAG_SCROLLABLE);

        panel = lv_obj_create(s_wait_overlay);
        lv_obj_set_size(panel, 360, 120);
        lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
        lv_obj_set_style_border_color(panel, lv_color_white(), 0);
        lv_obj_set_style_border_opa(panel, LV_OPA_30, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_radius(panel, 14, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(panel);

        s_wait_label = lv_label_create(panel);
        lv_obj_null_on_delete(&s_wait_label);
        lv_obj_set_style_text_font(s_wait_label, &APP_FONT, 0);
        lv_obj_set_style_text_color(s_wait_label, lv_color_white(), 0);
        lv_obj_center(s_wait_label);
    }
    if (s_wait_label) {
        lv_label_set_text(s_wait_label, TRS(message ? message : "进入屏保中..."));
    }
    if (s_wait_overlay) {
        if (visible) {
            lv_obj_remove_flag(s_wait_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_wait_overlay);
            lv_refr_now(NULL);
        } else {
            lv_obj_add_flag(s_wait_overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static int ss_component_width(lv_obj_t *obj)
{
    int w = obj ? lv_obj_get_width(obj) : 0;
    return w > 0 ? w : 80;
}

static int ss_component_height(lv_obj_t *obj)
{
    int h = obj ? lv_obj_get_height(obj) : 0;
    return h > 0 ? h : 40;
}

static int ss_clamp_component_x(lv_obj_t *obj, int x)
{
    (void)obj;
    return x;
}

static int ss_clamp_component_y(lv_obj_t *obj, int y)
{
    (void)obj;
    return y;
}

static void ss_position_component(lv_obj_t *obj, app_screensaver_component_t component)
{
    if (!obj) {
        return;
    }
    lv_obj_set_pos(obj,
                   ss_clamp_component_x(obj, app_settings_get_screensaver_component_x(component)),
                   ss_clamp_component_y(obj, app_settings_get_screensaver_component_y(component)));
}

static const lv_font_t *ss_font_for_component(app_screensaver_component_t component);

static bool ss_component_visible(app_screensaver_component_t component)
{
    return app_settings_get_screensaver_component_visible(component);
}

static lv_color_t ss_component_color(app_screensaver_component_t component)
{
    return lv_color_hex(app_settings_get_screensaver_component_color(component) & 0xFFFFFF);
}

static bool ss_component_uses_shadow(app_screensaver_component_t component)
{
    return component == APP_SETTINGS_SCREENSAVER_COMPONENT_TIME ||
           component == APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND ||
           component == APP_SETTINGS_SCREENSAVER_COMPONENT_DATE ||
           component == APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR;
}

static void ss_apply_text_shadow(lv_obj_t *shadow,
                                 lv_obj_t *label,
                                 app_screensaver_component_t component,
                                 int width,
                                 int height)
{
    int length;

    if (!shadow || !label || !ss_component_uses_shadow(component)) {
        return;
    }
    length = app_settings_get_screensaver_text_shadow_enabled()
                 ? app_settings_get_screensaver_text_shadow_length()
                 : 0;
    lv_obj_set_size(shadow, width, height);
    lv_obj_set_style_text_font(shadow, ss_font_for_component(component), 0);
    lv_obj_set_style_text_color(shadow, lv_color_black(), 0);
    lv_obj_set_style_text_opa(shadow, length > 0 ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_align(shadow,
                                component == APP_SETTINGS_SCREENSAVER_COMPONENT_TIME ||
                                        component == APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND
                                    ? LV_TEXT_ALIGN_LEFT
                                    : LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_set_pos(shadow, length, length);
    if (length > 0) {
        lv_obj_remove_flag(shadow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(shadow, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(label);
}

static void ss_set_component_hidden(lv_obj_t *obj,
                                    app_screensaver_component_t component,
                                    bool hidden_by_content)
{
    if (!obj) {
        return;
    }
    if (hidden_by_content || !ss_component_visible(component)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ss_resize_title_panel(void)
{
    lv_coord_t max_w = APP_SETTINGS_SCREENSAVER_SCREEN_W - 2 * SS_COMPONENT_MARGIN;

    if (!s_title_panel || !s_title_label) {
        return;
    }
    lv_obj_set_width(s_title_label, LV_SIZE_CONTENT);
    lv_obj_set_width(s_title_panel, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_title_panel);
    if (lv_obj_get_width(s_title_panel) > max_w) {
        lv_obj_set_width(s_title_panel, max_w);
        lv_obj_set_width(s_title_label, max_w - 28);
    }
    ss_position_component(s_title_panel, APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE);
}

static void ss_component_drag_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    app_screensaver_component_t component =
        (app_screensaver_component_t)(uintptr_t)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p;

    if (!obj || !indev) {
        return;
    }
    if (code == LV_EVENT_LONG_PRESSED) {
        lv_indev_get_point(indev, &s_drag_start_point);
        s_drag_start_x = lv_obj_get_x(obj);
        s_drag_start_y = lv_obj_get_y(obj);
        s_drag_obj = obj;
        s_drag_component = component;
        s_dragging_component = true;
        s_drag_click_suppressed = true;
        return;
    }
    if (code == LV_EVENT_PRESSING && s_dragging_component && s_drag_obj == obj) {
        lv_indev_get_point(indev, &p);
        lv_obj_set_pos(obj,
                       ss_clamp_component_x(obj, s_drag_start_x + p.x - s_drag_start_point.x),
                       ss_clamp_component_y(obj, s_drag_start_y + p.y - s_drag_start_point.y));
        return;
    }
    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
        s_dragging_component && s_drag_obj == obj) {
        s_dragging_component = false;
        (void)app_settings_set_screensaver_component_position(s_drag_component,
                                                              lv_obj_get_x(obj),
                                                              lv_obj_get_y(obj));
        s_drag_obj = NULL;
    }
}

static void ss_component_click_event(lv_event_t *e)
{
    (void)e;
    if (s_drag_click_suppressed) {
        s_drag_click_suppressed = false;
        return;
    }
    screensaver_service_notify_activity();
}

static const lv_font_t *ss_font_for_size(int font_size)
{
    switch (font_size) {
    case APP_SETTINGS_SCREENSAVER_FONT_20:
        return &MI_font_20;
    case APP_SETTINGS_SCREENSAVER_FONT_40:
        return &MI_font_40_time;
    case APP_SETTINGS_SCREENSAVER_FONT_60:
        return &MI_font_60_time;
    case APP_SETTINGS_SCREENSAVER_FONT_120:
        return &MI_font_120_time;
    case APP_SETTINGS_SCREENSAVER_FONT_160:
        return &MI_font_160_time;
    case APP_SETTINGS_SCREENSAVER_FONT_200:
        return &MI_font_200_time;
    case APP_SETTINGS_SCREENSAVER_FONT_80:
    default:
        return &MI_font_80_time;
    }
}

static const lv_font_t *ss_font_for_component(app_screensaver_component_t component)
{
    if (component == APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE) {
        return &MI_font_20;
    }
    return ss_font_for_size(app_settings_get_screensaver_component_font_size(component));
}

static int ss_font_unit_for_component(app_screensaver_component_t component)
{
    const lv_font_t *font = ss_font_for_component(component);

    if (font && font->line_height > 0) {
        return font->line_height;
    }
    return app_settings_get_screensaver_component_font_size(component);
}

static int ss_limit_wrap_width(int width)
{
    return width;
}

static int ss_limit_wrap_height(int height)
{
    return height;
}

static void ss_measure_text(const char *text, const lv_font_t *font, int *out_w, int *out_h)
{
    lv_point_t size = {0};

    lv_text_get_size(&size,
                     text && text[0] ? text : " ",
                     font ? font : &APP_FONT,
                     0,
                     0,
                     LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    if (out_w) {
        *out_w = size.x;
    }
    if (out_h) {
        *out_h = size.y;
    }
}

static void ss_resize_text_wrap(lv_obj_t *wrap,
                                lv_obj_t *label,
                                lv_obj_t *shadow,
                                app_screensaver_component_t component,
                                const char *text,
                                int min_w)
{
    const lv_font_t *font = ss_font_for_component(component);
    int text_w;
    int text_h;
    int pad_x = component == APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND ? 18 : 30;
    int pad_y = 28;
    int wrap_w;
    int wrap_h;

    if (!wrap || !label) {
        return;
    }
    ss_measure_text(text, font, &text_w, &text_h);
    wrap_w = ss_limit_wrap_width(text_w + pad_x);
    wrap_h = ss_limit_wrap_height(text_h + pad_y);
    if (wrap_w < min_w) {
        wrap_w = min_w;
    }
    lv_obj_set_size(wrap, wrap_w, wrap_h);
    lv_obj_set_size(label, wrap_w, wrap_h);
    ss_apply_text_shadow(shadow, label, component, wrap_w, wrap_h);
    lv_obj_set_style_text_align(label,
                                component == APP_SETTINGS_SCREENSAVER_COMPONENT_TIME ||
                                        component == APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND
                                    ? LV_TEXT_ALIGN_LEFT
                                    : LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    ss_position_component(wrap, component);
}

static int ss_clock_wrap_width(app_screensaver_component_t component)
{
    int size = ss_font_unit_for_component(component);
    int width;

    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
        width = size * 4 + 18;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        width = size * 2 + 18;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        width = size * 12 + 28;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        width = size * 8 + 28;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
    default:
        width = size * 10 + 28;
        break;
    }
    return ss_limit_wrap_width(width);
}

static int ss_clock_wrap_height(app_screensaver_component_t component)
{
    int size = ss_font_unit_for_component(component);
    return ss_limit_wrap_height(size + 18);
}

static lv_obj_t *ss_make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font ? font : &APP_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static lv_obj_t *ss_make_shadow_label(lv_obj_t *parent,
                                      const char *text,
                                      const lv_font_t *font)
{
    lv_obj_t *label = ss_make_label(parent, text, font, lv_color_black());

    lv_obj_set_style_text_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

static lv_obj_t *ss_create_clock_wrap(app_screensaver_component_t component, int w, int h)
{
    lv_obj_t *wrap = lv_obj_create(s_overlay);
    lv_obj_set_size(wrap, w, h);
    lv_obj_set_style_bg_color(wrap, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(wrap, 8, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_ext_click_area(wrap, 12);
    lv_obj_add_event_cb(wrap,
                        ss_component_drag_event,
                        LV_EVENT_LONG_PRESSED,
                        (void *)(uintptr_t)component);
    lv_obj_add_event_cb(wrap,
                        ss_component_drag_event,
                        LV_EVENT_PRESSING,
                        (void *)(uintptr_t)component);
    lv_obj_add_event_cb(wrap,
                        ss_component_drag_event,
                        LV_EVENT_RELEASED,
                        (void *)(uintptr_t)component);
    lv_obj_add_event_cb(wrap,
                        ss_component_drag_event,
                        LV_EVENT_PRESS_LOST,
                        (void *)(uintptr_t)component);
    lv_obj_add_event_cb(wrap, ss_component_click_event, LV_EVENT_CLICKED, NULL);
    return wrap;
}

static void ss_create_clock_components(void)
{
    s_time_wrap = ss_create_clock_wrap(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME,
                                       ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME),
                                       ss_clock_wrap_height(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME));
    lv_obj_null_on_delete(&s_time_wrap);
    s_time_shadow_label =
        ss_make_shadow_label(s_time_wrap,
                             "--:--",
                             ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME));
    s_time_label = ss_make_label(s_time_wrap,
                                 "--:--",
                                 ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME),
                                 ss_component_color(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME));
    lv_obj_null_on_delete(&s_time_shadow_label);
    lv_obj_set_pos(s_time_label, 0, 0);
    ss_position_component(s_time_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_TIME);
    ss_set_component_hidden(s_time_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_TIME, false);

    s_second_wrap = ss_create_clock_wrap(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND,
                                         ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND),
                                         ss_clock_wrap_height(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND));
    lv_obj_null_on_delete(&s_second_wrap);
    s_second_shadow_label =
        ss_make_shadow_label(s_second_wrap,
                             "--",
                             ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND));
    s_second_label = ss_make_label(s_second_wrap,
                                   "--",
                                   ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND),
                                   ss_component_color(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND));
    lv_obj_null_on_delete(&s_second_shadow_label);
    lv_obj_set_pos(s_second_label, 0, 0);
    ss_position_component(s_second_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND);
    ss_set_component_hidden(s_second_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND, false);

    s_date_wrap = ss_create_clock_wrap(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE,
                                       ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE),
                                       ss_clock_wrap_height(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE));
    lv_obj_null_on_delete(&s_date_wrap);
    s_date_shadow_label =
        ss_make_shadow_label(s_date_wrap,
                             "",
                             ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE));
    s_date_label = ss_make_label(s_date_wrap,
                                 "",
                                 ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE),
                                 ss_component_color(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE));
    lv_obj_null_on_delete(&s_date_shadow_label);
    lv_obj_set_width(s_date_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_date_label);
    ss_position_component(s_date_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_DATE);
    ss_set_component_hidden(s_date_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_DATE, false);

    s_lunar_wrap = ss_create_clock_wrap(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR,
                                        ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR),
                                        ss_clock_wrap_height(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR));
    lv_obj_null_on_delete(&s_lunar_wrap);
    s_lunar_shadow_label =
        ss_make_shadow_label(s_lunar_wrap,
                             "",
                             ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR));
    s_lunar_label = ss_make_label(s_lunar_wrap,
                                  "",
                                  ss_font_for_component(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR),
                                  ss_component_color(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR));
    lv_obj_null_on_delete(&s_lunar_shadow_label);
    lv_obj_set_width(s_lunar_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_lunar_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_lunar_label);
    ss_position_component(s_lunar_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR);
    ss_set_component_hidden(s_lunar_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR, false);
}

static void ss_create_title_panel(void)
{
    s_title_panel = lv_obj_create(s_overlay);
    lv_obj_null_on_delete(&s_title_panel);
    lv_obj_set_size(s_title_panel,
                    LV_SIZE_CONTENT,
                    ss_clock_wrap_height(APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE));
    lv_obj_set_style_bg_color(s_title_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_title_panel, LV_OPA_30, 0);
    lv_obj_set_style_border_opa(s_title_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_title_panel, 14, 0);
    lv_obj_set_style_pad_left(s_title_panel, 14, 0);
    lv_obj_set_style_pad_right(s_title_panel, 14, 0);
    lv_obj_set_style_pad_top(s_title_panel, 0, 0);
    lv_obj_set_style_pad_bottom(s_title_panel, 0, 0);
    lv_obj_clear_flag(s_title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_title_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_title_panel, 12);
    lv_obj_add_event_cb(s_title_panel,
                        ss_component_drag_event,
                        LV_EVENT_LONG_PRESSED,
                        (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE);
    lv_obj_add_event_cb(s_title_panel,
                        ss_component_drag_event,
                        LV_EVENT_PRESSING,
                        (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE);
    lv_obj_add_event_cb(s_title_panel,
                        ss_component_drag_event,
                        LV_EVENT_RELEASED,
                        (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE);
    lv_obj_add_event_cb(s_title_panel,
                        ss_component_drag_event,
                        LV_EVENT_PRESS_LOST,
                        (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE);
    lv_obj_add_event_cb(s_title_panel, ss_component_click_event, LV_EVENT_CLICKED, NULL);

    s_title_label = ss_make_label(
        s_title_panel,
        "",
        &MI_font_20,
        ss_component_color(APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE));
    lv_obj_set_width(s_title_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_title_label);
    ss_resize_title_panel();
    ss_set_component_hidden(s_title_panel, APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE, false);
}

static void screensaver_update_clock(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    char text[96];
    char lunar[96];
    static const char *const zh_weekdays[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
    };
    static const char *const en_weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };

    if (!s_active || !s_time_label || !s_second_label || !s_date_label ||
        !s_lunar_label || !s_time_shadow_label || !s_second_shadow_label ||
        !s_date_shadow_label || !s_lunar_shadow_label ||
        now <= 0 || !localtime_r(&now, &tm_now)) {
        return;
    }
    if (now == s_clock_last_drawn) {
        return;
    }
    s_clock_last_drawn = now;
    snprintf(text, sizeof(text), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    lv_label_set_text(s_time_label, text);
    lv_label_set_text(s_time_shadow_label, text);
    if (ss_component_visible(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME)) {
        ss_resize_text_wrap(s_time_wrap,
                            s_time_label,
                            s_time_shadow_label,
                            APP_SETTINGS_SCREENSAVER_COMPONENT_TIME,
                            text,
                            ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME));
    }
    ss_set_component_hidden(s_time_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_TIME, false);
    snprintf(text, sizeof(text), "%02d", tm_now.tm_sec);
    lv_label_set_text(s_second_label, text);
    lv_label_set_text(s_second_shadow_label, text);
    if (ss_component_visible(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND)) {
        ss_resize_text_wrap(s_second_wrap,
                            s_second_label,
                            s_second_shadow_label,
                            APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND,
                            text,
                            ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND));
    }
    ss_set_component_hidden(s_second_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND, false);

    if (app_i18n_get_language() == APP_LANGUAGE_EN) {
        snprintf(text,
                 sizeof(text),
                 "%04d-%02d-%02d %s",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday,
                 en_weekdays[tm_now.tm_wday]);
        lv_label_set_text(s_date_label, text);
        lv_label_set_text(s_date_shadow_label, text);
        if (ss_component_visible(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE)) {
            ss_resize_text_wrap(s_date_wrap,
                                s_date_label,
                                s_date_shadow_label,
                                APP_SETTINGS_SCREENSAVER_COMPONENT_DATE,
                                text,
                                ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE));
        }
        ss_set_component_hidden(s_date_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_DATE, false);
        lv_label_set_text(s_lunar_label, "");
        lv_label_set_text(s_lunar_shadow_label, "");
        ss_set_component_hidden(s_lunar_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR, true);
    } else {
        snprintf(text,
                 sizeof(text),
                 "%04d年%02d月%02d日 %s",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday,
                 zh_weekdays[tm_now.tm_wday]);
        lv_label_set_text(s_date_label, text);
        lv_label_set_text(s_date_shadow_label, text);
        if (ss_component_visible(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE)) {
            ss_resize_text_wrap(s_date_wrap,
                                s_date_label,
                                s_date_shadow_label,
                                APP_SETTINGS_SCREENSAVER_COMPONENT_DATE,
                                text,
                                ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_DATE));
        }
        ss_set_component_hidden(s_date_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_DATE, false);
        if (ss_lunar_format(&tm_now, lunar, sizeof(lunar))) {
            lv_label_set_text(s_lunar_label, lunar);
            lv_label_set_text(s_lunar_shadow_label, lunar);
            if (ss_component_visible(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR)) {
                ss_resize_text_wrap(s_lunar_wrap,
                                    s_lunar_label,
                                    s_lunar_shadow_label,
                                    APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR,
                                    lunar,
                                    ss_clock_wrap_width(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR));
            }
            ss_set_component_hidden(s_lunar_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR, false);
        } else {
            lv_label_set_text(s_lunar_label, "");
            lv_label_set_text(s_lunar_shadow_label, "");
            ss_set_component_hidden(s_lunar_wrap, APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR, true);
        }
    }
}

static void screensaver_update_clock_force(void)
{
    s_clock_last_drawn = 0;
    screensaver_update_clock();
}

static void ss_clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    screensaver_update_clock();
    screensaver_poll_loader_result();
}

static void ss_switch_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_active || s_blank_mode || s_image_count == 0) {
        return;
    }
    if (s_loader_busy) {
        s_loader_pending_next = true;
        return;
    }
    ss_select_next_image();
    screensaver_load_current_image(false);
}

static void screensaver_fit_image(void)
{
    uint32_t w = s_current_image_w;
    uint32_t h = s_current_image_h;
    uint32_t scale_w;
    uint32_t scale_h;
    uint32_t scale;

    if (!s_wallpaper || w == 0 || h == 0) {
        return;
    }
    scale_w = (APP_SETTINGS_SCREENSAVER_SCREEN_W * 256U) / w;
    scale_h = (APP_SETTINGS_SCREENSAVER_SCREEN_H * 256U) / h;
    scale = scale_w > scale_h ? scale_w : scale_h;
    if (scale == 0) {
        scale = 256;
    }
    lv_image_set_scale(s_wallpaper, scale);
    lv_obj_center(s_wallpaper);
}

static void screensaver_load_current_image(bool show_wait)
{
    if (s_image_count == 0 || !s_wallpaper) {
        if (s_title_label) {
            lv_label_set_text(s_title_label, TRS("未找到屏保图片"));
            ss_resize_title_panel();
            ss_set_component_hidden(s_title_panel,
                                    APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE,
                                    false);
        }
        return;
    }
    if (!screensaver_start_image_load(show_wait)) {
        if (show_wait) {
            ss_set_wait_visible(false, NULL);
        }
        if (s_title_label) {
            lv_label_set_text(s_title_label, TRS("图片加载失败"));
            ss_resize_title_panel();
            ss_set_component_hidden(s_title_panel,
                                    APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE,
                                    false);
        }
    }
}

static void screensaver_create_overlay(void)
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_null_on_delete(&s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, ss_activity_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_overlay, ss_activity_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_overlay, ss_activity_event_cb, LV_EVENT_GESTURE, NULL);
}

static void screensaver_enter(bool manual)
{
    app_screensaver_mode_t mode = app_settings_get_screensaver_mode();
    uint32_t switch_ms;

    if (s_active) {
        return;
    }
    if (!manual && mode == APP_SETTINGS_SCREENSAVER_MODE_OFF) {
        return;
    }
    if (manual && mode == APP_SETTINGS_SCREENSAVER_MODE_OFF) {
        mode = APP_SETTINGS_SCREENSAVER_MODE_WALLPAPER;
    }

    s_active = true;
    s_blank_mode = mode == APP_SETTINGS_SCREENSAVER_MODE_BLANK;
    screensaver_create_overlay();

    if (s_blank_mode) {
        (void)bsp_display_backlight_off();
        return;
    }

    (void)app_settings_apply_brightness();
    ss_set_wait_visible(true, "进入屏保中...");

    s_wallpaper = lv_image_create(s_overlay);
    lv_obj_null_on_delete(&s_wallpaper);
    lv_image_set_antialias(s_wallpaper, true);
    lv_obj_center(s_wallpaper);

    ss_create_title_panel();
    ss_create_clock_components();
    screensaver_update_clock_force();
    s_clock_timer = lv_timer_create(ss_clock_timer_cb, 250, NULL);

    ss_collect_images();
    screensaver_load_current_image(true);
    switch_ms = (uint32_t)app_settings_get_screensaver_switch_minutes() * 60U * 1000U;
    s_switch_timer = lv_timer_create(ss_switch_timer_cb, switch_ms, NULL);
}

static void screensaver_exit(void)
{
    if (!s_active) {
        return;
    }
    if (s_switch_timer) {
        lv_timer_del(s_switch_timer);
        s_switch_timer = NULL;
    }
    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
        s_clock_timer = NULL;
    }
    screensaver_cancel_loader(true);
    ss_release_image();
    lv_image_cache_drop(NULL);
    if (s_overlay) {
        lv_obj_del(s_overlay);
    }
    s_overlay = NULL;
    s_wallpaper = NULL;
    s_title_panel = NULL;
    s_title_label = NULL;
    s_time_wrap = NULL;
    s_second_wrap = NULL;
    s_date_wrap = NULL;
    s_lunar_wrap = NULL;
    s_time_label = NULL;
    s_second_label = NULL;
    s_date_label = NULL;
    s_lunar_label = NULL;
    s_time_shadow_label = NULL;
    s_second_shadow_label = NULL;
    s_date_shadow_label = NULL;
    s_lunar_shadow_label = NULL;
    s_wait_overlay = NULL;
    s_wait_label = NULL;
    s_clock_last_drawn = 0;
    s_active = false;
    s_blank_mode = false;
    s_dragging_component = false;
    s_drag_click_suppressed = false;
    s_drag_obj = NULL;
    s_last_activity_tick = lv_tick_get();
    (void)app_settings_apply_brightness();
}

static void screensaver_timer_cb(lv_timer_t *timer)
{
    app_screensaver_mode_t mode;
    uint32_t idle_ms;
    (void)timer;

    if ((!s_active || s_blank_mode) && screensaver_touch_pressed()) {
        screensaver_service_notify_activity();
        return;
    }
    if (s_active && screensaver_ssh_block_active()) {
        screensaver_service_notify_activity();
        return;
    }
    mode = app_settings_get_screensaver_mode();
    if (s_active || mode == APP_SETTINGS_SCREENSAVER_MODE_OFF) {
        return;
    }
    if (screensaver_ssh_block_active()) {
        s_last_activity_tick = lv_tick_get();
        return;
    }
    idle_ms = (uint32_t)app_settings_get_screensaver_idle_minutes() * 60U * 1000U;
    if (lv_tick_elaps(s_last_activity_tick) >= idle_ms) {
        screensaver_enter(false);
    }
}

static void ss_activity_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_dragging_component) {
        return;
    }
    screensaver_service_notify_activity();
}
