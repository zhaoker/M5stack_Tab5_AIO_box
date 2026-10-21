#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_KEYBOARD_MAX_CHARS 9

typedef struct {
    uint8_t modifier;
    uint8_t length;
    char chars[TAB5_KEYBOARD_MAX_CHARS + 1];
} tab5_keyboard_event_t;

esp_err_t tab5_keyboard_init(void);
esp_err_t tab5_keyboard_probe_presence(void);
void tab5_keyboard_deinit(void);
bool tab5_keyboard_is_present(void);
bool tab5_keyboard_was_detected(void);
bool tab5_keyboard_was_detected_at_startup(void);
void tab5_keyboard_dump_state(const char *where);
esp_err_t tab5_keyboard_read_event(tab5_keyboard_event_t *out_event);
esp_err_t tab5_keyboard_set_indicator_brightness(uint8_t brightness_percent);
esp_err_t tab5_keyboard_get_indicator_brightness(uint8_t *out_brightness_percent);

#ifdef __cplusplus
}
#endif
