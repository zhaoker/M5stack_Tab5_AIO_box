#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_pinyin_ime_ext ui_pinyin_ime_ext_t;

#if LV_USE_IME_PINYIN != 0
ui_pinyin_ime_ext_t *ui_pinyin_ime_ext_create(lv_obj_t *pinyin_ime,
                                              lv_obj_t *keyboard,
                                              lv_obj_t *cand_panel);
void ui_pinyin_ime_ext_destroy(ui_pinyin_ime_ext_t **ctx_ref);
void ui_pinyin_ime_ext_forget_objects(ui_pinyin_ime_ext_t *ctx);
void ui_pinyin_ime_ext_reset(ui_pinyin_ime_ext_t *ctx);
void ui_pinyin_ime_ext_set_enabled(ui_pinyin_ime_ext_t *ctx, bool enabled);
void ui_pinyin_ime_ext_refresh(ui_pinyin_ime_ext_t *ctx);
#else
static inline ui_pinyin_ime_ext_t *ui_pinyin_ime_ext_create(lv_obj_t *pinyin_ime,
                                                            lv_obj_t *keyboard,
                                                            lv_obj_t *cand_panel)
{
    (void)pinyin_ime;
    (void)keyboard;
    (void)cand_panel;
    return NULL;
}

static inline void ui_pinyin_ime_ext_destroy(ui_pinyin_ime_ext_t **ctx_ref)
{
    (void)ctx_ref;
}

static inline void ui_pinyin_ime_ext_forget_objects(ui_pinyin_ime_ext_t *ctx)
{
    (void)ctx;
}

static inline void ui_pinyin_ime_ext_reset(ui_pinyin_ime_ext_t *ctx)
{
    (void)ctx;
}

static inline void ui_pinyin_ime_ext_set_enabled(ui_pinyin_ime_ext_t *ctx, bool enabled)
{
    (void)ctx;
    (void)enabled;
}

static inline void ui_pinyin_ime_ext_refresh(ui_pinyin_ime_ext_t *ctx)
{
    (void)ctx;
}
#endif

#ifdef __cplusplus
}
#endif
