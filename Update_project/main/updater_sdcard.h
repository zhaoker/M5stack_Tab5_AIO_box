#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t updater_sdcard_mount(void);
bool updater_sdcard_is_mounted(void);
esp_err_t updater_sdcard_ensure_update_dir(void);
void updater_write_last_error(const char *message);
void updater_append_history(const char *state, const char *message);
void updater_write_progress(const char *state, int percent, const char *message);

#ifdef __cplusplus
}
#endif
