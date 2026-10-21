#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_UPDATE_SERVER_MANIFEST_URL "https://yourself.upload.server"
#define OTA_UPDATE_DIR "/sdcard/update"
#define OTA_UPDATE_FIRMWARE_PATH OTA_UPDATE_DIR "/update.bin"
#define OTA_UPDATE_REQUEST_PATH OTA_UPDATE_DIR "/request.json"
#define OTA_UPDATE_MIN_FREE_BYTES (20ULL * 1024ULL * 1024ULL)

typedef enum {
    OTA_UPDATE_STATE_IDLE = 0,
    OTA_UPDATE_STATE_CHECKING,
    OTA_UPDATE_STATE_UP_TO_DATE,
    OTA_UPDATE_STATE_UPDATE_AVAILABLE,
    OTA_UPDATE_STATE_DOWNLOADING,
    OTA_UPDATE_STATE_READY_TO_REBOOT,
    OTA_UPDATE_STATE_UPDATER_INSTALLING,
    OTA_UPDATE_STATE_ERROR,
} ota_update_state_t;

typedef struct {
    ota_update_state_t state;
    bool busy;
    bool update_available;
    bool updater_update_available;
    bool updater_busy;
    bool ready_to_reboot;
    bool launcher_mode;
    int progress;
    uint64_t downloaded;
    uint64_t total;
    uint32_t speed_bps;
    char current_version[64];
    char latest_version[64];
    char latest_build_time[64];
    char updater_current_version[64];
    char updater_latest_version[64];
    char updater_build_time[64];
    char firmware_url[192];
    char updater_url[192];
    char changelog[4096];
    char status[160];
    char error[160];
} ota_update_status_t;

esp_err_t ota_update_service_init(void);
esp_err_t ota_update_scan_ready_package(void);
void ota_update_get_status(ota_update_status_t *out_status);
esp_err_t ota_update_start_check(void);
esp_err_t ota_update_start_download(void);
esp_err_t ota_update_start_updater_install(void);
esp_err_t ota_update_reboot_to_updater(void);
const char *ota_update_state_text(ota_update_state_t state);

#ifdef __cplusplus
}
#endif
