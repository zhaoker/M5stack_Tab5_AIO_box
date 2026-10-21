static uint32_t audio_player_pcm_elapsed_ms(uint64_t bytes,
                                            uint32_t sample_rate,
                                            uint16_t bits_per_sample,
                                            uint16_t channel_count)
{
    if (sample_rate == 0 || bits_per_sample == 0 || channel_count == 0) {
        return 0;
    }
    uint64_t bytes_per_second = (uint64_t)sample_rate * bits_per_sample * channel_count / 8ULL;
    if (bytes_per_second == 0) {
        return 0;
    }
    uint64_t ms = (bytes * 1000ULL) / bytes_per_second;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static uint32_t audio_player_current_duration_locked(void)
{
    if (s_player.tracks && s_player.track_count > 0 && s_player.current_index < s_player.track_count) {
        return s_player.tracks[s_player.current_index].duration_ms;
    }
    return 0;
}

static void audio_player_reset_progress_locked(void)
{
    s_player.status.elapsed_ms = 0;
    s_player.status.duration_ms = audio_player_current_duration_locked();
}

static void audio_player_publish_progress(uint32_t elapsed_ms, bool force)
{
    audio_player_status_cb_t cb;
    void *cb_user_data;
    audio_player_status_t snapshot;
    TickType_t now = xTaskGetTickCount();

    if (!s_player.lock) {
        return;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    if (!force && now - s_player.last_duration_publish_tick < pdMS_TO_TICKS(AUDIO_PLAYER_PROGRESS_MS)) {
        xSemaphoreGive(s_player.lock);
        return;
    }
    s_player.last_duration_publish_tick = now;
    s_player.status.elapsed_ms = elapsed_ms;
    audio_player_copy_track_to_status_locked();
    snapshot = s_player.status;
    cb = s_player.cb;
    cb_user_data = s_player.cb_user_data;
    xSemaphoreGive(s_player.lock);

    if (cb) {
        cb(&snapshot, cb_user_data);
    }
}

static void audio_player_update_track_duration_by_path(const char *path,
                                                       uint64_t size,
                                                       uint64_t mtime,
                                                       uint32_t duration_ms,
                                                       bool publish)
{
    audio_player_status_cb_t cb = NULL;
    void *cb_user_data = NULL;
    audio_player_status_t snapshot;
    bool changed = false;

    if (!path || !path[0] || !s_player.lock) {
        return;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    for (size_t i = 0; s_player.tracks && i < s_player.track_count; i++) {
        audio_library_track_t *track = &s_player.tracks[i];
        if (track->size == size && track->mtime == mtime && strcmp(track->path, path) == 0) {
            if (track->duration_ms != duration_ms) {
                track->duration_ms = duration_ms;
                changed = true;
            }
        }
    }
    if (changed && publish) {
        audio_player_copy_track_to_status_locked();
        snapshot = s_player.status;
        cb = s_player.cb;
        cb_user_data = s_player.cb_user_data;
    }
    xSemaphoreGive(s_player.lock);

    if (cb) {
        cb(&snapshot, cb_user_data);
    }
}

static void audio_player_update_current_duration(uint32_t duration_ms)
{
    if (!s_player.lock || duration_ms == 0) {
        return;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    if (s_player.tracks && s_player.current_index < s_player.track_count) {
        s_player.tracks[s_player.current_index].duration_ms = duration_ms;
        s_player.status.duration_ms = duration_ms;
    }
    xSemaphoreGive(s_player.lock);
}

static void audio_player_duration_update_cb(const char *path,
                                            uint64_t size,
                                            uint64_t mtime,
                                            uint32_t duration_ms,
                                            void *user_data)
{
    bool publish = false;
    TickType_t now = xTaskGetTickCount();
    (void)user_data;

    if (!s_player.lock) {
        return;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    if (s_player.tracks && s_player.current_index < s_player.track_count) {
        const audio_library_track_t *current = &s_player.tracks[s_player.current_index];
        publish = current->size == size && current->mtime == mtime && strcmp(current->path, path) == 0;
    }
    if (!publish && now - s_player.last_duration_publish_tick >= pdMS_TO_TICKS(AUDIO_PLAYER_PROGRESS_MS)) {
        publish = true;
        s_player.last_duration_publish_tick = now;
    }
    xSemaphoreGive(s_player.lock);

    audio_player_update_track_duration_by_path(path, size, mtime, duration_ms, publish);
}

static void audio_player_start_duration_scan(const audio_library_track_t *tracks, size_t count)
{
    if (!tracks || count == 0) {
        return;
    }
    esp_err_t ret = audio_duration_cache_scan_async(tracks, count, audio_player_duration_update_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "duration scan start failed: %s", esp_err_to_name(ret));
    }
}
