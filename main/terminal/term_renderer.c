/**
 * @file term_renderer.c
 * @brief LVGL Canvas 终端渲染器实现
 *
 * 渲染流程（每次 term_renderer_refresh 调用）：
 *   1. lv_canvas_init_layer 获取 layer
 *   2. 遍历 dirty_rows，对每个脏行的每个单元格：
 *      a. lv_draw_rect 绘制背景色
 *      b. lv_draw_label 绘制字符
 *   3. 如果 dirty_cursor，在光标处绘制光标矩形
 *   4. lv_canvas_finish_layer 提交绘制
 *
 * 内存：canvas 像素缓冲区（TERM_COLS × (TERM_HISTORY_ROWS + TERM_ROWS)）通过 heap_caps_malloc
 * 从 SPIRAM 分配。
 */
#include "term_renderer.h"
#include "term_special_chars.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "term_renderer";

/* Canvas 像素缓冲区（RGB565） */
#define CANVAS_W  (TERM_COLS * TERM_CELL_W)
#define CANVAS_ROWS (TERM_HISTORY_ROWS + TERM_ROWS)
#define CANVAS_H  (CANVAS_ROWS * TERM_CELL_H)

static const lv_font_t *s_font = &APP_FONT;

/* ─── 颜色转换 RGB888 → lv_color_t ─── */
static inline lv_color_t rgb888_to_lv(uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >>  8) & 0xFF;
    uint8_t b =  rgb        & 0xFF;
    return lv_color_make(r, g, b);
}

/* ─── 公共接口 ─── */

void term_renderer_set_font(const lv_font_t *font)
{
    if (font) s_font = font;
}

static void on_canvas_delete(lv_event_t *e)
{
    void *buf = lv_event_get_user_data(e);
    if (buf) {
        heap_caps_free(buf);
        ESP_LOGI(TAG, "Canvas buffer freed: %p", buf);
    }
}

lv_obj_t *term_renderer_create(lv_obj_t *parent)
{
    /* 从 PSRAM 分配像素缓冲区 */
    size_t buf_size = (size_t)CANVAS_W * CANVAS_H * sizeof(uint16_t); /* RGB565 */
    void *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to alloc canvas buffer (%u bytes)", (unsigned)buf_size);
        return NULL;
    }
    memset(buf, 0, buf_size);

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_event_cb(canvas, on_canvas_delete, LV_EVENT_DELETE, buf);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_size(canvas, CANVAS_W, CANVAS_H);

    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    ESP_LOGI(TAG, "Canvas created: %dx%d bytes=%u, buf=%p",
             CANVAS_W, CANVAS_H, (unsigned)buf_size, buf);
    return canvas;
}

/* ─── 绘制单行 ─── */

int term_renderer_get_canvas_height(void)
{
    return CANVAS_H;
}

static int visual_base_row(const term_state_t *state)
{
    int total_rows = ansi_parser_total_rows(state);
    int base_row = CANVAS_ROWS - total_rows;
    return base_row > 0 ? base_row : 0;
}

static void render_row(lv_layer_t *layer, const term_cell_t *row_cells, int canvas_row)
{
    for (int col = 0; col < TERM_COLS; col++) {
        const term_cell_t *c = &row_cells[col];

        /* 宽字符右半占位格不单独绘制 */
        if (c->wide == 2) continue;

        int cell_w = (c->wide == 1) ? TERM_CELL_W * 2 : TERM_CELL_W;
        int x      = col * TERM_CELL_W;
        int y      = canvas_row * TERM_CELL_H;

        uint32_t fg = c->fg;
        uint32_t bg = c->bg;

        /* REVERSE 属性：交换前景/背景 */
        if (c->attrs & TERM_ATTR_REVERSE) {
            uint32_t tmp = fg; fg = bg; bg = tmp;
        }

        /* 绘制背景矩形 */
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color   = rgb888_to_lv(bg);
        rect_dsc.bg_opa     = LV_OPA_COVER;
        rect_dsc.border_opa = LV_OPA_TRANSP;
        rect_dsc.radius     = 0;

        lv_area_t bg_area = {
            .x1 = x, .y1 = y,
            .x2 = x + cell_w - 1, .y2 = y + TERM_CELL_H - 1,
        };
        lv_draw_rect(layer, &rect_dsc, &bg_area);

        /* 绘制字符（若非空） */
        if (c->codepoint > 0 && !(c->attrs & TERM_ATTR_INVISIBLE)) {
            /* 将 Unicode 码点编码为 UTF-8 用于 lv_draw_label */
            char utf8[8] = {0};
            uint32_t cp = c->codepoint;
            if (cp < 0x80) {
                utf8[0] = (char)cp;
            } else if (cp < 0x800) {
                utf8[0] = (char)(0xC0 | (cp >> 6));
                utf8[1] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                utf8[0] = (char)(0xE0 | (cp >> 12));
                utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[2] = (char)(0x80 | (cp & 0x3F));
            } else {
                utf8[0] = (char)(0xF0 | (cp >> 18));
                utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                utf8[2] = (char)(0x80 | ((cp >>  6) & 0x3F));
                utf8[3] = (char)(0x80 | (cp & 0x3F));
            }

            lv_color_t txt_color = rgb888_to_lv(fg);
            /* DIM 属性：降低亮度 */
            if (c->attrs & TERM_ATTR_DIM) {
                txt_color = lv_color_mix(txt_color, lv_color_black(), 128);
            }

            /* ── 特殊字符软件渲染（box drawing / block / braille / 几何箭头）── */
            bool drawn_special = term_render_special_char(
                layer, cp, x, y, cell_w, TERM_CELL_H, txt_color);

            if (!drawn_special) {
                lv_draw_label_dsc_t lbl_dsc;
                lv_draw_label_dsc_init(&lbl_dsc);
                lbl_dsc.font         = s_font;
                lbl_dsc.color        = txt_color;
                lbl_dsc.opa          = LV_OPA_COVER;
                lbl_dsc.align        = LV_TEXT_ALIGN_LEFT;
                lbl_dsc.text         = utf8;
                lbl_dsc.text_local   = 1;

                lv_area_t txt_area = {
                    .x1 = x, .y1 = y,
                    .x2 = x + cell_w - 1, .y2 = y + TERM_CELL_H - 1,
                };
                lv_draw_label(layer, &lbl_dsc, &txt_area);
            }

            /* UNDERLINE 属性：绘制下划线 */
            if (c->attrs & TERM_ATTR_UNDERLINE) {
                lv_draw_rect_dsc_t ul_dsc;
                lv_draw_rect_dsc_init(&ul_dsc);
                ul_dsc.bg_color = txt_color;
                ul_dsc.bg_opa   = LV_OPA_COVER;
                ul_dsc.radius   = 0;
                lv_area_t ul_area = {
                    .x1 = x, .y1 = y + TERM_CELL_H - 2,
                    .x2 = x + cell_w - 1, .y2 = y + TERM_CELL_H - 1,
                };
                lv_draw_rect(layer, &ul_dsc, &ul_area);
            }

            /* STRIKE 属性：绘制删除线 */
            if (c->attrs & TERM_ATTR_STRIKE) {
                lv_draw_rect_dsc_t st_dsc;
                lv_draw_rect_dsc_init(&st_dsc);
                st_dsc.bg_color = txt_color;
                st_dsc.bg_opa   = LV_OPA_COVER;
                st_dsc.radius   = 0;
                lv_area_t st_area = {
                    .x1 = x, .y1 = y + TERM_CELL_H / 2,
                    .x2 = x + cell_w - 1, .y2 = y + TERM_CELL_H / 2 + 1,
                };
                lv_draw_rect(layer, &st_dsc, &st_area);
            }
        }
    }
}

/* ─── 绘制光标 ─── */

static void render_cursor(lv_layer_t *layer, const term_state_t *state)
{
    if (!state->cursor_visible) return;

    int visual_row = state->alt_screen ? state->cursor_row :
                     state->history_count + state->cursor_row;
    int x = state->cursor_col * TERM_CELL_W;
    int y = (visual_base_row(state) + visual_row) * TERM_CELL_H;

    lv_draw_rect_dsc_t cur_dsc;
    lv_draw_rect_dsc_init(&cur_dsc);
    cur_dsc.bg_color   = lv_color_white();
    cur_dsc.bg_opa     = LV_OPA_50;     /* 半透明光标块 */
    cur_dsc.border_opa = LV_OPA_TRANSP;
    cur_dsc.radius     = 0;

    lv_area_t cur_area = {
        .x1 = x, .y1 = y,
        .x2 = x + TERM_CELL_W - 1, .y2 = y + TERM_CELL_H - 1,
    };
    lv_draw_rect(layer, &cur_dsc, &cur_area);
}

/* ─── 公共刷新接口 ─── */

void term_renderer_refresh(lv_obj_t *canvas, term_state_t *state)
{
    /* 检查是否有脏行或光标需要更新 */
    bool need_draw = state->dirty_cursor || state->history_dirty;
    for (int r = 0; r < TERM_ROWS && !need_draw; r++) {
        if (state->dirty_rows[r]) need_draw = true;
    }
    if (!need_draw) return;

    /*
     * 逐行单独 init/finish_layer，避免 draw task 积压撑爆 64KB LVGL 内存池。
     * 每行最多 80×2=160 个 task，峰值内存 <32KB，远低于 64KB 上限。
     */
    int base_row = visual_base_row(state);
    if (state->history_dirty) {
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
        ansi_parser_mark_all_dirty(state);
        state->history_dirty = false;
        for (int r = 0; r < state->history_count; r++) {
            const term_cell_t *row_cells = ansi_parser_get_visual_row(state, r);
            if (!row_cells) continue;
            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);
            render_row(&layer, row_cells, base_row + r);
            lv_canvas_finish_layer(canvas, &layer);
        }
    }

    int screen_row_offset = state->alt_screen ? 0 : state->history_count;
    for (int r = 0; r < TERM_ROWS; r++) {
        if (!state->dirty_rows[r]) continue;
        const term_cell_t *row_cells = ansi_parser_get_visual_row(state, screen_row_offset + r);
        if (!row_cells) continue;
        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);
        render_row(&layer, row_cells, base_row + screen_row_offset + r);
        state->dirty_rows[r] = false;
        lv_canvas_finish_layer(canvas, &layer);
    }

    /* 绘制光标 */
    if (state->dirty_cursor) {
        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);
        render_cursor(&layer, state);
        state->dirty_cursor = false;
        lv_canvas_finish_layer(canvas, &layer);
    }

    /*
     * ESP32-P4 双核 SMP 缓存一致性修复：
     * swdraw 线程（无核绑定）可能在 Core 1 写入 canvas PSRAM 缓冲区，
     * 脏行存在于 Core 1 的 L1 D-cache 中，尚未刷入 L2/物理内存。
     * LVGL 渲染任务（可能在 Core 0）读取同一缓冲区时会得到陈旧数据（黑色）。
     * esp_cache_msync C2M 将所有核的 L1 脏行写回物理内存，
     * 确保后续任意核读取时拿到最新像素数据。
     */
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(canvas);
    if (draw_buf && draw_buf->data) {
        esp_cache_msync((void *)draw_buf->data,
                        (size_t)draw_buf->header.stride * draw_buf->header.h,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
}

void term_renderer_full_refresh(lv_obj_t *canvas, term_state_t *state)
{
    ansi_parser_mark_all_dirty(state);
    term_renderer_refresh(canvas, state);
}
