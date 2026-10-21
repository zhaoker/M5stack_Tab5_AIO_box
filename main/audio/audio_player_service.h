#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio/audio_library.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PLAYER_STATE_IDLE,
    AUDIO_PLAYER_STATE_QUEUED,
    AUDIO_PLAYER_STATE_SCANNING,
    AUDIO_PLAYER_STATE_PREPARING,
    AUDIO_PLAYER_STATE_PLAYING,
    AUDIO_PLAYER_STATE_PAUSED,
    AUDIO_PLAYER_STATE_STOPPED,
    AUDIO_PLAYER_STATE_ERROR,
} audio_player_state_t;

typedef enum {
    AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP,
    AUDIO_PLAYER_PLAY_MODE_LIST_LOOP,
    AUDIO_PLAYER_PLAY_MODE_SHUFFLE,
} audio_player_play_mode_t;

typedef struct {
    audio_player_state_t state;
    audio_player_play_mode_t play_mode;
    size_t track_count;
    size_t current_index;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    uint32_t message_seq;
    uint8_t volume;
    bool muted;
    char title[AUDIO_LIBRARY_NAME_MAX];
    char artist[AUDIO_LIBRARY_NAME_MAX];
    char path[AUDIO_LIBRARY_PATH_MAX];
    char message[96];
} audio_player_status_t;

typedef void (*audio_player_status_cb_t)(const audio_player_status_t *status, void *user_data);

esp_err_t audio_player_service_init(void);
bool audio_player_service_is_initialized(void);
esp_err_t audio_player_service_set_status_cb(audio_player_status_cb_t cb, void *user_data);
esp_err_t audio_player_play_file(const char *path);
esp_err_t audio_player_play_index(size_t index);
esp_err_t audio_player_next(void);
esp_err_t audio_player_prev(void);
esp_err_t audio_player_pause(void);
esp_err_t audio_player_resume(void);
esp_err_t audio_player_stop(void);
esp_err_t audio_player_set_volume(uint8_t volume);
esp_err_t audio_player_set_mute(bool muted);
esp_err_t audio_player_set_play_mode(audio_player_play_mode_t mode);
esp_err_t audio_player_scan_favorites(void);
esp_err_t audio_player_scan_dir(const char *dir_path);
esp_err_t audio_player_scan_all(const char *root_path);
esp_err_t audio_player_get_status(audio_player_status_t *out_status);
const audio_library_track_t *audio_player_get_track(size_t index);
size_t audio_player_get_track_count(void);

#ifdef __cplusplus
}
#endif
