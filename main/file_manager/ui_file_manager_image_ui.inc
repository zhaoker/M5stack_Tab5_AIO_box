static void fm_show_image_viewer(const char *path)
{
    char backend_status[128];
    lv_obj_t *bar;
    lv_obj_t *title;
    lv_obj_t *btn;

    if (!path || !path[0]) {
        return;
    }

    ESP_LOGI(TAG, "show image viewer: %s", path);
    fm_log_heap("viewer open before");
    fm_close_text_viewer();
    fm_close_viewer();
    s_image_scale = 256;
    s_image_fit_scale = 256;
    s_image_rotation = 0;
    s_pinch_active = false;
    backend_status[0] = '\0';
    strlcpy(s_viewer_path, path, sizeof(s_viewer_path));
    (void)fm_collect_viewer_images(path);
    ESP_LOGI(TAG, "viewer collected images: count=%u pos=%u active=%s",
             (unsigned)s_viewer_image_count,
             (unsigned)(s_viewer_image_pos + 1),
             s_active_name);

    s_viewer = lv_obj_create(lv_scr_act());
    lv_obj_null_on_delete(&s_viewer);
    lv_obj_set_size(s_viewer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_viewer, 0, 0);
    lv_obj_set_style_bg_color(s_viewer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_viewer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_viewer, 0, 0);
    lv_obj_set_style_pad_all(s_viewer, 0, 0);
    lv_obj_clear_flag(s_viewer, LV_OBJ_FLAG_SCROLLABLE);

    bar = lv_obj_create(s_viewer);
    lv_obj_set_size(bar, LV_PCT(100), FM_TOPBAR_H);
    fm_style_surface(bar, THEME_BG_PANEL, THEME_BG_PANEL, 0);
    lv_obj_set_style_pad_left(bar, FM_PAD, 0);
    lv_obj_set_style_pad_right(bar, FM_PAD, 0);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    title = fm_label(bar, LV_SYMBOL_IMAGE " 图片查看器", THEME_TEXT_PRIMARY);
    lv_obj_set_flex_grow(title, 1);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    btn = fm_button(bar, LV_SYMBOL_LEFT, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 54, 44);
    if (s_viewer_image_count <= 1) {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_event_cb(btn, fm_on_viewer_prev, LV_EVENT_CLICKED, NULL);
    }

    btn = fm_button(bar, LV_SYMBOL_RIGHT, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 54, 44);
    if (s_viewer_image_count <= 1) {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_event_cb(btn, fm_on_viewer_next, LV_EVENT_CLICKED, NULL);
    }

    btn = fm_button(bar, LV_SYMBOL_CLOSE, THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, 52, 44);
    lv_obj_add_event_cb(btn, fm_on_viewer_close, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);

    s_viewer_canvas = lv_obj_create(s_viewer);
    lv_obj_null_on_delete(&s_viewer_canvas);
    lv_obj_set_size(s_viewer_canvas,
                    lv_display_get_horizontal_resolution(NULL),
                    lv_display_get_vertical_resolution(NULL));
    lv_obj_set_pos(s_viewer_canvas, 0, 0);
    lv_obj_set_style_bg_color(s_viewer_canvas, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewer_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_viewer_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_viewer_canvas, 0, 0);
    lv_obj_set_style_pad_all(s_viewer_canvas, 0, 0);
    lv_obj_set_scroll_dir(s_viewer_canvas, LV_DIR_HOR | LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_viewer_canvas, LV_SCROLLBAR_MODE_AUTO);

    s_viewer_img = lv_image_create(s_viewer_canvas);
    lv_obj_null_on_delete(&s_viewer_img);
    lv_image_set_antialias(s_viewer_img, true);

    lv_obj_update_layout(s_viewer);
    s_viewer_prev_btn = fm_button(s_viewer, LV_SYMBOL_LEFT, lv_color_black(), lv_color_white());
    lv_obj_null_on_delete(&s_viewer_prev_btn);
    lv_obj_set_size(s_viewer_prev_btn, 82, 104);
    lv_obj_set_pos(s_viewer_prev_btn, 18,
                   (lv_obj_get_height(s_viewer) - 104) / 2);
    fm_viewer_style_overlay_button(s_viewer_prev_btn, LV_OPA_60, 18);
    lv_obj_add_event_cb(s_viewer_prev_btn, fm_on_viewer_control_pressed, LV_EVENT_PRESSED, NULL);
    if (s_viewer_image_count <= 1) {
        lv_obj_add_state(s_viewer_prev_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(s_viewer_prev_btn, LV_OPA_30, 0);
    } else {
        lv_obj_add_event_cb(s_viewer_prev_btn, fm_on_viewer_prev, LV_EVENT_CLICKED, NULL);
    }

    s_viewer_next_btn = fm_button(s_viewer, LV_SYMBOL_RIGHT, lv_color_black(), lv_color_white());
    lv_obj_null_on_delete(&s_viewer_next_btn);
    lv_obj_set_size(s_viewer_next_btn, 82, 104);
    lv_obj_align(s_viewer_next_btn, LV_ALIGN_RIGHT_MID, -18, 0);
    fm_viewer_style_overlay_button(s_viewer_next_btn, LV_OPA_60, 18);
    lv_obj_add_event_cb(s_viewer_next_btn, fm_on_viewer_control_pressed, LV_EVENT_PRESSED, NULL);
    if (s_viewer_image_count <= 1) {
        lv_obj_add_state(s_viewer_next_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(s_viewer_next_btn, LV_OPA_30, 0);
    } else {
        lv_obj_add_event_cb(s_viewer_next_btn, fm_on_viewer_next, LV_EVENT_CLICKED, NULL);
    }

    s_viewer_close_btn = fm_button(s_viewer, LV_SYMBOL_CLOSE, THEME_DANGER, THEME_DANGER);
    lv_obj_null_on_delete(&s_viewer_close_btn);
    lv_obj_set_size(s_viewer_close_btn, 104, 88);
    lv_obj_align(s_viewer_close_btn, LV_ALIGN_TOP_RIGHT, -18, 18);
    fm_viewer_style_overlay_button(s_viewer_close_btn, LV_OPA_80, 18);
    lv_obj_add_event_cb(s_viewer_close_btn, fm_on_viewer_control_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_viewer_close_btn, fm_on_viewer_close, LV_EVENT_CLICKED, NULL);

    s_viewer_rotate_btn = fm_button(s_viewer, LV_SYMBOL_LOOP, lv_color_black(), lv_color_white());
    lv_obj_null_on_delete(&s_viewer_rotate_btn);
    lv_obj_set_size(s_viewer_rotate_btn, 76, 76);
    lv_obj_align_to(s_viewer_rotate_btn, s_viewer_close_btn, LV_ALIGN_OUT_LEFT_MID, -14, 0);
    fm_viewer_style_overlay_button(s_viewer_rotate_btn, LV_OPA_60, 16);
    lv_obj_add_event_cb(s_viewer_rotate_btn, fm_on_viewer_control_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_viewer_rotate_btn, fm_on_viewer_rotate, LV_EVENT_CLICKED, NULL);

    s_viewer_exif_top_panel = lv_obj_create(s_viewer);
    lv_obj_null_on_delete(&s_viewer_exif_top_panel);
    lv_obj_set_size(s_viewer_exif_top_panel, LV_SIZE_CONTENT, 48);
    lv_obj_align(s_viewer_exif_top_panel, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_bg_color(s_viewer_exif_top_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewer_exif_top_panel, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_viewer_exif_top_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_viewer_exif_top_panel, 16, 0);
    lv_obj_set_style_shadow_width(s_viewer_exif_top_panel, 0, 0);
    lv_obj_set_style_pad_left(s_viewer_exif_top_panel, 18, 0);
    lv_obj_set_style_pad_right(s_viewer_exif_top_panel, 18, 0);
    lv_obj_set_style_pad_top(s_viewer_exif_top_panel, 0, 0);
    lv_obj_set_style_pad_bottom(s_viewer_exif_top_panel, 0, 0);
    lv_obj_clear_flag(s_viewer_exif_top_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_viewer_exif_top_label = fm_label(s_viewer_exif_top_panel, "", lv_color_white());
    lv_obj_null_on_delete(&s_viewer_exif_top_label);
    lv_obj_set_width(s_viewer_exif_top_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(s_viewer_exif_top_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_viewer_exif_top_label, &APP_FONT, 0);
    lv_obj_center(s_viewer_exif_top_label);
    lv_obj_add_flag(s_viewer_exif_top_panel, LV_OBJ_FLAG_HIDDEN);

    s_viewer_exif_panel = lv_obj_create(s_viewer);
    lv_obj_null_on_delete(&s_viewer_exif_panel);
    lv_obj_set_size(s_viewer_exif_panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_viewer_exif_panel, LV_ALIGN_BOTTOM_LEFT, 18, -86);
    lv_obj_set_style_bg_color(s_viewer_exif_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewer_exif_panel, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_viewer_exif_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_viewer_exif_panel, 14, 0);
    lv_obj_set_style_shadow_width(s_viewer_exif_panel, 0, 0);
    lv_obj_set_style_pad_all(s_viewer_exif_panel, 14, 0);
    lv_obj_clear_flag(s_viewer_exif_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_viewer_exif_label = fm_label(s_viewer_exif_panel, "", lv_color_white());
    lv_obj_null_on_delete(&s_viewer_exif_label);
    lv_obj_set_width(s_viewer_exif_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(s_viewer_exif_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_viewer_exif_label, &APP_FONT, 0);
    lv_obj_center(s_viewer_exif_label);
    lv_obj_add_flag(s_viewer_exif_panel, LV_OBJ_FLAG_HIDDEN);

    s_viewer_info_panel = lv_obj_create(s_viewer);
    lv_obj_null_on_delete(&s_viewer_info_panel);
    lv_obj_set_size(s_viewer_info_panel, LV_SIZE_CONTENT, 48);
    lv_obj_align(s_viewer_info_panel, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(s_viewer_info_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewer_info_panel, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_viewer_info_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_viewer_info_panel, 16, 0);
    lv_obj_set_style_shadow_width(s_viewer_info_panel, 0, 0);
    lv_obj_set_style_pad_left(s_viewer_info_panel, 22, 0);
    lv_obj_set_style_pad_right(s_viewer_info_panel, 22, 0);
    lv_obj_set_style_pad_top(s_viewer_info_panel, 0, 0);
    lv_obj_set_style_pad_bottom(s_viewer_info_panel, 0, 0);
    lv_obj_clear_flag(s_viewer_info_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_viewer_status = fm_label(s_viewer_info_panel,
                               backend_status[0] ? backend_status : TRS("加载图片"),
                               lv_color_white());
    lv_obj_null_on_delete(&s_viewer_status);
    lv_obj_set_width(s_viewer_status, LV_PCT(100));
    lv_label_set_long_mode(s_viewer_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_viewer_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_viewer_status);
    lv_obj_move_foreground(s_viewer_prev_btn);
    lv_obj_move_foreground(s_viewer_next_btn);
    lv_obj_move_foreground(s_viewer_rotate_btn);
    lv_obj_move_foreground(s_viewer_close_btn);
    lv_obj_move_foreground(s_viewer_exif_top_panel);
    lv_obj_move_foreground(s_viewer_exif_panel);
    lv_obj_move_foreground(s_viewer_info_panel);
    s_viewer_controls_last_touch = lv_tick_get();
    fm_viewer_set_controls_visible(true);
    s_viewer_touch_timer = lv_timer_create(fm_viewer_touch_timer_cb, FM_VIEWER_TOUCH_MS, NULL);
    (void)fm_viewer_load_path(path, s_active_name, backend_status, sizeof(backend_status));
    ESP_LOGI(TAG, "show image viewer done");
}

static lv_color_t fm_audio_status_color(audio_player_state_t state);
static void fm_register_audio_status_cb(void);
static void fm_unregister_audio_status_cb(void);
static void fm_audio_render_playlist(const audio_player_status_t *status);

typedef enum {
    FM_AUDIO_SIDE_NOW,
    FM_AUDIO_SIDE_FAVORITES,
    FM_AUDIO_SIDE_PLAY_MODE,
    FM_AUDIO_SIDE_SETTINGS,
} fm_audio_side_action_t;

static lv_obj_t *fm_audio_icon_button(lv_obj_t *parent, const char *symbol, int32_t size, lv_color_t bg)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(btn, size, size);
    fm_style_surface(btn, bg, bg, size / 2);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    label = fm_label(btn, symbol, THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    return btn;
}

static const char *fm_audio_play_mode_icon(audio_player_play_mode_t mode)
{
    switch (mode) {
    case AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP:
        return LV_SYMBOL_LOOP "1";
    case AUDIO_PLAYER_PLAY_MODE_SHUFFLE:
        return LV_SYMBOL_SHUFFLE;
    case AUDIO_PLAYER_PLAY_MODE_LIST_LOOP:
    default:
        return LV_SYMBOL_LOOP;
    }
}

static const char *fm_audio_play_mode_text(audio_player_play_mode_t mode)
{
    switch (mode) {
    case AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP:
        return TRS("单曲循环");
    case AUDIO_PLAYER_PLAY_MODE_SHUFFLE:
        return TRS("随机播放");
    case AUDIO_PLAYER_PLAY_MODE_LIST_LOOP:
    default:
        return TRS("列表循环");
    }
}

static audio_player_play_mode_t fm_audio_next_play_mode(audio_player_play_mode_t mode)
{
    switch (mode) {
    case AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP:
        return AUDIO_PLAYER_PLAY_MODE_LIST_LOOP;
    case AUDIO_PLAYER_PLAY_MODE_LIST_LOOP:
        return AUDIO_PLAYER_PLAY_MODE_SHUFFLE;
    case AUDIO_PLAYER_PLAY_MODE_SHUFFLE:
    default:
        return AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP;
    }
}

static void fm_audio_update_play_mode_button(audio_player_play_mode_t mode)
{
    if (s_audio_mode_icon) {
        lv_label_set_text(s_audio_mode_icon, fm_audio_play_mode_icon(mode));
    }
    if (s_audio_mode_label) {
        lv_label_set_text(s_audio_mode_label, fm_audio_play_mode_text(mode));
    }
}

static void fm_close_audio_player(void)
{
    fm_audio_cancel_list_status_timer();
    fm_audio_cancel_cover_timer();
    fm_audio_release_cover();
    fm_audio_release_favorites_cache();
    if (s_audio_viewer) {
        lv_obj_del(s_audio_viewer);
    }
    fm_unregister_audio_status_cb();
    ui_audio_background_set_music_ui_visible(false);
    s_audio_viewer = NULL;
    s_audio_title = NULL;
    s_audio_artist = NULL;
    s_audio_status = NULL;
    s_audio_index = NULL;
    s_audio_play_label = NULL;
    s_audio_mode_icon = NULL;
    s_audio_mode_label = NULL;
    s_audio_progress = NULL;
    s_audio_elapsed = NULL;
    s_audio_duration = NULL;
    s_audio_volume = NULL;
    s_audio_volume_value = NULL;
    s_audio_playlist = NULL;
    s_audio_list_status = NULL;
    s_audio_playlist_count_label = NULL;
    s_audio_tab_now = NULL;
    s_audio_tab_favorites = NULL;
    s_audio_cover_img = NULL;
    s_audio_cover_band = NULL;
    s_audio_cover_icon = NULL;
    s_audio_cover_track_path[0] = '\0';
    s_audio_cover_pending_path[0] = '\0';
    s_audio_list_last_message_seq = 0;
    s_audio_showing_favorites = false;
    fm_audio_reset_playlist_cache();
}

static void fm_audio_on_minimize(lv_event_t *e)
{
    (void)e;
    fm_close_audio_player();
    fm_set_status(TRS("音乐已最小化，后台继续播放"), THEME_TEXT_SECONDARY);
}

static void fm_audio_on_close(lv_event_t *e)
{
    (void)e;
    (void)audio_player_stop();
    fm_close_audio_player();
    fm_set_status(TRS("音乐播放已停止"), THEME_TEXT_SECONDARY);
}

static void fm_audio_on_power(lv_event_t *e)
{
    (void)e;
    ui_server_list_show_power_detail();
}

static void fm_audio_on_wifi(lv_event_t *e)
{
    (void)e;
    ui_server_list_show_wifi_panel();
}

static void fm_audio_on_prev(lv_event_t *e)
{
    (void)e;
    (void)audio_player_prev();
}

static void fm_audio_on_next(lv_event_t *e)
{
    (void)e;
    (void)audio_player_next();
}

static void fm_audio_on_play_pause(lv_event_t *e)
{
    audio_player_status_t status;
    (void)e;
    if (audio_player_get_status(&status) != ESP_OK) {
        return;
    }
    if (status.state == AUDIO_PLAYER_STATE_PLAYING) {
        (void)audio_player_pause();
    } else {
        (void)audio_player_resume();
    }
}

static void fm_audio_on_volume(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    (void)audio_player_set_volume((uint8_t)value);
}

static void fm_audio_on_track(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (!fm_audio_track_click_should_play(index)) {
        return;
    }
    if (!fm_audio_prepare_playback_resources()) {
        return;
    }
    if (s_audio_showing_favorites) {
        if (index < s_audio_favorite_count && s_audio_favorite_tracks[index].path[0]) {
            (void)audio_player_play_file(s_audio_favorite_tracks[index].path);
        }
    } else {
        (void)audio_player_play_index(index);
    }
}

static void fm_audio_on_side_item(lv_event_t *e)
{
    fm_audio_side_action_t action = (fm_audio_side_action_t)(uintptr_t)lv_event_get_user_data(e);

    switch (action) {
    case FM_AUDIO_SIDE_NOW:
        s_audio_showing_favorites = false;
        fm_audio_update_tabs();
        fm_audio_mark_playlist_dirty();
        fm_audio_render_playlist(NULL);
        fm_audio_set_list_status(TRS("切到正在播放"), THEME_TEXT_SECONDARY);
        break;
    case FM_AUDIO_SIDE_FAVORITES:
        s_audio_showing_favorites = true;
        fm_audio_update_tabs();
        fm_audio_mark_playlist_dirty();
        if (fm_audio_load_favorites_cache()) {
            fm_audio_render_playlist(NULL);
            if (s_audio_favorite_removed_count > 0) {
                fm_audio_set_list_status(TRS("收藏夹已清理"), THEME_WARNING_LIGHT);
            } else {
                fm_audio_set_list_status(TRS("切到收藏夹"), THEME_TEXT_SECONDARY);
            }
        } else {
            fm_audio_render_playlist(NULL);
            fm_audio_set_list_status(TRS("收藏夹加载失败"), THEME_DANGER_LIGHT);
        }
        break;
    case FM_AUDIO_SIDE_PLAY_MODE: {
        audio_player_status_t status;
        if (audio_player_get_status(&status) == ESP_OK) {
            audio_player_play_mode_t next = fm_audio_next_play_mode(status.play_mode);
            if (audio_player_set_play_mode(next) == ESP_OK) {
                fm_audio_update_play_mode_button(next);
            }
        }
        break;
    }
    case FM_AUDIO_SIDE_SETTINGS:
        ui_settings_create(lv_scr_act());
        break;
    default:
        break;
    }
}

static lv_obj_t *fm_audio_add_side_item(lv_obj_t *sidebar,
                                        const char *icon,
                                        const char *text,
                                        bool active,
                                        fm_audio_side_action_t action)
{
    lv_obj_t *row = lv_obj_create(sidebar);
    lv_obj_t *icon_label;
    lv_obj_t *label;

    lv_obj_set_size(row, LV_PCT(100), 74);
    fm_style_surface(row, active ? lv_color_hex(0x202642) : lv_color_hex(0x141A27),
                     active ? THEME_PRIMARY : lv_color_hex(0x141A27), 0);
    lv_obj_set_style_border_width(row, active ? 0 : 0, 0);
    lv_obj_set_style_pad_left(row, 20, 0);
    lv_obj_set_style_pad_right(row, 10, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, fm_audio_on_side_item, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    icon_label = fm_label(row, icon, active ? lv_color_hex(0x8B6DFF) : THEME_TEXT_SECONDARY);
    lv_obj_set_width(icon_label, 34);
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);

    label = fm_label(row, text, active ? lv_color_hex(0x8B6DFF) : THEME_TEXT_SECONDARY);
    lv_obj_set_width(label, 130);
    lv_obj_set_height(label, 26);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return row;
}

static void fm_audio_add_play_mode_item(lv_obj_t *sidebar, audio_player_play_mode_t mode)
{
    lv_obj_t *row = fm_audio_add_side_item(sidebar,
                                           fm_audio_play_mode_icon(mode),
                                           fm_audio_play_mode_text(mode),
                                           false,
                                           FM_AUDIO_SIDE_PLAY_MODE);
    s_audio_mode_icon = lv_obj_get_child(row, 0);
    s_audio_mode_label = lv_obj_get_child(row, 1);
    lv_obj_null_on_delete(&s_audio_mode_icon);
    lv_obj_null_on_delete(&s_audio_mode_label);
}

static void fm_audio_format_time(uint32_t ms, char *out, size_t out_len)
{
    uint32_t total_seconds = (ms + 500U) / 1000U;
    uint32_t seconds = total_seconds % 60U;
    uint32_t minutes = (total_seconds / 60U) % 60U;
    uint32_t hours = total_seconds / 3600U;

    if (!out || out_len == 0) {
        return;
    }
    if (ms == 0) {
        strlcpy(out, "--:--", out_len);
    } else if (hours > 0) {
        snprintf(out, out_len, "%u:%02u:%02u", (unsigned)hours, (unsigned)minutes, (unsigned)seconds);
    } else {
        snprintf(out, out_len, "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
    }
}

static int32_t fm_audio_progress_percent(uint32_t elapsed_ms, uint32_t duration_ms)
{
    if (duration_ms == 0) {
        return 0;
    }
    if (elapsed_ms >= duration_ms) {
        return 100;
    }
    return (int32_t)(((uint64_t)elapsed_ms * 100U) / duration_ms);
}

static void fm_audio_render_playlist(const audio_player_status_t *status)
{
    audio_player_status_t local_status;
    const audio_player_status_t *view_status = status;
    size_t count;
    size_t current = (size_t)-1;
    char text[192];

    if (!s_audio_playlist) {
        return;
    }
    if (!view_status && audio_player_get_status(&local_status) == ESP_OK) {
        view_status = &local_status;
    }
    if (s_audio_showing_favorites) {
        if (!s_audio_favorite_tracks) {
            (void)fm_audio_load_favorites_cache();
        }
        count = s_audio_favorite_count;
        if (view_status && view_status->path[0]) {
            for (size_t i = 0; i < count; i++) {
                if (strcmp(s_audio_favorite_tracks[i].path, view_status->path) == 0) {
                    current = i;
                    break;
                }
            }
        }
    } else {
        count = view_status ? view_status->track_count : audio_player_get_track_count();
        current = view_status ? view_status->current_index : 0;
        (void)fm_audio_load_favorites_cache();
    }
    lv_obj_clean(s_audio_playlist);
    if (s_audio_playlist_count_label) {
        snprintf(text, sizeof(text), TRS("%u 首"), (unsigned)count);
        lv_label_set_text(s_audio_playlist_count_label, text);
    }
    if (count == 0) {
        lv_obj_t *empty = fm_label(s_audio_playlist,
                                   s_audio_showing_favorites ? TRS("收藏夹为空") : TRS("没有播放列表"),
                                   THEME_TEXT_TERTIARY);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        const audio_library_track_t *track = s_audio_showing_favorites
                                                ? (s_audio_favorite_tracks ? &s_audio_favorite_tracks[i] : NULL)
                                                : audio_player_get_track(i);
        lv_obj_t *row;
        lv_obj_t *favorite_cb;
        lv_obj_t *idx;
        lv_obj_t *title;
        lv_obj_t *duration;
        bool active = i == current;
        bool favorite_checked;

        if (!track) {
            continue;
        }
        row = lv_obj_create(s_audio_playlist);
        lv_obj_set_size(row, LV_PCT(100), 50);
        fm_style_surface(row, active ? lv_color_hex(0x242944) : lv_color_hex(0x151B27),
                         active ? lv_color_hex(0x242944) : lv_color_hex(0x1F2937), 8);
        lv_obj_set_style_pad_left(row, 12, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_set_style_pad_top(row, 0, 0);
        lv_obj_set_style_pad_bottom(row, 0, 0);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, fm_audio_on_track_press_state, LV_EVENT_PRESSED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, fm_audio_on_track_press_state, LV_EVENT_PRESSING, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, fm_audio_on_track_press_state, LV_EVENT_PRESS_LOST, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, fm_audio_on_track_press_state, LV_EVENT_DELETE, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, fm_audio_on_track, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        favorite_cb = lv_checkbox_create(row);
        lv_checkbox_set_text(favorite_cb, "");
        lv_obj_set_size(favorite_cb, 30, 30);
        lv_obj_add_flag(favorite_cb, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_style_pad_all(favorite_cb, 0, 0);
        lv_obj_set_style_bg_opa(favorite_cb, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(favorite_cb, lv_color_hex(0x293244), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(favorite_cb, lv_color_hex(0x7C5CFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(favorite_cb, lv_color_hex(0x4B5563), LV_PART_INDICATOR);
        lv_obj_set_style_border_color(favorite_cb, lv_color_hex(0x7C5CFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
        favorite_checked = s_audio_showing_favorites || fm_audio_favorites_cache_contains(track->path);
        if (favorite_checked) {
            lv_obj_add_state(favorite_cb, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(favorite_cb, fm_audio_on_favorite_checkbox, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);

        snprintf(text, sizeof(text), active ? LV_SYMBOL_PLAY " %u" : "%u", (unsigned)(i + 1));
        idx = fm_label(row, text, active ? lv_color_hex(0x8B6DFF) : THEME_TEXT_TERTIARY);
        lv_obj_set_width(idx, 44);
        lv_obj_set_height(idx, 26);

        title = fm_label(row, track->title[0] ? track->title : track->name,
                         active ? lv_color_hex(0x9C84FF) : THEME_TEXT_PRIMARY);
        lv_obj_set_width(title, 260);
        lv_obj_set_height(title, 26);
        lv_obj_set_flex_grow(title, 1);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

        fm_audio_format_time(track->duration_ms, text, sizeof(text));
        duration = fm_label(row, text, THEME_TEXT_SECONDARY);
        lv_obj_set_width(duration, 64);
        lv_obj_set_height(duration, 26);
        lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
    }
}

static void fm_audio_apply_status_to_player(const audio_player_status_t *status)
{
    char buf[256];
    char elapsed_text[16];
    char duration_text[16];
    bool playing;
    uint8_t volume;

    if (!status || !s_audio_viewer) {
        return;
    }
    playing = status->state == AUDIO_PLAYER_STATE_PLAYING;
    volume = status->volume > 100 ? 100 : status->volume;

    if (s_audio_title) {
        lv_label_set_text(s_audio_title, status->title[0] ? status->title : TRS("未选择歌曲"));
    }
    if (s_audio_artist) {
        lv_label_set_text(s_audio_artist, status->artist);
    }
    if (s_audio_status) {
        fm_audio_format_short_hint(status, buf, sizeof(buf));
        lv_label_set_text(s_audio_status, buf);
        lv_obj_set_style_text_color(s_audio_status, fm_audio_status_color(status->state), 0);
    }
    if (s_audio_index) {
        fm_audio_format_time(status->elapsed_ms, elapsed_text, sizeof(elapsed_text));
        fm_audio_format_time(status->duration_ms, duration_text, sizeof(duration_text));
        snprintf(buf, sizeof(buf), "%u/%u  %s / %s",
                 status->track_count ? (unsigned)(status->current_index + 1) : 0,
                 (unsigned)status->track_count,
                 status->elapsed_ms ? elapsed_text : "00:00",
                 duration_text);
        lv_label_set_text(s_audio_index, buf);
    }
    if (s_audio_playlist_count_label) {
        snprintf(buf,
                 sizeof(buf),
                 TRS("%u 首"),
                 (unsigned)(s_audio_showing_favorites ? s_audio_favorite_count : status->track_count));
        lv_label_set_text(s_audio_playlist_count_label, buf);
    }
    if (s_audio_play_label) {
        lv_label_set_text(s_audio_play_label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    fm_audio_update_play_mode_button(status->play_mode);
    if (s_audio_progress) {
        lv_slider_set_value(s_audio_progress,
                            fm_audio_progress_percent(status->elapsed_ms, status->duration_ms),
                            LV_ANIM_OFF);
    }
    fm_audio_format_time(status->elapsed_ms, elapsed_text, sizeof(elapsed_text));
    fm_audio_format_time(status->duration_ms, duration_text, sizeof(duration_text));
    if (s_audio_elapsed) {
        lv_label_set_text(s_audio_elapsed, status->elapsed_ms ? elapsed_text : "00:00");
    }
    if (s_audio_duration) {
        lv_label_set_text(s_audio_duration, duration_text);
    }
    if (s_audio_volume) {
        lv_slider_set_value(s_audio_volume, volume, LV_ANIM_OFF);
    }
    if (s_audio_volume_value) {
        snprintf(buf, sizeof(buf), "%u", volume);
        lv_label_set_text(s_audio_volume_value, buf);
    }
    fm_audio_update_cover(status->path);
    if (fm_audio_playlist_needs_render(status)) {
        fm_audio_render_playlist(status);
    }
}

static void fm_show_audio_player(const char *path)
{
    lv_obj_t *topbar;
    lv_obj_t *sidebar;
    lv_obj_t *now;
    lv_obj_t *cover;
    lv_obj_t *cover_band;
    lv_obj_t *controls;
    lv_obj_t *btn;
    lv_obj_t *vol_row;
    lv_obj_t *list_panel;
    lv_obj_t *header;
    lv_obj_t *columns;
    lv_obj_t *label;
    lv_obj_t *sidebar_spacer;
    audio_player_status_t status;
    power_monitor_status_t power;
    char count_text[64];
    char power_text[16] = "--";
    int32_t sw = lv_display_get_horizontal_resolution(NULL);
    int32_t sh = lv_display_get_vertical_resolution(NULL);
    int32_t left_w = 600;
    int32_t sidebar_x = 18;
    int32_t sidebar_w = 210;
    int32_t panel_gap = 12;
    int32_t now_x = sidebar_x + sidebar_w + panel_gap;
    int32_t now_w = left_w - now_x;
    int32_t content_y = 68;
    int32_t content_h = sh - content_y - 14;
    int32_t list_w = sw - left_w - 42;

    fm_close_audio_player();
    ui_audio_background_set_music_ui_visible(true);
    fm_register_audio_status_cb();
    if (sw <= 0) {
        sw = 1280;
    }
    if (sh <= 0) {
        sh = 720;
    }
    if (list_w < 420) {
        list_w = 420;
    }
    if (content_h < 560) {
        content_h = sh - content_y;
    }
    if (audio_player_get_status(&status) != ESP_OK) {
        memset(&status, 0, sizeof(status));
        status.play_mode = AUDIO_PLAYER_PLAY_MODE_LIST_LOOP;
    }

    s_audio_viewer = lv_obj_create(s_overlay ? s_overlay : lv_scr_act());
    lv_obj_null_on_delete(&s_audio_viewer);
    lv_obj_set_size(s_audio_viewer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_audio_viewer, 0, 0);
    lv_obj_set_style_bg_color(s_audio_viewer, lv_color_hex(0x0C1018), 0);
    lv_obj_set_style_bg_opa(s_audio_viewer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_audio_viewer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_audio_viewer, 0, 0);
    lv_obj_clear_flag(s_audio_viewer, LV_OBJ_FLAG_SCROLLABLE);

    topbar = lv_obj_create(s_audio_viewer);
    lv_obj_set_size(topbar, LV_PCT(100), 70);
    fm_style_surface(topbar, lv_color_hex(0x0C1018), lv_color_hex(0x0C1018), 0);
    lv_obj_set_style_pad_left(topbar, 36, 0);
    lv_obj_set_style_pad_right(topbar, 20, 0);
    lv_obj_set_style_pad_column(topbar, 14, 0);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

    label = fm_label(topbar, LV_SYMBOL_AUDIO "  音乐播放器", THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, 260);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    memset(&power, 0, sizeof(power));
    if (power_monitor_get_cached_status(&power) &&
        power.last_error == ESP_OK &&
        power.battery_present) {
        snprintf(power_text, sizeof(power_text), "%.0f", power.battery_percent);
    } else if (power.usb_c_present) {
        strlcpy(power_text, "USB", sizeof(power_text));
    }
    btn = fm_button(topbar, power_text, THEME_SUCCESS_BG, THEME_SUCCESS);
    lv_obj_set_size(btn, 76, 44);
    lv_obj_add_event_cb(btn, fm_audio_on_power, LV_EVENT_CLICKED, NULL);

    btn = fm_button(topbar,
                    wifi_manager_get_state() == WIFI_STATE_CONNECTED ? TRS(LV_SYMBOL_WIFI " 已连接") : LV_SYMBOL_WIFI,
                    wifi_manager_get_state() == WIFI_STATE_CONNECTED ? THEME_SUCCESS_BG : THEME_BG_HOVER,
                    wifi_manager_get_state() == WIFI_STATE_CONNECTED ? THEME_SUCCESS : THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 136, 44);
    lv_obj_add_event_cb(btn, fm_audio_on_wifi, LV_EVENT_CLICKED, NULL);

    btn = fm_button(topbar, LV_SYMBOL_MINUS, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 54, 44);
    lv_obj_add_event_cb(btn, fm_audio_on_minimize, LV_EVENT_CLICKED, NULL);

    btn = fm_button(topbar, LV_SYMBOL_CLOSE, THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, 70, 44);
    lv_obj_add_event_cb(btn, fm_audio_on_close, LV_EVENT_CLICKED, NULL);

    sidebar = lv_obj_create(s_audio_viewer);
    lv_obj_set_size(sidebar, sidebar_w, content_h);
    lv_obj_set_pos(sidebar, sidebar_x, content_y);
    fm_style_surface(sidebar, lv_color_hex(0x141A27), lv_color_hex(0x141A27), 8);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    s_audio_tab_now = fm_audio_add_side_item(sidebar, LV_SYMBOL_AUDIO, "正在播放", true, FM_AUDIO_SIDE_NOW);
    lv_obj_null_on_delete(&s_audio_tab_now);
    s_audio_tab_favorites = fm_audio_add_side_item(sidebar, LV_SYMBOL_SAVE, "收藏夹", false, FM_AUDIO_SIDE_FAVORITES);
    lv_obj_null_on_delete(&s_audio_tab_favorites);
    sidebar_spacer = lv_obj_create(sidebar);
    lv_obj_set_size(sidebar_spacer, LV_PCT(100), 1);
    lv_obj_set_flex_grow(sidebar_spacer, 1);
    lv_obj_set_style_bg_opa(sidebar_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(sidebar_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(sidebar_spacer, 0, 0);
    lv_obj_clear_flag(sidebar_spacer, LV_OBJ_FLAG_SCROLLABLE);
    fm_audio_add_play_mode_item(sidebar, status.play_mode);
    fm_audio_add_side_item(sidebar, LV_SYMBOL_SETTINGS, "设置", false, FM_AUDIO_SIDE_SETTINGS);
    fm_audio_update_tabs();

    now = lv_obj_create(s_audio_viewer);
    lv_obj_set_size(now, now_w, content_h);
    lv_obj_set_pos(now, now_x, content_y);
    fm_style_surface(now, lv_color_hex(0x151B27), lv_color_hex(0x202A3C), 8);
    lv_obj_set_style_pad_all(now, 0, 0);
    lv_obj_clear_flag(now, LV_OBJ_FLAG_SCROLLABLE);

    cover = lv_obj_create(now);
    lv_obj_set_size(cover, 250, 250);
    lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 28);
    fm_style_surface(cover, lv_color_hex(0x291E5E), lv_color_hex(0x291E5E), 8);
    lv_obj_set_style_bg_grad_color(cover, lv_color_hex(0xB026C9), 0);
    lv_obj_set_style_bg_grad_dir(cover, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_clip_corner(cover, true, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);

    cover_band = lv_obj_create(cover);
    s_audio_cover_band = cover_band;
    lv_obj_set_size(cover_band, 214, 76);
    lv_obj_align(cover_band, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_bg_color(cover_band, lv_color_hex(0x3C5BFF), 0);
    lv_obj_set_style_bg_grad_color(cover_band, lv_color_hex(0xE02ACB), 0);
    lv_obj_set_style_bg_grad_dir(cover_band, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(cover_band, LV_OPA_70, 0);
    lv_obj_set_style_border_opa(cover_band, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(cover_band, 8, 0);
    lv_obj_clear_flag(cover_band, LV_OBJ_FLAG_SCROLLABLE);

    label = fm_label(cover, LV_SYMBOL_AUDIO, lv_color_hex(0xE8DEFF));
    s_audio_cover_icon = label;
    lv_obj_center(label);

    s_audio_cover_img = lv_image_create(cover);
    lv_obj_null_on_delete(&s_audio_cover_img);
    lv_obj_add_flag(s_audio_cover_img, LV_OBJ_FLAG_HIDDEN);

    s_audio_title = fm_label(now, TRS("未选择歌曲"), THEME_TEXT_PRIMARY);
    lv_obj_set_width(s_audio_title, LV_PCT(82));
    lv_obj_align(s_audio_title, LV_ALIGN_TOP_MID, 0, 304);
    lv_obj_set_style_text_align(s_audio_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_audio_title, LV_LABEL_LONG_DOT);

    s_audio_artist = fm_label(now, "", THEME_TEXT_TERTIARY);
    lv_obj_set_width(s_audio_artist, LV_PCT(76));
    lv_obj_align(s_audio_artist, LV_ALIGN_TOP_MID, 0, 338);
    lv_obj_set_style_text_align(s_audio_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_audio_artist, LV_LABEL_LONG_DOT);

    s_audio_progress = lv_slider_create(now);
    lv_obj_set_size(s_audio_progress, 280, 8);
    lv_obj_align(s_audio_progress, LV_ALIGN_TOP_MID, 0, 386);
    lv_slider_set_range(s_audio_progress, 0, 100);
    lv_slider_set_value(s_audio_progress, 0, LV_ANIM_OFF);
    lv_obj_remove_style(s_audio_progress, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_audio_progress, lv_color_hex(0x293244), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_audio_progress, lv_color_hex(0x7C5CFF), LV_PART_INDICATOR);

    s_audio_elapsed = fm_label(now, "00:00", THEME_TEXT_TERTIARY);
    lv_obj_align_to(s_audio_elapsed, s_audio_progress, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    s_audio_duration = fm_label(now, "--:--", THEME_TEXT_TERTIARY);
    lv_obj_align_to(s_audio_duration, s_audio_progress, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 10);

    controls = lv_obj_create(now);
    lv_obj_set_size(controls, 280, 82);
    lv_obj_align(controls, LV_ALIGN_TOP_MID, 0, 438);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_style_pad_column(controls, 34, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    btn = fm_audio_icon_button(controls, LV_SYMBOL_PREV, 54, lv_color_hex(0x151B27));
    lv_obj_add_event_cb(btn, fm_audio_on_prev, LV_EVENT_CLICKED, NULL);
    btn = fm_audio_icon_button(controls, LV_SYMBOL_PLAY, 72, lv_color_hex(0x5636D8));
    s_audio_play_label = lv_obj_get_child(btn, 0);
    lv_obj_null_on_delete(&s_audio_play_label);
    lv_obj_add_event_cb(btn, fm_audio_on_play_pause, LV_EVENT_CLICKED, NULL);
    btn = fm_audio_icon_button(controls, LV_SYMBOL_NEXT, 54, lv_color_hex(0x151B27));
    lv_obj_add_event_cb(btn, fm_audio_on_next, LV_EVENT_CLICKED, NULL);

    vol_row = lv_obj_create(now);
    lv_obj_set_size(vol_row, 318, 66);
    lv_obj_align(vol_row, LV_ALIGN_TOP_MID, 0, 548);
    lv_obj_set_style_bg_opa(vol_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(vol_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(vol_row, 0, 0);
    lv_obj_set_style_pad_column(vol_row, 12, 0);
    lv_obj_set_flex_flow(vol_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(vol_row, LV_OBJ_FLAG_SCROLLABLE);

    fm_label(vol_row, LV_SYMBOL_VOLUME_MID, THEME_TEXT_PRIMARY);
    s_audio_volume = lv_slider_create(vol_row);
    lv_obj_set_width(s_audio_volume, 220);
    lv_obj_set_height(s_audio_volume, 24);
    lv_slider_set_range(s_audio_volume, 0, 100);
    lv_obj_set_style_bg_color(s_audio_volume, lv_color_hex(0x293244), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_audio_volume, lv_color_hex(0x7C5CFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_audio_volume, lv_color_hex(0x7C5CFF), LV_PART_KNOB);
    lv_obj_add_event_cb(s_audio_volume, fm_audio_on_volume, LV_EVENT_VALUE_CHANGED, NULL);
    s_audio_volume_value = fm_label(vol_row, "80", THEME_TEXT_TERTIARY);
    lv_obj_set_width(s_audio_volume_value, 42);

    list_panel = lv_obj_create(s_audio_viewer);
    lv_obj_set_size(list_panel, list_w, content_h);
    lv_obj_set_pos(list_panel, left_w + 14, content_y);
    fm_style_surface(list_panel, lv_color_hex(0x151B27), lv_color_hex(0x202A3C), 8);
    lv_obj_set_style_pad_all(list_panel, 18, 0);
    lv_obj_clear_flag(list_panel, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(list_panel);
    lv_obj_set_size(header, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    label = fm_label(header, TRS("播放列表"), THEME_TEXT_PRIMARY);
    lv_obj_set_pos(label, 4, 2);
    snprintf(count_text, sizeof(count_text), TRS("%u 首"), (unsigned)status.track_count);
    s_audio_playlist_count_label = fm_label(header, count_text, THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_audio_playlist_count_label);
    lv_obj_set_pos(s_audio_playlist_count_label, 116, 2);
    label = fm_label(header, LV_SYMBOL_LIST, THEME_TEXT_SECONDARY);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -8, 2);

    columns = lv_obj_create(list_panel);
    lv_obj_set_size(columns, LV_PCT(100), 28);
    lv_obj_set_pos(columns, 0, 58);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(columns, 12, 0);
    lv_obj_set_style_pad_right(columns, 12, 0);
    lv_obj_set_style_pad_top(columns, 0, 0);
    lv_obj_set_style_pad_bottom(columns, 0, 0);
    lv_obj_set_style_pad_column(columns, 12, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);

    label = fm_label(columns, "", THEME_TEXT_TERTIARY);
    lv_obj_set_width(label, 30);
    lv_obj_set_height(label, 26);
    label = fm_label(columns, "#", THEME_TEXT_TERTIARY);
    lv_obj_set_width(label, 44);
    lv_obj_set_height(label, 26);
    label = fm_label(columns, TRS("歌曲"), THEME_TEXT_TERTIARY);
    lv_obj_set_width(label, 260);
    lv_obj_set_height(label, 26);
    lv_obj_set_flex_grow(label, 1);
    label = fm_label(columns, TRS("时长"), THEME_TEXT_TERTIARY);
    lv_obj_set_width(label, 64);
    lv_obj_set_height(label, 26);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);

    s_audio_playlist = lv_obj_create(list_panel);
    lv_obj_set_size(s_audio_playlist, LV_PCT(100), content_h - 142);
    lv_obj_set_pos(s_audio_playlist, 0, 92);
    lv_obj_set_style_bg_opa(s_audio_playlist, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_audio_playlist, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_audio_playlist, 0, 0);
    lv_obj_set_style_pad_row(s_audio_playlist, 8, 0);
    lv_obj_set_flex_flow(s_audio_playlist, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_audio_playlist, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_audio_playlist, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(s_audio_playlist, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(s_audio_playlist, fm_audio_on_playlist_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_audio_playlist, fm_audio_on_playlist_scroll, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_audio_playlist, fm_audio_on_playlist_scroll, LV_EVENT_SCROLL_END, NULL);

    s_audio_list_status = fm_label(list_panel, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_audio_list_status);
    lv_obj_set_width(s_audio_list_status, LV_PCT(100));
    lv_obj_set_style_text_align(s_audio_list_status, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_audio_list_status, LV_LABEL_LONG_DOT);
    /* 提示栏坐标统一由 ui_file_manager_audio_helpers.inc 顶部的 FM_AUDIO_LIST_STATUS_* 参数控制。 */
    lv_obj_set_pos(s_audio_list_status, FM_AUDIO_LIST_STATUS_X, fm_audio_list_status_y(content_h));
    fm_audio_set_list_status(TRS("播放列表就绪"), THEME_TEXT_TERTIARY);

    fm_audio_apply_status_to_player(&status);
    if (path && path[0]) {
        esp_err_t ret = fm_audio_prepare_playback_resources() ? audio_player_play_file(path) : ESP_ERR_INVALID_STATE;
        fm_audio_set_list_status(ret == ESP_OK ? TRS("播放已排队") : TRS("播放请求失败"),
                                 ret == ESP_OK ? THEME_TEXT_SECONDARY : THEME_DANGER_LIGHT);
    }
}

void ui_file_manager_audio_play_reserved(const char *path)
{
    fm_show_audio_player(path);
}

typedef struct {
    uint32_t generation;
    audio_player_status_t status;
} fm_audio_status_async_t;

static lv_color_t fm_audio_status_color(audio_player_state_t state)
{
    switch (state) {
    case AUDIO_PLAYER_STATE_PLAYING:
        return THEME_SUCCESS_LIGHT;
    case AUDIO_PLAYER_STATE_QUEUED:
    case AUDIO_PLAYER_STATE_SCANNING:
    case AUDIO_PLAYER_STATE_PREPARING:
    case AUDIO_PLAYER_STATE_PAUSED:
        return THEME_WARNING_LIGHT;
    case AUDIO_PLAYER_STATE_ERROR:
        return THEME_DANGER_LIGHT;
    default:
        return THEME_TEXT_TERTIARY;
    }
}

static void fm_audio_status_apply(void *arg)
{
    fm_audio_status_async_t *async = (fm_audio_status_async_t *)arg;
    char text[320];
    const audio_player_status_t *status;

    if (!async) {
        return;
    }
    status = &async->status;
    if (async->generation == s_ui_generation && (s_overlay || s_audio_viewer)) {
        fm_audio_apply_status_to_player(status);
        fm_audio_format_short_hint(status, text, sizeof(text));
        if (s_audio_viewer) {
            if (status->message_seq != s_audio_list_last_message_seq) {
                s_audio_list_last_message_seq = status->message_seq;
                fm_audio_set_list_status(text, fm_audio_status_color(status->state));
            }
        } else if (s_overlay) {
            fm_set_status(text, fm_audio_status_color(status->state));
        }
    }
    free(async);
}

static void fm_audio_status_cb(const audio_player_status_t *status, void *user_data)
{
    fm_audio_status_async_t *async;

    if (!status) {
        return;
    }
    async = malloc(sizeof(*async));
    if (!async) {
        return;
    }
    async->generation = (uint32_t)(uintptr_t)user_data;
    async->status = *status;
    if (lv_async_call(fm_audio_status_apply, async) != LV_RESULT_OK) {
        free(async);
    }
}

static void fm_register_audio_status_cb(void)
{
    esp_err_t ret = audio_player_service_set_status_cb(fm_audio_status_cb,
                                                       (void *)(uintptr_t)s_ui_generation);
    if (ret == ESP_OK) {
        s_audio_status_cb_registered = true;
    } else {
        s_audio_status_cb_registered = false;
        ESP_LOGW(TAG, "audio status callback registration failed: %s", esp_err_to_name(ret));
    }
}

static void fm_unregister_audio_status_cb(void)
{
    if (!s_audio_status_cb_registered) {
        return;
    }
    (void)audio_player_service_set_status_cb(NULL, NULL);
    s_audio_status_cb_registered = false;
}
