#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_image_dsc_t dsc;
    uint32_t source_w;
    uint32_t source_h;
    bool hardware_jpeg;
    bool ppa_ready;
} file_manager_image_t;

esp_err_t file_manager_media_load_image(const char *path,
                                        file_manager_image_t *image,
                                        char *status,
                                        size_t status_len);
esp_err_t file_manager_media_load_image_for_area(const char *path,
                                                 uint32_t area_w,
                                                 uint32_t area_h,
                                                 file_manager_image_t *image,
                                                 char *status,
                                                 size_t status_len);
void file_manager_media_release_image(file_manager_image_t *image);

bool file_manager_media_prepare_ppa(char *status, size_t status_len);
void file_manager_media_audio_play_reserved(const char *path);

#ifdef __cplusplus
}
#endif
