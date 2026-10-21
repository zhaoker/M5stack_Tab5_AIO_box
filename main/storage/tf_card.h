#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TF_CARD_MOUNT_POINT "/sdcard"
/* TF 卡根目录下的系统文件夹；系统临时文件、缓存文件统一放这里，避免散落在根目录。 */
#define TF_CARD_SYS_DIR TF_CARD_MOUNT_POINT "/.sys"

esp_err_t tf_card_mount_on_boot(void);
esp_err_t tf_card_unmount_for_usb_disk(void);
esp_err_t tf_card_mount_after_usb_disk(void);
esp_err_t tf_card_init_raw_for_usb_disk(sdmmc_card_t **out_card);
esp_err_t tf_card_deinit_raw_for_usb_disk(sdmmc_card_t *card);
void tf_card_set_usb_disk_access_blocked(bool blocked);
bool tf_card_is_mounted(void);
bool tf_card_is_vfs_mounted_for_usb_disk(void);
const char *tf_card_get_mount_point(void);
esp_err_t tf_card_ensure_sys_dir(void);
const char *tf_card_get_sys_dir(void);

#ifdef __cplusplus
}
#endif
