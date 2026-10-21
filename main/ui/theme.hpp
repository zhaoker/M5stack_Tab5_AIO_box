#pragma once

/*
 * Shared theme tokens for the LVGL UI.
 *
 * The project is built as C, so this file keeps the requested `theme.hpp`
 * name while exposing C-compatible macros instead of C++ namespaces.
 */

#include "lvgl.h"

#define THEME_BG_PRIMARY           lv_color_hex(0x0B1220)
#define THEME_BG_SECONDARY         lv_color_hex(0x111827)
#define THEME_BG_PANEL             lv_color_hex(0x172033)
#define THEME_BG_CARD              lv_color_hex(0x1E293B)
#define THEME_BG_HOVER             lv_color_hex(0x243044)

#define THEME_BORDER_PRIMARY       lv_color_hex(0x2A3547)
#define THEME_BORDER_SECONDARY     lv_color_hex(0x334155)
#define THEME_BORDER_DIVIDER       lv_color_hex(0x3B475C)

#define THEME_TEXT_PRIMARY         lv_color_hex(0xF3F6FB)
#define THEME_TEXT_SECONDARY       lv_color_hex(0xCBD5E1)
#define THEME_TEXT_TERTIARY        lv_color_hex(0x94A3B8)
#define THEME_TEXT_DISABLED        lv_color_hex(0x64748B)

#define THEME_PRIMARY              lv_color_hex(0x3B82F6)
#define THEME_PRIMARY_HOVER        lv_color_hex(0x2563EB)
#define THEME_PRIMARY_ACTIVE       lv_color_hex(0x1D4ED8)
#define THEME_PRIMARY_LIGHT        lv_color_hex(0x60A5FA)
#define THEME_PRIMARY_GLOW         lv_color_hex(0x93C5FD)

#define THEME_SUCCESS              lv_color_hex(0x22C55E)
#define THEME_SUCCESS_LIGHT        lv_color_hex(0x4ADE80)
#define THEME_SUCCESS_BG           lv_color_hex(0x052E16)

#define THEME_WARNING              lv_color_hex(0xF59E0B)
#define THEME_WARNING_LIGHT        lv_color_hex(0xFBBF24)
#define THEME_WARNING_BG           lv_color_hex(0x3B1F00)

#define THEME_DANGER               lv_color_hex(0xEF4444)
#define THEME_DANGER_LIGHT         lv_color_hex(0xF87171)
#define THEME_DANGER_BG            lv_color_hex(0x450A0A)

#define THEME_ACCENT_CYAN          lv_color_hex(0x06B6D4)
#define THEME_ACCENT_SKY           lv_color_hex(0x0EA5E9)
#define THEME_ACCENT_TEAL          lv_color_hex(0x14B8A6)

#define THEME_INPUT_BG             lv_color_hex(0x111827)
#define THEME_INPUT_BORDER         lv_color_hex(0x334155)
#define THEME_INPUT_FOCUS          lv_color_hex(0x3B82F6)
#define THEME_INPUT_TEXT           lv_color_hex(0xF3F6FB)
#define THEME_INPUT_PLACEHOLDER    lv_color_hex(0x64748B)

#define THEME_RADIUS_SMALL         8
#define THEME_RADIUS_MEDIUM        14
#define THEME_RADIUS_LARGE         20
#define THEME_RADIUS_PILL          999

#define THEME_SPACE_XS             6
#define THEME_SPACE_SM             10
#define THEME_SPACE_MD             14
#define THEME_SPACE_LG             18
#define THEME_SPACE_XL             24

#define THEME_OVERLAY_OPA          170
#define THEME_CARD_SHADOW_OPA      72
#define THEME_GLOW_SHADOW_OPA      92
