#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ina226_present;
    bool battery_present;
    bool usb_c_present;
    bool charging;
    bool discharging;
    bool power_only_usb;
    bool charge_enabled;
    bool fast_charge_enabled;
    float bus_voltage_v;
    float shunt_voltage_v;
    float current_a;
    float display_current_a;
    float power_w;
    float battery_percent;
    float estimated_runtime_h;
    uint16_t manufacturer_id;
    uint16_t die_id;
    esp_err_t last_error;
} power_monitor_status_t;

esp_err_t power_monitor_init(void);
esp_err_t power_monitor_read(power_monitor_status_t *status);
bool power_monitor_get_cached_status(power_monitor_status_t *status);
esp_err_t power_monitor_start_background(void);
esp_err_t power_monitor_apply_boot_policy_async(bool auto_charge_enabled,
                                                bool fast_charge_enabled,
                                                uint32_t delay_ms);
esp_err_t power_monitor_set_charge_enabled(bool enabled, bool fast_charge_enabled);
esp_err_t power_monitor_set_fast_charge_enabled(bool enabled);
esp_err_t power_monitor_start_delayed_charge(uint32_t delay_ms, bool fast_charge_enabled);
esp_err_t power_monitor_start_delayed_standard_charge(uint32_t delay_ms);
bool power_monitor_get_fast_charge_enabled(void);
float power_monitor_battery_percentage(float bus_voltage_v);

#ifdef __cplusplus
}
#endif
