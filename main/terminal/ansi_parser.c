/**
 * @file ansi_parser.c
 * @brief ANSI/VT100 终端解析器实现
 *
 * 状态机:
 *   NORMAL → ESC → CSI / OSC / ESC_HASH / charset ...
 *
 * 颜色编码:
 *   标准  0-7:  #000000 #CD0000 #00CD00 #CDCD00 #0000EE #CD00CD #00CDCD #E5E5E5
 *   高亮  8-15: #7F7F7F #FF0000 #00FF00 #FFFF00 #5C5CFF #FF00FF #00FFFF #FFFFFF
 *   256色 16-231: 6×6×6 RGB cube
 *   256色 232-255: 灰阶
 */
#include "ansi_parser.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

/* ─── 256 色调色板 ─── */
static const uint32_t COLOR16[16] = {
    0x000000, 0xCD0000, 0x00CD00, 0xCDCD00,
    0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
    0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00,
    0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
};

#define TERM_DEFAULT_FG  0xC0C0C0u
#define TERM_DEFAULT_BG  0x000000u

static uint32_t color256_table[256];
static bool     color_table_ready = false;

static void build_color_table(void)
{
    if (color_table_ready) return;
    /* 0-15: 标准 16 色 */
    for (int i = 0; i < 16; i++) color256_table[i] = COLOR16[i];
    /* 16-231: 6×6×6 RGB cube */
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                uint32_t rv = r ? (uint32_t)(r * 40 + 55) : 0;
                uint32_t gv = g ? (uint32_t)(g * 40 + 55) : 0;
                uint32_t bv = b ? (uint32_t)(b * 40 + 55) : 0;
                color256_table[16 + r * 36 + g * 6 + b] = (rv << 16) | (gv << 8) | bv;
            }
        }
    }
    /* 232-255: 灰阶 */
    for (int i = 0; i < 24; i++) {
        uint32_t v = (uint32_t)(8 + i * 10);
        color256_table[232 + i] = (v << 16) | (v << 8) | v;
    }
    color_table_ready = true;
}

/* ─── 解析器内部状态 ─── */
/* ─── 工具函数 ─── */

static inline term_cell_t *cell(term_state_t *s, int row, int col)
{
    if (s->alt_screen)
        return &s->alt_cells[row][col];
    return &s->cells[row][col];
}

static inline void dirty(term_state_t *s, int row)
{
    s->dirty_rows[row] = true;
}

static void clear_cell(term_state_t *s, int row, int col)
{
    term_cell_t *c = cell(s, row, col);
    c->codepoint = 0;
    c->fg        = s->cur_fg;
    c->bg        = s->cur_bg;
    c->attrs     = 0;
    c->wide      = 0;
}

static void push_history_line(term_state_t *s, const term_cell_t *line)
{
    if (s->alt_screen || s->scroll_top != 0 || !line || !s->history) {
        return;
    }

    int dst;
    if (s->history_count < TERM_HISTORY_ROWS) {
        dst = (s->history_start + s->history_count) % TERM_HISTORY_ROWS;
        s->history_count++;
    } else {
        dst = s->history_start;
        s->history_start = (s->history_start + 1) % TERM_HISTORY_ROWS;
    }

    memcpy(s->history[dst], line, sizeof(s->history[0]));
    s->history_dirty = true;
}

static void clamp_cursor(term_state_t *s)
{
    if (s->cursor_row < 0)           s->cursor_row = 0;
    if (s->cursor_row >= TERM_ROWS)  s->cursor_row = TERM_ROWS - 1;
    if (s->cursor_col < 0)           s->cursor_col = 0;
    if (s->cursor_col >= TERM_COLS)  s->cursor_col = TERM_COLS - 1;
}

/* 在 scroll_top~scroll_bot 范围内向上滚动 n 行 */
static void scroll_up(term_state_t *s, int n)
{
    int top = s->scroll_top;
    int bot = s->scroll_bot;
    if (n <= 0) n = 1;
    for (int rep = 0; rep < n; rep++) {
        const term_cell_t *top_line = s->alt_screen ? s->alt_cells[top] : s->cells[top];
        push_history_line(s, top_line);

        for (int r = top; r < bot; r++) {
            if (s->alt_screen)
                memcpy(s->alt_cells[r], s->alt_cells[r + 1], sizeof(s->alt_cells[0]));
            else
                memcpy(s->cells[r], s->cells[r + 1], sizeof(s->cells[0]));
            dirty(s, r);
        }
        /* 清空底行 */
        for (int c = 0; c < TERM_COLS; c++) clear_cell(s, bot, c);
        dirty(s, bot);
    }
}

static void scroll_down(term_state_t *s, int n)
{
    int top = s->scroll_top;
    int bot = s->scroll_bot;
    if (n <= 0) n = 1;
    for (int rep = 0; rep < n; rep++) {
        for (int r = bot; r > top; r--) {
            if (s->alt_screen)
                memcpy(s->alt_cells[r], s->alt_cells[r - 1], sizeof(s->alt_cells[0]));
            else
                memcpy(s->cells[r], s->cells[r - 1], sizeof(s->cells[0]));
            dirty(s, r);
        }
        for (int c = 0; c < TERM_COLS; c++) clear_cell(s, top, c);
        dirty(s, top);
    }
}

/* ─── 写入一个字符到光标位置 ─── */

static void put_char(term_state_t *s, uint32_t cp)
{
    if (s->cursor_col >= TERM_COLS) {
        /* 自动换行 */
        s->cursor_col = 0;
        s->cursor_row++;
        if (s->cursor_row > s->scroll_bot) {
            s->cursor_row = s->scroll_bot;
            scroll_up(s, 1);
        }
    }

    /* 判断是否为宽字符（CJK、Braille 等） */
    bool is_wide = false;
    if ((cp >= 0x1100 && cp <= 0x115F) ||  /* Hangul Jamo */
        (cp >= 0x2E80 && cp <= 0xA4CF) ||  /* CJK etc. */
        (cp >= 0xA960 && cp <= 0xA97F) ||
        (cp >= 0xAC00 && cp <= 0xD7FF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE6F) ||
        (cp >= 0xFF01 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) || /* Emoji */
        (cp >= 0x20000 && cp <= 0x2FFFD)) {
        is_wide = true;
    }

    term_cell_t *c = cell(s, s->cursor_row, s->cursor_col);
    c->codepoint = cp;
    c->fg        = s->cur_fg;
    c->bg        = s->cur_bg;
    c->attrs     = s->cur_attrs;
    c->wide      = is_wide ? 1 : 0;
    dirty(s, s->cursor_row);

    if (is_wide && s->cursor_col + 1 < TERM_COLS) {
        term_cell_t *c2 = cell(s, s->cursor_row, s->cursor_col + 1);
        c2->codepoint = 0;
        c2->fg        = s->cur_fg;
        c2->bg        = s->cur_bg;
        c2->attrs     = s->cur_attrs;
        c2->wide      = 2;
        s->cursor_col += 2;
    } else {
        s->cursor_col++;
    }
    s->dirty_cursor = true;
}

/* ─── SGR 处理 ─── */

static void handle_sgr(term_state_t *s, int *params, int count)
{
    if (count == 0) {
        /* CSI m → reset */
        s->cur_fg    = TERM_DEFAULT_FG;
        s->cur_bg    = TERM_DEFAULT_BG;
        s->cur_attrs = 0;
        return;
    }

    for (int i = 0; i < count; i++) {
        int p = params[i];
        switch (p) {
            case 0:   s->cur_fg = TERM_DEFAULT_FG; s->cur_bg = TERM_DEFAULT_BG; s->cur_attrs = 0; break;
            case 1:   s->cur_attrs |=  TERM_ATTR_BOLD;      break;
            case 2:   s->cur_attrs |=  TERM_ATTR_DIM;       break;
            case 3:   s->cur_attrs |=  TERM_ATTR_ITALIC;    break;
            case 4:   s->cur_attrs |=  TERM_ATTR_UNDERLINE; break;
            case 5:   s->cur_attrs |=  TERM_ATTR_BLINK;     break;
            case 7:   s->cur_attrs |=  TERM_ATTR_REVERSE;   break;
            case 8:   s->cur_attrs |=  TERM_ATTR_INVISIBLE; break;
            case 9:   s->cur_attrs |=  TERM_ATTR_STRIKE;    break;
            case 22:  s->cur_attrs &= ~(TERM_ATTR_BOLD | TERM_ATTR_DIM);  break;
            case 23:  s->cur_attrs &= ~TERM_ATTR_ITALIC;    break;
            case 24:  s->cur_attrs &= ~TERM_ATTR_UNDERLINE; break;
            case 25:  s->cur_attrs &= ~TERM_ATTR_BLINK;     break;
            case 27:  s->cur_attrs &= ~TERM_ATTR_REVERSE;   break;
            case 28:  s->cur_attrs &= ~TERM_ATTR_INVISIBLE; break;
            case 29:  s->cur_attrs &= ~TERM_ATTR_STRIKE;    break;
            /* 标准 fg */
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                s->cur_fg = color256_table[p - 30]; break;
            case 38: /* 扩展 fg */
                if (i + 1 < count && params[i + 1] == 5 && i + 2 < count) {
                    s->cur_fg = color256_table[params[i + 2] & 0xFF];
                    i += 2;
                } else if (i + 1 < count && params[i + 1] == 2 && i + 4 < count) {
                    s->cur_fg = ((uint32_t)params[i+2] << 16) |
                                ((uint32_t)params[i+3] << 8)  |
                                 (uint32_t)params[i+4];
                    i += 4;
                }
                break;
            case 39:  s->cur_fg = TERM_DEFAULT_FG; break;
            /* 标准 bg */
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                s->cur_bg = color256_table[p - 40]; break;
            case 48: /* 扩展 bg */
                if (i + 1 < count && params[i + 1] == 5 && i + 2 < count) {
                    s->cur_bg = color256_table[params[i + 2] & 0xFF];
                    i += 2;
                } else if (i + 1 < count && params[i + 1] == 2 && i + 4 < count) {
                    s->cur_bg = ((uint32_t)params[i+2] << 16) |
                                ((uint32_t)params[i+3] << 8)  |
                                 (uint32_t)params[i+4];
                    i += 4;
                }
                break;
            case 49:  s->cur_bg = TERM_DEFAULT_BG; break;
            /* 高亮 fg 90-97 */
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                s->cur_fg = color256_table[8 + (p - 90)]; break;
            /* 高亮 bg 100-107 */
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                s->cur_bg = color256_table[8 + (p - 100)]; break;
            default: break;
        }
    }
}

/* ─── CSI 序列处理 ─── */

static void handle_csi(term_state_t *s, parser_ctx_t *ctx, bool priv, char final)
{
    /* 刷新最后一个参数 */
    if (ctx->param_count < MAX_PARAMS) {
        ctx->params[ctx->param_count++] = ctx->cur_param;
    }
    int *p = ctx->params;
    int  n = ctx->param_count;
    int  p0 = (n > 0) ? p[0] : 0;
    int  p1 = (n > 1) ? p[1] : 0;

    if (priv) {
        /* 私有序列 ESC [ ? ... h/l */
        switch (final) {
            case 'h':
                if (p0 == 25) { s->cursor_visible = true; s->dirty_cursor = true; }
                else if (p0 == 1049) {
                    if (!s->alt_screen) {
                        memcpy(s->alt_cells, s->cells, sizeof(s->cells));
                        s->alt_screen = true;
                        ansi_parser_mark_all_dirty(s);
                    }
                }
                break;
            case 'l':
                if (p0 == 25) { s->cursor_visible = false; s->dirty_cursor = true; }
                else if (p0 == 1049) {
                    if (s->alt_screen) {
                        s->alt_screen = false;
                        ansi_parser_mark_all_dirty(s);
                    }
                }
                break;
            default: break;
        }
        return;
    }

    switch (final) {
        case 'A': /* CUU - 光标上移 */
            s->cursor_row -= (p0 ? p0 : 1);
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'B': /* CUD - 光标下移 */
            s->cursor_row += (p0 ? p0 : 1);
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'C': /* CUF - 光标右移 */
            s->cursor_col += (p0 ? p0 : 1);
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'D': /* CUB - 光标左移 */
            s->cursor_col -= (p0 ? p0 : 1);
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'E': /* CNL - 光标下移到行首 */
            s->cursor_row += (p0 ? p0 : 1);
            s->cursor_col = 0;
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'F': /* CPL - 光标上移到行首 */
            s->cursor_row -= (p0 ? p0 : 1);
            s->cursor_col = 0;
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'G': /* CHA - 光标移到指定列 */
            s->cursor_col = (p0 ? p0 - 1 : 0);
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'H': /* CUP - 光标定位 */
        case 'f':
            s->cursor_row = (p0 ? p0 - 1 : 0);
            s->cursor_col = (p1 ? p1 - 1 : 0);
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        case 'J': /* ED - 清屏 */
            if (p0 == 0) {
                /* 从光标到屏幕末尾 */
                for (int c = s->cursor_col; c < TERM_COLS; c++) clear_cell(s, s->cursor_row, c);
                dirty(s, s->cursor_row);
                for (int r = s->cursor_row + 1; r < TERM_ROWS; r++) {
                    for (int c = 0; c < TERM_COLS; c++) clear_cell(s, r, c);
                    dirty(s, r);
                }
            } else if (p0 == 1) {
                /* 从屏幕开头到光标 */
                for (int r = 0; r < s->cursor_row; r++) {
                    for (int c = 0; c < TERM_COLS; c++) clear_cell(s, r, c);
                    dirty(s, r);
                }
                for (int c = 0; c <= s->cursor_col; c++) clear_cell(s, s->cursor_row, c);
                dirty(s, s->cursor_row);
            } else if (p0 == 2 || p0 == 3) {
                /* 清全屏 */
                for (int r = 0; r < TERM_ROWS; r++) {
                    for (int c = 0; c < TERM_COLS; c++) clear_cell(s, r, c);
                    dirty(s, r);
                }
            }
            break;
        case 'K': /* EL - 清行 */
            if (p0 == 0) {
                for (int c = s->cursor_col; c < TERM_COLS; c++) clear_cell(s, s->cursor_row, c);
            } else if (p0 == 1) {
                for (int c = 0; c <= s->cursor_col; c++) clear_cell(s, s->cursor_row, c);
            } else if (p0 == 2) {
                for (int c = 0; c < TERM_COLS; c++) clear_cell(s, s->cursor_row, c);
            }
            dirty(s, s->cursor_row);
            break;
        case 'L': /* IL - 插入 n 行 */ {
            int cnt = p0 ? p0 : 1;
            for (int rep = 0; rep < cnt; rep++) {
                for (int r = s->scroll_bot; r > s->cursor_row; r--) {
                    if (s->alt_screen)
                        memcpy(s->alt_cells[r], s->alt_cells[r-1], sizeof(s->alt_cells[0]));
                    else
                        memcpy(s->cells[r], s->cells[r-1], sizeof(s->cells[0]));
                    dirty(s, r);
                }
                for (int c = 0; c < TERM_COLS; c++) clear_cell(s, s->cursor_row, c);
                dirty(s, s->cursor_row);
            }
            break;
        }
        case 'M': /* DL - 删除 n 行 */
            scroll_up(s, p0 ? p0 : 1);
            break;
        case 'P': /* DCH - 删除 n 字符 */ {
            int cnt = p0 ? p0 : 1;
            int row = s->cursor_row;
            int col = s->cursor_col;
            for (int c = col; c < TERM_COLS - cnt; c++) {
                *cell(s, row, c) = *cell(s, row, c + cnt);
            }
            for (int c = TERM_COLS - cnt; c < TERM_COLS; c++) clear_cell(s, row, c);
            dirty(s, row);
            break;
        }
        case '@': /* ICH - 插入 n 空字符 */ {
            int cnt = p0 ? p0 : 1;
            int row = s->cursor_row;
            int col = s->cursor_col;
            for (int c = TERM_COLS - 1; c >= col + cnt; c--) {
                *cell(s, row, c) = *cell(s, row, c - cnt);
            }
            for (int c = col; c < col + cnt && c < TERM_COLS; c++) clear_cell(s, row, c);
            dirty(s, row);
            break;
        }
        case 'S': /* SU - 向上滚动 */
            scroll_up(s, p0 ? p0 : 1);
            break;
        case 'T': /* SD - 向下滚动 */
            scroll_down(s, p0 ? p0 : 1);
            break;
        case 'm': /* SGR */
            handle_sgr(s, p, n);
            break;
        case 'r': /* DECSTBM - 设置滚动区域 */
            s->scroll_top = (p0 ? p0 - 1 : 0);
            s->scroll_bot = (p1 ? p1 - 1 : TERM_ROWS - 1);
            if (s->scroll_top < 0)            s->scroll_top = 0;
            if (s->scroll_bot >= TERM_ROWS)   s->scroll_bot = TERM_ROWS - 1;
            if (s->scroll_top >= s->scroll_bot) {
                s->scroll_top = 0;
                s->scroll_bot = TERM_ROWS - 1;
            }
            s->cursor_row = 0;
            s->cursor_col = 0;
            s->dirty_cursor = true;
            break;
        case 's': /* 保存光标 */
            s->saved_row = s->cursor_row;
            s->saved_col = s->cursor_col;
            break;
        case 'u': /* 恢复光标 */
            s->cursor_row = s->saved_row;
            s->cursor_col = s->saved_col;
            clamp_cursor(s);
            s->dirty_cursor = true;
            break;
        default: break;
    }
}

/* ─── 公共接口实现 ─── */

void ansi_parser_init(term_state_t *state)
{
    build_color_table();
    term_cell_t (*history)[TERM_COLS] = state->history;
    if (!history) {
        history = heap_caps_calloc(TERM_HISTORY_ROWS, sizeof(history[0]),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!history) {
            history = heap_caps_calloc(TERM_HISTORY_ROWS, sizeof(history[0]),
                                       MALLOC_CAP_8BIT);
        }
    }

    memset(state, 0, sizeof(term_state_t));
    state->history        = history;
    state->cursor_visible = true;
    state->cur_fg         = TERM_DEFAULT_FG;
    state->cur_bg         = TERM_DEFAULT_BG;
    state->scroll_top     = 0;
    state->scroll_bot     = TERM_ROWS - 1;
    if (state->history) {
        memset(state->history, 0, TERM_HISTORY_ROWS * sizeof(state->history[0]));
    }
    /* 初始化所有单元格为默认颜色 */
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            state->cells[r][c].fg = TERM_DEFAULT_FG;
            state->cells[r][c].bg = TERM_DEFAULT_BG;
        }
    }
    ansi_parser_mark_all_dirty(state);
    memset(&state->parser, 0, sizeof(state->parser));
    state->parser.state = ST_NORMAL;
}

void ansi_parser_deinit(term_state_t *state)
{
    if (!state) {
        return;
    }
    if (state->history) {
        heap_caps_free(state->history);
        state->history = NULL;
    }
}

void ansi_parser_mark_all_dirty(term_state_t *state)
{
    for (int r = 0; r < TERM_ROWS; r++) {
        state->dirty_rows[r] = true;
    }
    state->dirty_cursor = true;
}

int ansi_parser_total_rows(const term_state_t *state)
{
    if (!state || state->alt_screen) {
        return TERM_ROWS;
    }

    return state->history_count + TERM_ROWS;
}

const term_cell_t *ansi_parser_get_visual_row(const term_state_t *state, int row)
{
    if (!state || row < 0) {
        return NULL;
    }

    if (state->alt_screen) {
        if (row >= TERM_ROWS) {
            return NULL;
        }
        return state->alt_cells[row];
    }

    if (row < state->history_count) {
        if (!state->history) {
            return NULL;
        }
        int idx = (state->history_start + row) % TERM_HISTORY_ROWS;
        return state->history[idx];
    }

    row -= state->history_count;
    if (row >= TERM_ROWS) {
        return NULL;
    }
    return state->cells[row];
}

void ansi_parser_feed(term_state_t *state, const char *data, size_t len)
{
    parser_ctx_t *ctx = state ? &state->parser : NULL;

    if (!state || !data || !ctx) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char byte = (unsigned char)data[i];

        /* ── UTF-8 多字节继续字节 ── */
        if (ctx->state == ST_UTF8) {
            if ((byte & 0xC0) == 0x80) {
                ctx->utf8_cp = (ctx->utf8_cp << 6) | (byte & 0x3F);
                ctx->utf8_rem--;
                if (ctx->utf8_rem == 0) {
                    if (ctx->utf8_cp >= ctx->utf8_min) {
                        put_char(state, ctx->utf8_cp);
                    }
                    ctx->state = ST_NORMAL;
                }
            } else {
                /* 格式错误，回退 */
                ctx->state = ST_NORMAL;
                /* 重新处理本字节 */
                i--;
            }
            continue;
        }

        switch (ctx->state) {
            case ST_NORMAL:
                if (byte == 0x1B) {
                    ctx->state = ST_ESC;
                } else if (byte == 0x0D) {
                    /* CR */
                    state->cursor_col = 0;
                    state->dirty_cursor = true;
                } else if (byte == 0x0A || byte == 0x0B || byte == 0x0C) {
                    /* LF / VT / FF */
                    state->cursor_row++;
                    if (state->cursor_row > state->scroll_bot) {
                        state->cursor_row = state->scroll_bot;
                        scroll_up(state, 1);
                    }
                    state->dirty_cursor = true;
                } else if (byte == 0x08) {
                    /* BS */
                    if (state->cursor_col > 0) state->cursor_col--;
                    state->dirty_cursor = true;
                } else if (byte == 0x09) {
                    /* TAB → 下一个 8 的倍数列 */
                    state->cursor_col = ((state->cursor_col / 8) + 1) * 8;
                    if (state->cursor_col >= TERM_COLS) state->cursor_col = TERM_COLS - 1;
                    state->dirty_cursor = true;
                } else if (byte == 0x07 || byte == 0x00 || byte == 0x01 ||
                           byte == 0x02 || byte == 0x03 || byte == 0x05 ||
                           byte == 0x06 || byte == 0x0E || byte == 0x0F) {
                    /* 忽略 BEL, NUL, SO/SI 等 */
                } else if (byte < 0x20) {
                    /* 其他控制字符忽略 */
                } else if (byte < 0x80) {
                    /* ASCII 可打印字符 */
                    put_char(state, (uint32_t)byte);
                } else if ((byte & 0xE0) == 0xC0) {
                    /* UTF-8 2 字节序列 */
                    ctx->utf8_cp  = byte & 0x1F;
                    ctx->utf8_rem = 1;
                    ctx->utf8_min = 0x80;
                    ctx->state    = ST_UTF8;
                } else if ((byte & 0xF0) == 0xE0) {
                    /* UTF-8 3 字节序列 */
                    ctx->utf8_cp  = byte & 0x0F;
                    ctx->utf8_rem = 2;
                    ctx->utf8_min = 0x800;
                    ctx->state    = ST_UTF8;
                } else if ((byte & 0xF8) == 0xF0) {
                    /* UTF-8 4 字节序列 */
                    ctx->utf8_cp  = byte & 0x07;
                    ctx->utf8_rem = 3;
                    ctx->utf8_min = 0x10000;
                    ctx->state    = ST_UTF8;
                }
                break;

            case ST_ESC:
                if (byte == '[') {
                    ctx->state       = ST_CSI;
                    ctx->param_count = 0;
                    ctx->cur_param   = 0;
                } else if (byte == ']') {
                    ctx->state   = ST_OSC;
                    ctx->osc_len = 0;
                } else if (byte == '7') {
                    state->saved_row = state->cursor_row;
                    state->saved_col = state->cursor_col;
                    ctx->state = ST_NORMAL;
                } else if (byte == '8') {
                    state->cursor_row = state->saved_row;
                    state->cursor_col = state->saved_col;
                    clamp_cursor(state);
                    state->dirty_cursor = true;
                    ctx->state = ST_NORMAL;
                } else if (byte == 'M') {
                    /* RI - 反向换行 */
                    if (state->cursor_row == state->scroll_top) {
                        scroll_down(state, 1);
                    } else {
                        state->cursor_row--;
                        if (state->cursor_row < 0) state->cursor_row = 0;
                    }
                    state->dirty_cursor = true;
                    ctx->state = ST_NORMAL;
                } else if (byte == 'c') {
                    /* RIS - 全复位 */
                    ansi_parser_init(state);
                    ctx->state = ST_NORMAL;
                } else if (byte == '(' || byte == ')' || byte == '*' || byte == '+') {
                    /* 字符集选择（忽略，下一字节消耗） */
                    ctx->state = ST_NORMAL; /* 简化：直接忽略 designator 字节 */
                    if (i + 1 < len) i++; /* 跳过 designator */
                } else {
                    ctx->state = ST_NORMAL;
                }
                break;

            case ST_CSI:
                if (byte == '?') {
                    ctx->state = ST_CSI_PRIV;
                } else if (byte >= '0' && byte <= '9') {
                    ctx->cur_param = ctx->cur_param * 10 + (byte - '0');
                } else if (byte == ';') {
                    if (ctx->param_count < MAX_PARAMS)
                        ctx->params[ctx->param_count++] = ctx->cur_param;
                    ctx->cur_param = 0;
                } else if (byte >= 0x40 && byte <= 0x7E) {
                    /* Final byte */
                    handle_csi(state, ctx, false, (char)byte);
                    ctx->state = ST_NORMAL;
                } else if (byte == 0x1B) {
                    /* ESC 中途出现 → 重置 */
                    ctx->state = ST_ESC;
                }
                break;

            case ST_CSI_PRIV:
                if (byte >= '0' && byte <= '9') {
                    ctx->cur_param = ctx->cur_param * 10 + (byte - '0');
                } else if (byte == ';') {
                    if (ctx->param_count < MAX_PARAMS)
                        ctx->params[ctx->param_count++] = ctx->cur_param;
                    ctx->cur_param = 0;
                } else if (byte >= 0x40 && byte <= 0x7E) {
                    handle_csi(state, ctx, true, (char)byte);
                    ctx->state = ST_NORMAL;
                } else if (byte == 0x1B) {
                    ctx->state = ST_ESC;
                }
                break;

            case ST_OSC:
                if (byte == 0x07 ||
                    (byte == 0x5C && ctx->osc_len > 0 &&
                     ctx->osc_buf[ctx->osc_len - 1] == 0x1B)) {
                    /* OSC 终止（BEL 或 ST = ESC \） */
                    ctx->osc_len = 0;
                    ctx->state   = ST_NORMAL;
                } else if (byte == 0x1B) {
                    /* 可能是 ESC \ 终止 */
                    if (ctx->osc_len < MAX_OSC_LEN - 1)
                        ctx->osc_buf[ctx->osc_len++] = (char)byte;
                } else {
                    if (ctx->osc_len < MAX_OSC_LEN - 1)
                        ctx->osc_buf[ctx->osc_len++] = (char)byte;
                }
                break;

            default:
                ctx->state = ST_NORMAL;
                break;
        }
    }
}
