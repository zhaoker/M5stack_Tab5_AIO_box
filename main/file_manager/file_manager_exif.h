#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t width;
    uint32_t height;
    char title[160];
    char comment[160];
    char camera[80];
    char lens[80];
    char datetime[32];
    char aperture[24];
    char shutter[32];
    char iso[24];
    char focal_length[24];
    char exposure_bias[24];
} file_manager_exif_info_t;

esp_err_t file_manager_exif_read(const char *path, file_manager_exif_info_t *info);
bool file_manager_exif_format_lines(const file_manager_exif_info_t *info, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
