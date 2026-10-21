app_timezone_t app_settings_get_timezone(void)
{
    return s_timezone;
}

static esp_err_t save_timezone_and_utc_offset(app_timezone_t timezone, int offset_minutes)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for timezone save failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(h, SETTINGS_NVS_TIMEZONE_KEY, (uint8_t)timezone);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, SETTINGS_NVS_UTC_OFFSET_KEY, offset_minutes);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save timezone failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_settings_set_timezone(app_timezone_t timezone)
{
    if (timezone < 0 || timezone >= APP_SETTINGS_TIMEZONE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_timezone = timezone;
    s_timezone_configured = true;
    return save_u8_setting(SETTINGS_NVS_TIMEZONE_KEY, (uint8_t)s_timezone, "timezone");
}

int app_settings_get_utc_offset_minutes(void)
{
    return s_utc_offset_minutes;
}

esp_err_t app_settings_set_utc_offset_minutes(int offset_minutes)
{
    if (!utc_offset_minutes_is_valid(offset_minutes)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_timezone = APP_SETTINGS_TIMEZONE_UTC;
    s_utc_offset_minutes = offset_minutes;
    s_timezone_configured = true;
    return save_timezone_and_utc_offset(s_timezone, s_utc_offset_minutes);
}

void app_settings_format_utc_offset(int offset_minutes, char *out, size_t out_len)
{
    int clamped = clamp_utc_offset_minutes(offset_minutes);
    int abs_minutes = clamped < 0 ? -clamped : clamped;
    int hours = abs_minutes / 60;
    int minutes = abs_minutes % 60;

    if (!out || out_len == 0) {
        return;
    }
    if (minutes == 0) {
        snprintf(out, out_len, "UTC%c%d", clamped >= 0 ? '+' : '-', hours);
    } else {
        snprintf(out, out_len, "UTC%c%d:%02d", clamped >= 0 ? '+' : '-', hours, minutes);
    }
}

static const char *format_custom_utc_tz(void)
{
    static char tz[24];
    int offset = clamp_utc_offset_minutes(s_utc_offset_minutes);
    int abs_minutes = offset < 0 ? -offset : offset;
    int hours = abs_minutes / 60;
    int minutes = abs_minutes % 60;
    char posix_sign = offset > 0 ? '-' : '+';

    if (offset == 0) {
        return "UTC0";
    }
    if (minutes == 0) {
        snprintf(tz, sizeof(tz), "UTC%c%d", posix_sign, hours);
    } else {
        snprintf(tz, sizeof(tz), "UTC%c%d:%02d", posix_sign, hours, minutes);
    }
    return tz;
}

const char *app_settings_get_timezone_tz(app_timezone_t timezone)
{
    switch (timezone) {
    case APP_SETTINGS_TIMEZONE_NEW_YORK:
        return "EST5EDT,M3.2.0/2,M11.1.0/2";
    case APP_SETTINGS_TIMEZONE_UTC:
        return format_custom_utc_tz();
    case APP_SETTINGS_TIMEZONE_LONDON:
        return "GMT0BST,M3.5.0/1,M10.5.0/2";
    case APP_SETTINGS_TIMEZONE_TOKYO:
        return "JST-9";
    case APP_SETTINGS_TIMEZONE_CENTRAL_EUROPE:
        return "CET-1CEST,M3.5.0/2,M10.5.0/3";
    case APP_SETTINGS_TIMEZONE_BEIJING:
    default:
        return "CST-8";
    }
}

const char *app_settings_get_timezone_source_label(app_timezone_t timezone)
{
    static char utc_label[24];

    switch (timezone) {
    case APP_SETTINGS_TIMEZONE_NEW_YORK:
        return "纽约时间";
    case APP_SETTINGS_TIMEZONE_UTC:
        app_settings_format_utc_offset(s_utc_offset_minutes, utc_label, sizeof(utc_label));
        return utc_label;
    case APP_SETTINGS_TIMEZONE_LONDON:
        return "伦敦时间";
    case APP_SETTINGS_TIMEZONE_TOKYO:
        return "东京时间";
    case APP_SETTINGS_TIMEZONE_CENTRAL_EUROPE:
        return "中欧时间";
    case APP_SETTINGS_TIMEZONE_BEIJING:
    default:
        return "北京时间";
    }
}

const char *app_settings_get_current_timezone_tz(void)
{
    return app_settings_get_timezone_tz(s_timezone);
}

const char *app_settings_get_current_timezone_source_label(void)
{
    return app_settings_get_timezone_source_label(s_timezone);
}
