#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UI_HW_MOD_CTRL 0x01U
#define UI_HW_MOD_ALT  0x04U

typedef enum {
    UI_HW_TEXTAREA_KEY_NONE = 0,
    UI_HW_TEXTAREA_KEY_ENTER,
    UI_HW_TEXTAREA_KEY_TAB,
    UI_HW_TEXTAREA_KEY_BACKSPACE,
    UI_HW_TEXTAREA_KEY_DELETE_FORWARD,
    UI_HW_TEXTAREA_KEY_ESCAPE,
    UI_HW_TEXTAREA_KEY_LEFT,
    UI_HW_TEXTAREA_KEY_RIGHT,
    UI_HW_TEXTAREA_KEY_UP,
    UI_HW_TEXTAREA_KEY_DOWN,
    UI_HW_TEXTAREA_KEY_HOME,
    UI_HW_TEXTAREA_KEY_END,
    UI_HW_TEXTAREA_KEY_SPACE,
} ui_hw_textarea_key_t;

static inline bool ui_hw_ascii_streq_ignore_case(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static inline bool ui_hw_textarea_parse_key(const char *txt, size_t len, ui_hw_textarea_key_t *key)
{
    if (!txt || !key) return false;
    if (len == 0) {
        len = strlen(txt);
    }

    if (len == 1) {
        switch (txt[0]) {
        case '\r':
        case '\n':
            *key = UI_HW_TEXTAREA_KEY_ENTER;
            return true;
        case '\t':
            *key = UI_HW_TEXTAREA_KEY_TAB;
            return true;
        case '\b':
            *key = UI_HW_TEXTAREA_KEY_BACKSPACE;
            return true;
        case 0x1b:
            *key = UI_HW_TEXTAREA_KEY_ESCAPE;
            return true;
        case ' ':
            *key = UI_HW_TEXTAREA_KEY_SPACE;
            return true;
        default:
            if ((uint8_t)txt[0] == 0x7f) {
                *key = UI_HW_TEXTAREA_KEY_BACKSPACE;
                return true;
            }
            return false;
        }
    }

    if (ui_hw_ascii_streq_ignore_case(txt, "Enter") ||
        ui_hw_ascii_streq_ignore_case(txt, "Return") ||
        strcmp(txt, LV_SYMBOL_NEW_LINE) == 0 ||
        strcmp(txt, LV_SYMBOL_OK) == 0) {
        *key = UI_HW_TEXTAREA_KEY_ENTER;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Tab")) {
        *key = UI_HW_TEXTAREA_KEY_TAB;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Backspace") ||
        ui_hw_ascii_streq_ignore_case(txt, "BS") ||
        strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        *key = UI_HW_TEXTAREA_KEY_BACKSPACE;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Delete") ||
        ui_hw_ascii_streq_ignore_case(txt, "Del")) {
        *key = UI_HW_TEXTAREA_KEY_DELETE_FORWARD;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "ESC") ||
        ui_hw_ascii_streq_ignore_case(txt, "Escape")) {
        *key = UI_HW_TEXTAREA_KEY_ESCAPE;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Left") ||
        ui_hw_ascii_streq_ignore_case(txt, "ArrowLeft") ||
        strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        *key = UI_HW_TEXTAREA_KEY_LEFT;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Right") ||
        ui_hw_ascii_streq_ignore_case(txt, "ArrowRight") ||
        strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        *key = UI_HW_TEXTAREA_KEY_RIGHT;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Up") ||
        ui_hw_ascii_streq_ignore_case(txt, "ArrowUp") ||
        strcmp(txt, LV_SYMBOL_UP) == 0) {
        *key = UI_HW_TEXTAREA_KEY_UP;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Down") ||
        ui_hw_ascii_streq_ignore_case(txt, "ArrowDown") ||
        strcmp(txt, LV_SYMBOL_DOWN) == 0) {
        *key = UI_HW_TEXTAREA_KEY_DOWN;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Home")) {
        *key = UI_HW_TEXTAREA_KEY_HOME;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "End")) {
        *key = UI_HW_TEXTAREA_KEY_END;
        return true;
    }
    if (ui_hw_ascii_streq_ignore_case(txt, "Space") ||
        ui_hw_ascii_streq_ignore_case(txt, "Spacebar")) {
        *key = UI_HW_TEXTAREA_KEY_SPACE;
        return true;
    }

    return false;
}

static inline bool ui_hw_textarea_modifier_blocks_text(uint8_t modifier)
{
    return (modifier & (UI_HW_MOD_CTRL | UI_HW_MOD_ALT)) != 0U;
}

static inline bool ui_hw_textarea_apply_edit_key(lv_obj_t *ta, ui_hw_textarea_key_t key)
{
    if (!ta) return false;

    switch (key) {
    case UI_HW_TEXTAREA_KEY_BACKSPACE:
        lv_textarea_delete_char(ta);
        return true;
    case UI_HW_TEXTAREA_KEY_DELETE_FORWARD:
        lv_textarea_delete_char_forward(ta);
        return true;
    case UI_HW_TEXTAREA_KEY_LEFT: {
        uint32_t pos = lv_textarea_get_cursor_pos(ta);
        if (pos > 0) {
            lv_textarea_set_cursor_pos(ta, (int32_t)(pos - 1));
        }
        return true;
    }
    case UI_HW_TEXTAREA_KEY_RIGHT:
        lv_textarea_set_cursor_pos(ta, (int32_t)(lv_textarea_get_cursor_pos(ta) + 1));
        return true;
    case UI_HW_TEXTAREA_KEY_HOME:
        lv_textarea_set_cursor_pos(ta, 0);
        return true;
    case UI_HW_TEXTAREA_KEY_END:
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
        return true;
    case UI_HW_TEXTAREA_KEY_SPACE:
        lv_textarea_add_char(ta, ' ');
        return true;
    default:
        break;
    }

    return false;
}
