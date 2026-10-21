#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ui_hw_pinyin_input ui_hw_pinyin_input_t;

typedef struct {
    lv_obj_t *parent;
    lv_obj_t *target_ta;
    lv_obj_t *anchor;
    lv_coord_t width;
    lv_coord_t height;
} ui_hw_pinyin_input_config_t;

ui_hw_pinyin_input_t *ui_hw_pinyin_input_create(const ui_hw_pinyin_input_config_t *cfg);
void ui_hw_pinyin_input_destroy(ui_hw_pinyin_input_t **ctx_ref);
void ui_hw_pinyin_input_set_target(ui_hw_pinyin_input_t *ctx, lv_obj_t *target_ta, lv_obj_t *anchor);
void ui_hw_pinyin_input_set_enabled(ui_hw_pinyin_input_t *ctx, bool enabled);
bool ui_hw_pinyin_input_is_enabled(const ui_hw_pinyin_input_t *ctx);
bool ui_hw_pinyin_input_has_composition(const ui_hw_pinyin_input_t *ctx);
bool ui_hw_pinyin_input_handle_event(ui_hw_pinyin_input_t *ctx, const char *txt, size_t len, uint8_t modifier);
void ui_hw_pinyin_input_reset(ui_hw_pinyin_input_t *ctx);
void ui_hw_pinyin_input_reposition(ui_hw_pinyin_input_t *ctx);
