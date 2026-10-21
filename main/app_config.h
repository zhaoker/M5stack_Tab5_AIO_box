/**
 * @file app_config.h
 * @brief Application-wide build-time configuration.
 */
#pragma once

#include "lvgl.h"
#include "fonts/MI_font_20.h"
#include "i18n/app_i18n.h"

/* Tab5 NP-F550 battery monitor tuning. */
#define APP_BATTERY_MIN_V          6.0f
#define APP_BATTERY_MAX_V          8.23f
#define APP_BATTERY_PRESENT_MIN_V  5.5f
#define APP_BATTERY_CAPACITY_MAH   2200.0f

/* Default CJK-capable font used by app UI labels and terminal rendering. */
#define APP_FONT MI_font_20

/* Font used for pinyin candidate Chinese characters. */
#define APP_IME_CAND_FONT MI_font_20

/*
 * 文件管理器文本阅读/编辑器配色。
 * 下面颜色仅影响 TXT/JSON/CPP 等文本文件打开后的全屏阅读器，
 * 使用 0xRRGGBB 十六进制颜色值，方便手工微调。
 */

/* 暗色模式：整体背景色。 */
#define APP_TEXT_THEME_DARK_BG              0x0B0F14
/* 暗色模式：顶部标题栏、底部状态栏、软键盘背景色。 */
#define APP_TEXT_THEME_DARK_BAR             0x101722
/* 暗色模式：正文编辑区背景色。 */
#define APP_TEXT_THEME_DARK_EDITOR_BG       0x05070A
/* 暗色模式：正文文字颜色。 */
#define APP_TEXT_THEME_DARK_TEXT            0xF7F7F5
/* 暗色模式：边框和分隔线颜色。 */
#define APP_TEXT_THEME_DARK_BORDER          0x182333
/* 暗色模式：状态栏弱提示文字颜色。 */
#define APP_TEXT_THEME_DARK_MUTED           0x7D8795
/* 暗色模式：编辑光标颜色，建议保持高对比。 */
#define APP_TEXT_THEME_DARK_CURSOR          0xFBBF24
/* 暗色模式：软键盘按下/候选按下背景色。 */
#define APP_TEXT_THEME_DARK_KEY_PRESSED     0x243044

/* 亮色模式：整体背景色。 */
#define APP_TEXT_THEME_LIGHT_BG             0xF3F5F7
/* 亮色模式：顶部标题栏、底部状态栏、软键盘背景色。 */
#define APP_TEXT_THEME_LIGHT_BAR            0xE9EDF2
/* 亮色模式：正文编辑区背景色。 */
#define APP_TEXT_THEME_LIGHT_EDITOR_BG      0xFAFAF8
/* 亮色模式：正文文字颜色。 */
#define APP_TEXT_THEME_LIGHT_TEXT           0x050505
/* 亮色模式：边框和分隔线颜色。 */
#define APP_TEXT_THEME_LIGHT_BORDER         0xD5DAE1
/* 亮色模式：状态栏弱提示文字颜色。 */
#define APP_TEXT_THEME_LIGHT_MUTED          0x505968
/* 亮色模式：编辑光标颜色，建议保持高对比。 */
#define APP_TEXT_THEME_LIGHT_CURSOR         0xDC2626
/* 亮色模式：软键盘按下/候选按下背景色。 */
#define APP_TEXT_THEME_LIGHT_KEY_PRESSED    0xDBEAFE

/* 护眼模式：整体背景色。 */
#define APP_TEXT_THEME_EYE_BG               0x101612
/* 护眼模式：顶部标题栏、底部状态栏、软键盘背景色。 */
#define APP_TEXT_THEME_EYE_BAR              0x151D18
/* 护眼模式：正文编辑区背景色。 */
#define APP_TEXT_THEME_EYE_EDITOR_BG        0x0B110D
/* 护眼模式：正文文字颜色。 */
#define APP_TEXT_THEME_EYE_TEXT             0xDCE8D3
/* 护眼模式：边框和分隔线颜色。 */
#define APP_TEXT_THEME_EYE_BORDER           0x1E2A22
/* 护眼模式：状态栏弱提示文字颜色。 */
#define APP_TEXT_THEME_EYE_MUTED            0x7B8B7A
/* 护眼模式：编辑光标颜色，建议保持高对比。 */
#define APP_TEXT_THEME_EYE_CURSOR           0xB45309
/* 护眼模式：软键盘按下/候选按下背景色。 */
#define APP_TEXT_THEME_EYE_KEY_PRESSED      0xD9E8C9

/* 文本编辑光标宽度，单位：像素。 */
#define APP_TEXT_EDITOR_CURSOR_W            4
/* 文本编辑光标闪烁半周期，单位：毫秒，数值越小闪得越快。 */
#define APP_TEXT_EDITOR_CURSOR_BLINK_MS     320

/* Terminal title bar / hotspot tuning. */
#define APP_TERM_STATUSBAR_H      50
#define APP_TERM_HOTZONE_TOP_H    100
#define APP_TERM_HOTZONE_LEFT_W   200
#define APP_TERM_HOTZONE_RIGHT_W  200
#define APP_TERM_CONTROL_BAR_H    50
#define APP_TERM_CURSOR_SAFE_OFFSET 20

/* Tab5 Keyboard (A164) on ExtPort1: default I2C address 0x6D, SDA=GPIO0, SCL=GPIO1. */
#define APP_TAB5_KEYBOARD_I2C_PORT 1
#define APP_TAB5_KEYBOARD_I2C_SDA  0
#define APP_TAB5_KEYBOARD_I2C_SCL  1
#define APP_TAB5_KEYBOARD_I2C_ADDR 0x6D

/* Terminal title text now uses the same UI font as the rest of the app. */
#define APP_TERM_TITLE_FONT (&APP_FONT)

/*
 * LVGL's built-in keyboard uses Montserrat by default. Keep edit fields and
 * keyboard keys on the same Latin/digit font so server host/user/port text is
 * always readable, while CJK labels can still use APP_FONT.
 */
static inline const lv_font_t *app_keyboard_font(void)
{
#if LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#elif LV_FONT_MONTSERRAT_30
    return &lv_font_montserrat_30;
#elif LV_FONT_MONTSERRAT_28
    return &lv_font_montserrat_28;
#else
    return LV_FONT_DEFAULT;
#endif
}

#define APP_KEYBOARD_FONT app_keyboard_font()
