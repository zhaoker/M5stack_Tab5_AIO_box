#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_number_input_attach(lv_obj_t *textarea, lv_obj_t *keyboard_parent);
void ui_number_input_close(void);

#ifdef __cplusplus
}
#endif
