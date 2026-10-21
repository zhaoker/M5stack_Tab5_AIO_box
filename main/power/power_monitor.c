#include "power_monitor.h"

#include "app_config.h"
#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "power_monitor";

enum {
    INA226_ADDR = 0x41,
    INA226_REG_CONFIG = 0x00,
    INA226_REG_SHUNT_VOLTAGE = 0x01,
    INA226_REG_BUS_VOLTAGE = 0x02,
    INA226_REG_POWER = 0x03,
    INA226_REG_CURRENT = 0x04,
    INA226_REG_CALIBRATION = 0x05,
    INA226_REG_MANUFACTURER_ID = 0xFE,
    INA226_REG_DIE_ID = 0xFF,
    INA226_I2C_TIMEOUT_MS = 50,
    INA226_CONFIG_TAB5 = 0x0527,
    INA226_CALIBRATION_TAB5 = 0x0D55,
    POWER_MONITOR_TASK_STACK = 4096,
    POWER_MONITOR_TASK_PRIORITY = 1,
    POWER_MONITOR_REFRESH_MS = 5000,
};

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define POWER_MONITOR_TASK_CORE 1
#else
#define POWER_MONITOR_TASK_CORE tskNO_AFFINITY
#endif

#define INA226_SHUNT_RESISTOR_OHM 0.005f
#define INA226_MAX_EXPECTED_A     8.192f
#define INA226_CURRENT_LSB_A      0.0003f
#define INA226_POWER_LSB_W        0.0075f

static SemaphoreHandle_t s_io_mutex;
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;
static power_monitor_status_t s_cached_status;
static i2c_master_dev_handle_t s_ina226_dev;
static TaskHandle_t s_monitor_task;
static bool s_cached_status_valid;
static bool s_initialized;
static bool s_ina226_present;
static bool s_charge_enabled;
static bool s_fast_charge_enabled;
static bool s_charge_mode_manual;
static bool s_boot_auto_charge_pending;
static uint32_t s_boot_auto_charge_delay_ms;
static TickType_t s_boot_auto_charge_start_tick;
static uint16_t s_manufacturer_id;
static uint16_t s_die_id;

static bool power_monitor_ensure_io_mutex(void)
{
    if (s_io_mutex) {
        return true;
    }
    s_io_mutex = xSemaphoreCreateMutex();
    return s_io_mutex != NULL;
}

static bool power_monitor_lock_io(TickType_t ticks_to_wait)
{
    return power_monitor_ensure_io_mutex() &&
           xSemaphoreTake(s_io_mutex, ticks_to_wait) == pdTRUE;
}

static void power_monitor_unlock_io(void)
{
    if (s_io_mutex) {
        xSemaphoreGive(s_io_mutex);
    }
}

static void power_monitor_default_status(power_monitor_status_t *status)
{
    if (!status) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->charge_enabled = s_charge_enabled;
    status->fast_charge_enabled = s_fast_charge_enabled;
    status->last_error = ESP_ERR_INVALID_STATE;
}

static void power_monitor_update_cache(const power_monitor_status_t *status)
{
    if (!status) {
        return;
    }
    portENTER_CRITICAL(&s_status_mux);
    s_cached_status = *status;
    s_cached_status_valid = true;
    portEXIT_CRITICAL(&s_status_mux);
}

static void power_monitor_update_cached_charge_state(void)
{
    portENTER_CRITICAL(&s_status_mux);
    if (s_cached_status_valid) {
        s_cached_status.charge_enabled = s_charge_enabled;
        s_cached_status.fast_charge_enabled = s_fast_charge_enabled;
    }
    portEXIT_CRITICAL(&s_status_mux);
}

static esp_err_t ina226_read_reg(uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    esp_err_t err;

    if (!s_ina226_dev || !value) {
        return ESP_ERR_INVALID_STATE;
    }

    err = i2c_master_transmit_receive(s_ina226_dev, &reg, 1, data, sizeof(data), INA226_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

static esp_err_t ina226_write_reg(uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };

    if (!s_ina226_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit(s_ina226_dev, data, sizeof(data), INA226_I2C_TIMEOUT_MS);
}

float power_monitor_battery_percentage(float bus_voltage_v)
{
    float pct = (bus_voltage_v - APP_BATTERY_MIN_V) /
                (APP_BATTERY_MAX_V - APP_BATTERY_MIN_V) * 100.0f;

    if (pct > 100.0f) {
        pct = 100.0f;
    } else if (pct < 0.0f) {
        pct = 0.0f;
    }
    return pct;
}

static esp_err_t power_monitor_init_locked(void)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err;

    if (s_initialized) {
        return s_ina226_present ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    err = bsp_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_i2c_init failed: %s", esp_err_to_name(err));
        return err;
    }

    bus = bsp_i2c_get_handle();
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }

    bsp_io_expander_pi4ioe_init(bus);
    bsp_set_charge_qc_en(s_charge_enabled && s_fast_charge_enabled);
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_set_charge_en(s_charge_enabled);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_ADDR,
        .scl_speed_hz = 400000,
    };

    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_ina226_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add INA226 failed: %s", esp_err_to_name(err));
        s_initialized = true;
        s_ina226_present = false;
        return err;
    }

    err = i2c_master_probe(bus, INA226_ADDR, INA226_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 not found at 0x%02X: %s", INA226_ADDR, esp_err_to_name(err));
        s_initialized = true;
        s_ina226_present = false;
        return err;
    }

    (void)ina226_read_reg(INA226_REG_MANUFACTURER_ID, &s_manufacturer_id);
    (void)ina226_read_reg(INA226_REG_DIE_ID, &s_die_id);

    err = ina226_write_reg(INA226_REG_CALIBRATION, INA226_CALIBRATION_TAB5);
    if (err == ESP_OK) {
        err = ina226_write_reg(INA226_REG_CONFIG, INA226_CONFIG_TAB5);
    }

    s_initialized = true;
    s_ina226_present = (err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 configure failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "INA226 ready, manufacturer=0x%04X die=0x%04X",
                 s_manufacturer_id, s_die_id);
    }
    return err;
}

esp_err_t power_monitor_init(void)
{
    esp_err_t err;

    if (!power_monitor_lock_io(portMAX_DELAY)) {
        return ESP_ERR_NO_MEM;
    }
    err = power_monitor_init_locked();
    power_monitor_unlock_io();
    return err;
}

static void power_monitor_apply_charge_outputs_locked(void)
{
    bsp_set_charge_qc_en(s_charge_enabled && s_fast_charge_enabled);
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_set_charge_en(s_charge_enabled);
}

esp_err_t power_monitor_set_charge_enabled(bool enabled, bool fast_charge_enabled)
{
    esp_err_t err;

    s_charge_enabled = enabled;
    s_fast_charge_enabled = fast_charge_enabled;
    s_charge_mode_manual = true;
    s_boot_auto_charge_pending = false;

    if (!power_monitor_lock_io(portMAX_DELAY)) {
        return ESP_ERR_NO_MEM;
    }
    err = power_monitor_init_locked();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        power_monitor_unlock_io();
        return err;
    }

    power_monitor_apply_charge_outputs_locked();
    power_monitor_unlock_io();
    power_monitor_update_cached_charge_state();
    ESP_LOGI(TAG,
             "charge %s, mode: %s",
             s_charge_enabled ? "enabled" : "disabled",
             s_fast_charge_enabled ? "fast" : "standard");
    return ESP_OK;
}

esp_err_t power_monitor_set_fast_charge_enabled(bool enabled)
{
    return power_monitor_set_charge_enabled(true, enabled);
}

static void delayed_charge_task(void *arg)
{
    uintptr_t packed = (uintptr_t)arg;
    bool fast_charge_enabled = (packed & 0x1U) != 0;
    uint32_t delay_ms = (uint32_t)(packed >> 1);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    if (!s_charge_mode_manual) {
        s_fast_charge_enabled = fast_charge_enabled;
        s_charge_enabled = true;
        if (power_monitor_lock_io(portMAX_DELAY)) {
            power_monitor_apply_charge_outputs_locked();
            power_monitor_unlock_io();
        }
        ESP_LOGI(TAG, "delayed %s charge enabled", s_fast_charge_enabled ? "fast" : "standard");
    }
    vTaskDelete(NULL);
}

esp_err_t power_monitor_start_delayed_charge(uint32_t delay_ms, bool fast_charge_enabled)
{
    s_charge_enabled = false;
    s_fast_charge_enabled = fast_charge_enabled;
    s_charge_mode_manual = false;

    esp_err_t err = power_monitor_init();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }
    if (power_monitor_lock_io(portMAX_DELAY)) {
        power_monitor_apply_charge_outputs_locked();
        power_monitor_unlock_io();
    }
    if (xTaskCreate(delayed_charge_task, "charge_delay", 3072,
                    (void *)(((uintptr_t)delay_ms << 1) | (fast_charge_enabled ? 1U : 0U)),
                    5,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t power_monitor_start_delayed_standard_charge(uint32_t delay_ms)
{
    return power_monitor_start_delayed_charge(delay_ms, false);
}

bool power_monitor_get_fast_charge_enabled(void)
{
    return s_fast_charge_enabled;
}

static esp_err_t power_monitor_read_live_locked(power_monitor_status_t *status)
{
    uint16_t raw_bus = 0;
    uint16_t raw_shunt = 0;
    uint16_t raw_current = 0;
    uint16_t raw_power = 0;
    esp_err_t err;

    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->last_error = ESP_OK;

    err = power_monitor_init_locked();
    status->charge_enabled = s_charge_enabled;
    status->fast_charge_enabled = s_fast_charge_enabled;
    status->usb_c_present = bsp_usb_c_detect();
    status->manufacturer_id = s_manufacturer_id;
    status->die_id = s_die_id;
    if (err != ESP_OK || !s_ina226_present) {
        status->last_error = err;
        status->power_only_usb = status->usb_c_present;
        status->ina226_present = false;
        return err;
    }

    status->ina226_present = true;

    err = ina226_read_reg(INA226_REG_SHUNT_VOLTAGE, &raw_shunt);
    if (err == ESP_OK) {
        err = ina226_read_reg(INA226_REG_BUS_VOLTAGE, &raw_bus);
    }
    if (err == ESP_OK) {
        err = ina226_read_reg(INA226_REG_CURRENT, &raw_current);
    }
    if (err == ESP_OK) {
        err = ina226_read_reg(INA226_REG_POWER, &raw_power);
    }

    if (err != ESP_OK) {
        status->last_error = err;
        status->power_only_usb = status->usb_c_present;
        return err;
    }

    status->shunt_voltage_v = (float)(int16_t)raw_shunt * 0.0000025f;
    status->bus_voltage_v = (float)(int16_t)raw_bus * 0.00125f;
    status->current_a = (float)(int16_t)raw_current * INA226_CURRENT_LSB_A;
    status->power_w = (float)raw_power * INA226_POWER_LSB_W;
    status->display_current_a = status->current_a < 0.0f ? -status->current_a : status->current_a;
    status->battery_percent = power_monitor_battery_percentage(status->bus_voltage_v);
    status->battery_present = status->bus_voltage_v >= APP_BATTERY_PRESENT_MIN_V;
    status->charging = status->current_a < -0.02f;
    status->discharging = status->current_a > 0.02f;
    status->power_only_usb = status->usb_c_present && !status->battery_present;

    if (status->discharging && status->display_current_a > 0.01f) {
        status->estimated_runtime_h =
            (APP_BATTERY_CAPACITY_MAH * (status->battery_percent / 100.0f)) /
            (status->display_current_a * 1000.0f);
    }

    return ESP_OK;
}

esp_err_t power_monitor_read(power_monitor_status_t *status)
{
    esp_err_t err;

    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!power_monitor_lock_io(portMAX_DELAY)) {
        power_monitor_default_status(status);
        return ESP_ERR_NO_MEM;
    }
    err = power_monitor_read_live_locked(status);
    power_monitor_unlock_io();
    power_monitor_update_cache(status);
    return err;
}

bool power_monitor_get_cached_status(power_monitor_status_t *status)
{
    bool valid;

    if (!status) {
        return false;
    }

    portENTER_CRITICAL(&s_status_mux);
    valid = s_cached_status_valid;
    if (valid) {
        *status = s_cached_status;
    }
    portEXIT_CRITICAL(&s_status_mux);

    if (!valid) {
        power_monitor_default_status(status);
    }
    return valid;
}

static void power_monitor_task(void *arg)
{
    (void)arg;

    for (;;) {
        power_monitor_status_t status;
        bool delayed_charge_enabled = false;

        power_monitor_default_status(&status);
        if (power_monitor_lock_io(pdMS_TO_TICKS(1000))) {
            (void)power_monitor_init_locked();
            if (s_boot_auto_charge_pending &&
                !s_charge_mode_manual &&
                xTaskGetTickCount() - s_boot_auto_charge_start_tick >=
                    pdMS_TO_TICKS(s_boot_auto_charge_delay_ms)) {
                s_charge_enabled = true;
                power_monitor_apply_charge_outputs_locked();
                s_boot_auto_charge_pending = false;
                delayed_charge_enabled = true;
            }
            (void)power_monitor_read_live_locked(&status);
            power_monitor_unlock_io();
            power_monitor_update_cache(&status);
        }

        if (delayed_charge_enabled) {
            ESP_LOGI(TAG, "delayed %s charge enabled",
                     s_fast_charge_enabled ? "fast" : "standard");
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_MONITOR_REFRESH_MS));
    }
}

esp_err_t power_monitor_start_background(void)
{
    if (s_monitor_task) {
        return ESP_OK;
    }
    if (!power_monitor_ensure_io_mutex()) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(power_monitor_task,
                                                     "pwr_mon",
                                                     POWER_MONITOR_TASK_STACK,
                                                     NULL,
                                                     POWER_MONITOR_TASK_PRIORITY,
                                                     &s_monitor_task,
                                                     POWER_MONITOR_TASK_CORE,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ret = xTaskCreatePinnedToCore(power_monitor_task,
                                      "pwr_mon",
                                      POWER_MONITOR_TASK_STACK,
                                      NULL,
                                      POWER_MONITOR_TASK_PRIORITY,
                                      &s_monitor_task,
                                      POWER_MONITOR_TASK_CORE);
    }
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t power_monitor_apply_boot_policy_async(bool auto_charge_enabled,
                                                bool fast_charge_enabled,
                                                uint32_t delay_ms)
{
    s_charge_enabled = false;
    s_fast_charge_enabled = fast_charge_enabled;
    s_charge_mode_manual = false;
    s_boot_auto_charge_pending = auto_charge_enabled;
    s_boot_auto_charge_delay_ms = delay_ms;
    s_boot_auto_charge_start_tick = xTaskGetTickCount();
    power_monitor_update_cached_charge_state();

    return power_monitor_start_background();
}
