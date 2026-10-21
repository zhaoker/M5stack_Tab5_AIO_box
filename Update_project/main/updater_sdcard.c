#include "updater_sdcard.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "bsp/m5stack_tab5.h"
#include "esp_err.h"
#include "esp_log.h"
#include "updater_config.h"

#define UPDATER_TF_MAX_FILES 4

static const char *TAG = "updater_sd";
static bool s_mounted;

static void write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        ESP_LOGW(TAG, "open %s failed", path);
        return;
    }
    fputs(text ? text : "", fp);
    fclose(fp);
}

esp_err_t updater_sdcard_mount(void)
{
    if (s_mounted) {
        s_mounted = true;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "mount TF card at %s", UPDATER_TF_MOUNT_POINT);
    esp_err_t ret = bsp_sdcard_init((char *)UPDATER_TF_MOUNT_POINT, UPDATER_TF_MAX_FILES);
    if (ret != ESP_OK) {
        s_mounted = false;
        ESP_LOGW(TAG, "TF mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "TF card mounted");
    return ESP_OK;
}

bool updater_sdcard_is_mounted(void)
{
    return s_mounted;
}

esp_err_t updater_sdcard_ensure_update_dir(void)
{
    struct stat st = {0};
    if (stat(UPDATER_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        ESP_LOGI(TAG, "update dir exists: %s", UPDATER_DIR);
        return ESP_OK;
    }
    if (mkdir(UPDATER_DIR, 0775) == 0) {
        ESP_LOGI(TAG, "update dir created: %s", UPDATER_DIR);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "create update dir failed: %s", UPDATER_DIR);
    return ESP_FAIL;
}

void updater_write_last_error(const char *message)
{
    if (!s_mounted) {
        return;
    }
    write_text_file(UPDATER_LAST_ERROR_PATH, message ? message : "");
}

void updater_append_history(const char *state, const char *message)
{
    if (!s_mounted) {
        return;
    }
    FILE *fp = fopen(UPDATER_HISTORY_PATH, "a");
    if (fp == NULL) {
        ESP_LOGW(TAG, "open history failed");
        return;
    }

    const time_t now = time(NULL);
    fprintf(fp,
            "{\"time\":%lld,\"state\":\"%s\",\"message\":\"%s\"}\n",
            (long long)now,
            state ? state : "",
            message ? message : "");
    fclose(fp);
}

void updater_write_progress(const char *state, int percent, const char *message)
{
    if (!s_mounted) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    FILE *fp = fopen(UPDATER_PROGRESS_PATH, "w");
    if (fp == NULL) {
        ESP_LOGW(TAG, "open progress failed");
        return;
    }
    fprintf(fp,
            "{\"state\":\"%s\",\"progress\":%d,\"message\":\"%s\"}\n",
            state ? state : "",
            percent,
            message ? message : "");
    fclose(fp);
}
