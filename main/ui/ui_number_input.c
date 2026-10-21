#include "ui_number_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "hw/tab5_keyboard.h"
#include "theme.hpp"
#include "ui_hw_textarea_input.h"

enum {
    UI_NUMBER_KB_W = 400,
    UI_NUMBER_KB_H = 400,
    UI_NUMBER_HW_POLL_MS = 20,
    UI_NUMBER_BTN_W = 112,
    UI_NUMBER_BTN_H = 78,
};

static lv_obj_t *s_active_ta;
static lv_obj_t *s_keyboard_parent;
static lv_obj_t *s_keypad;
static lv_timer_t *s_hw_timer;
static bool s_hw_keyboard_active;

static void number_input_stop_hw_keyboard(void)
{
    if (s_hw_timer) {
        lv_timer_del(s_hw_timer);
        s_hw_timer = NULL;
    }
    if (s_hw_keyboard_active) {
        tab5_keyboard_deinit();
        s_hw_keyboard_active = false;
    }
}

static void number_input_filter_text(lv_obj_t *ta)
{
    const char *text;
    char digits[16];
    size_t out = 0;
    bool changed = false;

    if (!ta) {
        return;
    }
    text = lv_textarea_get_text(ta);
    if (!text) {
        return;
    }
    for (size_t i = 0; text[i] && out + 1 < sizeof(digits); i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            digits[out++] = text[i];
        } else {
            changed = true;
        }
    }
    digits[out] = '\0';
    if (changed || strlen(text) != out) {
        lv_textarea_set_text(ta, digits);
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

static void number_input_hide_keypad(void)
{
    if (s_keypad) {
        lv_obj_add_flag(s_keypad, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_number_input_close(void)
{
    number_input_stop_hw_keyboard();
    number_input_hide_keypad();
    s_active_ta = NULL;
    s_keyboard_parent = NULL;
}

static void number_input_keypad_action(lv_event_t *e)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(e);

    if (!s_active_ta) {
        return;
    }
    if (action >= 0 && action <= 9) {
        lv_textarea_add_char(s_active_ta, (uint32_t)('0' + action));
        return;
    }
    if (action == 10) {
        lv_textarea_delete_char(s_active_ta);
        return;
    }
    if (action == 11) {
        ui_number_input_close();
    }
}

static lv_obj_t *number_input_create_key(lv_obj_t *parent, const char *text, intptr_t action)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, UI_NUMBER_BTN_W, UI_NUMBER_BTN_H);
    lv_obj_set_style_bg_color(btn, THEME_BG_HOVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, number_input_keypad_action, LV_EVENT_CLICKED, (void *)action);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_PRIMARY, 0);
    lv_obj_center(label);
    return btn;
}

static void number_input_show_keypad(void)
{
    static const struct {
        const char *text;
        intptr_t action;
    } keys[] = {
        {"1", 1}, {"2", 2}, {"3", 3},
        {"4", 4}, {"5", 5}, {"6", 6},
        {"7", 7}, {"8", 8}, {"9", 9},
        {"OK", 11}, {"0", 0}, {LV_SYMBOL_BACKSPACE, 10},
    };
    lv_obj_t *parent = s_keyboard_parent ? s_keyboard_parent : lv_layer_top();

    if (!parent) {
        return;
    }
    if (!s_keypad) {
        s_keypad = lv_obj_create(parent);
        lv_obj_null_on_delete(&s_keypad);
        lv_obj_set_size(s_keypad, UI_NUMBER_KB_W, UI_NUMBER_KB_H);
        lv_obj_align(s_keypad, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(s_keypad, THEME_BG_PANEL, 0);
        lv_obj_set_style_bg_opa(s_keypad, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_keypad, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_border_width(s_keypad, 1, 0);
        lv_obj_set_style_radius(s_keypad, 0, 0);
        lv_obj_set_style_pad_all(s_keypad, 16, 0);
        lv_obj_set_style_pad_row(s_keypad, 10, 0);
        lv_obj_set_style_pad_column(s_keypad, 12, 0);
        lv_obj_set_flex_flow(s_keypad, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(s_keypad,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(s_keypad, LV_OBJ_FLAG_SCROLLABLE);

        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            number_input_create_key(s_keypad, keys[i].text, keys[i].action);
        }
    } else if (lv_obj_get_parent(s_keypad) != parent) {
        lv_obj_set_parent(s_keypad, parent);
        lv_obj_align(s_keypad, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_clear_flag(s_keypad, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keypad);
}

static void number_input_insert_digits(const char *text, size_t len, uint8_t modifier)
{
    if (!s_active_ta || !text || len == 0 || ui_hw_textarea_modifier_blocks_text(modifier)) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            lv_textarea_add_char(s_active_ta, (uint32_t)text[i]);
        }
    }
}

static bool number_input_handle_hw_key(const tab5_keyboard_event_t *evt)
{
    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;

    if (!evt || !s_active_ta) {
        return false;
    }
    if (ui_hw_textarea_parse_key(evt->chars, evt->length, &key)) {
        switch (key) {
        case UI_HW_TEXTAREA_KEY_ENTER:
        case UI_HW_TEXTAREA_KEY_ESCAPE:
            ui_number_input_close();
            return true;
        case UI_HW_TEXTAREA_KEY_BACKSPACE:
        case UI_HW_TEXTAREA_KEY_DELETE_FORWARD:
        case UI_HW_TEXTAREA_KEY_LEFT:
        case UI_HW_TEXTAREA_KEY_RIGHT:
        case UI_HW_TEXTAREA_KEY_HOME:
        case UI_HW_TEXTAREA_KEY_END:
            return ui_hw_textarea_apply_edit_key(s_active_ta, key);
        default:
            return true;
        }
    }
    number_input_insert_digits(evt->chars, evt->length, evt->modifier);
    return true;
}

static void number_input_hw_timer_cb(lv_timer_t *timer)
{
    tab5_keyboard_event_t evt;
    esp_err_t err;
    (void)timer;

    if (!s_active_ta) {
        ui_number_input_close();
        return;
    }
    err = tab5_keyboard_read_event(&evt);
    if (err == ESP_ERR_NOT_FOUND || evt.length == 0) {
        return;
    }
    if (err != ESP_OK) {
        number_input_stop_hw_keyboard();
        number_input_show_keypad();
        return;
    }
    (void)number_input_handle_hw_key(&evt);
}

static bool number_input_start_hw_keyboard(void)
{
    if (tab5_keyboard_init() != ESP_OK) {
        return false;
    }
    s_hw_keyboard_active = true;
    if (!s_hw_timer) {
        s_hw_timer = lv_timer_create(number_input_hw_timer_cb, UI_NUMBER_HW_POLL_MS, NULL);
    }
    if (!s_hw_timer) {
        number_input_stop_hw_keyboard();
        return false;
    }
    return true;
}

static void number_input_on_focus(lv_event_t *e)
{
    s_active_ta = lv_event_get_target(e);
    s_keyboard_parent = (lv_obj_t *)lv_event_get_user_data(e);
    number_input_filter_text(s_active_ta);
    number_input_hide_keypad();
    number_input_stop_hw_keyboard();
    if (!number_input_start_hw_keyboard()) {
        number_input_show_keypad();
    }
}

static void number_input_on_value_changed(lv_event_t *e)
{
    number_input_filter_text(lv_event_get_target(e));
}

static void number_input_on_delete(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (ta == s_active_ta) {
        ui_number_input_close();
    }
}

static void number_input_apply_textarea_style(lv_obj_t *ta)
{
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_obj_set_style_bg_color(ta, THEME_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, THEME_INPUT_BORDER, 0);
    lv_obj_set_style_border_color(ta, THEME_INPUT_FOCUS, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_text_color(ta, THEME_INPUT_TEXT, 0);
    lv_obj_set_style_bg_color(ta, THEME_PRIMARY_GLOW, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_text_color(ta, THEME_BG_PRIMARY, LV_PART_CURSOR);
    lv_obj_set_style_width(ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_pad_left(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_pad_right(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_outline_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(ta, 320, LV_PART_CURSOR);
}

void ui_number_input_attach(lv_obj_t *textarea, lv_obj_t *keyboard_parent)
{
    if (!textarea) {
        return;
    }
    number_input_apply_textarea_style(textarea);
    lv_obj_add_event_cb(textarea, number_input_on_focus, LV_EVENT_FOCUSED, keyboard_parent);
    lv_obj_add_event_cb(textarea, number_input_on_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(textarea, number_input_on_delete, LV_EVENT_DELETE, NULL);
}
