#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t screenshot_service_start(void);
void screenshot_service_refresh_enabled(void);
esp_err_t screenshot_service_request_capture(void);
bool screenshot_service_is_capture_busy(void);

#ifdef __cplusplus
}
#endif
