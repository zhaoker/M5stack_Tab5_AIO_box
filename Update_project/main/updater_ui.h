#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t updater_ui_init(void);
bool updater_ui_is_ready(void);
void updater_ui_force_backlight_off(void);
void updater_ui_show_update_mode(const char *status);
void updater_ui_set_status(const char *status);
void updater_ui_set_progress(int percent);
void updater_ui_set_error(const char *message);
void updater_ui_blackout(void);
void updater_ui_pump(void);

#ifdef __cplusplus
}
#endif
