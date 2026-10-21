static const char *disconnect_reason_name(uint8_t reason)
{
    switch ((wifi_err_reason_t)reason) {
        case WIFI_REASON_AUTH_EXPIRE:
            return "AUTH_EXPIRE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "BEACON_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND:
            return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL:
            return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL:
            return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL:
            return "CONNECTION_FAIL";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "NO_AP_FOUND_COMPATIBLE_SECURITY";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "NO_AP_FOUND_AUTHMODE_THRESHOLD";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "NO_AP_FOUND_RSSI_THRESHOLD";
        default:
            return "UNKNOWN";
    }
}

static const char *auth_mode_name(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        case WIFI_AUTH_OWE:
            return "OWE";
        case WIFI_AUTH_WPA3_ENT_192:
            return "WPA3_ENT_192";
        case WIFI_AUTH_WPA3_EXT_PSK:
            return "WPA3_EXT_PSK";
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
            return "WPA3_EXT_PSK_MIXED";
        case WIFI_AUTH_DPP:
            return "DPP";
        case WIFI_AUTH_WPA3_ENTERPRISE:
            return "WPA3_ENTERPRISE";
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
            return "WPA2_WPA3_ENTERPRISE";
        default:
            return "UNKNOWN";
    }
}

static const char *cipher_name(wifi_cipher_type_t cipher)
{
    switch (cipher) {
        case WIFI_CIPHER_TYPE_NONE:
            return "NONE";
        case WIFI_CIPHER_TYPE_WEP40:
            return "WEP40";
        case WIFI_CIPHER_TYPE_WEP104:
            return "WEP104";
        case WIFI_CIPHER_TYPE_TKIP:
            return "TKIP";
        case WIFI_CIPHER_TYPE_CCMP:
            return "CCMP";
        case WIFI_CIPHER_TYPE_TKIP_CCMP:
            return "TKIP_CCMP";
        case WIFI_CIPHER_TYPE_AES_CMAC128:
            return "AES_CMAC128";
        case WIFI_CIPHER_TYPE_SMS4:
            return "SMS4";
        case WIFI_CIPHER_TYPE_GCMP:
            return "GCMP";
        case WIFI_CIPHER_TYPE_GCMP256:
            return "GCMP256";
        case WIFI_CIPHER_TYPE_AES_GMAC128:
            return "AES_GMAC128";
        case WIFI_CIPHER_TYPE_AES_GMAC256:
            return "AES_GMAC256";
        case WIFI_CIPHER_TYPE_UNKNOWN:
            return "UNKNOWN";
        default:
            return "INVALID";
    }
}

static const char *bandwidth_name(wifi_bandwidth_t bandwidth)
{
    switch (bandwidth) {
        case WIFI_BW_HT20:
            return "HT20";
        case WIFI_BW_HT40:
            return "HT40";
        default:
            return "UNKNOWN";
    }
}

static const char *second_chan_name(wifi_second_chan_t second)
{
    switch (second) {
        case WIFI_SECOND_CHAN_NONE:
            return "NONE";
        case WIFI_SECOND_CHAN_ABOVE:
            return "ABOVE";
        case WIFI_SECOND_CHAN_BELOW:
            return "BELOW";
        default:
            return "UNKNOWN";
    }
}

static bool ap_record_ssid_matches(const wifi_ap_record_t *record, const char *ssid)
{
    return record && ssid && ssid[0] &&
           strncmp((const char *)record->ssid, ssid, sizeof(record->ssid)) == 0;
}

static void log_ap_record_detail(const char *prefix, const wifi_ap_record_t *record)
{
    if (!record || record->ssid[0] == '\0') {
        return;
    }

    ESP_LOGI(TAG,
             "%s ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%u second=%s rssi=%d "
             "auth=%s pairwise=%s group=%s phy=b%d/g%d/n%d/ax%d wps=%d country=%c%c/%u-%u bw=%s",
             prefix ? prefix : "AP",
             (const char *)record->ssid,
             record->bssid[0],
             record->bssid[1],
             record->bssid[2],
             record->bssid[3],
             record->bssid[4],
             record->bssid[5],
             record->primary,
             second_chan_name(record->second),
             record->rssi,
             auth_mode_name(record->authmode),
             cipher_name(record->pairwise_cipher),
             cipher_name(record->group_cipher),
             record->phy_11b,
             record->phy_11g,
             record->phy_11n,
             record->phy_11ax,
             record->wps,
             record->country.cc[0] ? record->country.cc[0] : '-',
             record->country.cc[1] ? record->country.cc[1] : '-',
             record->country.schan,
             record->country.nchan,
             bandwidth_name(record->bandwidth));
}

static void log_scan_records(const char *prefix,
                             const wifi_ap_record_t *records,
                             uint16_t count)
{
    for (uint16_t i = 0; records && i < count; ++i) {
        log_ap_record_detail(prefix, &records[i]);
    }
}

static bool select_best_ap_record(const char *ssid,
                                  const wifi_ap_record_t *records,
                                  uint16_t count,
                                  wifi_ap_record_t *out_record)
{
    bool found = false;
    wifi_ap_record_t best = {0};

    for (uint16_t i = 0; records && i < count; ++i) {
        if (!ap_record_ssid_matches(&records[i], ssid)) {
            continue;
        }

        if (!found || records[i].rssi > best.rssi) {
            best = records[i];
            found = true;
        }
    }

    if (found && out_record) {
        *out_record = best;
    }
    return found;
}

static bool select_cached_ap_record(const char *ssid, wifi_ap_record_t *out_record)
{
    wifi_ap_record_t best = {0};
    wifi_ap_record_t candidate = {0};
    bool found = false;

    if (select_best_ap_record(ssid, s_scan_records, s_scan_record_count, &candidate)) {
        best = candidate;
        found = true;
    }
    if (select_best_ap_record(ssid, s_auto_scan_records, s_auto_scan_record_count, &candidate) &&
        (!found || candidate.rssi > best.rssi)) {
        best = candidate;
        found = true;
    }

    if (found && out_record) {
        *out_record = best;
    }
    return found;
}

static bool country_code_is_valid(const wifi_country_t *country)
{
    return country && country->cc[0] >= 'A' && country->cc[0] <= 'Z' &&
           country->cc[1] >= 'A' && country->cc[1] <= 'Z' &&
           country->schan >= 1 && country->nchan > 0;
}

static void apply_ap_country_if_needed(const wifi_ap_record_t *record)
{
    if (!record || record->primary <= 11) {
        return;
    }

    if (!country_code_is_valid(&record->country)) {
        ESP_LOGW(TAG,
                 "Target AP channel %u is outside world-safe 1-11, but scan has no valid country IE",
                 record->primary);
        return;
    }

    esp_err_t err = esp_wifi_set_country(&record->country);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Applied AP country %c%c for channel %u",
                 record->country.cc[0],
                 record->country.cc[1],
                 record->primary);
    } else {
        ESP_LOGW(TAG, "esp_wifi_set_country(%c%c) failed: %s",
                 record->country.cc[0],
                 record->country.cc[1],
                 esp_err_to_name(err));
    }
}

static void configure_c6_sdio_drive_capability(void)
{
    static const gpio_num_t c6_sdio_gpios[] = {
        GPIO_NUM_8,
        GPIO_NUM_9,
        GPIO_NUM_10,
        GPIO_NUM_11,
        GPIO_NUM_12,
        GPIO_NUM_13,
        GPIO_NUM_15,
    };

    for (size_t i = 0; i < sizeof(c6_sdio_gpios) / sizeof(c6_sdio_gpios[0]); ++i) {
        esp_err_t err = gpio_set_drive_capability(c6_sdio_gpios[i], GPIO_DRIVE_CAP_0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio_set_drive_capability(%d) failed: %s",
                     (int)c6_sdio_gpios[i],
                     esp_err_to_name(err));
        }
    }
}

static void configure_started_wifi_compat(void)
{
    esp_err_t err;

    if (s_wifi_compat_configured) {
        return;
    }

    err = esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_band_mode(2G) failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_protocol(WIFI_IF_STA,
                                WIFI_PROTOCOL_11B |
                                WIFI_PROTOCOL_11G |
                                WIFI_PROTOCOL_11N |
                                WIFI_PROTOCOL_11AX);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_protocol(11b/g/n/ax) failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_bandwidth(HT20) failed: %s", esp_err_to_name(err));
    }

    s_wifi_compat_configured = true;
}
