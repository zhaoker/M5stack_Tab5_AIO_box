#include "storage/tf_card.h"

#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>

#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define TF_CARD_MAX_FILES 8

static const char *TAG = "tf_card";
static bool s_tf_card_mounted;
static bool s_tf_card_raw_usb_active;
static bool s_tf_card_usb_disk_access_blocked;

esp_err_t tf_card_ensure_sys_dir(void)
{
    struct stat st;

    if (!tf_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (stat(TF_CARD_SYS_DIR, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (mkdir(TF_CARD_SYS_DIR, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "create sys dir failed: %s errno=%d", TF_CARD_SYS_DIR, errno);
    return ESP_FAIL;
}

static const char *tf_card_type_name(const sdmmc_card_t *card)
{
    if (card == NULL) {
        return "unknown";
    }
    if (card->is_mmc) {
        return "MMC";
    }
    if (card->is_sdio) {
        return "SDIO";
    }
    if (card->is_mem) {
        return "SD memory";
    }
    return "unknown";
}

static void tf_card_log_card_info(const sdmmc_card_t *card)
{
    if (card == NULL) {
        ESP_LOGW(TAG, "TF card mounted but card handle is NULL");
        return;
    }

    const uint64_t sector_count = (uint64_t)card->csd.capacity;
    const uint64_t sector_size = (uint64_t)card->csd.sector_size;
    const uint64_t capacity_bytes = sector_count * sector_size;
    const uint64_t capacity_mib = capacity_bytes / (1024ULL * 1024ULL);
    const unsigned int bus_width = 1U << card->log_bus_width;

    ESP_LOGI(TAG,
             "TF card info: name='%.8s', type=%s, max_freq=%" PRIu32 " kHz, real_freq=%d kHz",
             card->cid.name,
             tf_card_type_name(card),
             card->max_freq_khz,
             card->real_freq_khz);
    ESP_LOGI(TAG,
             "TF card capacity: sectors=%" PRIu64 ", sector_size=%" PRIu64 " bytes, capacity=%" PRIu64
             " bytes (%" PRIu64 " MiB)",
             sector_count,
             sector_size,
             capacity_bytes,
             capacity_mib);
    ESP_LOGI(TAG,
             "TF card bus: rca=0x%04x, bus_width=%u-bit, uhs1=%s, ddr=%s",
             card->rca,
             bus_width,
             card->is_uhs1 ? "yes" : "no",
             card->is_ddr ? "yes" : "no");
}

static void tf_card_log_filesystem_info(void)
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info(TF_CARD_MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF card filesystem info unavailable: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG,
             "TF card filesystem: mount=%s, total=%" PRIu64 " bytes (%" PRIu64
             " MiB), free=%" PRIu64 " bytes (%" PRIu64 " MiB)",
             TF_CARD_MOUNT_POINT,
             total_bytes,
             total_bytes / (1024ULL * 1024ULL),
             free_bytes,
             free_bytes / (1024ULL * 1024ULL));
}

esp_err_t tf_card_mount_on_boot(void)
{
    if (s_tf_card_raw_usb_active) {
        ESP_LOGW(TAG, "TF card mount ignored: raw card is owned by USB disk mode");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tf_card_mounted || bsp_sdcard != NULL) {
        s_tf_card_mounted = true;
        ESP_LOGI(TAG, "TF card already mounted at %s", TF_CARD_MOUNT_POINT);
        (void)tf_card_ensure_sys_dir();
        tf_card_log_card_info(bsp_sdcard);
        tf_card_log_filesystem_info();
        return ESP_OK;
    }

    ESP_LOGI(TAG, "TF card mount start: mount=%s, max_files=%d", TF_CARD_MOUNT_POINT, TF_CARD_MAX_FILES);
    esp_err_t ret = bsp_sdcard_init((char *)TF_CARD_MOUNT_POINT, TF_CARD_MAX_FILES);
    if (ret != ESP_OK) {
        s_tf_card_mounted = false;
        ESP_LOGW(TAG, "TF card mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }

    s_tf_card_mounted = true;
    ESP_LOGI(TAG, "TF card mounted successfully at %s", TF_CARD_MOUNT_POINT);
    (void)tf_card_ensure_sys_dir();
    tf_card_log_card_info(bsp_sdcard);
    tf_card_log_filesystem_info();
    return ESP_OK;
}

esp_err_t tf_card_unmount_for_usb_disk(void)
{
    if (!s_tf_card_mounted && bsp_sdcard == NULL) {
        ESP_LOGI(TAG, "TF card already unavailable for VFS");
        s_tf_card_mounted = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "TF card VFS unmount for USB disk mode");
    esp_err_t ret = bsp_sdcard_deinit((char *)TF_CARD_MOUNT_POINT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF card VFS unmount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }
    s_tf_card_mounted = false;
    return ESP_OK;
}

esp_err_t tf_card_mount_after_usb_disk(void)
{
    ESP_LOGI(TAG, "TF card remount after USB disk mode");
    return tf_card_mount_on_boot();
}

esp_err_t tf_card_init_raw_for_usb_disk(sdmmc_card_t **out_card)
{
    if (s_tf_card_raw_usb_active) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "TF card raw init for USB disk mode");
    esp_err_t ret = bsp_sdcard_init_raw(out_card);
    if (ret == ESP_OK) {
        s_tf_card_raw_usb_active = true;
    }
    return ret;
}

esp_err_t tf_card_deinit_raw_for_usb_disk(sdmmc_card_t *card)
{
    ESP_LOGI(TAG, "TF card raw deinit for USB disk mode");
    esp_err_t ret = bsp_sdcard_deinit_raw(card);
    if (ret != ESP_ERR_INVALID_STATE) {
        s_tf_card_raw_usb_active = false;
    }
    return ret;
}

bool tf_card_is_mounted(void)
{
    return s_tf_card_mounted && !s_tf_card_usb_disk_access_blocked;
}

bool tf_card_is_vfs_mounted_for_usb_disk(void)
{
    return s_tf_card_mounted;
}

void tf_card_set_usb_disk_access_blocked(bool blocked)
{
    s_tf_card_usb_disk_access_blocked = blocked;
}

const char *tf_card_get_mount_point(void)
{
    return TF_CARD_MOUNT_POINT;
}

const char *tf_card_get_sys_dir(void)
{
    return TF_CARD_SYS_DIR;
}
