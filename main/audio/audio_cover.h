#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_cover_extract_to_file(const char *audio_path, char *out_path, size_t out_len);

#ifdef __cplusplus
}
#endif
