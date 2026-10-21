static bool load_ready_package_from_tf(char *reason, size_t reason_len, bool update_status)
{
    uint64_t firmware_size = 0;
    char *request_text = NULL;
    cJSON *root = NULL;
    char project[64] = {0};
    char version[64] = {0};
    char build_time[64] = {0};
    char sha256[65] = {0};
    uint64_t request_size = 0;

    if (!tf_card_is_mounted()) {
        copy_text(reason, reason_len, TRS("未检测到 TF 卡"));
        return false;
    }
    if (!file_exists_with_size(OTA_UPDATE_REQUEST_PATH, NULL) ||
        !file_exists_with_size(OTA_UPDATE_FIRMWARE_PATH, &firmware_size) ||
        firmware_size == 0) {
        copy_text(reason, reason_len, TRS("没有待安装升级包"));
        return false;
    }

    request_text = read_small_file(OTA_UPDATE_REQUEST_PATH, 4096, reason, reason_len);
    if (!request_text) {
        return false;
    }
    root = cJSON_Parse(request_text);
    free(request_text);
    if (!root) {
        copy_text(reason, reason_len, TRS("升级请求 JSON 无法解析"));
        return false;
    }

    bool ok = json_get_string(root, "project", project, sizeof(project)) &&
              json_get_string(root, "version", version, sizeof(version)) &&
              json_get_string(root, "sha256", sha256, sizeof(sha256));
    json_get_string(root, "build_time", build_time, sizeof(build_time));
    request_size = json_get_u64(root, "firmware_size");
    cJSON_Delete(root);

    if (!ok) {
        copy_text(reason, reason_len, TRS("升级请求缺少必要字段"));
        return false;
    }
    if (strcmp(project, OTA_PROJECT_NAME) != 0) {
        copy_text(reason, reason_len, TRS("待安装固件项目名不匹配"));
        return false;
    }
    if (strlen(sha256) != 64) {
        copy_text(reason, reason_len, TRS("待安装固件 SHA256 长度不正确"));
        return false;
    }
    if (request_size > 0 && request_size != firmware_size) {
        snprintf(reason,
                 reason_len,
                 TRS("待安装固件大小不匹配：%llu/%llu"),
                 (unsigned long long)firmware_size,
                 (unsigned long long)request_size);
        return false;
    }

    char current[64];
    get_current_version(current, sizeof(current));
    bool strict_time_version = false;
    int version_cmp = compare_latest_to_current_version(version, current, &strict_time_version);
    ESP_LOGI(TAG,
             "ready package version compare: current=%s package=%s cmp=%d strict_time=%d",
             current,
             version,
             version_cmp,
             strict_time_version ? 1 : 0);
    if (version_cmp < 0 && strict_time_version) {
        copy_text(reason, reason_len, TRS("TF 卡中的升级包版本较旧，已忽略。"));
        return false;
    }
    if (version_cmp == 0) {
        copy_text(reason, reason_len, TRS("TF 卡中的升级包与当前版本相同，已忽略。"));
        return false;
    }

    if (update_status) {
        ota_lock();
        copy_text(s_status.current_version, sizeof(s_status.current_version), current);
        copy_text(s_status.latest_version, sizeof(s_status.latest_version), version);
        copy_text(s_status.latest_build_time, sizeof(s_status.latest_build_time), build_time);
        s_status.firmware_url[0] = '\0';
        copy_text(s_status.changelog,
                  sizeof(s_status.changelog),
                  TRS("已在 TF 卡检测到下载完成的升级包，可直接安装。建议安装前保持稳定供电。"));
        s_status.error[0] = '\0';
        s_status.update_available = false;
        s_status.ready_to_reboot = true;
        s_status.progress = 100;
        s_status.downloaded = firmware_size;
        s_status.total = request_size > 0 ? request_size : firmware_size;
        s_status.speed_bps = 0;
        copy_text(s_expected_sha256, sizeof(s_expected_sha256), sha256);
        copy_text(s_manifest_version, sizeof(s_manifest_version), version);
        copy_text(s_manifest_build_time, sizeof(s_manifest_build_time), build_time);
        s_manifest_size = request_size > 0 ? request_size : firmware_size;
        set_state_locked(OTA_UPDATE_STATE_READY_TO_REBOOT, TRS("检测到 TF 卡中已有下载完成的固件，等待确认安装。"));
        ota_unlock();
    }
    copy_text(reason, reason_len, TRS("已检测到待安装升级包"));
    ESP_LOGI(TAG, "ready package found: version=%s size=%llu",
             version,
             (unsigned long long)firmware_size);
    return true;
}

static bool verify_firmware_file_sha256(const char *path,
                                        const char *expected_sha,
                                        uint64_t expected_size,
                                        char *reason,
                                        size_t reason_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        copy_text(reason, reason_len, TRS("无法打开待安装固件"));
        return false;
    }

    unsigned char *buffer = heap_caps_malloc(OTA_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    if (!buffer) {
        fclose(fp);
        copy_text(reason, reason_len, TRS("校验固件时内存不足"));
        return false;
    }

    mbedtls_sha256_context sha_ctx;
    unsigned char digest[32];
    char actual_sha[65];
    uint64_t total = 0;
    bool ok = true;

    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    while (true) {
        size_t got = fread(buffer, 1, OTA_DOWNLOAD_BUFFER_SIZE, fp);
        if (got > 0) {
            mbedtls_sha256_update(&sha_ctx, buffer, got);
            total += got;
        }
        if (got < OTA_DOWNLOAD_BUFFER_SIZE) {
            if (ferror(fp)) {
                copy_text(reason, reason_len, TRS("读取待安装固件失败"));
                ok = false;
            }
            break;
        }
    }
    if (ok) {
        mbedtls_sha256_finish(&sha_ctx, digest);
        bytes_to_hex(digest, sizeof(digest), actual_sha, sizeof(actual_sha));
        if (expected_size > 0 && total != expected_size) {
            snprintf(reason,
                     reason_len,
                     TRS("待安装固件大小不匹配：%llu/%llu"),
                     (unsigned long long)total,
                     (unsigned long long)expected_size);
            ok = false;
        } else if (!expected_sha || strlen(expected_sha) != 64 || strcmp(actual_sha, expected_sha) != 0) {
            copy_text(reason, reason_len, TRS("待安装固件 SHA256 校验失败"));
            ok = false;
        }
    }
    mbedtls_sha256_free(&sha_ctx);
    free(buffer);
    fclose(fp);
    if (ok) {
        ESP_LOGI(TAG, "ready firmware SHA256 verified: size=%llu sha=%s",
                 (unsigned long long)total,
                 actual_sha);
        reason[0] = '\0';
    }
    return ok;
}

static esp_err_t http_get_text(const char *url, char *buf, size_t buf_len, int *out_status)
{
    esp_http_client_config_t config = {
        .url = url,
        .user_agent = "M5Tab5-OTA/1.0",
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        log_ota_heap("manifest connection failed");
        esp_http_client_cleanup(client);
        return err;
    }
    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int total = 0;
    while (total < (int)buf_len - 1) {
        int got = esp_http_client_read(client, buf + total, (int)buf_len - 1 - total);
        if (got < 0) {
            err = ESP_FAIL;
            break;
        }
        if (got == 0) {
            break;
        }
        total += got;
    }
    buf[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (out_status) {
        *out_status = status;
    }
    if (err != ESP_OK) {
        return err;
    }
    return (status >= 200 && status < 300 && total > 0) ? ESP_OK : ESP_FAIL;
}

static bool parse_manifest(const char *json_text, char *error, size_t error_len)
{
    char project[64] = {0};
    char version[64] = {0};
    char build_time[64] = {0};
    char firmware_url[192] = {0};
    char download_url[192] = {0};
    char sha256[65] = {0};
    char changelog[4096] = {0};
    char updater_version[64] = {0};
    char updater_binary_version[64] = {0};
    char updater_build_time[64] = {0};
    char updater_url[192] = {0};
    char updater_download_url[192] = {0};
    char updater_sha256[65] = {0};
    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        copy_text(error, error_len, TRS("版本信息 JSON 无法解析"));
        return false;
    }

    bool ok = json_get_string(root, "project", project, sizeof(project)) &&
              json_get_string(root, "version", version, sizeof(version)) &&
              json_get_string(root, "firmware_url", firmware_url, sizeof(firmware_url)) &&
              json_get_string(root, "sha256", sha256, sizeof(sha256));
    json_get_string(root, "build_time", build_time, sizeof(build_time));
    json_get_string(root, "changelog", changelog, sizeof(changelog));
    uint64_t size = json_get_u64(root, "size");
    bool updater_ok = json_get_child_string(root, "updater", "version", updater_version, sizeof(updater_version)) &&
                      json_get_child_string(root, "updater", "firmware_url", updater_url, sizeof(updater_url)) &&
                      json_get_child_string(root, "updater", "sha256", updater_sha256, sizeof(updater_sha256));
    json_get_child_string(root, "updater", "binary_project_version", updater_binary_version, sizeof(updater_binary_version));
    json_get_child_string(root, "updater", "build_time", updater_build_time, sizeof(updater_build_time));
    uint64_t updater_size = json_get_child_u64(root, "updater", "size");
    bool ota_blocked = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ota_blocked")) ||
                       cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "manual_reinstall_required"));
    cJSON_Delete(root);

    if (!ok || size == 0) {
        copy_text(error, error_len, TRS("版本信息缺少必要字段"));
        return false;
    }
    if (strcmp(project, OTA_PROJECT_NAME) != 0) {
        copy_text(error, error_len, TRS("服务器固件项目名不匹配"));
        return false;
    }
    if (strlen(sha256) != 64) {
        copy_text(error, error_len, TRS("服务器 SHA256 长度不正确"));
        return false;
    }
    if (updater_ok && (strlen(updater_sha256) != 64 || updater_size == 0)) {
        copy_text(error, error_len, TRS("服务器 UPLOAD 固件信息不完整"));
        return false;
    }
    if (ota_blocked) {
        ota_lock();
        copy_text(s_status.latest_version, sizeof(s_status.latest_version), version);
        copy_text(s_status.latest_build_time, sizeof(s_status.latest_build_time), build_time);
        copy_text(s_status.changelog,
                  sizeof(s_status.changelog),
                  changelog[0] ? changelog : TRS("服务器未提供更新日志。"));
        s_status.firmware_url[0] = '\0';
        s_status.update_available = false;
        s_status.updater_update_available = false;
        s_status.ready_to_reboot = false;
        s_status.total = 0;
        s_expected_sha256[0] = '\0';
        s_manifest_version[0] = '\0';
        s_manifest_build_time[0] = '\0';
        s_manifest_size = 0;
        s_manifest_ota_blocked = true;
        ota_unlock();
        return true;
    }
    make_plain_http_download_url(firmware_url, download_url, sizeof(download_url));
    ESP_LOGI(TAG, "firmware download url: %s", download_url);
    if (updater_ok) {
        make_plain_http_download_url(updater_url, updater_download_url, sizeof(updater_download_url));
        ESP_LOGI(TAG, "updater download url: %s", updater_download_url);
    }

    ota_lock();
    copy_text(s_status.latest_version, sizeof(s_status.latest_version), version);
    copy_text(s_status.latest_build_time, sizeof(s_status.latest_build_time), build_time);
    copy_text(s_status.firmware_url, sizeof(s_status.firmware_url), download_url);
    copy_text(s_status.changelog, sizeof(s_status.changelog), changelog[0] ? changelog : TRS("服务器未提供更新日志。"));
    s_status.total = size;
    copy_text(s_expected_sha256, sizeof(s_expected_sha256), sha256);
    copy_text(s_manifest_version, sizeof(s_manifest_version), version);
    copy_text(s_manifest_build_time, sizeof(s_manifest_build_time), build_time);
    s_manifest_size = size;
    s_manifest_ota_blocked = false;
    if (updater_ok) {
        const char *effective_updater_version = updater_binary_version[0] ? updater_binary_version : updater_version;
        copy_text(s_status.updater_latest_version, sizeof(s_status.updater_latest_version), effective_updater_version);
        copy_text(s_status.updater_build_time, sizeof(s_status.updater_build_time), updater_build_time);
        copy_text(s_status.updater_url, sizeof(s_status.updater_url), updater_download_url);
        copy_text(s_updater_expected_sha256, sizeof(s_updater_expected_sha256), updater_sha256);
        copy_text(s_updater_manifest_version, sizeof(s_updater_manifest_version), effective_updater_version);
        copy_text(s_updater_manifest_build_time, sizeof(s_updater_manifest_build_time), updater_build_time);
        s_updater_manifest_size = updater_size;
    } else {
        s_status.updater_latest_version[0] = '\0';
        s_status.updater_build_time[0] = '\0';
        s_status.updater_url[0] = '\0';
        s_updater_expected_sha256[0] = '\0';
        s_updater_manifest_version[0] = '\0';
        s_updater_manifest_build_time[0] = '\0';
        s_updater_manifest_size = 0;
        s_status.updater_update_available = false;
    }
    ota_unlock();
    return true;
}

static bool http_error_can_retry(esp_err_t err, int status)
{
    return err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_HTTP_FETCH_HEADER ||
           err == ESP_ERR_HTTP_INVALID_TRANSPORT || err == ESP_ERR_TIMEOUT ||
           (err == ESP_FAIL && status == 0);
}

static esp_err_t http_download_to_memory(const char *url,
                                         unsigned char **out_data,
                                         uint64_t expected_size,
                                         const char *expected_sha,
                                         char *reason,
                                         size_t reason_len)
{
    if (!url || !url[0] || !out_data || expected_size == 0 || expected_size > 2 * 1024 * 1024) {
        copy_text(reason, reason_len, TRS("UPLOAD 固件下载参数无效"));
        return ESP_ERR_INVALID_ARG;
    }

    unsigned char *data = heap_caps_malloc((size_t)expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        data = heap_caps_malloc((size_t)expected_size, MALLOC_CAP_DEFAULT);
    }
    if (!data) {
        copy_text(reason, reason_len, TRS("UPLOAD 固件下载内存不足"));
        return ESP_ERR_NO_MEM;
    }

    bool use_tls = url_uses_https(url);
    esp_http_client_config_t config = {
        .url = url,
        .user_agent = "M5Tab5-OTA/1.0",
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = OTA_DOWNLOAD_BUFFER_SIZE,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = use_tls ? esp_crt_bundle_attach : NULL,
        .disable_auto_redirect = false,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(data);
        copy_text(reason, reason_len, TRS("创建 UPLOAD HTTP 客户端失败"));
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        log_ota_heap("updater download connection failed");
        esp_http_client_cleanup(client);
        free(data);
        snprintf(reason, reason_len, TRS("打开 UPLOAD 下载连接失败：%s"), esp_err_to_name(err));
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(data);
        snprintf(reason, reason_len, TRS("UPLOAD 下载失败：HTTP %d"), status);
        return ESP_FAIL;
    }
    if (content_length > 0 && (uint64_t)content_length != expected_size) {
        ESP_LOGW(TAG, "updater manifest size=%llu, http content-length=%lld",
                 (unsigned long long)expected_size,
                 (long long)content_length);
    }

    uint64_t downloaded = 0;
    int64_t last_data_us = esp_timer_get_time();
    while (downloaded < expected_size) {
        int got = esp_http_client_read(client,
                                       (char *)data + downloaded,
                                       (int)((expected_size - downloaded) > OTA_DOWNLOAD_BUFFER_SIZE
                                                 ? OTA_DOWNLOAD_BUFFER_SIZE
                                                 : (expected_size - downloaded)));
        int64_t now_us = esp_timer_get_time();
        if (got < 0) {
            int saved_errno = errno;
            bool retryable = saved_errno == EAGAIN || saved_errno == EWOULDBLOCK ||
                             saved_errno == ETIMEDOUT;
            if (retryable &&
                now_us - last_data_us < OTA_DOWNLOAD_STALL_TIMEOUT_MS * 1000LL) {
                vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_IDLE_DELAY_MS));
                continue;
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(data);
            snprintf(reason, reason_len, TRS("UPLOAD 下载读取失败：errno=%d"), saved_errno);
            return ESP_FAIL;
        }
        if (got == 0) {
            if (now_us - last_data_us < OTA_DOWNLOAD_STALL_TIMEOUT_MS * 1000LL) {
                vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_IDLE_DELAY_MS));
                continue;
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(data);
            copy_text(reason, reason_len, TRS("UPLOAD 下载连接长时间无数据"));
            return ESP_ERR_TIMEOUT;
        }
        downloaded += (uint64_t)got;
        last_data_us = now_us;
        vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_YIELD_MS));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    unsigned char digest[32];
    char actual_sha[65];
    mbedtls_sha256(data, (size_t)expected_size, digest, 0);
    bytes_to_hex(digest, sizeof(digest), actual_sha, sizeof(actual_sha));
    if (!expected_sha || strlen(expected_sha) != 64 || strcmp(actual_sha, expected_sha) != 0) {
        free(data);
        copy_text(reason, reason_len, TRS("UPLOAD 固件 SHA256 校验失败"));
        return ESP_ERR_INVALID_CRC;
    }

    *out_data = data;
    reason[0] = '\0';
    return ESP_OK;
}

static void ota_updater_install_task(void *arg)
{
    (void)arg;
    char reason[160];
    char url[192];
    char expected_sha[65];
    char expected_version[64];
    uint64_t expected_size;
    unsigned char *data = NULL;
    esp_app_desc_t desc = {0};
    const esp_partition_t *factory = NULL;
    esp_err_t err;

    ota_lock();
    s_updater_task_running = true;
    s_status.updater_busy = true;
    s_status.error[0] = '\0';
    copy_text(url, sizeof(url), s_status.updater_url);
    copy_text(expected_sha, sizeof(expected_sha), s_updater_expected_sha256);
    copy_text(expected_version, sizeof(expected_version), s_updater_manifest_version);
    expected_size = s_updater_manifest_size;
    s_status.progress = 0;
    s_status.downloaded = 0;
    s_status.total = expected_size;
    s_status.speed_bps = 0;
    set_state_locked(OTA_UPDATE_STATE_UPDATER_INSTALLING, TRS("正在更新 UPLOAD 固件..."));
    ota_unlock();

    if (url[0] == '\0' || expected_sha[0] == '\0' || expected_size == 0 || expected_version[0] == '\0') {
        set_error(TRS("没有可用的 UPLOAD 固件信息，请先检测更新"));
        goto done;
    }

    factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    if (!factory) {
        set_error(TRS("未找到 factory updater 分区"));
        goto done;
    }
    if (expected_size > factory->size) {
        snprintf(reason,
                 sizeof(reason),
                 TRS("UPLOAD 固件过大：%llu > %lu"),
                 (unsigned long long)expected_size,
                 (unsigned long)factory->size);
        set_error(reason);
        goto done;
    }

    ESP_LOGI(TAG,
             "updater install start: url=%s size=%llu version=%s partition=%s offset=0x%lx size=0x%lx",
             url,
             (unsigned long long)expected_size,
             expected_version,
             factory->label,
             (unsigned long)factory->address,
             (unsigned long)factory->size);

    err = http_download_to_memory(url, &data, expected_size, expected_sha, reason, sizeof(reason));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "updater download failed: %s", reason);
        set_error(reason);
        goto done;
    }
    ESP_LOGI(TAG, "updater download verified, erase/write factory partition");

    size_t erase_size = (size_t)((expected_size + OTA_FLASH_SECTOR_SIZE - 1) &
                                 ~(uint64_t)(OTA_FLASH_SECTOR_SIZE - 1));
    err = esp_partition_erase_range(factory, 0, erase_size);
    if (err != ESP_OK) {
        snprintf(reason, sizeof(reason), TRS("擦除 UPLOAD 分区失败：%s"), esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", reason);
        set_error(reason);
        goto done;
    }
    err = esp_partition_write(factory, 0, data, (size_t)expected_size);
    if (err != ESP_OK) {
        snprintf(reason, sizeof(reason), TRS("写入 UPLOAD 分区失败：%s"), esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", reason);
        set_error(reason);
        goto done;
    }
    ESP_LOGI(TAG, "updater partition write complete");

    err = esp_ota_get_partition_description(factory, &desc);
    if (err != ESP_OK) {
        snprintf(reason, sizeof(reason), TRS("读取 UPLOAD 固件版本失败：%s"), esp_err_to_name(err));
        set_error(reason);
        goto done;
    }
    if (expected_version[0] && strcmp(desc.version, expected_version) != 0) {
        ESP_LOGW(TAG,
                 "updater binary version differs from manifest version: binary=%s manifest=%s; keep installed image",
                 desc.version,
                 expected_version);
    }

    ota_lock();
    copy_text(s_status.updater_current_version,
              sizeof(s_status.updater_current_version),
              desc.version);
    s_status.updater_update_available = false;
    s_status.updater_busy = false;
    ota_update_state_t next_state = s_status.update_available ? OTA_UPDATE_STATE_UPDATE_AVAILABLE
                                                              : OTA_UPDATE_STATE_UP_TO_DATE;
    set_state_locked(next_state,
                     s_status.update_available ? TRS("UPLOAD 固件已更新，主程序仍可更新。")
                                               : TRS("UPLOAD 固件已更新。"));
    ota_unlock();
    ESP_LOGI(TAG, "updater install complete: version=%s", desc.version);

done:
    if (data) {
        free(data);
    }
    ota_lock();
    s_updater_task_running = false;
    s_status.updater_busy = false;
    s_status.busy = s_check_task_running || s_download_task_running;
    ota_unlock();
    ota_delete_current_task();
}

static void ota_check_task(void *arg)
{
    (void)arg;
    char reason[128];
    char current[64];
    char updater_current[64];
    char manifest_url[320];
    char *manifest = NULL;
    int status = 0;

    ota_lock();
    s_check_task_running = true;
    s_status.error[0] = '\0';
    s_status.progress = 0;
    s_status.downloaded = 0;
    s_status.speed_bps = 0;
    s_status.update_available = false;
    s_status.updater_update_available = false;
    s_manifest_ota_blocked = false;
    set_state_locked(OTA_UPDATE_STATE_CHECKING, TRS("正在检测最新版本..."));
    ota_unlock();

    if (!environment_ready(reason, sizeof(reason), false)) {
        set_error(reason);
        goto done;
    }

    build_manifest_url(manifest_url, sizeof(manifest_url));
    manifest = heap_caps_malloc(OTA_MANIFEST_MAX_BYTES, MALLOC_CAP_DEFAULT);
    if (!manifest) {
        set_error(TRS("版本检测缓冲区内存不足"));
        goto done;
    }
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= OTA_HTTP_MAX_ATTEMPTS; ++attempt) {
        status = 0;
        err = http_get_text(manifest_url, manifest, OTA_MANIFEST_MAX_BYTES, &status);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG,
                 "manifest request failed (%d/%d): HTTP %d %s",
                 attempt,
                 OTA_HTTP_MAX_ATTEMPTS,
                 status,
                 esp_err_to_name(err));
        if (attempt < OTA_HTTP_MAX_ATTEMPTS && http_error_can_retry(err, status)) {
            vTaskDelay(pdMS_TO_TICKS(OTA_HTTP_RETRY_DELAY_MS * attempt));
            continue;
        }
        break;
    }
    if (err != ESP_OK) {
        snprintf(reason, sizeof(reason), TRS("检测失败：HTTP %d %s，请稍后重试"), status, esp_err_to_name(err));
        set_error(reason);
        goto done;
    }
    if (!parse_manifest(manifest, reason, sizeof(reason))) {
        set_error(reason);
        goto done;
    }

    get_current_version(current, sizeof(current));
    get_factory_updater_version(updater_current, sizeof(updater_current));
    ota_lock();
    copy_text(s_status.current_version, sizeof(s_status.current_version), current);
    copy_text(s_status.updater_current_version, sizeof(s_status.updater_current_version), updater_current);
    if (s_status.launcher_mode) {
        s_status.update_available = false;
        s_status.updater_update_available = false;
        s_status.ready_to_reboot = false;
        s_status.progress = 0;
        s_status.downloaded = 0;
        s_status.total = 0;
        s_status.speed_bps = 0;
        set_state_locked(OTA_UPDATE_STATE_UP_TO_DATE, TRS("无OTA分区"));
        ota_unlock();
        goto done;
    }
    if (s_manifest_ota_blocked) {
        s_status.update_available = false;
        s_status.updater_update_available = false;
        s_status.ready_to_reboot = false;
        s_status.progress = 0;
        s_status.downloaded = 0;
        s_status.total = 0;
        s_status.speed_bps = 0;
        set_state_locked(OTA_UPDATE_STATE_UP_TO_DATE,
                         s_status.changelog[0] ? s_status.changelog : TRS("当前已是最新版本。"));
        ota_unlock();
        goto done;
    }
    bool strict_time_version = false;
    int version_cmp = compare_latest_to_current_version(s_status.latest_version, current, &strict_time_version);
    ESP_LOGI(TAG,
             "manifest version compare: current=%s latest=%s cmp=%d strict_time=%d",
             current,
             s_status.latest_version,
             version_cmp,
             strict_time_version ? 1 : 0);
    if (version_cmp > 0) {
        s_status.update_available = true;
        set_state_locked(OTA_UPDATE_STATE_UPDATE_AVAILABLE, TRS("发现新版本，可立即更新。"));
    } else if (version_cmp < 0 && strict_time_version) {
        s_status.update_available = false;
        set_state_locked(OTA_UPDATE_STATE_UP_TO_DATE, TRS("当前固件版本较新无需升级。"));
    } else {
        s_status.update_available = false;
        set_state_locked(OTA_UPDATE_STATE_UP_TO_DATE, TRS("当前已是最新版本。"));
    }
    if (s_status.updater_latest_version[0] && updater_current[0]) {
        bool updater_strict_time_version = false;
        int updater_cmp = compare_latest_to_current_version(s_status.updater_latest_version,
                                                            updater_current,
                                                            &updater_strict_time_version);
        ESP_LOGI(TAG,
                 "updater version compare: current=%s latest=%s cmp=%d strict_time=%d",
                 updater_current,
                 s_status.updater_latest_version,
                 updater_cmp,
                 updater_strict_time_version ? 1 : 0);
        s_status.updater_update_available = updater_cmp > 0;
    } else if (s_status.updater_latest_version[0]) {
        s_status.updater_update_available = true;
    }
    ota_unlock();

done:
    if (manifest) {
        free(manifest);
    }
    ota_lock();
    s_check_task_running = false;
    s_status.busy = s_download_task_running || s_updater_task_running;
    ota_unlock();
    ota_delete_current_task();
}

static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out, size_t out_len)
{
    static const char hex[] = "0123456789abcdef";
    if (!out || out_len < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[bytes[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static bool write_request_json(void)
{
    FILE *fp = fopen(OTA_UPDATE_REQUEST_PATH, "w");
    if (!fp) {
        return false;
    }
    time_t now = time(NULL);
    fprintf(fp,
            "{\n"
            "  \"schema\": 1,\n"
            "  \"action\": \"install_from_tf\",\n"
            "  \"project\": \"%s\",\n"
            "  \"model\": \"m5tab5\",\n"
            "  \"chip\": \"esp32p4\",\n"
            "  \"partition_scheme\": \"factory-updater-v1\",\n"
            "  \"target_partition\": \"main_app\",\n"
            "  \"firmware_path\": \"%s\",\n"
            "  \"firmware_size\": %llu,\n"
            "  \"version\": \"%s\",\n"
            "  \"build_time\": \"%s\",\n"
            "  \"sha256\": \"%s\",\n"
            "  \"created_at\": %lld\n"
            "}\n",
            OTA_PROJECT_NAME,
            OTA_UPDATE_FIRMWARE_PATH,
            (unsigned long long)s_manifest_size,
            s_manifest_version,
            s_manifest_build_time,
            s_expected_sha256,
            (long long)now);
    fclose(fp);
    return true;
}

static void ota_download_task(void *arg)
{
    (void)arg;
    char reason[160];
    char url[192];
    char expected_sha[65];
    uint64_t expected_size;
    FILE *fp = NULL;
    unsigned char *buffer = NULL;
    unsigned char *stage_buffer = NULL;
    unsigned char *write_buffer = NULL;
    size_t stage_used = 0;
    mbedtls_sha256_context sha_ctx;
    bool sha_ctx_ready = false;
    unsigned char digest[32];
    char actual_sha[65];
    int status = 0;
    int64_t content_length = 0;
    int64_t start_us = 0;
    int64_t last_ui_us = 0;
    int64_t last_log_us = 0;
    uint64_t last_ui_bytes = 0;
    uint64_t last_log_bytes = 0;
    uint64_t write_us_total = 0;
    uint64_t downloaded = 0;
    uint64_t written = 0;

    ota_lock();
    s_download_task_running = true;
    s_status.error[0] = '\0';
    s_status.progress = 0;
    s_status.downloaded = 0;
    s_status.total = s_manifest_size;
    s_status.speed_bps = 0;
    set_state_locked(OTA_UPDATE_STATE_DOWNLOADING, TRS("正在下载固件..."));
    copy_text(url, sizeof(url), s_status.firmware_url);
    copy_text(expected_sha, sizeof(expected_sha), s_expected_sha256);
    expected_size = s_manifest_size;
    ota_unlock();

    if (!environment_ready(reason, sizeof(reason), true)) {
        set_error(reason);
        goto done;
    }
    if (url[0] == '\0' || expected_sha[0] == '\0' || expected_size == 0) {
        set_error(TRS("没有可下载的固件信息，请先检测更新"));
        goto done;
    }

    bool use_tls = url_uses_https(url);
    ESP_LOGI(TAG,
             "download start: url=%s tls=%d expected=%llu",
             url,
             use_tls ? 1 : 0,
             (unsigned long long)expected_size);
    esp_http_client_config_t config = {
        .url = url,
        .user_agent = "M5Tab5-OTA/1.0",
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = OTA_DOWNLOAD_BUFFER_SIZE,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = use_tls ? esp_crt_bundle_attach : NULL,
        .disable_auto_redirect = false,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = NULL;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= OTA_HTTP_MAX_ATTEMPTS; ++attempt) {
        client = esp_http_client_init(&config);
        if (!client) {
            set_error(TRS("创建 HTTP 客户端失败"));
            goto done;
        }

        err = esp_http_client_open(client, 0);
        if (err == ESP_OK) {
            break;
        }
        log_ota_heap("firmware download connection failed");
        ESP_LOGW(TAG,
                 "download connection failed (%d/%d): %s",
                 attempt,
                 OTA_HTTP_MAX_ATTEMPTS,
                 esp_err_to_name(err));
        esp_http_client_cleanup(client);
        client = NULL;
        if (attempt < OTA_HTTP_MAX_ATTEMPTS && http_error_can_retry(err, 0)) {
            vTaskDelay(pdMS_TO_TICKS(OTA_HTTP_RETRY_DELAY_MS * attempt));
            continue;
        }
        break;
    }
    if (err != ESP_OK) {
        snprintf(reason, sizeof(reason), TRS("打开下载连接失败：%s，请稍后重试"), esp_err_to_name(err));
        set_error(reason);
        goto done;
    }
    content_length = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        snprintf(reason, sizeof(reason), TRS("下载失败：HTTP %d"), status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        set_error(reason);
        goto done;
    }
    if (content_length > 0 && (uint64_t)content_length != expected_size) {
        ESP_LOGW(TAG, "manifest size=%llu, http content-length=%lld",
                 (unsigned long long)expected_size,
                 (long long)content_length);
    }

    fp = fopen(OTA_UPDATE_FIRMWARE_PATH, "wb");
    if (!fp) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        set_error(TRS("无法写入 TF 卡固件文件"));
        goto done;
    }
    setvbuf(fp, NULL, _IONBF, 0);

    buffer = heap_caps_malloc(OTA_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = heap_caps_malloc(OTA_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    }
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        set_error(TRS("下载缓冲区内存不足"));
        goto done;
    }
    stage_buffer = heap_caps_malloc(OTA_PSRAM_STAGE_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stage_buffer) {
        ESP_LOGI(TAG,
                 "PSRAM stage buffer enabled: %u bytes",
                 (unsigned)OTA_PSRAM_STAGE_BUFFER_SIZE);
    } else {
        ESP_LOGW(TAG, "PSRAM stage buffer unavailable, fallback to direct TF writes");
    }
    write_buffer = heap_caps_malloc(OTA_STAGE_WRITE_CHUNK_SIZE,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!write_buffer) {
        write_buffer = heap_caps_malloc(OTA_STAGE_WRITE_CHUNK_SIZE,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (write_buffer) {
        ESP_LOGI(TAG,
                 "TF write buffer enabled: %u bytes internal, throttle=%u B/s",
                 (unsigned)OTA_STAGE_WRITE_CHUNK_SIZE,
                 (unsigned)OTA_DOWNLOAD_MAX_BPS);
    } else {
        ESP_LOGW(TAG, "internal TF write buffer unavailable, direct fwrite path will be used");
    }

    mbedtls_sha256_init(&sha_ctx);
    sha_ctx_ready = true;
    mbedtls_sha256_starts(&sha_ctx, 0);
    start_us = esp_timer_get_time();
    last_ui_us = start_us;
    last_log_us = start_us;
    int64_t last_data_us = start_us;
    while (true) {
        int got = esp_http_client_read(client, (char *)buffer, OTA_DOWNLOAD_BUFFER_SIZE);
        int64_t now_us = esp_timer_get_time();
        if (got < 0) {
            int saved_errno = errno;
            bool retryable = saved_errno == EAGAIN || saved_errno == EWOULDBLOCK ||
                             saved_errno == ETIMEDOUT;
            if (retryable &&
                now_us - last_data_us < OTA_DOWNLOAD_STALL_TIMEOUT_MS * 1000LL) {
                vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_IDLE_DELAY_MS));
                continue;
            }
            snprintf(reason, sizeof(reason), TRS("下载过程中读取失败：errno=%d"), saved_errno);
            set_error(reason);
            break;
        }
        if (got == 0) {
            if (downloaded >= expected_size) {
                break;
            }
            if (now_us - last_data_us < OTA_DOWNLOAD_STALL_TIMEOUT_MS * 1000LL) {
                vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_IDLE_DELAY_MS));
                continue;
            }
            set_error(TRS("下载连接长时间无数据"));
            break;
        }
        mbedtls_sha256_update(&sha_ctx, buffer, (size_t)got);
        downloaded += (uint64_t)got;
        if (downloaded > expected_size) {
            set_error(TRS("下载数据超过服务器声明大小"));
            break;
        }
        if (stage_buffer) {
            size_t copied = 0;
            while (copied < (size_t)got) {
                size_t space = OTA_PSRAM_STAGE_BUFFER_SIZE - stage_used;
                if (space == 0) {
                    if (!flush_download_stage(fp,
                                              stage_buffer,
                                              write_buffer,
                                              &stage_used,
                                              &written,
                                              &write_us_total,
                                              reason,
                                              sizeof(reason))) {
                        set_error(reason);
                        break;
                    }
                    space = OTA_PSRAM_STAGE_BUFFER_SIZE;
                }
                size_t chunk = (size_t)got - copied;
                if (chunk > space) {
                    chunk = space;
                }
                memcpy(stage_buffer + stage_used, buffer + copied, chunk);
                stage_used += chunk;
                copied += chunk;
                if (stage_used >= OTA_PSRAM_STAGE_BUFFER_SIZE) {
                    if (!flush_download_stage(fp,
                                              stage_buffer,
                                              write_buffer,
                                              &stage_used,
                                              &written,
                                              &write_us_total,
                                              reason,
                                              sizeof(reason))) {
                        set_error(reason);
                        break;
                    }
                }
            }
            if (s_status.state == OTA_UPDATE_STATE_ERROR) {
                break;
            }
        } else {
            size_t direct_used = (size_t)got;
            if (!flush_download_stage(fp,
                                      buffer,
                                      write_buffer,
                                      &direct_used,
                                      &written,
                                      &write_us_total,
                                      reason,
                                      sizeof(reason))) {
                set_error(reason);
                break;
            }
        }
        apply_download_speed_limit(downloaded, start_us);
        now_us = esp_timer_get_time();
        last_data_us = now_us;
        int64_t ui_elapsed_us = now_us - last_ui_us;
        if (ui_elapsed_us >= 250000 || downloaded == expected_size) {
            uint64_t window_bytes = downloaded - last_ui_bytes;
            uint32_t speed = ui_elapsed_us > 0 ? (uint32_t)((window_bytes * 1000000ULL) / (uint64_t)ui_elapsed_us) : 0;
            int progress = expected_size ? (int)((downloaded * 100ULL) / expected_size) : 0;
            if (progress > 100) {
                progress = 100;
            }
            ota_lock();
            s_status.downloaded = downloaded;
            s_status.progress = progress;
            s_status.speed_bps = speed;
            ota_unlock();
            last_ui_us = now_us;
            last_ui_bytes = downloaded;
        }
        int64_t log_elapsed_us = now_us - last_log_us;
        if (log_elapsed_us >= 3000000) {
            uint64_t window_bytes = downloaded - last_log_bytes;
            uint32_t speed = (uint32_t)((window_bytes * 1000000ULL) / (uint64_t)log_elapsed_us);
            ESP_LOGI(TAG,
                     "download progress: %llu/%llu bytes, speed=%lu B/s",
                     (unsigned long long)downloaded,
                     (unsigned long long)expected_size,
                     (unsigned long)speed);
            last_log_us = now_us;
            last_log_bytes = downloaded;
        }
        if (downloaded >= expected_size) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_YIELD_MS));
    }
    if (s_status.state != OTA_UPDATE_STATE_ERROR && stage_buffer && stage_used > 0) {
        if (!flush_download_stage(fp,
                                  stage_buffer,
                                  write_buffer,
                                  &stage_used,
                                  &written,
                                  &write_us_total,
                                  reason,
                                  sizeof(reason))) {
            set_error(reason);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (s_status.state == OTA_UPDATE_STATE_ERROR) {
        goto done;
    }
    fflush(fp);
    fclose(fp);
    fp = NULL;
    mbedtls_sha256_finish(&sha_ctx, digest);
    bytes_to_hex(digest, sizeof(digest), actual_sha, sizeof(actual_sha));

    if (downloaded != expected_size || written != expected_size) {
        snprintf(reason,
                 sizeof(reason),
                 TRS("固件大小不匹配：接收%llu/写入%llu/期望%llu"),
                 (unsigned long long)downloaded,
                 (unsigned long long)written,
                 (unsigned long long)expected_size);
        set_error(reason);
        goto done;
    }
    if (strcmp(actual_sha, expected_sha) != 0) {
        set_error(TRS("固件 SHA256 校验失败"));
        goto done;
    }
    int64_t total_us = esp_timer_get_time() - start_us;
    if (total_us > 0) {
        ESP_LOGI(TAG,
                 "download complete: size=%llu written=%llu avg=%llu B/s tf_write=%llu ms",
                 (unsigned long long)downloaded,
                 (unsigned long long)written,
                 (unsigned long long)((downloaded * 1000000ULL) / (uint64_t)total_us),
                 (unsigned long long)(write_us_total / 1000ULL));
    }
    if (!write_request_json()) {
        set_error(TRS("写入升级请求失败"));
        goto done;
    }

    ota_lock();
    s_status.ready_to_reboot = true;
    s_status.update_available = false;
    s_status.progress = 100;
    s_status.downloaded = downloaded;
    set_state_locked(OTA_UPDATE_STATE_READY_TO_REBOOT, TRS("固件已下载到 TF 卡，等待确认安装。"));
    ota_unlock();

done:
    if (fp) {
        fclose(fp);
    }
    if (buffer) {
        free(buffer);
    }
    if (stage_buffer) {
        free(stage_buffer);
    }
    if (write_buffer) {
        free(write_buffer);
    }
    if (sha_ctx_ready) {
        mbedtls_sha256_free(&sha_ctx);
    }
    ota_lock();
    s_download_task_running = false;
    s_status.busy = s_check_task_running || s_updater_task_running;
    ota_unlock();
    ota_delete_current_task();
}
