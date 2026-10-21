#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_LIBRARY_MAX_TRACKS 256
#define AUDIO_LIBRARY_PATH_MAX   512
#define AUDIO_LIBRARY_NAME_MAX   160

typedef struct {
    char path[AUDIO_LIBRARY_PATH_MAX];
    char dir[AUDIO_LIBRARY_PATH_MAX];
    char name[AUDIO_LIBRARY_NAME_MAX];
    char title[AUDIO_LIBRARY_NAME_MAX];
    char artist[AUDIO_LIBRARY_NAME_MAX];
    char format[8];
    uint64_t size;
    uint64_t mtime;
    uint32_t duration_ms;
} audio_library_track_t;

bool audio_library_is_supported(const char *path);
const char *audio_library_format_for_path(const char *path);
esp_err_t audio_library_track_from_path(const char *path, audio_library_track_t *out_track);
esp_err_t audio_library_scan_dir(const char *dir_path,
                                 audio_library_track_t *tracks,
                                 size_t max_tracks,
                                 size_t *out_count);
esp_err_t audio_library_scan_recursive(const char *root_path,
                                       audio_library_track_t *tracks,
                                       size_t max_tracks,
                                       size_t *out_count);
esp_err_t audio_library_scan_dir_for_file(const char *file_path,
                                          audio_library_track_t *tracks,
                                          size_t max_tracks,
                                          size_t *out_count,
                                          size_t *out_index);

#ifdef __cplusplus
}
#endif
