/**
 * @file ansi_parser.h
 * @brief ANSI/VT100 终端解析器接口
 *
 * 解析 SSH 通道接收到的 ANSI 转义序列，维护字符单元格缓冲区（term_state_t）。
 *
 * 支持的特性：
 *   - UTF-8 多字节解码
 *   - CSI SGR：标准 16 色、256 色、真彩色 24-bit
 *   - CSI 光标移动：CUP/CUU/CUD/CUF/CUB/CNL/CPL/CHA
 *   - CSI 清屏/清行：ED/EL
 *   - CSI 行操作：IL（插入行）/DL（删除行）
 *   - CSI 字符操作：ICH（插入字符）/DCH（删除字符）
 *   - CSI 滚动：SU/SD
 *   - CSI 私有序列：?25h/l（光标显示）、?1049h/l（备用屏幕）
 *   - ESC 7/8：保存/恢复光标
 *   - OSC 序列（忽略内容，用于标题设定等）
 *   - 控制字符：\r \n \b \t \a
 *   - 滚动区域：DECSTBM (CSI r)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ─── 终端尺寸 ─── */
#define TERM_CELL_W   12   /**< 每列像素宽（hm_font_20 等宽约 12px） */
#define TERM_CELL_H   26   /**< 每行像素高（hm_font_20 line_height = 26px） */
#define TERM_COLS     106  /**< 终端列数：1280 / 12 = 106 */
#define TERM_HISTORY_ROWS 40
#define TERM_ROWS     27   /**< 终端行数：702  / 26 = 27（canvas 高 702px） */

#define MAX_PARAMS  16
#define MAX_OSC_LEN 256

typedef enum {
    ST_NORMAL,    /**< 普通字符 */
    ST_ESC,       /**< 收到 ESC */
    ST_CSI,       /**< 收到 ESC [ */
    ST_CSI_PRIV,  /**< 收到 ESC [ ? */
    ST_OSC,       /**< 收到 ESC ] */
    ST_UTF8,      /**< 正在读取 UTF-8 多字节序列 */
} parser_state_t;

typedef struct {
    parser_state_t state;
    int params[MAX_PARAMS];
    int param_count;
    int cur_param;
    uint32_t utf8_cp;
    int utf8_rem;
    uint32_t utf8_min;
    char osc_buf[MAX_OSC_LEN];
    int osc_len;
} parser_ctx_t;

/* ─── 单元格属性位掩码 ─── */
#define TERM_ATTR_BOLD      (1u << 0)
#define TERM_ATTR_DIM       (1u << 1)
#define TERM_ATTR_ITALIC    (1u << 2)
#define TERM_ATTR_UNDERLINE (1u << 3)
#define TERM_ATTR_BLINK     (1u << 4)
#define TERM_ATTR_REVERSE   (1u << 5)
#define TERM_ATTR_INVISIBLE (1u << 6)
#define TERM_ATTR_STRIKE    (1u << 7)

/* ─── 单元格 ─── */
typedef struct {
    uint32_t codepoint;  /**< Unicode 码点（0 = 空白） */
    uint32_t fg;         /**< 前景色 RGB888（默认 0xC0C0C0） */
    uint32_t bg;         /**< 背景色 RGB888（默认 0x000000） */
    uint8_t  attrs;      /**< 属性位掩码 TERM_ATTR_* */
    uint8_t  wide;       /**< 0=普通, 1=宽字符左半, 2=宽字符右半 */
} term_cell_t;

/* ─── 终端状态 ─── */
typedef struct {
    term_cell_t cells[TERM_ROWS][TERM_COLS]; /**< 主屏幕字符缓冲区 */
    term_cell_t alt_cells[TERM_ROWS][TERM_COLS]; /**< 备用屏幕缓冲区 */

    int  cursor_row;     /**< 当前光标行（0-based） */
    int  cursor_col;     /**< 当前光标列（0-based） */
    bool cursor_visible; /**< 光标是否可见 */

    int saved_row;       /**< 保存的光标行 */
    int saved_col;       /**< 保存的光标列 */

    int scroll_top;      /**< 滚动区域顶行（0-based，默认 0） */
    int scroll_bot;      /**< 滚动区域底行（0-based，默认 TERM_ROWS-1） */

    bool alt_screen;     /**< 是否处于备用屏幕模式 */

    term_cell_t (*history)[TERM_COLS];
    int history_start;
    int history_count;
    bool history_dirty;

    bool dirty_rows[TERM_ROWS]; /**< 脏行标记（被修改尚未重绘） */
    bool dirty_cursor;          /**< 光标位置变化标记 */

    /* 当前图形属性（用于新写入字符） */
    uint32_t cur_fg;
    uint32_t cur_bg;
    uint8_t  cur_attrs;

    parser_ctx_t parser;
} term_state_t;

/**
 * @brief 初始化终端状态（清屏、光标归位）
 */
void ansi_parser_init(term_state_t *state);
void ansi_parser_deinit(term_state_t *state);

/**
 * @brief 向解析器输入原始字节流（来自 SSH 通道）
 *
 * 该函数是线程安全的（假设由单一 SSH 接收任务调用，渲染器在持有锁后读取）。
 * 解析完成后自动更新 state->dirty_rows。
 *
 * @param state  终端状态指针
 * @param data   数据指针
 * @param len    数据长度（字节）
 */
void ansi_parser_feed(term_state_t *state, const char *data, size_t len);

/**
 * @brief 标记所有行为脏（强制完整重绘）
 */
void ansi_parser_mark_all_dirty(term_state_t *state);

int ansi_parser_total_rows(const term_state_t *state);

const term_cell_t *ansi_parser_get_visual_row(const term_state_t *state, int row);

#ifdef __cplusplus
}
#endif
