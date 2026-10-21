static const char *webfm_chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    case CHIP_ESP32P4:
        return "ESP32-P4";
    default:
        return "ESP32";
    }
}

static const char *webfm_wifi_state_text(wifi_state_t state)
{
    switch (state) {
    case WIFI_STATE_IDLE:
        return TRS("空闲");
    case WIFI_STATE_CONNECTING:
        return TRS("连接中");
    case WIFI_STATE_CONNECTED:
        return TRS("已连接");
    case WIFI_STATE_DISCONNECTED:
        return TRS("已断开");
    case WIFI_STATE_FAILED:
        return TRS("连接失败");
    default:
        return TRS("未知");
    }
}

static void webfm_format_uptime(char *out, size_t out_len)
{
    uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
    snprintf(out, out_len, "%uh %02um %02us", (unsigned)(sec / 3600),
             (unsigned)((sec % 3600) / 60), (unsigned)(sec % 60));
}
