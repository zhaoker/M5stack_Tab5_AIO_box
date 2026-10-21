#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "updater_config.h"
#include "updater_sdcard.h"
#include "updater_ui.h"

static const char *TAG = "factory_updater";

static void fail_and_stay(const char *message);
static void reboot_to_partition(const esp_partition_t *partition, const char *status);

static const char *partition_type_name(esp_partition_type_t type)
{
    switch (type) {
    case ESP_PARTITION_TYPE_APP:
        return "app";
    case ESP_PARTITION_TYPE_DATA:
        return "data";
    default:
        return "unknown";
    }
}

static const char *partition_subtype_name(const esp_partition_t *partition)
{
    if (partition == NULL) {
        return "";
    }
    if (partition->type == ESP_PARTITION_TYPE_APP) {
        switch (partition->subtype) {
        case ESP_PARTITION_SUBTYPE_APP_FACTORY:
            return "factory";
        case ESP_PARTITION_SUBTYPE_APP_TEST:
            return "test";
        default:
            if (partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
                partition->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
                return "ota";
            }
            return "app-other";
        }
    }
    if (partition->type == ESP_PARTITION_TYPE_DATA) {
        switch (partition->subtype) {
        case ESP_PARTITION_SUBTYPE_DATA_NVS:
            return "nvs";
        case ESP_PARTITION_SUBTYPE_DATA_PHY:
            return "phy";
        case ESP_PARTITION_SUBTYPE_DATA_OTA:
            return "ota-data";
        case ESP_PARTITION_SUBTYPE_DATA_FAT:
            return "fat";
        case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
            return "spiffs";
        default:
            return "data-other";
        }
    }
    return "unknown";
}

static bool same_partition(const esp_partition_t *a, const esp_partition_t *b)
{
    return a != NULL && b != NULL && a->type == b->type && a->subtype == b->subtype && a->address == b->address;
}

static void dump_partition_table(const char *reason)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    ESP_LOGW(TAG, "partition table dump: %s", reason ? reason : "");
    if (running != NULL) {
        ESP_LOGW(TAG,
                 "running partition: label=%s type=%s subtype=0x%02x(%s) address=0x%lx size=0x%lx",
                 running->label,
                 partition_type_name(running->type),
                 running->subtype,
                 partition_subtype_name(running),
                 (unsigned long)running->address,
                 (unsigned long)running->size);
    } else {
        ESP_LOGW(TAG, "running partition: <none>");
    }
    if (boot != NULL) {
        ESP_LOGW(TAG,
                 "boot partition: label=%s type=%s subtype=0x%02x(%s) address=0x%lx size=0x%lx",
                 boot->label,
                 partition_type_name(boot->type),
                 boot->subtype,
                 partition_subtype_name(boot),
                 (unsigned long)boot->address,
                 (unsigned long)boot->size);
    } else {
        ESP_LOGW(TAG, "boot partition: <none>");
    }

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    int index = 0;
    while (it != NULL) {
        const esp_partition_t *partition = esp_partition_get(it);
        if (partition != NULL) {
            const bool is_running = same_partition(partition, running);
            const bool is_boot = same_partition(partition, boot);
            ESP_LOGW(TAG,
                     "partition[%02d]%s%s label=%s type=0x%02x(%s) subtype=0x%02x(%s) address=0x%lx size=0x%lx",
                     index,
                     is_running ? " running" : "",
                     is_boot ? " boot" : "",
                     partition->label,
                     partition->type,
                     partition_type_name(partition->type),
                     partition->subtype,
                     partition_subtype_name(partition),
                     (unsigned long)partition->address,
                     (unsigned long)partition->size);
            if (partition->type == ESP_PARTITION_TYPE_APP) {
                esp_app_desc_t desc = {0};
                esp_err_t ret = esp_ota_get_partition_description(partition, &desc);
                if (ret == ESP_OK) {
                    ESP_LOGW(TAG,
                             "partition[%02d] app: project=%.32s version=%.32s time=%.16s %.16s",
                             index,
                             desc.project_name,
                             desc.version,
                             desc.date,
                             desc.time);
                } else {
                    ESP_LOGW(TAG, "partition[%02d] app: invalid image: %s", index, esp_err_to_name(ret));
                }
            }
        }
        it = esp_partition_next(it);
        ++index;
    }
    if (it != NULL) {
        esp_partition_iterator_release(it);
    }
}

static const esp_partition_t *find_main_app_partition(const char *reason)
{
    const esp_partition_t *main_app = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, UPDATER_TARGET_PARTITION_LABEL);
    if (main_app == NULL) {
        dump_partition_table(reason);
    }
    return main_app;
}

static const esp_partition_t *find_bootable_factory_fallback(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    while (it != NULL) {
        const esp_partition_t *partition = esp_partition_get(it);
        if (partition != NULL && !same_partition(partition, running)) {
            esp_app_desc_t desc = {0};
            esp_err_t ret = esp_ota_get_partition_description(partition, &desc);
            if (ret == ESP_OK) {
                ESP_LOGW(TAG,
                         "factory fallback candidate: label=%s address=0x%lx size=0x%lx project=%.32s version=%.32s",
                         partition->label,
                         (unsigned long)partition->address,
                         (unsigned long)partition->size,
                         desc.project_name,
                         desc.version);
                esp_partition_iterator_release(it);
                return partition;
            }
            ESP_LOGW(TAG,
                     "skip invalid factory fallback: label=%s address=0x%lx err=%s",
                     partition->label,
                     (unsigned long)partition->address,
                     esp_err_to_name(ret));
        }
        it = esp_partition_next(it);
    }
    return NULL;
}

static void boot_factory_fallback_or_fail(const char *message)
{
    const esp_partition_t *fallback = find_bootable_factory_fallback();
    if (fallback != NULL) {
        ESP_LOGW(TAG,
                 "%s; boot fallback partition label=%s address=0x%lx",
                 message ? message : "main_app unavailable",
                 fallback->label,
                 (unsigned long)fallback->address);
        updater_append_history("fallback_boot", fallback->label);
        reboot_to_partition(fallback, "launcher");
    }
    fail_and_stay(message);
}

static void fail_and_stay(const char *message)
{
    ESP_LOGE(TAG, "%s", message);
    updater_write_last_error(message);
    updater_append_history("install_failed", message);
    updater_write_progress("failed", 0, message);
    updater_ui_set_error(message);
    while (true) {
        updater_ui_pump();
    }
}

static void reboot_to_partition(const esp_partition_t *partition, const char *status)
{
    if (partition == NULL) {
        fail_and_stay("boot partition not found");
    }
    ESP_LOGI(TAG,
             "set boot partition: label=%s type=%d subtype=0x%x address=0x%lx size=0x%lx status=%s",
             partition->label,
             partition->type,
             partition->subtype,
             (unsigned long)partition->address,
             (unsigned long)partition->size,
             status ? status : "");
    if (updater_ui_is_ready()) {
        updater_ui_set_status(status);
    }
    esp_err_t ret = esp_ota_set_boot_partition(partition);
    if (ret != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "set boot partition failed: %s", esp_err_to_name(ret));
        fail_and_stay(msg);
    }
    ESP_LOGI(TAG, "boot partition set, turn backlight off before restart");
    if (updater_ui_is_ready()) {
        updater_ui_blackout();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    ESP_LOGI(TAG, "restart now");
    esp_restart();
}

static void boot_existing_app_or_wait(void)
{
    const esp_partition_t *main_app = find_main_app_partition("main_app not found while booting existing app");
    if (main_app == NULL) {
        boot_factory_fallback_or_fail("main_app partition not found");
    }

    ESP_LOGI(TAG,
             "check existing app: label=%s address=0x%lx size=0x%lx",
             main_app->label,
             (unsigned long)main_app->address,
             (unsigned long)main_app->size);
    esp_app_desc_t desc = {0};
    if (esp_ota_get_partition_description(main_app, &desc) == ESP_OK) {
        ESP_LOGI(TAG,
                 "valid main app found: project=%.32s version=%.32s time=%.16s %.16s",
                 desc.project_name,
                 desc.version,
                 desc.date,
                 desc.time);
        updater_append_history("no_update_request", "boot main_app");
        reboot_to_partition(main_app, "boot");
    }

    ESP_LOGW(TAG, "main_app has no valid app image");
    fail_and_stay("no valid app");
}

static esp_err_t get_file_size(const char *path, size_t *out_size)
{
    struct stat st = {0};
    if (stat(path, &st) != 0) {
        return ESP_FAIL;
    }
    if (st.st_size <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_size = (size_t)st.st_size;
    return ESP_OK;
}

static bool file_exists(const char *path)
{
    struct stat st = {0};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static esp_err_t validate_image_header(FILE *fp, esp_app_desc_t *out_desc)
{
    esp_image_header_t header = {0};
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(&header, 1, sizeof(header), fp) != sizeof(header)) {
        ESP_LOGW(TAG, "read image header failed");
        return ESP_FAIL;
    }
    if (header.magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGW(TAG, "invalid image magic: 0x%02x", header.magic);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (fseek(fp, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), SEEK_SET) != 0 ||
        fread(out_desc, 1, sizeof(*out_desc), fp) != sizeof(*out_desc)) {
        ESP_LOGW(TAG, "read app descriptor failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "image descriptor: project=%.32s version=%.32s time=%.16s %.16s",
             out_desc->project_name,
             out_desc->version,
             out_desc->date,
             out_desc->time);
    if (strncmp(out_desc->project_name, UPDATER_EXPECTED_PROJECT, sizeof(out_desc->project_name)) != 0) {
        ESP_LOGW(TAG,
                 "unexpected project: expected=%s actual=%.32s",
                 UPDATER_EXPECTED_PROJECT,
                 out_desc->project_name);
        return ESP_ERR_INVALID_VERSION;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "rewind firmware image failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t write_main_app(FILE *fp, size_t image_size, const esp_partition_t *main_app)
{
    ESP_LOGI(TAG,
             "begin ota write: target=%s address=0x%lx partition_size=%lu image_size=%lu buffer=%d",
             main_app->label,
             (unsigned long)main_app->address,
             (unsigned long)main_app->size,
             (unsigned long)image_size,
             UPDATER_IO_BUFFER_SIZE);
    esp_ota_handle_t handle = 0;
    esp_err_t ret = esp_ota_begin(main_app, image_size, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    void *buffer = malloc(UPDATER_IO_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "allocate ota buffer failed");
        esp_ota_abort(handle);
        return ESP_ERR_NO_MEM;
    }

    size_t written = 0;
    size_t chunk_count = 0;
    int last_logged_percent = -10;
    int last_progress_file_percent = -10;
    int64_t last_progress_file_us = 0;
    while (written < image_size) {
        const size_t to_read = (image_size - written) > UPDATER_IO_BUFFER_SIZE
                                   ? UPDATER_IO_BUFFER_SIZE
                                   : (image_size - written);
        const size_t got = fread(buffer, 1, to_read, fp);
        if (got == 0) {
            ESP_LOGE(TAG, "read firmware failed at %lu/%lu", (unsigned long)written, (unsigned long)image_size);
            free(buffer);
            esp_ota_abort(handle);
            return ESP_FAIL;
        }

        ret = esp_ota_write(handle, buffer, got);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %lu/%lu: %s",
                     (unsigned long)written,
                     (unsigned long)image_size,
                     esp_err_to_name(ret));
            free(buffer);
            esp_ota_abort(handle);
            return ret;
        }

        written += got;
        ++chunk_count;
        const int percent = 8 + (int)((written * 88ULL) / image_size);
        vTaskDelay(pdMS_TO_TICKS(UPDATER_FLASH_WRITE_SETTLE_MS));
        if (percent >= 96 || percent - last_logged_percent >= 10) {
            last_logged_percent = percent;
            ESP_LOGI(TAG,
                     "ota write progress: %d%% (%lu/%lu bytes)",
                     percent,
                     (unsigned long)written,
                     (unsigned long)image_size);
        }
        updater_ui_set_progress(percent);
        const int64_t now_us = esp_timer_get_time();
        const bool write_progress_file = percent >= 96 ||
                                         last_progress_file_percent < 0 ||
                                         percent - last_progress_file_percent >=
                                             UPDATER_PROGRESS_FILE_MIN_STEP ||
                                         now_us - last_progress_file_us >=
                                             (int64_t)UPDATER_PROGRESS_FILE_MIN_INTERVAL_MS * 1000LL;
        if (write_progress_file) {
            updater_write_progress("write", percent, "writing");
            last_progress_file_percent = percent;
            last_progress_file_us = now_us;
        }
        if (percent >= 96 || chunk_count % UPDATER_UI_PUMP_EVERY_CHUNKS == 0) {
            updater_ui_pump();
        }
    }

    free(buffer);
    ret = esp_ota_end(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ota write complete");
    return ESP_OK;
}

static void install_from_tf(void)
{
    ESP_LOGI(TAG, "check update request: %s", UPDATER_REQUEST_PATH);
    if (!file_exists(UPDATER_REQUEST_PATH)) {
        ESP_LOGI(TAG, "no update request, boot existing app");
        boot_existing_app_or_wait();
    }
    ESP_LOGI(TAG, "update request found");

    size_t image_size = 0;
    ESP_LOGI(TAG, "check firmware file: %s", UPDATER_FIRMWARE_PATH);
    esp_err_t ret = get_file_size(UPDATER_FIRMWARE_PATH, &image_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "firmware not found: %s", esp_err_to_name(ret));
        boot_existing_app_or_wait();
    }
    ESP_LOGI(TAG, "firmware file size: %lu bytes", (unsigned long)image_size);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *main_app = find_main_app_partition("main_app not found before installing update.bin");
    if (main_app == NULL) {
        boot_factory_fallback_or_fail("main_app partition not found");
    }
    if (running != NULL) {
        ESP_LOGI(TAG,
                 "running partition: label=%s address=0x%lx size=0x%lx",
                 running->label,
                 (unsigned long)running->address,
                 (unsigned long)running->size);
    }
    ESP_LOGI(TAG,
             "target partition: label=%s address=0x%lx size=0x%lx",
             main_app->label,
             (unsigned long)main_app->address,
             (unsigned long)main_app->size);
    if (running != NULL && running->address == main_app->address) {
        fail_and_stay("refuse to write running partition");
    }
    if (image_size > main_app->size) {
        ESP_LOGE(TAG,
                 "firmware too large: image=%lu partition=%lu",
                 (unsigned long)image_size,
                 (unsigned long)main_app->size);
        fail_and_stay("firmware too large for main_app");
    }

    FILE *fp = fopen(UPDATER_FIRMWARE_PATH, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "open firmware failed");
        boot_existing_app_or_wait();
    }

    ESP_LOGI(TAG, "update request and firmware found, init display");
    ret = updater_ui_init();
    if (ret != ESP_OK) {
        fclose(fp);
        char msg[96];
        snprintf(msg, sizeof(msg), "display init failed: %s", esp_err_to_name(ret));
        fail_and_stay(msg);
    }

    ESP_LOGI(TAG, "start update UI and validate image");
    updater_ui_show_update_mode("update...");
    updater_append_history("install_start", "install update.bin");
    updater_write_progress("start", 0, "start");
    updater_ui_set_status("check");
    updater_ui_set_progress(3);

    esp_app_desc_t desc = {0};
    ret = validate_image_header(fp, &desc);
    if (ret != ESP_OK) {
        fclose(fp);
        fail_and_stay("invalid firmware image");
    }
    ESP_LOGI(TAG, "install accepted: project=%.32s version=%.32s", desc.project_name, desc.version);

    updater_ui_set_status("write");
    ret = write_main_app(fp, image_size, main_app);
    fclose(fp);
    if (ret != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "write main_app failed: %s", esp_err_to_name(ret));
        fail_and_stay(msg);
    }

    updater_ui_set_progress(98);
    updater_write_progress("set_boot", 98, "set boot partition");
    updater_append_history("install_success", desc.version);
    ret = remove(UPDATER_REQUEST_PATH) == 0 ? ESP_OK : ESP_FAIL;
    ESP_LOGI(TAG, "remove request file: %s", ret == ESP_OK ? "ok" : "failed");
    reboot_to_partition(main_app, "reboot");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Tab5 factory updater starting: core=%d", xPortGetCoreID());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init requires erase: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS ready");

    ESP_LOGI(TAG, "init TF card");
    ret = updater_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF unavailable, skip update and boot existing app");
        boot_existing_app_or_wait();
    }
    ret = updater_sdcard_ensure_update_dir();
    ESP_LOGI(TAG, "ensure update dir %s: %s", UPDATER_DIR, ret == ESP_OK ? "ok" : esp_err_to_name(ret));

    install_from_tf();
}
