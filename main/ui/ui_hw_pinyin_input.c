#include "ui_hw_pinyin_input.h"

#include "pinyin/pinyin_dict_custom.h"
#include "pinyin/pinyin_phrase_dict.h"
#include "settings/app_settings.h"
#include "theme.hpp"
#include "ui_hw_textarea_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if LV_USE_IME_PINYIN != 0

#define UI_HW_PINYIN_INPUT_MAX     15
#define UI_HW_PINYIN_PHRASE_MAX    128
#define UI_HW_PINYIN_PANEL_H       52
#define UI_HW_PINYIN_INPUT_BAR_H   38
#define UI_HW_PINYIN_FALLBACK_W    420
#define UI_HW_PINYIN_PAGE_SIZE     LV_IME_PINYIN_CAND_TEXT_NUM
#define UI_HW_PINYIN_DIGIT_FONT    (&lv_font_montserrat_16)

struct ui_hw_pinyin_input {
    lv_obj_t *parent;
    lv_obj_t *target_ta;
    lv_obj_t *anchor;
    lv_obj_t *panel;
    lv_obj_t *input_bar;
    lv_obj_t *input_label;
    lv_obj_t *cand_matrix;
    lv_obj_t *digit_labels[UI_HW_PINYIN_PAGE_SIZE];
    lv_coord_t width;
    lv_coord_t height;
    bool enabled;
    char input[UI_HW_PINYIN_INPUT_MAX + 1];
    uint16_t page;
    char *single_candidates;
    uint16_t single_count;
    const char *phrase_words[UI_HW_PINYIN_PHRASE_MAX];
    uint16_t phrase_count;
    char single_page_buf[UI_HW_PINYIN_PAGE_SIZE][4];
    const char *cand_map[UI_HW_PINYIN_PAGE_SIZE + 3];
};

static bool ui_hw_pinyin_phrase_mode_enabled(void)
{
    return app_settings_get_ime_input_mode() == APP_SETTINGS_IME_INPUT_MODE_PHRASE;
}

static bool ui_hw_pinyin_has_input(const ui_hw_pinyin_input_t *ctx)
{
    return ctx && ctx->input[0] != '\0';
}

static uint16_t ui_hw_pinyin_candidate_total(const ui_hw_pinyin_input_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    uint32_t total = (uint32_t)ctx->single_count + ctx->phrase_count;
    return total > UINT16_MAX ? UINT16_MAX : (uint16_t)total;
}

static uint16_t ui_hw_pinyin_last_page(const ui_hw_pinyin_input_t *ctx)
{
    uint16_t total = ui_hw_pinyin_candidate_total(ctx);
    if (total == 0) {
        return 0;
    }
    return (uint16_t)((total - 1) / UI_HW_PINYIN_PAGE_SIZE);
}

static uint8_t ui_hw_pinyin_page_count(const ui_hw_pinyin_input_t *ctx)
{
    uint16_t total = ui_hw_pinyin_candidate_total(ctx);
    uint16_t offset;

    if (!ctx || total == 0) {
        return 0;
    }
    offset = ctx->page * UI_HW_PINYIN_PAGE_SIZE;
    if (offset >= total) {
        return 0;
    }
    total -= offset;
    return (uint8_t)(total > UI_HW_PINYIN_PAGE_SIZE ? UI_HW_PINYIN_PAGE_SIZE : total);
}

static char ui_hw_pinyin_digit_for_index(uint8_t idx)
{
    return idx == 9 ? '0' : (char)('1' + idx);
}

static bool ui_hw_pinyin_index_for_digit(char digit, uint8_t *idx)
{
    if (!idx) {
        return false;
    }
    if (digit == '0') {
        *idx = 9;
        return true;
    }
    if (digit >= '1' && digit <= '9') {
        *idx = (uint8_t)(digit - '1');
        return true;
    }
    return false;
}

static char *ui_hw_pinyin_find_single_candidates(const char *py, uint16_t *cand_num)
{
    const lv_pinyin_dict_t *entry = g_app_pinyin_dict;
    size_t len;

    if (cand_num) {
        *cand_num = 0;
    }
    if (!py || py[0] == '\0' || py[0] == 'i' || py[0] == 'u' || py[0] == 'v') {
        return NULL;
    }

    len = strlen(py);
    while (entry && entry->py && entry->py_mb) {
        if (strncmp(py, entry->py, len) == 0) {
            if (cand_num) {
                *cand_num = (uint16_t)(strlen(entry->py_mb) / 3);
            }
            return (char *)entry->py_mb;
        }
        entry++;
    }

    return NULL;
}

static bool ui_hw_pinyin_get_candidate(ui_hw_pinyin_input_t *ctx,
                                       uint16_t absolute_index,
                                       const char **text,
                                       char *single_buf,
                                       size_t single_buf_len)
{
    if (!ctx || !text) {
        return false;
    }

    if (absolute_index < ctx->single_count) {
        uint32_t byte_offset = (uint32_t)absolute_index * 3U;
        if (!ctx->single_candidates || !single_buf || single_buf_len < 4) {
            return false;
        }
        single_buf[0] = ctx->single_candidates[byte_offset];
        single_buf[1] = ctx->single_candidates[byte_offset + 1];
        single_buf[2] = ctx->single_candidates[byte_offset + 2];
        single_buf[3] = '\0';
        *text = single_buf;
        return true;
    }

    absolute_index = (uint16_t)(absolute_index - ctx->single_count);
    if (absolute_index >= ctx->phrase_count || !ctx->phrase_words[absolute_index]) {
        return false;
    }

    *text = ctx->phrase_words[absolute_index];
    return true;
}

static void ui_hw_pinyin_hide_digit_labels(ui_hw_pinyin_input_t *ctx)
{
    if (!ctx) {
        return;
    }

    for (uint8_t i = 0; i < UI_HW_PINYIN_PAGE_SIZE; i++) {
        if (ctx->digit_labels[i]) {
            lv_obj_add_flag(ctx->digit_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_hw_pinyin_position_digit_labels(ui_hw_pinyin_input_t *ctx, uint8_t count)
{
    lv_coord_t panel_w;
    lv_coord_t panel_h;
    lv_coord_t pad_left;
    lv_coord_t pad_right;
    lv_coord_t pad_top;
    lv_coord_t pad_bottom;
    lv_coord_t gap;
    lv_coord_t content_w;
    lv_coord_t content_h;
    lv_coord_t cell_w;
    const uint8_t button_count = UI_HW_PINYIN_PAGE_SIZE + 2;

    if (!ctx || !ctx->cand_matrix || count == 0) {
        ui_hw_pinyin_hide_digit_labels(ctx);
        return;
    }

    if (count > UI_HW_PINYIN_PAGE_SIZE) {
        count = UI_HW_PINYIN_PAGE_SIZE;
    }

    lv_obj_update_layout(ctx->cand_matrix);
    panel_w = lv_obj_get_width(ctx->cand_matrix);
    panel_h = lv_obj_get_height(ctx->cand_matrix);
    if (panel_w <= 0 || panel_h <= 0) {
        ui_hw_pinyin_hide_digit_labels(ctx);
        return;
    }

    pad_left = lv_obj_get_style_pad_left(ctx->cand_matrix, LV_PART_MAIN);
    pad_right = lv_obj_get_style_pad_right(ctx->cand_matrix, LV_PART_MAIN);
    pad_top = lv_obj_get_style_pad_top(ctx->cand_matrix, LV_PART_MAIN);
    pad_bottom = lv_obj_get_style_pad_bottom(ctx->cand_matrix, LV_PART_MAIN);
    gap = lv_obj_get_style_pad_column(ctx->cand_matrix, LV_PART_MAIN);
    content_w = panel_w - pad_left - pad_right;
    content_h = panel_h - pad_top - pad_bottom;
    cell_w = (content_w - (gap * (button_count - 1))) / button_count;
    if (cell_w < 1 || content_h < 1) {
        ui_hw_pinyin_hide_digit_labels(ctx);
        return;
    }

    for (uint8_t i = 0; i < UI_HW_PINYIN_PAGE_SIZE; i++) {
        lv_obj_t *label = ctx->digit_labels[i];

        if (!label) {
            continue;
        }
        if (i >= count) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_coord_t cell_x = pad_left + ((i + 1) * (cell_w + gap));
        lv_obj_set_size(label, 18, content_h);
        lv_obj_set_pos(label, cell_x + 4, pad_top);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    }
}

static void ui_hw_pinyin_apply_panel_hidden(ui_hw_pinyin_input_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->panel) {
        lv_obj_add_flag(ctx->panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->input_bar) {
        lv_obj_add_flag(ctx->input_bar, LV_OBJ_FLAG_HIDDEN);
    }
    ui_hw_pinyin_hide_digit_labels(ctx);
}

void ui_hw_pinyin_input_reposition(ui_hw_pinyin_input_t *ctx)
{
    lv_coord_t width;
    lv_coord_t parent_w;
    lv_coord_t parent_h;
    lv_coord_t panel_h;
    lv_coord_t input_h;
    lv_coord_t cand_h;
    lv_coord_t x;
    lv_coord_t panel_y;
    lv_coord_t input_y;
    bool panel_visible;
    bool input_visible;

    if (!ctx || !ctx->parent) {
        return;
    }

    panel_visible = ctx->panel && !lv_obj_has_flag(ctx->panel, LV_OBJ_FLAG_HIDDEN);
    input_visible = ctx->input_bar && !lv_obj_has_flag(ctx->input_bar, LV_OBJ_FLAG_HIDDEN);
    if (!panel_visible && !input_visible) {
        return;
    }

    lv_obj_update_layout(ctx->parent);
    parent_w = lv_obj_get_width(ctx->parent);
    parent_h = lv_obj_get_height(ctx->parent);
    if (parent_w <= 0) {
        parent_w = lv_display_get_horizontal_resolution(NULL);
    }
    if (parent_h <= 0) {
        parent_h = lv_display_get_vertical_resolution(NULL);
    }

    width = ctx->width > 0 ? ctx->width : parent_w;
    if (parent_w > 0 && width > parent_w) {
        width = parent_w;
    }
    if (width <= 0) {
        width = UI_HW_PINYIN_FALLBACK_W;
    }

    panel_h = ctx->height > 0 ? ctx->height : UI_HW_PINYIN_PANEL_H;
    input_h = UI_HW_PINYIN_INPUT_BAR_H;
    cand_h = panel_visible ? panel_h : 0;
    x = (parent_w > width) ? ((parent_w - width) / 2) : 0;

    if (ctx->panel) {
        lv_obj_set_size(ctx->panel, width, panel_h);
    }
    if (ctx->input_bar) {
        lv_obj_set_size(ctx->input_bar, width, input_h);
    }

    panel_y = parent_h - panel_h;
    if (panel_y < 0) {
        panel_y = 0;
    }
    input_y = parent_h - cand_h - input_h;
    if (input_y < 0) {
        input_y = 0;
    }

    if (panel_visible) {
        lv_obj_set_pos(ctx->panel, x, panel_y);
        lv_obj_move_foreground(ctx->panel);
    }
    if (input_visible) {
        lv_obj_set_pos(ctx->input_bar, x, input_y);
        lv_obj_move_foreground(ctx->input_bar);
    }

    if (panel_visible && ui_hw_pinyin_has_input(ctx)) {
        ui_hw_pinyin_position_digit_labels(ctx, ui_hw_pinyin_page_count(ctx));
    } else {
        ui_hw_pinyin_hide_digit_labels(ctx);
    }
}

static void ui_hw_pinyin_update_panel(ui_hw_pinyin_input_t *ctx)
{
    uint8_t count;
    uint16_t page_offset;

    if (!ctx || !ctx->panel || !ctx->input_label || !ctx->cand_matrix) {
        return;
    }

    if (!ctx->enabled || !ctx->target_ta || !ui_hw_pinyin_has_input(ctx)) {
        ui_hw_pinyin_apply_panel_hidden(ctx);
        return;
    }

    lv_label_set_text(ctx->input_label, ctx->input);
    if (ctx->input_bar) {
        lv_obj_remove_flag(ctx->input_bar, LV_OBJ_FLAG_HIDDEN);
    }

    ctx->cand_map[0] = "<";
    for (uint8_t i = 0; i < UI_HW_PINYIN_PAGE_SIZE; i++) {
        ctx->cand_map[i + 1] = " ";
        ctx->single_page_buf[i][0] = '\0';
    }
    ctx->cand_map[UI_HW_PINYIN_PAGE_SIZE + 1] = ">";
    ctx->cand_map[UI_HW_PINYIN_PAGE_SIZE + 2] = "";

    count = ui_hw_pinyin_page_count(ctx);
    if (count == 0) {
        lv_obj_add_flag(ctx->panel, LV_OBJ_FLAG_HIDDEN);
        ui_hw_pinyin_hide_digit_labels(ctx);
        ui_hw_pinyin_input_reposition(ctx);
        return;
    }

    page_offset = ctx->page * UI_HW_PINYIN_PAGE_SIZE;
    for (uint8_t i = 0; i < count; i++) {
        const char *candidate = NULL;
        if (ui_hw_pinyin_get_candidate(ctx,
                                       (uint16_t)(page_offset + i),
                                       &candidate,
                                       ctx->single_page_buf[i],
                                       sizeof(ctx->single_page_buf[i])) &&
            candidate) {
            ctx->cand_map[i + 1] = candidate;
        }
    }

    lv_buttonmatrix_set_map(ctx->cand_matrix, ctx->cand_map);
    lv_obj_remove_flag(ctx->panel, LV_OBJ_FLAG_HIDDEN);
    ui_hw_pinyin_input_reposition(ctx);
}

static void ui_hw_pinyin_rebuild(ui_hw_pinyin_input_t *ctx)
{
    if (!ctx) {
        return;
    }

    ctx->single_candidates = ui_hw_pinyin_find_single_candidates(ctx->input, &ctx->single_count);
    ctx->phrase_count = 0;
    memset(ctx->phrase_words, 0, sizeof(ctx->phrase_words));
    if (ui_hw_pinyin_phrase_mode_enabled()) {
        ctx->phrase_count = pinyin_phrase_collect(ctx->input, ctx->phrase_words, UI_HW_PINYIN_PHRASE_MAX);
    }
    ctx->page = 0;
    ui_hw_pinyin_update_panel(ctx);
}

void ui_hw_pinyin_input_reset(ui_hw_pinyin_input_t *ctx)
{
    if (!ctx) {
        return;
    }

    ctx->input[0] = '\0';
    ctx->page = 0;
    ctx->single_candidates = NULL;
    ctx->single_count = 0;
    ctx->phrase_count = 0;
    memset(ctx->phrase_words, 0, sizeof(ctx->phrase_words));
    ui_hw_pinyin_apply_panel_hidden(ctx);
}

static bool ui_hw_pinyin_add_char(ui_hw_pinyin_input_t *ctx, char ch)
{
    size_t len;

    if (!ctx || !ctx->enabled || !ctx->target_ta) {
        return false;
    }

    if (ch >= 'A' && ch <= 'Z') {
        ch = (char)(ch - 'A' + 'a');
    }
    if (ch < 'a' || ch > 'z') {
        return false;
    }

    len = strlen(ctx->input);
    if (len >= UI_HW_PINYIN_INPUT_MAX) {
        return true;
    }

    ctx->input[len] = ch;
    ctx->input[len + 1] = '\0';
    ui_hw_pinyin_rebuild(ctx);
    return true;
}

static bool ui_hw_pinyin_backspace(ui_hw_pinyin_input_t *ctx)
{
    size_t len;

    if (!ctx || !ui_hw_pinyin_has_input(ctx)) {
        return false;
    }

    len = strlen(ctx->input);
    ctx->input[len - 1] = '\0';
    if (ctx->input[0] == '\0') {
        ui_hw_pinyin_input_reset(ctx);
    } else {
        ui_hw_pinyin_rebuild(ctx);
    }
    return true;
}

static bool ui_hw_pinyin_commit_text(ui_hw_pinyin_input_t *ctx, const char *text)
{
    if (!ctx || !ctx->target_ta || !text || text[0] == '\0') {
        return false;
    }
    lv_textarea_add_text(ctx->target_ta, text);
    ui_hw_pinyin_input_reset(ctx);
    return true;
}

static bool ui_hw_pinyin_commit_raw(ui_hw_pinyin_input_t *ctx)
{
    char raw[sizeof(ctx->input)];

    if (!ctx || !ui_hw_pinyin_has_input(ctx)) {
        return false;
    }

    snprintf(raw, sizeof(raw), "%s", ctx->input);
    return ui_hw_pinyin_commit_text(ctx, raw);
}

static bool ui_hw_pinyin_commit_page_candidate(ui_hw_pinyin_input_t *ctx, uint8_t page_index)
{
    uint16_t absolute_index;
    const char *candidate = NULL;
    char single_buf[4];

    if (!ctx || page_index >= UI_HW_PINYIN_PAGE_SIZE) {
        return false;
    }

    absolute_index = (uint16_t)(ctx->page * UI_HW_PINYIN_PAGE_SIZE + page_index);
    if (!ui_hw_pinyin_get_candidate(ctx, absolute_index, &candidate, single_buf, sizeof(single_buf))) {
        return false;
    }

    return ui_hw_pinyin_commit_text(ctx, candidate);
}

static bool ui_hw_pinyin_page(ui_hw_pinyin_input_t *ctx, bool next)
{
    uint16_t last_page;

    if (!ctx || !ui_hw_pinyin_has_input(ctx)) {
        return false;
    }

    last_page = ui_hw_pinyin_last_page(ctx);
    if (next) {
        if (ctx->page < last_page) {
            ctx->page++;
        }
    } else if (ctx->page > 0) {
        ctx->page--;
    }
    ui_hw_pinyin_update_panel(ctx);
    return true;
}

static bool ui_hw_pinyin_select_digit(ui_hw_pinyin_input_t *ctx, char digit)
{
    uint8_t idx = 0;

    if (!ctx || !ui_hw_pinyin_has_input(ctx) || !ui_hw_pinyin_index_for_digit(digit, &idx)) {
        return false;
    }

    if (idx >= UI_HW_PINYIN_PAGE_SIZE) {
        return true;
    }
    return ui_hw_pinyin_commit_page_candidate(ctx, idx);
}

static bool ui_hw_pinyin_handle_composition_key(ui_hw_pinyin_input_t *ctx, ui_hw_textarea_key_t key)
{
    if (!ctx || !ui_hw_pinyin_has_input(ctx)) {
        return false;
    }

    switch (key) {
    case UI_HW_TEXTAREA_KEY_SPACE:
        if (ui_hw_pinyin_commit_page_candidate(ctx, 0)) {
            return true;
        }
        return ui_hw_pinyin_commit_raw(ctx);
    case UI_HW_TEXTAREA_KEY_BACKSPACE:
        return ui_hw_pinyin_backspace(ctx);
    case UI_HW_TEXTAREA_KEY_ENTER:
        return ui_hw_pinyin_commit_raw(ctx);
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        ui_hw_pinyin_input_reset(ctx);
        return true;
    case UI_HW_TEXTAREA_KEY_LEFT:
    case UI_HW_TEXTAREA_KEY_UP:
        return ui_hw_pinyin_page(ctx, false);
    case UI_HW_TEXTAREA_KEY_RIGHT:
    case UI_HW_TEXTAREA_KEY_DOWN:
        return ui_hw_pinyin_page(ctx, true);
    case UI_HW_TEXTAREA_KEY_TAB:
        ui_hw_pinyin_input_reset(ctx);
        return false;
    default:
        ui_hw_pinyin_input_reset(ctx);
        return false;
    }
}

static void ui_hw_pinyin_cand_event_cb(lv_event_t *e)
{
    ui_hw_pinyin_input_t *ctx = (ui_hw_pinyin_input_t *)lv_event_get_user_data(e);
    uint32_t id;

    if (!ctx || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    id = lv_buttonmatrix_get_selected_button(ctx->cand_matrix);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }

    if (id == 0) {
        (void)ui_hw_pinyin_page(ctx, false);
    } else if (id == UI_HW_PINYIN_PAGE_SIZE + 1) {
        (void)ui_hw_pinyin_page(ctx, true);
    } else if (id > 0 && id <= UI_HW_PINYIN_PAGE_SIZE) {
        (void)ui_hw_pinyin_commit_page_candidate(ctx, (uint8_t)(id - 1));
    }
}

ui_hw_pinyin_input_t *ui_hw_pinyin_input_create(const ui_hw_pinyin_input_config_t *cfg)
{
    ui_hw_pinyin_input_t *ctx;

    if (!cfg || !cfg->parent) {
        return NULL;
    }

    ctx = (ui_hw_pinyin_input_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    ctx->parent = cfg->parent;
    ctx->target_ta = cfg->target_ta;
    ctx->anchor = cfg->anchor ? cfg->anchor : cfg->target_ta;
    ctx->width = cfg->width;
    ctx->height = cfg->height > 0 ? cfg->height : UI_HW_PINYIN_PANEL_H;
    ctx->enabled = true;

    ctx->panel = lv_obj_create(cfg->parent);
    if (!ctx->panel) {
        free(ctx);
        return NULL;
    }
    lv_obj_set_size(ctx->panel, ctx->width > 0 ? ctx->width : UI_HW_PINYIN_FALLBACK_W, ctx->height);
    lv_obj_set_style_bg_color(ctx->panel, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(ctx->panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->panel, 1, 0);
    lv_obj_set_style_border_color(ctx->panel, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_radius(ctx->panel, 0, 0);
    lv_obj_set_style_pad_all(ctx->panel, 4, 0);
    lv_obj_set_style_pad_gap(ctx->panel, 4, 0);
    lv_obj_clear_flag(ctx->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->panel, LV_OBJ_FLAG_HIDDEN);

    ctx->input_bar = lv_obj_create(cfg->parent);
    if (!ctx->input_bar) {
        lv_obj_del(ctx->panel);
        free(ctx);
        return NULL;
    }
    lv_obj_set_size(ctx->input_bar,
                    ctx->width > 0 ? ctx->width : UI_HW_PINYIN_FALLBACK_W,
                    UI_HW_PINYIN_INPUT_BAR_H);
    lv_obj_set_style_bg_color(ctx->input_bar, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(ctx->input_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->input_bar, 1, 0);
    lv_obj_set_style_border_color(ctx->input_bar, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_radius(ctx->input_bar, 0, 0);
    lv_obj_set_style_pad_left(ctx->input_bar, 12, 0);
    lv_obj_set_style_pad_right(ctx->input_bar, 12, 0);
    lv_obj_set_style_pad_top(ctx->input_bar, 0, 0);
    lv_obj_set_style_pad_bottom(ctx->input_bar, 0, 0);
    lv_obj_clear_flag(ctx->input_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->input_bar, LV_OBJ_FLAG_HIDDEN);

    ctx->input_label = lv_label_create(ctx->input_bar);
    lv_label_set_long_mode(ctx->input_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ctx->input_label, LV_PCT(100));
    lv_obj_set_style_text_font(ctx->input_label, APP_KEYBOARD_FONT, 0);
    lv_obj_set_style_text_color(ctx->input_label, THEME_PRIMARY_LIGHT, 0);
    lv_label_set_text(ctx->input_label, "");
    lv_obj_align(ctx->input_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->cand_matrix = lv_buttonmatrix_create(ctx->panel);
    lv_obj_set_size(ctx->cand_matrix, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ctx->cand_matrix, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->cand_matrix, 0, 0);
    lv_obj_set_style_pad_all(ctx->cand_matrix, 0, 0);
    lv_obj_set_style_pad_gap(ctx->cand_matrix, 4, 0);
    lv_obj_set_style_text_font(ctx->cand_matrix, &APP_IME_CAND_FONT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ctx->cand_matrix, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ctx->cand_matrix, THEME_BG_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(ctx->cand_matrix, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(ctx->cand_matrix, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ctx->cand_matrix, THEME_PRIMARY_ACTIVE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ctx->cand_matrix, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(ctx->cand_matrix, ui_hw_pinyin_cand_event_cb, LV_EVENT_VALUE_CHANGED, ctx);

    ctx->cand_map[0] = "<";
    for (uint8_t i = 0; i < UI_HW_PINYIN_PAGE_SIZE; i++) {
        ctx->cand_map[i + 1] = " ";
    }
    ctx->cand_map[UI_HW_PINYIN_PAGE_SIZE + 1] = ">";
    ctx->cand_map[UI_HW_PINYIN_PAGE_SIZE + 2] = "";
    lv_buttonmatrix_set_map(ctx->cand_matrix, ctx->cand_map);

    for (uint8_t i = 0; i < UI_HW_PINYIN_PAGE_SIZE; i++) {
        char digit_text[2] = { ui_hw_pinyin_digit_for_index(i), '\0' };

        ctx->digit_labels[i] = lv_label_create(ctx->cand_matrix);
        if (!ctx->digit_labels[i]) {
            continue;
        }
        lv_label_set_text(ctx->digit_labels[i], digit_text);
        lv_obj_set_style_text_font(ctx->digit_labels[i], UI_HW_PINYIN_DIGIT_FONT, 0);
        lv_obj_set_style_text_color(ctx->digit_labels[i], THEME_WARNING_LIGHT, 0);
        lv_obj_set_style_text_align(ctx->digit_labels[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_clear_flag(ctx->digit_labels[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(ctx->digit_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    ctx->enabled = app_i18n_default_chinese_input();
    return ctx;
}

void ui_hw_pinyin_input_destroy(ui_hw_pinyin_input_t **ctx_ref)
{
    ui_hw_pinyin_input_t *ctx;

    if (!ctx_ref || !*ctx_ref) {
        return;
    }

    ctx = *ctx_ref;
    if (ctx->panel) {
        lv_obj_del(ctx->panel);
    }
    if (ctx->input_bar) {
        lv_obj_del(ctx->input_bar);
    }
    free(ctx);
    *ctx_ref = NULL;
}

void ui_hw_pinyin_input_set_target(ui_hw_pinyin_input_t *ctx, lv_obj_t *target_ta, lv_obj_t *anchor)
{
    if (!ctx) {
        return;
    }

    if (ctx->target_ta != target_ta) {
        ui_hw_pinyin_input_reset(ctx);
    }
    ctx->target_ta = target_ta;
    ctx->anchor = anchor ? anchor : target_ta;
}

void ui_hw_pinyin_input_set_enabled(ui_hw_pinyin_input_t *ctx, bool enabled)
{
    if (!ctx) {
        return;
    }

    if (ctx->enabled != enabled) {
        ui_hw_pinyin_input_reset(ctx);
    }
    ctx->enabled = enabled;
}

bool ui_hw_pinyin_input_is_enabled(const ui_hw_pinyin_input_t *ctx)
{
    return ctx && ctx->enabled;
}

bool ui_hw_pinyin_input_has_composition(const ui_hw_pinyin_input_t *ctx)
{
    return ui_hw_pinyin_has_input(ctx);
}

bool ui_hw_pinyin_input_handle_event(ui_hw_pinyin_input_t *ctx, const char *txt, size_t len, uint8_t modifier)
{
    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;

    if (!ctx || !ctx->target_ta || !txt || len == 0) {
        return false;
    }

    if ((modifier & UI_HW_MOD_CTRL) != 0U &&
        (modifier & UI_HW_MOD_ALT) == 0U &&
        len == 1 &&
        txt[0] == ' ') {
        ui_hw_pinyin_input_set_enabled(ctx, !ctx->enabled);
        return true;
    }

    if (!ctx->enabled) {
        return false;
    }

    if (ui_hw_textarea_parse_key(txt, len, &key)) {
        return ui_hw_pinyin_handle_composition_key(ctx, key);
    }

    if (ui_hw_textarea_modifier_blocks_text(modifier)) {
        return false;
    }

    if (ui_hw_pinyin_has_input(ctx) && len == 1 && txt[0] >= '0' && txt[0] <= '9') {
        return ui_hw_pinyin_select_digit(ctx, txt[0]);
    }

    bool handled = false;
    for (size_t i = 0; i < len; i++) {
        char ch = txt[i];
        if (ch >= '0' && ch <= '9' && ui_hw_pinyin_has_input(ctx)) {
            handled = ui_hw_pinyin_select_digit(ctx, ch) || handled;
            continue;
        }
        if (ui_hw_pinyin_add_char(ctx, ch)) {
            handled = true;
            continue;
        }
        if (ui_hw_pinyin_has_input(ctx)) {
            (void)ui_hw_pinyin_commit_raw(ctx);
        }
        return handled;
    }

    return handled;
}

#else

struct ui_hw_pinyin_input {
    bool unused;
};

ui_hw_pinyin_input_t *ui_hw_pinyin_input_create(const ui_hw_pinyin_input_config_t *cfg)
{
    (void)cfg;
    return NULL;
}

void ui_hw_pinyin_input_destroy(ui_hw_pinyin_input_t **ctx_ref)
{
    if (ctx_ref) {
        *ctx_ref = NULL;
    }
}

void ui_hw_pinyin_input_set_target(ui_hw_pinyin_input_t *ctx, lv_obj_t *target_ta, lv_obj_t *anchor)
{
    (void)ctx;
    (void)target_ta;
    (void)anchor;
}

void ui_hw_pinyin_input_set_enabled(ui_hw_pinyin_input_t *ctx, bool enabled)
{
    (void)ctx;
    (void)enabled;
}

bool ui_hw_pinyin_input_is_enabled(const ui_hw_pinyin_input_t *ctx)
{
    (void)ctx;
    return false;
}

bool ui_hw_pinyin_input_has_composition(const ui_hw_pinyin_input_t *ctx)
{
    (void)ctx;
    return false;
}

bool ui_hw_pinyin_input_handle_event(ui_hw_pinyin_input_t *ctx, const char *txt, size_t len, uint8_t modifier)
{
    (void)ctx;
    (void)txt;
    (void)len;
    (void)modifier;
    return false;
}

void ui_hw_pinyin_input_reset(ui_hw_pinyin_input_t *ctx)
{
    (void)ctx;
}

void ui_hw_pinyin_input_reposition(ui_hw_pinyin_input_t *ctx)
{
    (void)ctx;
}

#endif
