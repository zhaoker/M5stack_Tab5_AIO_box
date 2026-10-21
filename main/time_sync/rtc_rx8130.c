#include "time_sync/rtc_rx8130.h"

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include <string.h>

static const char *TAG = "rtc_rx8130";

#define RX8130_I2C_ADDR     0x32
#define RX8130_I2C_SPEED_HZ 400000

#define RX8130_REG_SEC   0x10
#define RX8130_REG_MIN   0x11
#define RX8130_REG_HOUR  0x12
#define RX8130_REG_WDAY  0x13
#define RX8130_REG_MDAY  0x14
#define RX8130_REG_MONTH 0x15
#define RX8130_REG_YEAR  0x16
#define RX8130_REG_FLAG  0x1D
#define RX8130_REG_CTRL0 0x1E
#define RX8130_REG_CTRL1 0x1F

#define RX8130_BIT_CTRL_STOP BIT(6)

static i2c_master_dev_handle_t s_dev;
static bool s_available;

static uint8_t bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0f));
}

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "rtc not initialized");
    ESP_RETURN_ON_FALSE(buf != NULL && len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid read args");
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 200);
}

static esp_err_t write_regs(uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t tmp[16];

    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "rtc not initialized");
    ESP_RETURN_ON_FALSE(buf != NULL && len > 0 && len + 1 <= sizeof(tmp),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid write args");

    tmp[0] = reg;
    memcpy(&tmp[1], buf, len);
    return i2c_master_transmit(s_dev, tmp, len + 1, 200);
}

static esp_err_t read_reg8(uint8_t reg, uint8_t *out_value)
{
    return read_regs(reg, out_value, 1);
}

static esp_err_t write_reg8(uint8_t reg, uint8_t value)
{
    return write_regs(reg, &value, 1);
}

esp_err_t rtc_rx8130_init(void)
{
    if (s_available) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "bsp i2c init failed");
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG, "bsp i2c handle unavailable");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RX8130_I2C_ADDR,
        .scl_speed_hz = RX8130_I2C_SPEED_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add rtc i2c device failed: %s", esp_err_to_name(err));
        s_dev = NULL;
        s_available = false;
        return err;
    }

    uint8_t seconds = 0;
    err = read_reg8(RX8130_REG_SEC, &seconds);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rtc probe failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        s_available = false;
        return err;
    }

    s_available = true;
    ESP_LOGI(TAG, "RX8130 initialized");
    return ESP_OK;
}

bool rtc_rx8130_is_available(void)
{
    return s_available;
}

esp_err_t rtc_rx8130_init_battery_backup(void)
{
    uint8_t ctrl1 = 0;

    ESP_RETURN_ON_ERROR(rtc_rx8130_init(), TAG, "rtc init failed");
    ESP_RETURN_ON_ERROR(read_reg8(RX8130_REG_CTRL1, &ctrl1), TAG, "read ctrl1 failed");
    ctrl1 |= BIT(4) | BIT(5);
    ESP_RETURN_ON_ERROR(write_reg8(RX8130_REG_CTRL1, ctrl1), TAG, "write ctrl1 failed");
    return ESP_OK;
}

esp_err_t rtc_rx8130_clear_irq_flags(void)
{
    ESP_RETURN_ON_ERROR(rtc_rx8130_init(), TAG, "rtc init failed");
    return write_reg8(RX8130_REG_FLAG, 0);
}

esp_err_t rtc_rx8130_disable_irq(void)
{
    ESP_RETURN_ON_ERROR(rtc_rx8130_init(), TAG, "rtc init failed");
    return write_reg8(RX8130_REG_CTRL0, 0);
}

esp_err_t rtc_rx8130_get_time(struct tm *out_time)
{
    uint8_t date[7] = {0};

    ESP_RETURN_ON_FALSE(out_time != NULL, ESP_ERR_INVALID_ARG, TAG, "time is NULL");
    ESP_RETURN_ON_ERROR(rtc_rx8130_init(), TAG, "rtc init failed");
    ESP_RETURN_ON_ERROR(read_regs(RX8130_REG_SEC, date, sizeof(date)), TAG, "read time failed");

    memset(out_time, 0, sizeof(*out_time));
    out_time->tm_sec = bcd_to_dec(date[RX8130_REG_SEC - RX8130_REG_SEC] & 0x7f);
    out_time->tm_min = bcd_to_dec(date[RX8130_REG_MIN - RX8130_REG_SEC] & 0x7f);
    out_time->tm_hour = bcd_to_dec(date[RX8130_REG_HOUR - RX8130_REG_SEC] & 0x3f);
    out_time->tm_wday = bcd_to_dec(date[RX8130_REG_WDAY - RX8130_REG_SEC] & 0x07);
    out_time->tm_mday = bcd_to_dec(date[RX8130_REG_MDAY - RX8130_REG_SEC] & 0x3f);
    out_time->tm_mon = bcd_to_dec(date[RX8130_REG_MONTH - RX8130_REG_SEC] & 0x1f) - 1;
    out_time->tm_year = bcd_to_dec(date[RX8130_REG_YEAR - RX8130_REG_SEC]) + 100;
    out_time->tm_isdst = -1;

    if (out_time->tm_mon < 0 || out_time->tm_mon > 11 ||
        out_time->tm_mday < 1 || out_time->tm_mday > 31 ||
        out_time->tm_hour > 23 || out_time->tm_min > 59 || out_time->tm_sec > 59) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t rtc_rx8130_set_time(const struct tm *time)
{
    uint8_t ctrl0 = 0;
    uint8_t date[7];
    struct tm normalized;

    ESP_RETURN_ON_FALSE(time != NULL, ESP_ERR_INVALID_ARG, TAG, "time is NULL");
    ESP_RETURN_ON_ERROR(rtc_rx8130_init(), TAG, "rtc init failed");

    normalized = *time;
    if (normalized.tm_wday < 0 || normalized.tm_wday > 6) {
        time_t ts = mktime(&normalized);
        if (ts > 0) {
            localtime_r(&ts, &normalized);
        }
    }

    date[0] = dec_to_bcd((uint8_t)normalized.tm_sec);
    date[1] = dec_to_bcd((uint8_t)normalized.tm_min);
    date[2] = dec_to_bcd((uint8_t)normalized.tm_hour);
    date[3] = dec_to_bcd((uint8_t)normalized.tm_wday);
    date[4] = dec_to_bcd((uint8_t)normalized.tm_mday);
    date[5] = dec_to_bcd((uint8_t)(normalized.tm_mon + 1));
    date[6] = dec_to_bcd((uint8_t)(normalized.tm_year % 100));

    ESP_RETURN_ON_ERROR(read_reg8(RX8130_REG_CTRL0, &ctrl0), TAG, "read ctrl0 failed");
    ESP_RETURN_ON_ERROR(write_reg8(RX8130_REG_CTRL0, ctrl0 | RX8130_BIT_CTRL_STOP),
                        TAG,
                        "set STOP failed");
    ESP_RETURN_ON_ERROR(write_regs(RX8130_REG_SEC, date, sizeof(date)), TAG, "write time failed");
    ESP_RETURN_ON_ERROR(write_reg8(RX8130_REG_CTRL0, ctrl0 & ~RX8130_BIT_CTRL_STOP),
                        TAG,
                        "clear STOP failed");
    return ESP_OK;
}
