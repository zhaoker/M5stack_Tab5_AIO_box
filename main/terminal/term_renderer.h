/**
 * @file term_renderer.h
 * @brief LVGL Canvas 终端渲染器接口
 *
 * 基于 lv_canvas 将 term_state_t 渲染到 1280×720 的像素缓冲区。
 * 使用脏行标记（dirty_rows）只重绘发生变化的行，减少 CPU 开销。
 * 字体通过 term_renderer_set_font() 注入，默认使用 hm_font_20。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ansi_parser.h"

/**
 * @brief 创建终端 Canvas 对象
 *
 * 在 parent 下创建一个 1280×720 的 lv_canvas，像素缓冲区从 PSRAM 分配。
 * 调用前需持有 LVGL 显示锁。
 *
 * @param parent  父容器（通常为 lv_scr_act()）
 * @return 创建的 canvas 对象
 */
lv_obj_t *term_renderer_create(lv_obj_t *parent);

int term_renderer_get_canvas_height(void);

/**
 * @brief 设置终端字体（默认 hm_font_20）
 */
void term_renderer_set_font(const lv_font_t *font);

/**
 * @brief 刷新终端：重绘所有脏行并绘制光标
 *
 * 必须在持有 LVGL 显示锁（bsp_display_lock）的上下文中调用。
 * 完成后清除 dirty_rows 标记。
 *
 * @param canvas  term_renderer_create 返回的 canvas 对象
 * @param state   终端状态指针（只读）
 */
void term_renderer_refresh(lv_obj_t *canvas, term_state_t *state);

/**
 * @brief 强制全屏重绘（通常在首次显示或分辨率变化时）
 */
void term_renderer_full_refresh(lv_obj_t *canvas, term_state_t *state);

#ifdef __cplusplus
}
#endif
