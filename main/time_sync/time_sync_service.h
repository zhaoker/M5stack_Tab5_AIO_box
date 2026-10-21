#pragma once

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t time_sync_service_init(void);
void time_sync_service_apply_timezone(void);
void time_sync_service_on_wifi_connected(void);
bool time_sync_service_system_time_is_valid(void);

#ifdef __cplusplus
}
#endif
