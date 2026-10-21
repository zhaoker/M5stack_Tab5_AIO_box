enum {
    /* 播放列表底部提示栏的 X 坐标，基于右侧播放列表面板左上角计算；增大则整体向右移动。 */
    FM_AUDIO_LIST_STATUS_X = 4,
    /*
     * 播放列表底部提示栏的 Y 偏移，基于右侧播放列表面板内容高度 content_h 计算。
     * 负数表示从底部往上抬；当前 -56 比原先 -36 上移 20px，后续可直接调这里。
     */
    FM_AUDIO_LIST_STATUS_Y_OFFSET = -56,
    /* 播放列表底部提示栏自动隐藏时间；任何操作提示显示 4 秒后自动清空。 */
    FM_AUDIO_LIST_STATUS_HIDE_MS = 4000,
    /* 音频提示最大显示字符数，右下角提示栏控制在 18 字以内，避免撑满播放列表。 */
    FM_AUDIO_HINT_MAX_CHARS = 18,
    /* 歌曲行按下后移动超过这个像素值，就认为用户是在滑动列表，不触发点歌。 */
    FM_AUDIO_TRACK_DRAG_CANCEL_PX = 8,
    /* 列表刚发生滚动后的保护时间，避免滚动结束时 LVGL 再派发一次误点击。 */
    FM_AUDIO_SCROLL_CLICK_SUPPRESS_MS = 420,
    /* 封面加载延迟，避免歌曲刚开始解码时同步读取封面和解码器抢 TF 卡。 */
    FM_AUDIO_COVER_LOAD_DELAY_MS = 700,
};

static lv_obj_t *s_audio_list_status;
static lv_timer_t *s_audio_list_status_timer;
static lv_obj_t *s_audio_playlist_count_label;
static lv_obj_t *s_audio_cover_img;
static lv_obj_t *s_audio_cover_band;
static lv_obj_t *s_audio_cover_icon;
static lv_obj_t *s_audio_tab_now;
static lv_obj_t *s_audio_tab_favorites;
static fm_audio_cover_cache_entry_t *s_audio_cover_entry;
static audio_library_track_t *s_audio_favorite_tracks;
static char s_audio_cover_track_path[AUDIO_LIBRARY_PATH_MAX];
static char s_audio_cover_pending_path[AUDIO_LIBRARY_PATH_MAX];
static char s_audio_playlist_last_path[AUDIO_LIBRARY_PATH_MAX];
static size_t s_audio_favorite_count;
static size_t s_audio_favorite_removed_count;
static size_t s_audio_playlist_last_count = (size_t)-1;
static size_t s_audio_playlist_last_current = (size_t)-1;
static uint32_t s_audio_list_last_message_seq;
static bool s_audio_showing_favorites;
static bool s_audio_playlist_dirty = true;
static lv_point_t s_audio_track_press_point;
static size_t s_audio_track_press_index = (size_t)-1;
static uint32_t s_audio_playlist_scroll_tick;
static bool s_audio_playlist_scrolling;
static bool s_audio_track_press_active;
static bool s_audio_track_dragging;
static lv_timer_t *s_audio_cover_timer;

static int32_t fm_audio_list_status_y(int32_t content_h)
{
    return content_h + FM_AUDIO_LIST_STATUS_Y_OFFSET;
}

static void fm_audio_cancel_list_status_timer(void)
{
    if (s_audio_list_status_timer) {
        lv_timer_del(s_audio_list_status_timer);
        s_audio_list_status_timer = NULL;
    }
}

static void fm_audio_list_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_audio_list_status_timer = NULL;
    if (s_audio_list_status) {
        lv_label_set_text(s_audio_list_status, "");
    }
}

static void fm_audio_start_list_status_timer(void)
{
    fm_audio_cancel_list_status_timer();
    s_audio_list_status_timer = lv_timer_create(fm_audio_list_status_timer_cb,
                                                FM_AUDIO_LIST_STATUS_HIDE_MS,
                                                NULL);
    if (s_audio_list_status_timer) {
        lv_timer_set_repeat_count(s_audio_list_status_timer, 1);
    }
}

static void fm_audio_set_list_status(const char *text, lv_color_t color)
{
    if (!s_audio_list_status) {
        if (!s_audio_viewer) {
            fm_set_status(text, color);
        }
        return;
    }
    lv_label_set_text(s_audio_list_status, text ? TRS(text) : "");
    lv_obj_set_style_text_color(s_audio_list_status, color, 0);
    if (text && text[0]) {
        fm_audio_start_list_status_timer();
    } else {
        fm_audio_cancel_list_status_timer();
    }
}

static bool fm_audio_prepare_playback_resources(void)
{
    if (web_file_manager_is_upload_active()) {
        fm_audio_set_list_status(TRS("上传中稍后播放"), THEME_WARNING_LIGHT);
        return false;
    }
    if (web_file_manager_is_running()) {
        web_file_manager_stop();
        fm_set_web_status(TRS("Web已为播放暂停"), THEME_TEXT_SECONDARY);
        fm_update_web_controls();
        vTaskDelay(pdMS_TO_TICKS(80));
        fm_audio_set_list_status(TRS("已释放Web资源"), THEME_TEXT_SECONDARY);
    }
    return true;
}

static void fm_audio_mark_playlist_dirty(void)
{
    s_audio_playlist_dirty = true;
}

static void fm_audio_reset_playlist_cache(void)
{
    s_audio_playlist_last_count = (size_t)-1;
    s_audio_playlist_last_current = (size_t)-1;
    s_audio_playlist_last_path[0] = '\0';
    s_audio_playlist_dirty = true;
}

static bool fm_audio_ensure_favorites_cache(void)
{
    if (s_audio_favorite_tracks) {
        return true;
    }
    s_audio_favorite_tracks = heap_caps_calloc(AUDIO_LIBRARY_MAX_TRACKS,
                                               sizeof(audio_library_track_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_audio_favorite_tracks) {
        s_audio_favorite_tracks = heap_caps_calloc(AUDIO_LIBRARY_MAX_TRACKS,
                                                   sizeof(audio_library_track_t),
                                                   MALLOC_CAP_8BIT);
    }
    return s_audio_favorite_tracks != NULL;
}

static void fm_audio_release_favorites_cache(void)
{
    free(s_audio_favorite_tracks);
    s_audio_favorite_tracks = NULL;
    s_audio_favorite_count = 0;
    s_audio_favorite_removed_count = 0;
}

static bool fm_audio_load_favorites_cache(void)
{
    if (!fm_audio_ensure_favorites_cache()) {
        s_audio_favorite_count = 0;
        s_audio_favorite_removed_count = 0;
        return false;
    }
    esp_err_t ret = audio_favorites_load(s_audio_favorite_tracks,
                                         AUDIO_LIBRARY_MAX_TRACKS,
                                         &s_audio_favorite_count,
                                         &s_audio_favorite_removed_count);
    if (ret != ESP_OK) {
        s_audio_favorite_count = 0;
        s_audio_favorite_removed_count = 0;
        return false;
    }
    return true;
}

static bool fm_audio_favorites_cache_contains(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    if (!s_audio_favorite_tracks && !fm_audio_load_favorites_cache()) {
        return false;
    }
    for (size_t i = 0; i < s_audio_favorite_count; i++) {
        if (strcmp(s_audio_favorite_tracks[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static void fm_audio_set_tab_active(lv_obj_t *tab, bool active)
{
    lv_obj_t *icon;
    lv_obj_t *label;

    if (!tab) {
        return;
    }
    fm_style_surface(tab,
                     active ? lv_color_hex(0x202642) : lv_color_hex(0x141A27),
                     active ? lv_color_hex(0x202642) : lv_color_hex(0x141A27),
                     0);
    icon = lv_obj_get_child(tab, 0);
    label = lv_obj_get_child(tab, 1);
    if (icon) {
        lv_obj_set_style_text_color(icon, active ? lv_color_hex(0x8B6DFF) : THEME_TEXT_SECONDARY, 0);
    }
    if (label) {
        lv_obj_set_style_text_color(label, active ? lv_color_hex(0x8B6DFF) : THEME_TEXT_SECONDARY, 0);
    }
}

static void fm_audio_update_tabs(void)
{
    fm_audio_set_tab_active(s_audio_tab_now, !s_audio_showing_favorites);
    fm_audio_set_tab_active(s_audio_tab_favorites, s_audio_showing_favorites);
}

static bool fm_audio_playlist_needs_render(const audio_player_status_t *status)
{
    const char *path = status ? status->path : "";
    size_t count = status ? status->track_count : audio_player_get_track_count();
    size_t current = status ? status->current_index : 0;

    if (s_audio_playlist_dirty && status && status->state == AUDIO_PLAYER_STATE_SCANNING) {
        return false;
    }
    if (!s_audio_playlist_dirty &&
        s_audio_playlist_last_count == count &&
        s_audio_playlist_last_current == current &&
        strcmp(s_audio_playlist_last_path, path ? path : "") == 0) {
        return false;
    }
    s_audio_playlist_last_count = count;
    s_audio_playlist_last_current = current;
    strlcpy(s_audio_playlist_last_path, path ? path : "", sizeof(s_audio_playlist_last_path));
    s_audio_playlist_dirty = false;
    return true;
}

static int32_t fm_audio_abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void fm_audio_reset_track_press_state(void)
{
    s_audio_track_press_index = (size_t)-1;
    s_audio_track_press_active = false;
    s_audio_track_dragging = false;
}

static void fm_audio_mark_playlist_scrolled(bool scrolling)
{
    s_audio_playlist_scroll_tick = lv_tick_get();
    s_audio_playlist_scrolling = scrolling;
    if (s_audio_track_press_active) {
        s_audio_track_dragging = true;
    }
}

static void fm_audio_on_playlist_scroll(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCROLL_BEGIN || code == LV_EVENT_SCROLL) {
        fm_audio_mark_playlist_scrolled(true);
    } else if (code == LV_EVENT_SCROLL_END) {
        fm_audio_mark_playlist_scrolled(false);
    }
}

static void fm_audio_on_track_press_state(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    lv_indev_t *indev = NULL;
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        indev = lv_event_get_indev(e);
        s_audio_track_press_index = index;
        s_audio_track_press_active = true;
        s_audio_track_dragging = false;
        if (indev) {
            lv_indev_get_point(indev, &s_audio_track_press_point);
        } else {
            s_audio_track_press_point.x = 0;
            s_audio_track_press_point.y = 0;
        }
        return;
    }
    if (code == LV_EVENT_PRESSING && s_audio_track_press_active &&
        s_audio_track_press_index == index) {
        indev = lv_event_get_indev(e);
        if (!indev) {
            return;
        }
        lv_indev_get_point(indev, &point);
        int32_t dx = fm_audio_abs_i32(point.x - s_audio_track_press_point.x);
        int32_t dy = fm_audio_abs_i32(point.y - s_audio_track_press_point.y);
        if (dx > FM_AUDIO_TRACK_DRAG_CANCEL_PX || dy > FM_AUDIO_TRACK_DRAG_CANCEL_PX) {
            s_audio_track_dragging = true;
        }
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        if (s_audio_track_press_active && s_audio_track_press_index == index) {
            s_audio_track_dragging = true;
        }
        return;
    }
    if (code == LV_EVENT_DELETE && s_audio_track_press_index == index) {
        fm_audio_reset_track_press_state();
    }
}

static bool fm_audio_track_click_should_play(size_t index)
{
    if (s_audio_playlist_scrolling) {
        fm_audio_reset_track_press_state();
        return false;
    }
    if (s_audio_track_press_active && s_audio_track_press_index == index && s_audio_track_dragging) {
        fm_audio_reset_track_press_state();
        return false;
    }
    if (s_audio_playlist_scroll_tick != 0 &&
        lv_tick_elaps(s_audio_playlist_scroll_tick) < FM_AUDIO_SCROLL_CLICK_SUPPRESS_MS) {
        fm_audio_reset_track_press_state();
        return false;
    }
    fm_audio_reset_track_press_state();
    return true;
}

static size_t fm_audio_utf8_step(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!p || !p[0]) {
        return 0;
    }
    if (p[0] < 0x80) {
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0 && p[1]) {
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        return 4;
    }
    return 1;
}

static size_t fm_audio_utf8_count(const char *text)
{
    size_t count = 0;

    while (text && *text) {
        size_t step = fm_audio_utf8_step(text);
        if (step == 0) {
            break;
        }
        text += step;
        count++;
    }
    return count;
}

static size_t fm_audio_utf8_copy_chars(char *dst, size_t dst_len, const char *src, size_t max_chars)
{
    size_t used = 0;
    size_t count = 0;

    if (!dst || dst_len == 0) {
        return 0;
    }
    dst[0] = '\0';
    while (src && *src && count < max_chars) {
        size_t step = fm_audio_utf8_step(src);
        if (step == 0 || used + step >= dst_len) {
            break;
        }
        memcpy(dst + used, src, step);
        used += step;
        src += step;
        count++;
    }
    dst[used] = '\0';
    return count;
}

static void fm_audio_upper_ext(const char *path, char *out, size_t out_len)
{
    const char *ext = path ? strrchr(path, '.') : NULL;
    size_t used = 0;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!ext || !ext[1]) {
        return;
    }
    ext++;
    while (*ext && *ext != '/' && *ext != '\\' && used + 1 < out_len && used < 8) {
        char c = *ext++;
        out[used++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    out[used] = '\0';
}

static const char *fm_audio_state_text(audio_player_state_t state)
{
    switch (state) {
    case AUDIO_PLAYER_STATE_IDLE:
        return TRS("空闲");
    case AUDIO_PLAYER_STATE_QUEUED:
        return TRS("已排队");
    case AUDIO_PLAYER_STATE_SCANNING:
        return TRS("正在扫描");
    case AUDIO_PLAYER_STATE_PREPARING:
        return TRS("准备播放");
    case AUDIO_PLAYER_STATE_PLAYING:
        return TRS("正在播放");
    case AUDIO_PLAYER_STATE_PAUSED:
        return TRS("已暂停");
    case AUDIO_PLAYER_STATE_STOPPED:
        return TRS("已停止");
    case AUDIO_PLAYER_STATE_ERROR:
        return TRS("播放出错");
    default:
        return TRS("音频");
    }
}

static const char *fm_audio_compact_message(const char *message)
{
    if (!message || !message[0]) {
        return "";
    }
    if (strcmp(message, TRS("播放请求已入队")) == 0) {
        return TRS("播放已排队");
    }
    if (strcmp(message, TRS("播放请求入队失败")) == 0) {
        return TRS("排队失败");
    }
    if (strcmp(message, TRS("正在扫描当前文件夹音频")) == 0 ||
        strcmp(message, TRS("扫描当前文件夹")) == 0) {
        return TRS("扫描当前文件夹");
    }
    if (strcmp(message, TRS("正在扫描全卡音频")) == 0 ||
        strcmp(message, TRS("扫描全卡音频")) == 0) {
        return TRS("扫描全卡音频");
    }
    if (strcmp(message, TRS("音频列表已就绪")) == 0 ||
        strcmp(message, TRS("列表已就绪")) == 0) {
        return TRS("列表已就绪");
    }
    if (strcmp(message, TRS("没有找到音频文件")) == 0 ||
        strcmp(message, TRS("未找到音频")) == 0) {
        return TRS("未找到音频");
    }
    if (strcmp(message, TRS("正在加载收藏夹")) == 0) {
        return TRS("加载收藏夹");
    }
    if (strcmp(message, TRS("收藏夹为空，失效歌曲已清理")) == 0 ||
        strcmp(message, TRS("收藏夹已清理")) == 0) {
        return TRS("收藏夹已清理");
    }
    if (strcmp(message, TRS("不支持的音频格式")) == 0) {
        return TRS("格式不支持");
    }
    if (strcmp(message, TRS("该格式需要解码器，未产生 PCM")) == 0) {
        return TRS("缺少解码器");
    }
    if (strstr(message, TRS("需要 ESP-ADF 解码器"))) {
        return TRS("缺少音频解码器");
    }
    if (strstr(message, TRS("解码任务启动失败"))) {
        return TRS("解码启动失败");
    }
    if (strstr(message, TRS("未产生 PCM"))) {
        return TRS("解码无输出");
    }
    return message;
}

static void fm_audio_append_short_title(char *out, size_t out_len, const char *title)
{
    size_t used_chars;
    size_t room_chars;
    char short_title[AUDIO_LIBRARY_NAME_MAX];

    if (!out || out_len == 0 || !title || !title[0]) {
        return;
    }
    used_chars = fm_audio_utf8_count(out);
    if (used_chars >= FM_AUDIO_HINT_MAX_CHARS) {
        return;
    }
    room_chars = FM_AUDIO_HINT_MAX_CHARS - used_chars;
    fm_audio_utf8_copy_chars(short_title, sizeof(short_title), title, room_chars);
    strlcat(out, short_title, out_len);
}

static void fm_audio_format_short_hint(const audio_player_status_t *status, char *out, size_t out_len)
{
    char format[12];
    char base[96];
    bool english;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!status) {
        return;
    }
    if (status->state == AUDIO_PLAYER_STATE_PLAYING ||
        status->state == AUDIO_PLAYER_STATE_PREPARING) {
        fm_audio_upper_ext(status->path, format, sizeof(format));
        english = app_i18n_get_language() == APP_LANGUAGE_EN;
        if (english) {
            snprintf(base,
                     sizeof(base),
                     "%s %s%s",
                     status->state == AUDIO_PLAYER_STATE_PLAYING ? "Play" : "Ready",
                     format[0] ? format : "Audio",
                     status->title[0] ? " " : "");
        } else {
            snprintf(base,
                     sizeof(base),
                     "%s%s%s",
                     status->state == AUDIO_PLAYER_STATE_PLAYING ? TRS("正在播放") : TRS("准备播放"),
                     format[0] ? format : "",
                     format[0] ? TRS("音乐") : TRS("音频"));
        }
        fm_audio_utf8_copy_chars(out, out_len, base, FM_AUDIO_HINT_MAX_CHARS);
        fm_audio_append_short_title(out, out_len, status->title);
        return;
    }
    if (status->message[0]) {
        fm_audio_utf8_copy_chars(out,
                                 out_len,
                                 fm_audio_compact_message(status->message),
                                 FM_AUDIO_HINT_MAX_CHARS);
    } else {
        fm_audio_utf8_copy_chars(out, out_len, fm_audio_state_text(status->state), FM_AUDIO_HINT_MAX_CHARS);
    }
}

static void fm_audio_render_playlist(const audio_player_status_t *status);

static void fm_audio_refresh_favorites_async(void *arg)
{
    (void)arg;
    if (!s_audio_viewer) {
        return;
    }
    if (!fm_audio_load_favorites_cache()) {
        fm_audio_set_list_status(TRS("收藏夹刷新失败"), THEME_DANGER_LIGHT);
        return;
    }
    fm_audio_mark_playlist_dirty();
    fm_audio_render_playlist(NULL);
}

static void fm_audio_on_favorite_checkbox(lv_event_t *e)
{
    lv_obj_t *cb = (lv_obj_t *)lv_event_get_target(e);
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const audio_library_track_t *track = s_audio_showing_favorites
                                            ? (index < s_audio_favorite_count && s_audio_favorite_tracks
                                                   ? &s_audio_favorite_tracks[index]
                                                   : NULL)
                                            : audio_player_get_track(index);
    bool checked = cb && lv_obj_has_state(cb, LV_STATE_CHECKED);
    esp_err_t ret;
    char path[AUDIO_LIBRARY_PATH_MAX];

    lv_event_stop_bubbling(e);
    if (!cb || !track || !track->path[0]) {
        fm_audio_set_list_status(TRS("歌曲不可用"), THEME_DANGER_LIGHT);
        return;
    }
    strlcpy(path, track->path, sizeof(path));
    if (checked) {
        ret = audio_favorites_add(path);
        if (ret == ESP_OK) {
            fm_audio_set_list_status(TRS("已加入收藏夹"), THEME_SUCCESS_LIGHT);
            if (s_audio_favorite_tracks) {
                (void)fm_audio_load_favorites_cache();
            }
        } else {
            lv_obj_remove_state(cb, LV_STATE_CHECKED);
            fm_audio_set_list_status(TRS("加入收藏夹失败"), THEME_DANGER_LIGHT);
        }
        return;
    }

    ret = audio_favorites_remove(path);
    if (ret == ESP_OK || ret == ESP_ERR_NOT_FOUND) {
        fm_audio_set_list_status(s_audio_showing_favorites ? TRS("已移出收藏夹") : TRS("已取消收藏"),
                                 THEME_WARNING_LIGHT);
        if (s_audio_showing_favorites) {
            if (lv_async_call(fm_audio_refresh_favorites_async, NULL) != LV_RESULT_OK) {
                fm_audio_refresh_favorites_async(NULL);
            }
        } else if (s_audio_favorite_tracks) {
            (void)fm_audio_load_favorites_cache();
        }
    } else {
        lv_obj_add_state(cb, LV_STATE_CHECKED);
        fm_audio_set_list_status(TRS("取消收藏失败"), THEME_DANGER_LIGHT);
    }
}

static void fm_audio_release_cover(void)
{
    if (s_audio_cover_img) {
        lv_image_set_src(s_audio_cover_img, NULL);
    }
    s_audio_cover_entry = NULL;
}

static void fm_audio_cancel_cover_timer(void)
{
    if (s_audio_cover_timer) {
        lv_timer_del(s_audio_cover_timer);
        s_audio_cover_timer = NULL;
    }
    s_audio_cover_pending_path[0] = '\0';
}

static void fm_audio_show_cover_placeholder(bool show)
{
    if (s_audio_cover_img) {
        if (show) {
            lv_obj_add_flag(s_audio_cover_img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_audio_cover_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_audio_cover_img);
        }
    }
    if (s_audio_cover_band) {
        if (show) {
            lv_obj_clear_flag(s_audio_cover_band, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_audio_cover_band, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_audio_cover_icon) {
        if (show) {
            lv_obj_clear_flag(s_audio_cover_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_audio_cover_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void fm_audio_fit_cover_image(void)
{
    lv_image_header_t header;
    const void *src;
    int32_t cover_w;
    int32_t cover_h;
    uint32_t scale_w;
    uint32_t scale_h;
    uint32_t scale;

    if (!s_audio_cover_img) {
        return;
    }
    src = lv_image_get_src(s_audio_cover_img);
    if (!src || lv_image_decoder_get_info(src, &header) != LV_RESULT_OK || header.w == 0 || header.h == 0) {
        return;
    }
    cover_w = lv_obj_get_parent(s_audio_cover_img) ? lv_obj_get_width(lv_obj_get_parent(s_audio_cover_img)) : 250;
    cover_h = lv_obj_get_parent(s_audio_cover_img) ? lv_obj_get_height(lv_obj_get_parent(s_audio_cover_img)) : 250;
    if (cover_w <= 0) {
        cover_w = 250;
    }
    if (cover_h <= 0) {
        cover_h = 250;
    }
    scale_w = (uint32_t)(((uint64_t)cover_w * 256U) / header.w);
    scale_h = (uint32_t)(((uint64_t)cover_h * 256U) / header.h);
    scale = scale_w < scale_h ? scale_w : scale_h;
    lv_image_set_scale(s_audio_cover_img, scale ? scale : 1);
    lv_image_set_inner_align(s_audio_cover_img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_center(s_audio_cover_img);
}

static void fm_audio_load_cover_now(const char *track_path)
{
    fm_audio_cover_cache_entry_t *entry = NULL;
    const void *src = NULL;
    esp_err_t ret;

    if (!s_audio_cover_img || !track_path || !track_path[0]) {
        fm_audio_show_cover_placeholder(true);
        return;
    }
    ret = fm_audio_cover_cache_load(track_path, &entry);
    if (ret == ESP_OK && entry) {
        src = entry->image.dsc.data ? (const void *)&entry->image.dsc :
              (entry->lv_path[0] ? (const void *)entry->lv_path : NULL);
    }
    if (src) {
        s_audio_cover_entry = entry;
        lv_image_set_src(s_audio_cover_img, src);
        fm_audio_show_cover_placeholder(false);
        fm_audio_fit_cover_image();
        return;
    }

    fm_audio_show_cover_placeholder(true);
}

static void fm_audio_cover_timer_cb(lv_timer_t *timer)
{
    char path[AUDIO_LIBRARY_PATH_MAX];

    (void)timer;
    s_audio_cover_timer = NULL;
    if (!s_audio_viewer || !s_audio_cover_img || !s_audio_cover_pending_path[0]) {
        return;
    }
    strlcpy(path, s_audio_cover_pending_path, sizeof(path));
    s_audio_cover_pending_path[0] = '\0';
    fm_audio_load_cover_now(path);
}

static void fm_audio_update_cover(const char *track_path)
{
    if (!s_audio_cover_img || !track_path || !track_path[0]) {
        fm_audio_cancel_cover_timer();
        s_audio_cover_track_path[0] = '\0';
        fm_audio_release_cover();
        fm_audio_show_cover_placeholder(true);
        return;
    }
    if (strcmp(s_audio_cover_track_path, track_path) == 0 ||
        strcmp(s_audio_cover_pending_path, track_path) == 0) {
        return;
    }
    strlcpy(s_audio_cover_track_path, track_path, sizeof(s_audio_cover_track_path));
    strlcpy(s_audio_cover_pending_path, track_path, sizeof(s_audio_cover_pending_path));
    fm_audio_release_cover();
    fm_audio_show_cover_placeholder(true);
    if (s_audio_cover_timer) {
        lv_timer_reset(s_audio_cover_timer);
        return;
    }
    s_audio_cover_timer = lv_timer_create(fm_audio_cover_timer_cb,
                                          FM_AUDIO_COVER_LOAD_DELAY_MS,
                                          NULL);
    if (s_audio_cover_timer) {
        lv_timer_set_repeat_count(s_audio_cover_timer, 1);
    }
}
