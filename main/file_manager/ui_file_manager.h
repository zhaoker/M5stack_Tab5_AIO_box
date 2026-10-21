#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_file_manager_create(lv_obj_t *parent);
void ui_file_manager_close(void);
void ui_file_manager_prepare_usb_unmount(void);
void ui_file_manager_audio_play_reserved(const char *path);

#ifdef __cplusplus
}
#endif
