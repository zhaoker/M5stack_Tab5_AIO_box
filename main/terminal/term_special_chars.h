/**
 * @file term_special_chars.h
 * @brief 终端特殊字符软件渲染器
 *
 * 为 btop / htop 等 TUI 程序使用的 Unicode 特殊字符提供像素级精确渲染：
 *   - Box Drawing   U+2500-U+257F  盒状绘图（终端边框）
 *   - Block Elements U+2580-U+259F  块状元素（图表）
 *   - Braille       U+2800-U+28FF  盲文图案（btop 折线图）
 *   - Geometric     U+25A0-U+25FF  几何图形（箭头、方块）
 *   - Arrows        U+2190-U+21FF  箭头符号
 *
 * 使用方式：在 render_row() 中调用 term_render_special_char()，若返回 true
 * 则已完成渲染，无需再走字体路径。
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 尝试用软件方式渲染一个特殊 Unicode 字符。
 *
 * @param layer   LVGL 绘制层
 * @param cp      Unicode 码点
 * @param cx      格子左上角 X（像素）
 * @param cy      格子左上角 Y（像素）
 * @param cw      格子宽度（像素）
 * @param ch      格子高度（像素）
 * @param fg      前景色
 * @return true   已渲染（调用方跳过字体渲染）
 * @return false  不支持，调用方应走字体路径
 */
bool term_render_special_char(lv_layer_t *layer, uint32_t cp,
                               int cx, int cy, int cw, int ch,
                               lv_color_t fg);
