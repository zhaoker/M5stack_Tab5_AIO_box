/**
 * @file term_special_chars.c
 * @brief 终端特殊字符软件渲染实现
 *
 * 渲染策略：
 *   Box Drawing  — 用 1-2px 矩形沿格子中心线绘制，边界延伸到格子边缘（确保邻接连续）
 *   Block Elements — 按字符定义填充格子的对应区域
 *   Braille       — 按 8-bit 编码在 2×4 网格上绘制圆点
 *   Geometric/Arrows — 简单填充/三角
 */
#include "term_special_chars.h"

/* ─── 内部工具 ─── */

static inline void fill_rect(lv_layer_t *layer, lv_color_t color,
                              int x1, int y1, int x2, int y2)
{
    if (x1 > x2 || y1 > y2) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color   = color;
    d.bg_opa     = LV_OPA_COVER;
    d.border_opa = LV_OPA_TRANSP;
    d.radius     = 0;
    lv_area_t a  = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

static inline int iabs_int(int v)
{
    return v < 0 ? -v : v;
}

static inline int imin_int(int a, int b)
{
    return a < b ? a : b;
}

static inline int imax_int(int a, int b)
{
    return a > b ? a : b;
}

static void draw_line(lv_layer_t *layer, lv_color_t color,
                      int x0, int y0, int x1, int y1, int w)
{
    int dx = iabs_int(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -iabs_int(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int half = w > 1 ? w / 2 : 0;

    while (true) {
        fill_rect(layer, color, x0 - half, y0 - half, x0 + half, y0 + half);
        if (x0 == x1 && y0 == y1) break;

        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_hseg(lv_layer_t *layer, lv_color_t color,
                      int x1, int x2, int y, int w)
{
    int half = w / 2;
    fill_rect(layer, color, x1, y - half, x2, y - half + w - 1);
}

static void draw_vseg(lv_layer_t *layer, lv_color_t color,
                      int x, int y1, int y2, int w)
{
    int half = w / 2;
    fill_rect(layer, color, x - half, y1, x - half + w - 1, y2);
}

static void fill_disc(lv_layer_t *layer, lv_color_t color,
                      int cx, int cy, int radius)
{
    int rr = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= rr) {
                fill_rect(layer, color, cx + x, cy + y, cx + x, cy + y);
            }
        }
    }
}

static bool render_seven_segment_digit(lv_layer_t *layer, uint8_t digit,
                                       int cx, int cy, int cw, int ch,
                                       lv_color_t fg)
{
    enum {
        SEG_A = 0x01,
        SEG_B = 0x02,
        SEG_C = 0x04,
        SEG_D = 0x08,
        SEG_E = 0x10,
        SEG_F = 0x20,
        SEG_G = 0x40,
    };
    static const uint8_t map[10] = {
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
        SEG_B | SEG_C,
        SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,
        SEG_B | SEG_C | SEG_F | SEG_G,
        SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,
        SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
        SEG_A | SEG_B | SEG_C,
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,
    };

    if (digit > 9) {
        return false;
    }

    int stroke = imax_int(1, imin_int(cw, ch) / 8);
    int x1 = cx + imax_int(1, cw / 5);
    int x2 = cx + cw - imax_int(2, cw / 5) - 1;
    int y1 = cy + imax_int(2, ch / 6);
    int y2 = cy + ch / 2;
    int y3 = cy + ch - imax_int(3, ch / 6) - 1;
    uint8_t seg = map[digit];

    if (seg & SEG_A) draw_hseg(layer, fg, x1, x2, y1, stroke);
    if (seg & SEG_B) draw_vseg(layer, fg, x2, y1, y2, stroke);
    if (seg & SEG_C) draw_vseg(layer, fg, x2, y2, y3, stroke);
    if (seg & SEG_D) draw_hseg(layer, fg, x1, x2, y3, stroke);
    if (seg & SEG_E) draw_vseg(layer, fg, x1, y2, y3, stroke);
    if (seg & SEG_F) draw_vseg(layer, fg, x1, y1, y2, stroke);
    if (seg & SEG_G) draw_hseg(layer, fg, x1, x2, y2, stroke);
    return true;
}

static void stroke_circle(lv_layer_t *layer, lv_color_t color,
                          int cx, int cy, int radius, int width)
{
    int outer = radius * radius;
    int inner_r = radius - width;
    int inner = inner_r > 0 ? inner_r * inner_r : 0;

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int d = x * x + y * y;
            if (d <= outer && d >= inner) {
                fill_rect(layer, color, cx + x, cy + y, cx + x, cy + y);
            }
        }
    }
}

/* ─── Box Drawing (U+2500-U+257F) ─── */

static bool render_box_drawing(lv_layer_t *layer, uint32_t cp,
                                int cx, int cy, int cw, int ch,
                                lv_color_t fg)
{
    /* 格子中心线位置 */
    int mx  = cx + cw / 2;
    int my  = cy + ch / 2;
    int r   = cx + cw - 1;
    int b   = cy + ch - 1;
    int lw  = 2;   /* 细线宽度（像素） */
    int tlw = 3;   /* 粗/双线宽度 */

    /* 用 5 个标志描述各方向线段：top/bot/left/right + 是否粗线 */
    bool top=0, bot=0, left=0, right=0, thick=0;

    switch (cp) {
        /* ─── 细横线 ─── */
        case 0x2500: left=right=1; break;           /* ─ */
        case 0x2508: case 0x250A: left=right=1; break; /* ╌ ╎ (虚线，渲染为实线) */
        /* ─── 细竖线 ─── */
        case 0x2502: top=bot=1; break;              /* │ */
        case 0x2504: case 0x2506: top=bot=1; break; /* ╍ ╎ */
        /* ─── 粗横线 ─── */
        case 0x2501: left=right=thick=1; break;     /* ━ */
        /* ─── 粗竖线 ─── */
        case 0x2503: top=bot=thick=1; break;        /* ┃ */
        /* ─── 细直角 ─── */
        case 0x250C: bot=right=1; break;            /* ┌ */
        case 0x2510: bot=left=1; break;             /* ┐ */
        case 0x2514: top=right=1; break;            /* └ */
        case 0x2518: top=left=1; break;             /* ┘ */
        /* ─── T形 ─── */
        case 0x251C: top=bot=right=1; break;        /* ├ */
        case 0x2524: top=bot=left=1; break;         /* ┤ */
        case 0x252C: bot=left=right=1; break;       /* ┬ */
        case 0x2534: top=left=right=1; break;       /* ┴ */
        case 0x253C: top=bot=left=right=1; break;   /* ┼ */
        /* ─── 圆角（渲染为普通角）─── */
        case 0x256D: bot=right=1; break;            /* ╭ */
        case 0x256E: bot=left=1; break;             /* ╮ */
        case 0x256F: top=left=1; break;             /* ╯ */
        case 0x2570: top=right=1; break;            /* ╰ */
        /* ─── 双横线 / 双竖线 ─── */
        case 0x2550: left=right=thick=1; break;     /* ═ */
        case 0x2551: top=bot=thick=1; break;        /* ║ */
        /* ─── 双线直角（简化为单线）─── */
        case 0x2554: bot=right=thick=1; break;      /* ╔ */
        case 0x2557: bot=left=thick=1; break;       /* ╗ */
        case 0x255A: top=right=thick=1; break;      /* ╚ */
        case 0x255D: top=left=thick=1; break;       /* ╝ */
        case 0x2560: top=bot=right=thick=1; break;  /* ╠ */
        case 0x2563: top=bot=left=thick=1; break;   /* ╣ */
        case 0x2566: bot=left=right=thick=1; break; /* ╦ */
        case 0x2569: top=left=right=thick=1; break; /* ╩ */
        case 0x256C: top=bot=left=right=thick=1; break; /* ╬ */
        /* ─── 混合（简化处理）─── */
        case 0x251D: top=bot=right=1; break;        /* ┝ */
        case 0x2525: top=bot=left=1; break;         /* ┥ */
        case 0x252F: bot=left=right=1; break;       /* ┯ */
        case 0x2537: top=left=right=1; break;       /* ┷ */
        case 0x253F: top=bot=left=right=1; break;   /* ┿ */
        /* ─── 竖线与单横混合 ─── */
        case 0x255E: top=bot=right=1; break;        /* ╞ */
        case 0x2561: top=bot=left=1; break;         /* ╡ */
        case 0x2564: bot=left=right=1; break;       /* ╤ */
        case 0x2567: top=left=right=1; break;       /* ╧ */
        case 0x256A: top=bot=left=right=1; break;   /* ╪ */
        default: return false;
    }

    int w = thick ? tlw : lw;
    int hw = w / 2;     /* 线宽的一半，用于居中 */

    /* 中心点（确保偶数宽度时对齐） */
    int cmx = mx - hw;
    int cmy = my - hw;

    if (left)  fill_rect(layer, fg, cx,    cmy, cmx,      cmy + w - 1);
    if (right) fill_rect(layer, fg, cmx,   cmy, r,        cmy + w - 1);
    if (top)   fill_rect(layer, fg, cmx,   cy,  cmx + w - 1, cmy);
    if (bot)   fill_rect(layer, fg, cmx,   cmy, cmx + w - 1, b);

    return true;
}

/* ─── Block Elements (U+2580-U+259F) ─── */

static bool render_block_element(lv_layer_t *layer, uint32_t cp,
                                  int cx, int cy, int cw, int ch,
                                  lv_color_t fg)
{
    int r  = cx + cw - 1;
    int b  = cy + ch - 1;
    int mx = cx + cw / 2;
    int my = cy + ch / 2;

    /* 预计算常用 Y 分隔线 */
    int y1 = cy + ch / 8;
    int y3 = cy + ch * 3 / 8;
    int y5 = cy + ch * 5 / 8;
    int y7 = cy + ch * 7 / 8;

    /* 预计算常用 X 分隔线 */
    int x1 = cx + cw / 8;
    int x3 = cx + cw * 3 / 8;
    int x5 = cx + cw * 5 / 8;
    int x7 = cx + cw * 7 / 8;

    switch (cp) {
        /* 下沿块系列 ─── */
        case 0x2580: fill_rect(layer, fg, cx, cy, r, my - 1); break;      /* ▀ 上半 */
        case 0x2581: fill_rect(layer, fg, cx, y7, r, b); break;           /* ▁ 下1/8 */
        case 0x2582: fill_rect(layer, fg, cx, my + ch/4, r, b); break;    /* ▂ 下1/4 */
        case 0x2583: fill_rect(layer, fg, cx, y5, r, b); break;           /* ▃ 下3/8 */
        case 0x2584: fill_rect(layer, fg, cx, my, r, b); break;           /* ▄ 下半 */
        case 0x2585: fill_rect(layer, fg, cx, y3, r, b); break;           /* ▅ 下5/8 */
        case 0x2586: fill_rect(layer, fg, cx, cy + ch/4, r, b); break;    /* ▆ 下3/4 */
        case 0x2587: fill_rect(layer, fg, cx, y1, r, b); break;           /* ▇ 下7/8 */
        case 0x2588: fill_rect(layer, fg, cx, cy, r, b); break;           /* █ 全块 */
        /* 右宽块系列 ─── */
        case 0x2589: fill_rect(layer, fg, cx, cy, x7 - 1, b); break;      /* ▉ 左7/8 */
        case 0x258A: fill_rect(layer, fg, cx, cy, mx + cw/4 - 1, b); break;/* ▊ 左3/4 */
        case 0x258B: fill_rect(layer, fg, cx, cy, x5 - 1, b); break;      /* ▋ 左5/8 */
        case 0x258C: fill_rect(layer, fg, cx, cy, mx - 1, b); break;      /* ▌ 左半 */
        case 0x258D: fill_rect(layer, fg, cx, cy, x3 - 1, b); break;      /* ▍ 左3/8 */
        case 0x258E: fill_rect(layer, fg, cx, cy, cx + cw/4 - 1, b); break;/* ▎ 左1/4 */
        case 0x258F: fill_rect(layer, fg, cx, cy, x1 - 1, b); break;      /* ▏ 左1/8 */
        case 0x2590: fill_rect(layer, fg, mx, cy, r, b); break;            /* ▐ 右半 */
        /* 色调块 ─── */
        case 0x2591: /* ░ 淡色 — 25% */
            for (int dy = cy; dy <= b; dy += 4)
                for (int dx = cx + ((dy / 4) & 1) * 2; dx <= r; dx += 4)
                    fill_rect(layer, fg, dx, dy, dx + 1, dy + 1);
            break;
        case 0x2592: /* ▒ 中色 — 50% */
            for (int dy = cy; dy <= b; dy += 2)
                for (int dx = cx + ((dy / 2) & 1); dx <= r; dx += 2)
                    fill_rect(layer, fg, dx, dy, dx, dy);
            break;
        case 0x2593: /* ▓ 深色 — 75% */
            for (int dy = cy; dy <= b; dy += 4)
                for (int dx = cx + ((dy / 4) & 1) * 2; dx <= r; dx += 4)
                    fill_rect(layer, fg, dx, dy, dx + 3, dy + 3);  /* 填充3/4 */
            /* 简化：用 fill_rect 近似深色 */
            fill_rect(layer, fg, cx, cy, r, b);  /* 全填，实际会覆盖；用透明度会更好 */
            break;
        case 0x2594: fill_rect(layer, fg, cx, cy, r, y1 - 1); break;     /* ▔ 上1/8 */
        case 0x2595: fill_rect(layer, fg, x7, cy, r, b); break;           /* ▕ 右1/8 */
        /* 四分象限系列 ─── */
        case 0x2596: fill_rect(layer, fg, cx, my, mx - 1, b); break;      /* ▖ 左下 */
        case 0x2597: fill_rect(layer, fg, mx, my, r,     b); break;       /* ▗ 右下 */
        case 0x2598: fill_rect(layer, fg, cx, cy, mx - 1, my - 1); break; /* ▘ 左上 */
        case 0x2599:                                                         /* ▙ 左上+左下+右下 */
            fill_rect(layer, fg, cx, cy, mx - 1, my - 1);
            fill_rect(layer, fg, cx, my, r,      b);
            break;
        case 0x259A:                                                         /* ▚ 左上+右下 */
            fill_rect(layer, fg, cx, cy, mx - 1, my - 1);
            fill_rect(layer, fg, mx, my, r,      b);
            break;
        case 0x259B:                                                         /* ▛ 左上+右上+左下 */
            fill_rect(layer, fg, cx, cy, r,      my - 1);
            fill_rect(layer, fg, cx, my, mx - 1, b);
            break;
        case 0x259C:                                                         /* ▜ 左上+右上+右下 */
            fill_rect(layer, fg, cx, cy, r,  my - 1);
            fill_rect(layer, fg, mx, my, r,  b);
            break;
        case 0x259D: fill_rect(layer, fg, mx, cy, r,     my - 1); break;  /* ▝ 右上 */
        case 0x259E:                                                         /* ▞ 右上+左下 */
            fill_rect(layer, fg, mx, cy, r,      my - 1);
            fill_rect(layer, fg, cx, my, mx - 1, b);
            break;
        case 0x259F:                                                         /* ▟ 右上+左下+右下 */
            fill_rect(layer, fg, mx, cy, r,  my - 1);
            fill_rect(layer, fg, cx, my, r,  b);
            break;
        default: return false;
    }
    return true;
}

/* ─── Braille Patterns (U+2800-U+28FF) ─── */
/*
 * Unicode 盲文点位编码（8-dot）：
 *   bit0 = 点1 (左列行1)   bit3 = 点4 (右列行1)
 *   bit1 = 点2 (左列行2)   bit4 = 点5 (右列行2)
 *   bit2 = 点3 (左列行3)   bit5 = 点6 (右列行3)
 *   bit6 = 点7 (左列行4)   bit7 = 点8 (右列行4)
 *
 * 布局（2列 × 4行）：
 *   col0  col1
 *   [1]   [4]    row0
 *   [2]   [5]    row1
 *   [3]   [6]    row2
 *   [7]   [8]    row3
 */
static bool render_braille(lv_layer_t *layer, uint32_t cp,
                            int cx, int cy, int cw, int ch,
                            lv_color_t fg)
{
    uint8_t dots = (uint8_t)(cp - 0x2800);
    if (dots == 0) return true;   /* U+2800 = 空白盲文格，已被背景覆盖 */

    /* 点的大小：约为格子 1/4 宽 × 1/5 高 */
    int dw = cw / 4;
    int dh = ch / 5;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    /* 两列 X 中心 */
    int col_cx[2] = {
        cx + cw / 4,
        cx + 3 * cw / 4,
    };
    /* 四行 Y 中心 */
    int row_cy[4];
    for (int i = 0; i < 4; i++) {
        row_cy[i] = cy + ch * (2 * i + 1) / 8;
    }

    /* 左列 bit0=row0, bit1=row1, bit2=row2, bit6=row3 */
    static const uint8_t left_bits[4]  = { 0x01, 0x02, 0x04, 0x40 };
    /* 右列 bit3=row0, bit4=row1, bit5=row2, bit7=row3 */
    static const uint8_t right_bits[4] = { 0x08, 0x10, 0x20, 0x80 };

    for (int i = 0; i < 4; i++) {
        if (dots & left_bits[i]) {
            int x1 = col_cx[0] - dw / 2;
            int y1 = row_cy[i] - dh / 2;
            fill_rect(layer, fg, x1, y1, x1 + dw - 1, y1 + dh - 1);
        }
        if (dots & right_bits[i]) {
            int x1 = col_cx[1] - dw / 2;
            int y1 = row_cy[i] - dh / 2;
            fill_rect(layer, fg, x1, y1, x1 + dw - 1, y1 + dh - 1);
        }
    }
    return true;
}

/* ─── 几何图形与箭头 ─── */

static bool render_geometric(lv_layer_t *layer, uint32_t cp,
                              int cx, int cy, int cw, int ch,
                              lv_color_t fg)
{
    int r  = cx + cw - 1;
    int b  = cy + ch - 1;
    int mx = cx + cw / 2;
    int my = cy + ch / 2;
    int hw = cw / 2 - 1;
    int hh = ch / 2 - 1;

    switch (cp) {
        case 0x25A0:  /* ■ 黑色方块（btop meter） */
            fill_rect(layer, fg, cx + 1, cy + 2, r - 1, b - 2);
            return true;

        case 0x25A1:  /* □ 空心方块（占位） */
            fill_rect(layer, fg, cx + 1, cy + 2, r - 1, cy + 3);
            fill_rect(layer, fg, cx + 1, b - 3,  r - 1, b - 2);
            fill_rect(layer, fg, cx + 1, cy + 2, cx + 2, b - 2);
            fill_rect(layer, fg, r - 2,  cy + 2, r - 1, b - 2);
            return true;

        case 0x25B2:  /* ▲ 上三角 */
            for (int dy = 0; dy <= hh; dy++) {
                int w = dy * hw / (hh > 0 ? hh : 1);
                fill_rect(layer, fg, mx - w, my - dy, mx + w, my - dy);
            }
            return true;

        case 0x25BC:  /* ▼ 下三角 */
            for (int dy = 0; dy <= hh; dy++) {
                int w = dy * hw / (hh > 0 ? hh : 1);
                fill_rect(layer, fg, mx - w, my + dy, mx + w, my + dy);
            }
            return true;

        case 0x25B6:  /* ▶ 右三角 */
            for (int dx = 0; dx <= hw; dx++) {
                int h = dx * hh / (hw > 0 ? hw : 1);
                fill_rect(layer, fg, mx + dx, my - h, mx + dx, my + h);
            }
            return true;

        case 0x25C0:  /* ◀ 左三角 */
            for (int dx = 0; dx <= hw; dx++) {
                int h = dx * hh / (hw > 0 ? hw : 1);
                fill_rect(layer, fg, mx - dx, my - h, mx - dx, my + h);
            }
            return true;

        case 0x25B4:  /* ▴ 小上三角 */
            for (int dy = 0; dy <= hh / 2; dy++) {
                int w = dy * (hw / 2) / (hh / 2 > 0 ? hh / 2 : 1);
                fill_rect(layer, fg, mx - w, my - dy, mx + w, my - dy);
            }
            return true;

        case 0x25BE:  /* ▾ 小下三角 */
            for (int dy = 0; dy <= hh / 2; dy++) {
                int w = dy * (hw / 2) / (hh / 2 > 0 ? hh / 2 : 1);
                fill_rect(layer, fg, mx - w, my + dy, mx + w, my + dy);
            }
            return true;

        case 0x2022:  /* • 圆点 */
        case 0x00B7:  /* · 中点 */
        case 0x2219:  /* ∙ 项目符号 */
        {
            int rr = cw / 4;
            if (rr < 1) rr = 1;
            fill_rect(layer, fg, mx - rr, my - rr, mx + rr, my + rr);
            return true;
        }
        default: break;
    }
    return false;
}

/* ─── 箭头符号 (U+2190-U+21FF) ─── */

static bool render_arrow(lv_layer_t *layer, uint32_t cp,
                          int cx, int cy, int cw, int ch,
                          lv_color_t fg)
{
    int mx = cx + cw / 2;
    int my = cy + ch / 2;
    int r  = cx + cw - 1;
    int b  = cy + ch - 1;
    int lw = 2;

    switch (cp) {
        case 0x2191:  /* ↑ */
            fill_rect(layer, fg, mx, cy + 2,    mx + lw - 1, b - 2);   /* 竖干 */
            fill_rect(layer, fg, cx + 2, my,    mx,          my + lw - 1); /* 左翼 */
            fill_rect(layer, fg, mx + lw, my,   r - 2,       my + lw - 1); /* 右翼 */
            return true;
        case 0x2193:  /* ↓ */
            fill_rect(layer, fg, mx, cy + 2,    mx + lw - 1, b - 2);   /* 竖干 */
            fill_rect(layer, fg, cx + 2, my,    mx,          my + lw - 1); /* 左翼 */
            fill_rect(layer, fg, mx + lw, my,   r - 2,       my + lw - 1); /* 右翼 */
            return true;
        case 0x2190:  /* ← */
            fill_rect(layer, fg, cx + 2, my,    r - 2, my + lw - 1);   /* 横干 */
            fill_rect(layer, fg, cx + 2, cy + 2, cx + 4, my);           /* 上翼 */
            fill_rect(layer, fg, cx + 2, my + lw, cx + 4, b - 2);       /* 下翼 */
            return true;
        case 0x2192:  /* → */
            fill_rect(layer, fg, cx + 2, my,    r - 2, my + lw - 1);   /* 横干 */
            fill_rect(layer, fg, r - 4, cy + 2, r - 2, my);             /* 上翼 */
            fill_rect(layer, fg, r - 4, my + lw, r - 2, b - 2);         /* 下翼 */
            return true;
        case 0x21B2:  /* ↲ enter 符号 */
        {
            /* 画一个向左折的箭头 */
            int ax = cx + cw * 3 / 4;
            fill_rect(layer, fg, ax, cy + 3, ax + lw - 1, my);          /* 竖段 */
            fill_rect(layer, fg, cx + 3, my,  ax + lw - 1, my + lw - 1); /* 横段 */
            fill_rect(layer, fg, cx + 3, my - lw, cx + 5, my + lw);      /* 箭头 */
            return true;
        }
        default: break;
    }
    return false;
}

/* ─── 公共入口 ─── */

static bool render_misc_symbol(lv_layer_t *layer, uint32_t cp,
                               int cx, int cy, int cw, int ch,
                               lv_color_t fg)
{
    int mx = cx + cw / 2;
    int my = cy + ch / 2;
    int r = cx + cw - 1;
    int b = cy + ch - 1;
    int lw = 2;

    switch (cp) {
        case 0x2726:
            draw_line(layer, fg, mx, cy + 2, mx, b - 2, lw);
            draw_line(layer, fg, cx + 2, my, r - 2, my, lw);
            draw_line(layer, fg, mx - 3, my - 3, mx + 3, my + 3, 1);
            draw_line(layer, fg, mx - 3, my + 3, mx + 3, my - 3, 1);
            return true;

        default:
            return false;
    }
}

static bool render_terminal_fallback_symbol(lv_layer_t *layer, uint32_t cp,
                                            int cx, int cy, int cw, int ch,
                                            lv_color_t fg)
{
    int mx = cx + cw / 2;
    int my = cy + ch / 2;
    int r = cx + cw - 1;
    int b = cy + ch - 1;
    int stroke = imax_int(1, imin_int(cw, ch) / 9);

    if (cp >= 0x0660 && cp <= 0x0669) {
        return render_seven_segment_digit(layer, (uint8_t)(cp - 0x0660), cx, cy, cw, ch, fg);
    }
    if (cp >= 0x06F0 && cp <= 0x06F9) {
        return render_seven_segment_digit(layer, (uint8_t)(cp - 0x06F0), cx, cy, cw, ch, fg);
    }
    if (cp >= 0x0E50 && cp <= 0x0E59) {
        return render_seven_segment_digit(layer, (uint8_t)(cp - 0x0E50), cx, cy, cw, ch, fg);
    }

    switch (cp) {
        case 0x0CA0: {
            int radius = imin_int(cw, ch) / 3;
            if (radius < 3) radius = 3;
            stroke_circle(layer, fg, mx, my, radius, stroke);
            fill_disc(layer, fg, mx, my, imax_int(1, radius / 3));
            draw_line(layer, fg, mx - radius, my - radius - 1,
                      mx + radius, my - radius - 1, stroke);
            return true;
        }

        case 0xFF65:
            fill_disc(layer, fg, mx, my, imax_int(1, imin_int(cw, ch) / 8));
            return true;

        case 0xFF61: {
            int radius = imax_int(2, imin_int(cw, ch) / 7);
            stroke_circle(layer, fg, mx, my, radius, stroke);
            return true;
        }

        case 0x2323: {
            int arc_y = cy + ch * 2 / 3;
            int left = cx + cw / 5;
            int right = r - cw / 5;
            int last_x = left;
            int last_y = arc_y;
            int steps = imax_int(4, cw / 2);
            for (int i = 1; i <= steps; i++) {
                int x = left + (right - left) * i / steps;
                int t = i - steps / 2;
                int y = arc_y + (t * t * ch) / (steps * steps);
                draw_line(layer, fg, last_x, last_y, x, y, stroke);
                last_x = x;
                last_y = y;
            }
            return true;
        }

        case 0x2661: {
            int top = cy + ch / 4;
            int lcx = cx + cw / 3;
            int rcx = cx + cw * 2 / 3;
            int lob_r = imax_int(2, imin_int(cw, ch) / 6);
            stroke_circle(layer, fg, lcx, top + lob_r, lob_r, stroke);
            stroke_circle(layer, fg, rcx, top + lob_r, lob_r, stroke);
            draw_line(layer, fg, lcx - lob_r, top + lob_r, mx, b - ch / 6, stroke);
            draw_line(layer, fg, rcx + lob_r, top + lob_r, mx, b - ch / 6, stroke);
            draw_line(layer, fg, lcx + lob_r / 2, top + lob_r / 2,
                      rcx - lob_r / 2, top + lob_r / 2, stroke);
            return true;
        }

        default:
            return false;
    }
}

bool term_render_special_char(lv_layer_t *layer, uint32_t cp,
                               int cx, int cy, int cw, int ch,
                               lv_color_t fg)
{
    if (cp >= 0x2500 && cp <= 0x257F)
        return render_box_drawing(layer, cp, cx, cy, cw, ch, fg);

    if (cp >= 0x2580 && cp <= 0x259F)
        return render_block_element(layer, cp, cx, cy, cw, ch, fg);

    if (cp >= 0x2800 && cp <= 0x28FF)
        return render_braille(layer, cp, cx, cy, cw, ch, fg);

    if ((cp >= 0x25A0 && cp <= 0x25FF) || cp == 0x2022 || cp == 0x2219)
        return render_geometric(layer, cp, cx, cy, cw, ch, fg);

    if (cp >= 0x2190 && cp <= 0x21FF)
        return render_arrow(layer, cp, cx, cy, cw, ch, fg);

    if (cp == 0x2726)
        return render_misc_symbol(layer, cp, cx, cy, cw, ch, fg);

    if (render_terminal_fallback_symbol(layer, cp, cx, cy, cw, ch, fg))
        return true;

    return false;
}
