#include "ui_pinyin_ime_ext.h"

#include "pinyin/pinyin_phrase_dict.h"
#include "settings/app_settings.h"
#include "ui_hw_textarea_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if LV_USE_IME_PINYIN != 0
#include "others/ime/lv_ime_pinyin_private.h"

#define UI_PINYIN_EXT_PHRASE_MAX 128

struct ui_pinyin_ime_ext {
    lv_obj_t *ime;
    lv_obj_t *keyboard;
    lv_obj_t *cand_panel;
    const char *const *default_map;
    const char *phrase_words[UI_PINYIN_EXT_PHRASE_MAX];
    char single_buf[LV_IME_PINYIN_CAND_TEXT_NUM][4];
    const char *cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 3];
    uint16_t phrase_count;
    bool enabled;
    bool custom_map_active;
};

static bool ui_pinyin_ime_ext_phrase_mode_enabled(void)
{
    return app_settings_get_ime_input_mode() == APP_SETTINGS_IME_INPUT_MODE_PHRASE;
}

static bool ui_pinyin_ime_ext_has_composition(const lv_ime_pinyin_t *ime)
{
    return ime && ime->ta_count > 0 && ime->input_char[0] != '\0';
}

static bool ui_pinyin_ime_ext_normalize_query(const char *py, char *out, size_t out_len)
{
    size_t i;

    if (!py || !py[0] || !out || out_len == 0) {
        return false;
    }

    for (i = 0; py[i] && i < out_len - 1; i++) {
        char ch = py[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch < 'a' || ch > 'z') {
            return false;
        }
        out[i] = ch;
    }
    if (py[i] != '\0') {
        return false;
    }
    out[i] = '\0';
    return true;
}

static uint16_t ui_pinyin_ime_ext_single_count(const lv_ime_pinyin_t *ime)
{
    return (ime && ime->cand_str) ? ime->cand_num : 0;
}

static uint16_t ui_pinyin_ime_ext_total_count(const ui_pinyin_ime_ext_t *ctx, const lv_ime_pinyin_t *ime)
{
    uint32_t total = (uint32_t)ui_pinyin_ime_ext_single_count(ime) + ctx->phrase_count;
    return total > UINT16_MAX ? UINT16_MAX : (uint16_t)total;
}

static uint8_t ui_pinyin_ime_ext_page_count(const ui_pinyin_ime_ext_t *ctx, const lv_ime_pinyin_t *ime)
{
    uint16_t total;
    uint16_t offset;

    if (!ctx || !ime) {
        return 0;
    }
    total = ui_pinyin_ime_ext_total_count(ctx, ime);
    if (total == 0) {
        return 0;
    }
    offset = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM;
    if (offset >= total) {
        return 0;
    }
    total -= offset;
    return (uint8_t)(total > LV_IME_PINYIN_CAND_TEXT_NUM ? LV_IME_PINYIN_CAND_TEXT_NUM : total);
}

static uint16_t ui_pinyin_ime_ext_last_page(const ui_pinyin_ime_ext_t *ctx, const lv_ime_pinyin_t *ime)
{
    uint16_t total = ui_pinyin_ime_ext_total_count(ctx, ime);
    if (total == 0) {
        return 0;
    }
    return (uint16_t)((total - 1) / LV_IME_PINYIN_CAND_TEXT_NUM);
}

static void ui_pinyin_ime_ext_restore_default(ui_pinyin_ime_ext_t *ctx)
{
    if (!ctx || !ctx->cand_panel) {
        return;
    }
    if (ctx->default_map && ctx->custom_map_active) {
        lv_buttonmatrix_set_map(ctx->cand_panel, ctx->default_map);
    }
    ctx->custom_map_active = false;
    ctx->phrase_count = 0;
    memset(ctx->phrase_words, 0, sizeof(ctx->phrase_words));
}

static bool ui_pinyin_ime_ext_get_candidate(ui_pinyin_ime_ext_t *ctx,
                                            lv_ime_pinyin_t *ime,
                                            uint8_t idx,
                                            const char **out,
                                            char *single_buf,
                                            size_t single_buf_len)
{
    uint16_t candidate_index;
    uint16_t single_count;

    if (!ctx || !ime || !out || idx >= LV_IME_PINYIN_CAND_TEXT_NUM) {
        return false;
    }

    candidate_index = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM + idx;
    if (candidate_index >= ui_pinyin_ime_ext_total_count(ctx, ime)) {
        return false;
    }

    single_count = ui_pinyin_ime_ext_single_count(ime);
    if (candidate_index < single_count) {
        uint16_t byte_offset = candidate_index * 3U;
        if (!ime->cand_str || !single_buf || single_buf_len < 4) {
            return false;
        }
        single_buf[0] = ime->cand_str[byte_offset];
        single_buf[1] = ime->cand_str[byte_offset + 1];
        single_buf[2] = ime->cand_str[byte_offset + 2];
        single_buf[3] = '\0';
        *out = single_buf;
        return true;
    }

    candidate_index = (uint16_t)(candidate_index - single_count);
    if (candidate_index >= ctx->phrase_count || !ctx->phrase_words[candidate_index]) {
        return false;
    }
    *out = ctx->phrase_words[candidate_index];
    return true;
}

static bool ui_pinyin_ime_ext_commit_candidate(ui_pinyin_ime_ext_t *ctx, uint8_t idx)
{
    lv_ime_pinyin_t *ime;
    lv_obj_t *ta;
    const char *selected = NULL;
    char selected_buf[4];

    if (!ctx || !ctx->ime || !ctx->keyboard || idx >= LV_IME_PINYIN_CAND_TEXT_NUM) {
        return false;
    }

    ime = (lv_ime_pinyin_t *)ctx->ime;
    if (!ui_pinyin_ime_ext_has_composition(ime)) {
        return false;
    }

    if (!ui_pinyin_ime_ext_get_candidate(ctx, ime, idx, &selected, selected_buf, sizeof(selected_buf)) ||
        !selected) {
        return false;
    }

    ta = lv_keyboard_get_textarea(ctx->keyboard);
    if (!ta) {
        return false;
    }

    for (uint16_t i = 0; i < ime->ta_count; i++) {
        lv_textarea_delete_char(ta);
    }
    lv_textarea_add_text(ta, selected);
    ime->ta_count = 0;
    ime->cand_num = 0;
    ime->py_page = 0;
    ime->cand_str = NULL;
    memset(ime->input_char, 0, sizeof(ime->input_char));
    ui_pinyin_ime_ext_restore_default(ctx);
    if (ctx->cand_panel) {
        lv_obj_add_flag(ctx->cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

static bool ui_pinyin_ime_ext_page(ui_pinyin_ime_ext_t *ctx, bool next)
{
    lv_ime_pinyin_t *ime;
    uint16_t last_page;

    if (!ctx || !ctx->ime) {
        return false;
    }
    ime = (lv_ime_pinyin_t *)ctx->ime;
    if (!ui_pinyin_ime_ext_has_composition(ime)) {
        return false;
    }

    last_page = ui_pinyin_ime_ext_last_page(ctx, ime);
    if (next) {
        if (ime->py_page < last_page) {
            ime->py_page++;
        }
    } else if (ime->py_page > 0) {
        ime->py_page--;
    }
    ui_pinyin_ime_ext_refresh(ctx);
    return true;
}

static void ui_pinyin_ime_ext_cand_event_cb(lv_event_t *e)
{
    ui_pinyin_ime_ext_t *ctx = (ui_pinyin_ime_ext_t *)lv_event_get_user_data(e);
    uint32_t id;

    if (!ctx || !ctx->custom_map_active || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    id = lv_buttonmatrix_get_selected_button(ctx->cand_panel);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    if (id == 0) {
        (void)ui_pinyin_ime_ext_page(ctx, false);
        lv_event_stop_processing(e);
        return;
    }
    if (id == LV_IME_PINYIN_CAND_TEXT_NUM + 1) {
        (void)ui_pinyin_ime_ext_page(ctx, true);
        lv_event_stop_processing(e);
        return;
    }
    if (id > 0 && id <= LV_IME_PINYIN_CAND_TEXT_NUM) {
        if (ui_pinyin_ime_ext_commit_candidate(ctx, (uint8_t)(id - 1))) {
            lv_event_stop_processing(e);
        }
    }
}

static void ui_pinyin_ime_ext_keyboard_event_cb(lv_event_t *e)
{
    ui_pinyin_ime_ext_t *ctx = (ui_pinyin_ime_ext_t *)lv_event_get_user_data(e);

    if (!ctx || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    ui_pinyin_ime_ext_refresh(ctx);
}

ui_pinyin_ime_ext_t *ui_pinyin_ime_ext_create(lv_obj_t *pinyin_ime,
                                              lv_obj_t *keyboard,
                                              lv_obj_t *cand_panel)
{
    ui_pinyin_ime_ext_t *ctx;

    if (!pinyin_ime || !keyboard || !cand_panel) {
        return NULL;
    }

    ctx = (ui_pinyin_ime_ext_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    ctx->ime = pinyin_ime;
    ctx->keyboard = keyboard;
    ctx->cand_panel = cand_panel;
    ctx->default_map = lv_buttonmatrix_get_map(cand_panel);
    ctx->enabled = true;

    lv_obj_add_event_cb(keyboard, ui_pinyin_ime_ext_keyboard_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(cand_panel,
                        ui_pinyin_ime_ext_cand_event_cb,
                        LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS,
                        ctx);
    return ctx;
}

void ui_pinyin_ime_ext_destroy(ui_pinyin_ime_ext_t **ctx_ref)
{
    ui_pinyin_ime_ext_t *ctx;

    if (!ctx_ref || !*ctx_ref) {
        return;
    }
    ctx = *ctx_ref;
    if (ctx->keyboard && lv_obj_is_valid(ctx->keyboard)) {
        lv_obj_remove_event_cb_with_user_data(ctx->keyboard, ui_pinyin_ime_ext_keyboard_event_cb, ctx);
    }
    if (ctx->cand_panel && lv_obj_is_valid(ctx->cand_panel)) {
        lv_obj_remove_event_cb_with_user_data(ctx->cand_panel, ui_pinyin_ime_ext_cand_event_cb, ctx);
    }
    free(ctx);
    *ctx_ref = NULL;
}

void ui_pinyin_ime_ext_forget_objects(ui_pinyin_ime_ext_t *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->ime = NULL;
    ctx->keyboard = NULL;
    ctx->cand_panel = NULL;
    ctx->default_map = NULL;
}

void ui_pinyin_ime_ext_reset(ui_pinyin_ime_ext_t *ctx)
{
    lv_ime_pinyin_t *ime;

    if (!ctx) {
        return;
    }
    if (ctx->ime && lv_obj_is_valid(ctx->ime)) {
        ime = (lv_ime_pinyin_t *)ctx->ime;
        ime->ta_count = 0;
        ime->cand_num = 0;
        ime->py_page = 0;
        ime->cand_str = NULL;
        memset(ime->input_char, 0, sizeof(ime->input_char));
#if LV_IME_PINYIN_USE_K9_MODE
        ime->k9_py_ll_pos = 0;
        ime->k9_legal_py_count = 0;
        ime->k9_input_str_len = 0;
        memset(ime->k9_input_str, 0, sizeof(ime->k9_input_str));
#endif
    }
    ui_pinyin_ime_ext_restore_default(ctx);
    if (ctx->cand_panel && lv_obj_is_valid(ctx->cand_panel)) {
        lv_obj_add_flag(ctx->cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_pinyin_ime_ext_set_enabled(ui_pinyin_ime_ext_t *ctx, bool enabled)
{
    if (!ctx) {
        return;
    }
    ctx->enabled = enabled;
    if (!enabled) {
        ui_pinyin_ime_ext_restore_default(ctx);
    }
}

void ui_pinyin_ime_ext_refresh(ui_pinyin_ime_ext_t *ctx)
{
    lv_ime_pinyin_t *ime;
    char query[sizeof(((lv_ime_pinyin_t *)0)->input_char)];
    uint8_t count;
    uint16_t page_offset;

    if (!ctx || !ctx->enabled || !ctx->ime || !ctx->cand_panel ||
        !ui_pinyin_ime_ext_phrase_mode_enabled()) {
        ui_pinyin_ime_ext_restore_default(ctx);
        return;
    }

    ime = (lv_ime_pinyin_t *)ctx->ime;
    if (!ui_pinyin_ime_ext_has_composition(ime) ||
        !ui_pinyin_ime_ext_normalize_query(ime->input_char, query, sizeof(query))) {
        ui_pinyin_ime_ext_restore_default(ctx);
        return;
    }

    memset(ctx->phrase_words, 0, sizeof(ctx->phrase_words));
    ctx->phrase_count = pinyin_phrase_collect(query, ctx->phrase_words, UI_PINYIN_EXT_PHRASE_MAX);
    if (ctx->phrase_count == 0) {
        ui_pinyin_ime_ext_restore_default(ctx);
        return;
    }

    memset(ctx->single_buf, 0, sizeof(ctx->single_buf));
    ctx->cand_map[0] = "<";
    for (uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
        ctx->cand_map[i + 1] = " ";
    }
    ctx->cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 1] = ">";
    ctx->cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 2] = "";

    page_offset = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM;
    if (page_offset >= ui_pinyin_ime_ext_total_count(ctx, ime)) {
        ime->py_page = 0;
        page_offset = 0;
    }
    count = ui_pinyin_ime_ext_page_count(ctx, ime);
    for (uint8_t i = 0; i < count; i++) {
        const char *candidate = NULL;
        if (ui_pinyin_ime_ext_get_candidate(ctx,
                                            ime,
                                            i,
                                            &candidate,
                                            ctx->single_buf[i],
                                            sizeof(ctx->single_buf[i])) &&
            candidate) {
            ctx->cand_map[i + 1] = candidate;
        }
    }

    lv_buttonmatrix_set_map(ctx->cand_panel, ctx->cand_map);
    ctx->custom_map_active = true;
    lv_obj_remove_flag(ctx->cand_panel, LV_OBJ_FLAG_HIDDEN);
}
#endif
