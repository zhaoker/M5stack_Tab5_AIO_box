#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rtc_rx8130_init(void);
bool rtc_rx8130_is_available(void);
esp_err_t rtc_rx8130_get_time(struct tm *out_time);
esp_err_t rtc_rx8130_set_time(const struct tm *time);
esp_err_t rtc_rx8130_init_battery_backup(void);
esp_err_t rtc_rx8130_clear_irq_flags(void);
esp_err_t rtc_rx8130_disable_irq(void);

#ifdef __cplusplus
}
#endif
