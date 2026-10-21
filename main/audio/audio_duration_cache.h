#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio/audio_library.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_duration_cache_update_cb_t)(const char *path,
                                                 uint64_t size,
                                                 uint64_t mtime,
                                                 uint32_t duration_ms,
                                                 void *user_data);

esp_err_t audio_duration_cache_get(const char *path,
                                   uint64_t size,
                                   uint64_t mtime,
                                   uint32_t *out_duration_ms);
esp_err_t audio_duration_cache_get_or_measure(const char *path,
                                              uint64_t size,
                                              uint64_t mtime,
                                              uint32_t *out_duration_ms);
esp_err_t audio_duration_cache_scan_async(const audio_library_track_t *tracks,
                                          size_t count,
                                          audio_duration_cache_update_cb_t cb,
                                          void *user_data);

#ifdef __cplusplus
}
#endif
