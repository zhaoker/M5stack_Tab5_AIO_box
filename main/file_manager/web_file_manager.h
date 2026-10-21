#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_file_manager_start(void);
void web_file_manager_stop(void);
bool web_file_manager_is_running(void);
bool web_file_manager_can_start(char *reason, size_t reason_len);
bool web_file_manager_get_url(char *buffer, size_t buffer_len);
bool web_file_manager_is_upload_active(void);

#ifdef __cplusplus
}
#endif
