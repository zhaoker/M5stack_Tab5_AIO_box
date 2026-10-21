#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "audio/audio_library.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_favorites_add(const char *path);
esp_err_t audio_favorites_remove(const char *path);
bool audio_favorites_contains(const char *path);
esp_err_t audio_favorites_load(audio_library_track_t *tracks,
                               size_t max_tracks,
                               size_t *out_count,
                               size_t *out_removed_count);

#ifdef __cplusplus
}
#endif
