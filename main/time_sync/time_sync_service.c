#include "time_sync/time_sync_service.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "settings/app_settings.h"
#include "time_sync/rtc_rx8130.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "time_sync";

#define TIME_SYNC_MIN_VALID_YEAR 2026
#define TIME_SYNC_MAX_VALID_YEAR 2036
#define TIME_SYNC_TASK_STACK     4096
#define TIME_SYNC_TASK_PRIORITY  2
#define TIME_SYNC_WAIT_MS        15000

static TaskHandle_t s_sync_task;
static bool s_sntp_initialized;
static bool s_sync_completed_once;

static bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int days_before_month(int year, int month)
{
    static const int normal_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int days = normal_days[month - 1];

    if (month > 2 && is_leap_year(year)) {
        days++;
    }
    return days;
}

static time_t utc_tm_to_epoch(const struct tm *tm_utc)
{
    int year;
    int month;
    int day;
    int64_t days = 0;

    if (!tm_utc) {
        return (time_t)-1;
    }

    year = tm_utc->tm_year + 1900;
    month = tm_utc->tm_mon + 1;
    day = tm_utc->tm_mday;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        tm_utc->tm_hour < 0 || tm_utc->tm_hour > 23 ||
        tm_utc->tm_min < 0 || tm_utc->tm_min > 59 ||
        tm_utc->tm_sec < 0 || tm_utc->tm_sec > 60) {
        return (time_t)-1;
    }

    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    days += days_before_month(year, month);
    days += day - 1;

    return (time_t)(days * 86400LL +
                    (int64_t)tm_utc->tm_hour * 3600LL +
                    (int64_t)tm_utc->tm_min * 60LL +
                    tm_utc->tm_sec);
}

void time_sync_service_apply_timezone(void)
{
    const char *tz = app_settings_get_current_timezone_tz();

    setenv("TZ", tz && tz[0] ? tz : "CST-8", 1);
    tzset();
    ESP_LOGI(TAG, "timezone applied: %s", tz && tz[0] ? tz : "CST-8");
}

bool time_sync_service_system_time_is_valid(void)
{
    time_t now = time(NULL);
    struct tm tm_utc;
    int year;

    if (now <= 0 || !gmtime_r(&now, &tm_utc)) {
        return false;
    }
    year = tm_utc.tm_year + 1900;
    return year >= TIME_SYNC_MIN_VALID_YEAR && year <= TIME_SYNC_MAX_VALID_YEAR;
}

static esp_err_t set_system_time_from_rtc(void)
{
    struct tm rtc_utc;
    struct timeval tv;
    time_t rtc_ts;

    ESP_RETURN_ON_ERROR(rtc_rx8130_get_time(&rtc_utc), TAG, "read rtc time failed");
    rtc_ts = utc_tm_to_epoch(&rtc_utc);
    if (rtc_ts <= 0 || (rtc_utc.tm_year + 1900) < TIME_SYNC_MIN_VALID_YEAR ||
        (rtc_utc.tm_year + 1900) > TIME_SYNC_MAX_VALID_YEAR) {
        ESP_LOGW(TAG, "rtc UTC time is invalid: year=%d", rtc_utc.tm_year + 1900);
        return ESP_ERR_INVALID_RESPONSE;
    }

    tv.tv_sec = rtc_ts;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "settimeofday from rtc failed: errno=%d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "system time restored from RTC UTC: %04d-%02d-%02d %02d:%02d:%02d",
             rtc_utc.tm_year + 1900,
             rtc_utc.tm_mon + 1,
             rtc_utc.tm_mday,
             rtc_utc.tm_hour,
             rtc_utc.tm_min,
             rtc_utc.tm_sec);
    return ESP_OK;
}

static esp_err_t write_system_time_to_rtc(void)
{
    time_t now = time(NULL);
    struct tm tm_utc;

    if (!gmtime_r(&now, &tm_utc) || !time_sync_service_system_time_is_valid()) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(rtc_rx8130_set_time(&tm_utc), TAG, "write rtc time failed");
    ESP_LOGI(TAG,
             "RTC updated from system UTC: %04d-%02d-%02d %02d:%02d:%02d",
             tm_utc.tm_year + 1900,
             tm_utc.tm_mon + 1,
             tm_utc.tm_mday,
             tm_utc.tm_hour,
             tm_utc.tm_min,
             tm_utc.tm_sec);
    return ESP_OK;
}

esp_err_t time_sync_service_init(void)
{
    time_sync_service_apply_timezone();

    esp_err_t err = rtc_rx8130_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC not available: %s", esp_err_to_name(err));
        return err;
    }

    (void)rtc_rx8130_init_battery_backup();
    (void)rtc_rx8130_clear_irq_flags();
    (void)rtc_rx8130_disable_irq();

    err = set_system_time_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "system time not restored from RTC: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t ensure_sntp_started(void)
{
    if (s_sntp_initialized) {
        return esp_netif_sntp_start();
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        4,
        ESP_SNTP_SERVER_LIST("pool.ntp.org",
                             "time.google.com",
                             "time.cloudflare.com",
                             "ntp.aliyun.com"));
    config.wait_for_sync = true;
    config.start = true;

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        s_sntp_initialized = true;
    }
    return err;
}

static void ntp_sync_task(void *arg)
{
    (void)arg;

    if (wifi_manager_get_state() != WIFI_STATE_CONNECTED || !wifi_manager_has_ipv4()) {
        ESP_LOGI(TAG, "wifi not ready for NTP sync");
        goto out;
    }

    if (time_sync_service_system_time_is_valid()) {
        ESP_LOGI(TAG, "system time is valid; run one NTP refresh for RTC calibration");
    }

    esp_err_t err = ensure_sntp_started();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
        goto out;
    }

    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(TIME_SYNC_WAIT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync wait failed: %s", esp_err_to_name(err));
        goto out;
    }

    if (!time_sync_service_system_time_is_valid()) {
        ESP_LOGW(TAG, "SNTP returned but system time is still invalid");
        goto out;
    }

    s_sync_completed_once = true;
    (void)write_system_time_to_rtc();

out:
    s_sync_task = NULL;
    vTaskDelete(NULL);
}

void time_sync_service_on_wifi_connected(void)
{
    if (s_sync_completed_once || s_sync_task) {
        return;
    }

    if (xTaskCreate(ntp_sync_task,
                    "ntp_sync",
                    TIME_SYNC_TASK_STACK,
                    NULL,
                    TIME_SYNC_TASK_PRIORITY,
                    &s_sync_task) != pdPASS) {
        s_sync_task = NULL;
        ESP_LOGW(TAG, "create ntp sync task failed");
    }
}
