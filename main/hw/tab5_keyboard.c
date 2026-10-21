#include "tab5_keyboard.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "screensaver/screensaver_service.h"
#include <string.h>

#ifndef APP_TAB5_KEYBOARD_I2C_PORT
#define APP_TAB5_KEYBOARD_I2C_PORT 1
#endif

#ifndef APP_TAB5_KEYBOARD_I2C_SDA
#define APP_TAB5_KEYBOARD_I2C_SDA 0
#endif

#ifndef APP_TAB5_KEYBOARD_I2C_SCL
#define APP_TAB5_KEYBOARD_I2C_SCL 1
#endif

#ifndef APP_TAB5_KEYBOARD_I2C_ADDR
#define APP_TAB5_KEYBOARD_I2C_ADDR 0x6D
#endif

#define TAB5_KB_I2C_SPEED_HZ 400000
#define TAB5_KB_I2C_TIMEOUT_MS 20

#define TAB5_KB_REG_EVENT_NUM 0x02
#define TAB5_KB_REG_BRIGHTNESS 0x03
#define TAB5_KB_REG_MODE_KEYBOARD 0x10
#define TAB5_KB_REG_CHAR_EVENT_LENGTH 0x40
#define TAB5_KB_REG_CHAR_EVENT 0x50
#define TAB5_KB_REG_FIRMWARE_VERSION 0xFE

#define TAB5_KB_MODE_CHARACTER 2

static const char *TAG = "tab5_kb";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_own_bus;
static bool s_present;
static bool s_was_detected;
static bool s_startup_probe_completed;
static bool s_startup_was_detected;
static bool s_auto_probe_disabled;
static bool s_indicator_brightness_configured;
static uint8_t s_indicator_brightness;

static esp_err_t get_keyboard_bus(bool *out_own_bus);

void tab5_keyboard_dump_state(const char *where)
{
    ESP_LOGD(TAG, "[%s] present=%d was_detected=%d bus=%p dev=%p own_bus=%d",
             where ? where : "?",
             s_present ? 1 : 0,
             s_was_detected ? 1 : 0,
             (void *)s_bus,
             (void *)s_dev,
             s_own_bus ? 1 : 0);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_dev || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, TAB5_KB_I2C_TIMEOUT_MS);
}

static esp_err_t read_reg8(uint8_t reg, uint8_t *value)
{
    return read_reg(reg, value, 1);
}

static esp_err_t write_reg8(uint8_t reg, uint8_t value)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), TAB5_KB_I2C_TIMEOUT_MS);
}

static esp_err_t write_reg8_to_dev(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(dev, buf, sizeof(buf), TAB5_KB_I2C_TIMEOUT_MS);
}

static esp_err_t read_reg8_from_dev(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    if (!dev || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(dev, &reg, 1, value, 1, TAB5_KB_I2C_TIMEOUT_MS);
}

static esp_err_t open_temp_device(i2c_master_dev_handle_t *out_dev,
                                  bool *out_own_bus,
                                  i2c_master_bus_handle_t *out_previous_bus)
{
    bool own_bus = false;
    esp_err_t err;

    if (!out_dev || !out_own_bus || !out_previous_bus) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_dev = NULL;
    *out_own_bus = false;
    *out_previous_bus = s_bus;

    err = get_keyboard_bus(&own_bus);
    if (err != ESP_OK || !s_bus) {
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    err = i2c_master_probe(s_bus, APP_TAB5_KEYBOARD_I2C_ADDR, TAB5_KB_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (own_bus && s_bus) {
            i2c_del_master_bus(s_bus);
            s_bus = *out_previous_bus;
        }
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = APP_TAB5_KEYBOARD_I2C_ADDR,
        .scl_speed_hz = TAB5_KB_I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, out_dev);
    if (err != ESP_OK) {
        if (own_bus && s_bus) {
            i2c_del_master_bus(s_bus);
            s_bus = *out_previous_bus;
        }
        return err;
    }

    *out_own_bus = own_bus;
    s_was_detected = true;
    return ESP_OK;
}

static void close_temp_device(i2c_master_dev_handle_t dev,
                              bool own_bus,
                              i2c_master_bus_handle_t previous_bus)
{
    if (dev) {
        i2c_master_bus_rm_device(dev);
    }
    if (own_bus && s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = previous_bus;
    }
}

static void cleanup_handles(void)
{
    tab5_keyboard_dump_state("cleanup before");
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus && s_own_bus) {
        i2c_del_master_bus(s_bus);
    }
    s_bus = NULL;
    s_own_bus = false;
    s_present = false;
    tab5_keyboard_dump_state("cleanup after");
}

static esp_err_t get_keyboard_bus(bool *out_own_bus)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = APP_TAB5_KEYBOARD_I2C_PORT,
        .sda_io_num = APP_TAB5_KEYBOARD_I2C_SDA,
        .scl_io_num = APP_TAB5_KEYBOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);

    if (out_own_bus) {
        *out_own_bus = false;
    }

    if (err == ESP_OK) {
        s_bus = bus;
        if (out_own_bus) {
            *out_own_bus = true;
        }
        ESP_LOGI(TAG, "created keyboard i2c bus port=%d sda=%d scl=%d addr=0x%02X",
                 APP_TAB5_KEYBOARD_I2C_PORT,
                 APP_TAB5_KEYBOARD_I2C_SDA,
                 APP_TAB5_KEYBOARD_I2C_SCL,
                 APP_TAB5_KEYBOARD_I2C_ADDR);
        return ESP_OK;
    }

    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "keyboard i2c bus already exists; getting handle for port=%d",
                 APP_TAB5_KEYBOARD_I2C_PORT);
        err = i2c_master_get_bus_handle(APP_TAB5_KEYBOARD_I2C_PORT, &s_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "get existing i2c bus failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    ESP_LOGW(TAG, "create i2c bus failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t tab5_keyboard_probe_presence(void)
{
    bool own_bus = false;
    i2c_master_bus_handle_t previous_bus = s_bus;
    esp_err_t err;

    tab5_keyboard_dump_state("probe enter");
    if (s_present || s_was_detected) {
        ESP_LOGI(TAG, "keyboard probe skipped; known present=%d was_detected=%d",
                 s_present ? 1 : 0, s_was_detected ? 1 : 0);
        if (!s_startup_probe_completed) {
            s_startup_probe_completed = true;
            s_startup_was_detected = true;
        }
        return ESP_OK;
    }

    err = get_keyboard_bus(&own_bus);
    if (err != ESP_OK || !s_bus) {
        tab5_keyboard_dump_state("probe get bus failed");
        if (!s_startup_probe_completed) {
            s_startup_probe_completed = true;
            s_startup_was_detected = false;
        }
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    err = i2c_master_probe(s_bus, APP_TAB5_KEYBOARD_I2C_ADDR, TAB5_KB_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        s_was_detected = true;
        ESP_LOGI(TAG, "keyboard probe-only detected at 0x%02X", APP_TAB5_KEYBOARD_I2C_ADDR);
    } else {
        ESP_LOGD(TAG, "keyboard probe-only not detected at 0x%02X: %s",
                 APP_TAB5_KEYBOARD_I2C_ADDR, esp_err_to_name(err));
    }

    if (own_bus && s_bus && !s_dev) {
        i2c_del_master_bus(s_bus);
        s_bus = previous_bus;
    }
    if (!s_startup_probe_completed) {
        s_startup_probe_completed = true;
        s_startup_was_detected = (err == ESP_OK);
        s_auto_probe_disabled = (err != ESP_OK);
    }
    tab5_keyboard_dump_state("probe leave");
    return err;
}

esp_err_t tab5_keyboard_init(void)
{
    tab5_keyboard_dump_state("init enter");
    if (s_present && s_dev) {
        ESP_LOGI(TAG, "keyboard init skipped; already present");
        return ESP_OK;
    }
    if (s_auto_probe_disabled && !s_was_detected) {
        return ESP_ERR_NOT_FOUND;
    }

    cleanup_handles();

    esp_err_t err = get_keyboard_bus(&s_own_bus);
    if (err != ESP_OK) {
        tab5_keyboard_dump_state("init create bus failed");
        return err;
    }

    err = i2c_master_probe(s_bus, APP_TAB5_KEYBOARD_I2C_ADDR, TAB5_KB_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "keyboard not detected at 0x%02X: %s", APP_TAB5_KEYBOARD_I2C_ADDR, esp_err_to_name(err));
        cleanup_handles();
        tab5_keyboard_dump_state("init probe failed");
        return err;
    }
    ESP_LOGI(TAG, "keyboard probe ok at 0x%02X", APP_TAB5_KEYBOARD_I2C_ADDR);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = APP_TAB5_KEYBOARD_I2C_ADDR,
        .scl_speed_hz = TAB5_KB_I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add keyboard device failed: %s", esp_err_to_name(err));
        cleanup_handles();
        tab5_keyboard_dump_state("init add device failed");
        return err;
    }

    uint8_t fw = 0;
    err = read_reg8(TAB5_KB_REG_FIRMWARE_VERSION, &fw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read firmware failed: %s", esp_err_to_name(err));
        cleanup_handles();
        tab5_keyboard_dump_state("init read fw failed");
        return err;
    }

    err = write_reg8(TAB5_KB_REG_MODE_KEYBOARD, TAB5_KB_MODE_CHARACTER);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set character mode failed: %s", esp_err_to_name(err));
        cleanup_handles();
        tab5_keyboard_dump_state("init set mode failed");
        return err;
    }
    (void)write_reg8(TAB5_KB_REG_EVENT_NUM, 0);

    s_present = true;
    s_was_detected = true;
    s_auto_probe_disabled = false;
    if (s_indicator_brightness_configured) {
        esp_err_t led_err = write_reg8(TAB5_KB_REG_BRIGHTNESS, s_indicator_brightness);
        if (led_err != ESP_OK) {
            ESP_LOGW(TAG, "apply indicator brightness failed: %s", esp_err_to_name(led_err));
        }
    }
    ESP_LOGI(TAG, "Tab5 keyboard detected, fw=0x%02X", fw);
    tab5_keyboard_dump_state("init success");
    return ESP_OK;
}

void tab5_keyboard_deinit(void)
{
    ESP_LOGI(TAG, "keyboard deinit requested");
    cleanup_handles();
}

bool tab5_keyboard_is_present(void)
{
    return s_present;
}

bool tab5_keyboard_was_detected(void)
{
    bool detected = s_present || s_was_detected;
    ESP_LOGD(TAG, "keyboard was_detected query: present=%d was_detected=%d result=%d",
             s_present ? 1 : 0,
             s_was_detected ? 1 : 0,
             detected ? 1 : 0);
    return detected;
}

bool tab5_keyboard_was_detected_at_startup(void)
{
    return s_startup_probe_completed && s_startup_was_detected;
}

esp_err_t tab5_keyboard_read_event(tab5_keyboard_event_t *out_event)
{
    if (!out_event) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_event, 0, sizeof(*out_event));
    if (!s_present || !s_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t count = 0;
    esp_err_t err = read_reg8(TAB5_KB_REG_EVENT_NUM, &count);
    if (err != ESP_OK) {
        s_present = false;
        return err;
    }
    if (count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t len = 0;
    err = read_reg8(TAB5_KB_REG_CHAR_EVENT_LENGTH, &len);
    if (err != ESP_OK) {
        s_present = false;
        return err;
    }
    if (len == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (len > TAB5_KEYBOARD_MAX_CHARS + 1) {
        (void)write_reg8(TAB5_KB_REG_EVENT_NUM, 0);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t buf[TAB5_KEYBOARD_MAX_CHARS + 1] = {0};
    err = read_reg(TAB5_KB_REG_CHAR_EVENT, buf, len);
    if (err != ESP_OK) {
        s_present = false;
        return err;
    }

    out_event->modifier = buf[0];
    out_event->length = (uint8_t)(len - 1);
    if (out_event->length > 0) {
        memcpy(out_event->chars, buf + 1, out_event->length);
        out_event->chars[out_event->length] = '\0';
        screensaver_service_notify_activity();
    }
    return ESP_OK;
}

esp_err_t tab5_keyboard_set_indicator_brightness(uint8_t brightness_percent)
{
    i2c_master_dev_handle_t temp_dev = NULL;
    i2c_master_bus_handle_t previous_bus = NULL;
    bool own_bus = false;
    esp_err_t err;

    if (brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    s_indicator_brightness = brightness_percent;
    s_indicator_brightness_configured = true;

    if (s_auto_probe_disabled && !s_was_detected) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_present && s_dev) {
        err = write_reg8(TAB5_KB_REG_BRIGHTNESS, brightness_percent);
        if (err != ESP_OK) {
            s_present = false;
        }
        return err;
    }

    err = open_temp_device(&temp_dev, &own_bus, &previous_bus);
    if (err == ESP_OK) {
        err = write_reg8_to_dev(temp_dev, TAB5_KB_REG_BRIGHTNESS, brightness_percent);
    }
    close_temp_device(temp_dev, own_bus, previous_bus);
    return err;
}

esp_err_t tab5_keyboard_get_indicator_brightness(uint8_t *out_brightness_percent)
{
    i2c_master_dev_handle_t temp_dev = NULL;
    i2c_master_bus_handle_t previous_bus = NULL;
    bool own_bus = false;
    esp_err_t err;

    if (!out_brightness_percent) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_auto_probe_disabled && !s_was_detected) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_present && s_dev) {
        err = read_reg8(TAB5_KB_REG_BRIGHTNESS, out_brightness_percent);
        if (err != ESP_OK) {
            s_present = false;
        }
        return err;
    }

    err = open_temp_device(&temp_dev, &own_bus, &previous_bus);
    if (err == ESP_OK) {
        err = read_reg8_from_dev(temp_dev, TAB5_KB_REG_BRIGHTNESS, out_brightness_percent);
    }
    close_temp_device(temp_dev, own_bus, previous_bus);
    return err;
}
