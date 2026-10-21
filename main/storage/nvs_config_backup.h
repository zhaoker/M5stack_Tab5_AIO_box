#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define NVS_CONFIG_BACKUP_PATH "/sdcard/backup/tab5_nvs_config.json"
#define NVS_CONFIG_BACKUP_PASSWORD_MIN_LEN 4
#define NVS_CONFIG_BACKUP_PASSWORD_MAX_LEN 64

esp_err_t nvs_config_backup_export(char *message, size_t message_len);
esp_err_t nvs_config_backup_import(char *message, size_t message_len);
esp_err_t nvs_config_backup_has_migration_payload(bool *out_has_migration,
                                                  char *message,
                                                  size_t message_len);
esp_err_t nvs_config_backup_export_with_password(const char *backup_password,
                                                 char *message,
                                                 size_t message_len);
esp_err_t nvs_config_backup_import_with_password(const char *backup_password,
                                                 char *message,
                                                 size_t message_len);

#ifdef __cplusplus
}
#endif
