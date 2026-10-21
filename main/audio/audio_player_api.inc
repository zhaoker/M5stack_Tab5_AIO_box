bool audio_player_service_is_initialized(void)
{
    return s_player.initialized;
}

esp_err_t audio_player_service_set_status_cb(audio_player_status_cb_t cb, void *user_data)
{
    esp_err_t ret = audio_player_service_init();
    if (ret != ESP_OK) {
        return ret;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    s_player.cb = cb;
    s_player.cb_user_data = user_data;
    xSemaphoreGive(s_player.lock);
    return ESP_OK;
}

esp_err_t audio_player_play_file(const char *path)
{
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_player_cmd_t cmd = {
        .type = AUDIO_PLAYER_CMD_PLAY_PATH,
    };
    strlcpy(cmd.path, path, sizeof(cmd.path));
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_play_index(size_t index)
{
    audio_player_cmd_t cmd = {
        .type = AUDIO_PLAYER_CMD_PLAY_INDEX,
        .index = index,
    };
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_next(void)
{
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_NEXT};
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_prev(void)
{
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_PREV};
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_pause(void)
{
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_PAUSE};
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_resume(void)
{
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_RESUME};
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_stop(void)
{
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_STOP};
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    audio_player_cmd_t cmd = {
        .type = AUDIO_PLAYER_CMD_SET_VOLUME,
        .volume = volume,
    };
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_set_mute(bool muted)
{
    audio_player_cmd_t cmd = {
        .type = AUDIO_PLAYER_CMD_SET_MUTE,
        .muted = muted,
    };
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_set_play_mode(audio_player_play_mode_t mode)
{
    if (!audio_player_play_mode_is_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_player_cmd_t cmd = {
        .type = AUDIO_PLAYER_CMD_SET_PLAY_MODE,
        .play_mode = mode,
    };
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_scan_favorites(void)
{
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_SCAN_FAVORITES};
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_scan_dir(const char *dir_path)
{
    if (!dir_path || !dir_path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_SCAN_DIR};
    strlcpy(cmd.path, dir_path, sizeof(cmd.path));
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_scan_all(const char *root_path)
{
    if (!root_path || !root_path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_player_cmd_t cmd = {.type = AUDIO_PLAYER_CMD_SCAN_ALL};
    strlcpy(cmd.path, root_path, sizeof(cmd.path));
    return audio_player_enqueue(&cmd);
}

esp_err_t audio_player_get_status(audio_player_status_t *out_status)
{
    if (!out_status) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = audio_player_service_init();
    if (ret != ESP_OK) {
        return ret;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    *out_status = s_player.status;
    xSemaphoreGive(s_player.lock);
    return ESP_OK;
}

const audio_library_track_t *audio_player_get_track(size_t index)
{
    if (!s_player.initialized || !s_player.tracks || index >= s_player.track_count) {
        return NULL;
    }
    return &s_player.tracks[index];
}

size_t audio_player_get_track_count(void)
{
    size_t count = 0;
    if (!s_player.lock) {
        return 0;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    count = s_player.track_count;
    xSemaphoreGive(s_player.lock);
    return count;
}
