#include "storage/usb_storage.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/m5stack_tab5.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_private/msc_scsi_bot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i18n/app_i18n.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "usb/usb_host.h"

static const char *TAG = "usb_storage";

enum {
    USB_STORAGE_QUEUE_LEN = 8,
    USB_STORAGE_HOST_TASK_STACK = 4096,
    USB_STORAGE_MGR_TASK_STACK = 4096,
    USB_STORAGE_HOST_TASK_PRIORITY = 10,
    USB_STORAGE_MGR_TASK_PRIORITY = 5,
    USB_STORAGE_MSC_TASK_PRIORITY = 5,
    USB_STORAGE_MSC_TASK_STACK = 4096,
    USB_STORAGE_MAX_FILES = 16,
};

typedef enum {
    USB_STORAGE_MSG_START,
    USB_STORAGE_MSG_STOP,
    USB_STORAGE_MSG_FORMAT,
    USB_STORAGE_MSG_DEVICE_CONNECTED,
    USB_STORAGE_MSG_DEVICE_DISCONNECTED,
    USB_STORAGE_MSG_HOST_DONE,
} usb_storage_msg_id_t;

typedef struct {
    usb_storage_msg_id_t id;
    union {
        uint8_t address;
        msc_host_device_handle_t device_handle;
        esp_err_t error;
        usb_storage_format_t format;
    } data;
} usb_storage_msg_t;

static QueueHandle_t s_usb_queue;
static SemaphoreHandle_t s_usb_lock;
static SemaphoreHandle_t s_usb_io_lock;
static TaskHandle_t s_mgr_task;
static TaskHandle_t s_host_task;
static msc_host_device_handle_t s_msc_device;
static msc_host_vfs_handle_t s_vfs_handle;
static usb_storage_status_t s_status = {
    .state = USB_STORAGE_STATE_OFF,
    .mount_point = USB_STORAGE_MOUNT_POINT,
};
static bool s_host_installed;
static bool s_msc_installed;
static uint8_t s_last_device_address;

static const char *usb_storage_format_name(usb_storage_format_t format)
{
    switch (format) {
    case USB_STORAGE_FORMAT_FAT32:
        return "FAT32";
    case USB_STORAGE_FORMAT_EXFAT:
        return "exFAT";
    default:
        return "FAT32";
    }
}

static void usb_storage_clear_fs_info_locked(void)
{
    s_status.fs_type[0] = '\0';
    s_status.total_bytes = 0;
    s_status.free_bytes = 0;
}

static void usb_storage_lock(void)
{
    if (s_usb_lock) {
        xSemaphoreTake(s_usb_lock, portMAX_DELAY);
    }
}

static void usb_storage_unlock(void)
{
    if (s_usb_lock) {
        xSemaphoreGive(s_usb_lock);
    }
}

static void usb_storage_set_state_locked(usb_storage_state_t state,
                                         esp_err_t err,
                                         const char *message)
{
    s_status.state = state;
    s_status.last_error = err;
    if (message) {
        strlcpy(s_status.message, message, sizeof(s_status.message));
    }
    s_status.generation++;
}

static bool usb_storage_detect_fs_from_sector(const uint8_t *sector,
                                              size_t sector_size,
                                              char *out,
                                              size_t out_len)
{
    if (!sector || !out || out_len == 0) {
        return false;
    }
    if (sector_size >= 11 && memcmp(sector + 3, "EXFAT   ", 8) == 0) {
        strlcpy(out, "exFAT", out_len);
        return true;
    }
    if (sector_size >= 90 && memcmp(sector + 82, "FAT32   ", 8) == 0) {
        strlcpy(out, "FAT32", out_len);
        return true;
    }
    if (sector_size >= 62 && memcmp(sector + 54, "FAT16   ", 8) == 0) {
        strlcpy(out, "FAT", out_len);
        return true;
    }
    if (sector_size >= 62 && memcmp(sector + 54, "FAT12   ", 8) == 0) {
        strlcpy(out, "FAT", out_len);
        return true;
    }
    return false;
}

static void usb_storage_probe_fs_type_locked(void)
{
    msc_host_device_info_t info;
    uint8_t *sector;
    bool found = false;

    s_status.fs_type[0] = '\0';
    if (!s_msc_device ||
        msc_host_get_device_info(s_msc_device, &info) != ESP_OK ||
        info.sector_size == 0 ||
        info.sector_size > 4096) {
        return;
    }

    sector = calloc(1, info.sector_size);
    if (!sector) {
        return;
    }
    if (scsi_cmd_read10(s_msc_device, sector, 0, 1, info.sector_size) == ESP_OK) {
        found = usb_storage_detect_fs_from_sector(sector,
                                                  info.sector_size,
                                                  s_status.fs_type,
                                                  sizeof(s_status.fs_type));
        if (!found && info.sector_size >= 512) {
            for (size_t i = 0; i < 4; i++) {
                size_t off = 446 + i * 16;
                uint32_t lba = (uint32_t)sector[off + 8] |
                               ((uint32_t)sector[off + 9] << 8) |
                               ((uint32_t)sector[off + 10] << 16) |
                               ((uint32_t)sector[off + 11] << 24);
                if (lba == 0) {
                    continue;
                }
                memset(sector, 0, info.sector_size);
                if (scsi_cmd_read10(s_msc_device, sector, lba, 1, info.sector_size) == ESP_OK &&
                    usb_storage_detect_fs_from_sector(sector,
                                                      info.sector_size,
                                                      s_status.fs_type,
                                                      sizeof(s_status.fs_type))) {
                    found = true;
                    break;
                }
            }
        }
    }
    free(sector);
    if (!found) {
        strlcpy(s_status.fs_type, "unknown", sizeof(s_status.fs_type));
    }
}

static void usb_storage_post(usb_storage_msg_t *msg)
{
    if (s_usb_queue && msg) {
        (void)xQueueSend(s_usb_queue, msg, 0);
    }
}

static void usb_storage_host_task(void *arg)
{
    (void)arg;
    bool free_requested = false;

    while (true) {
        uint32_t event_flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "USB host events failed: %s", esp_err_to_name(err));
            continue;
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            free_requested = true;
            err = usb_host_device_free_all();
            if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
                break;
            } else if (err != ESP_ERR_NOT_FINISHED) {
                ESP_LOGW(TAG, "free USB devices failed: %s", esp_err_to_name(err));
            }
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE && free_requested) {
            ESP_LOGI(TAG, "USB host all devices freed");
            break;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    esp_err_t err = usb_host_uninstall();
    usb_storage_msg_t msg = {
        .id = USB_STORAGE_MSG_HOST_DONE,
        .data.error = err,
    };
    usb_storage_post(&msg);
    vTaskDelete(NULL);
}

static void usb_storage_msc_event_cb(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    if (!event) {
        return;
    }

    usb_storage_msg_t msg = {0};
    if (event->event == MSC_DEVICE_CONNECTED) {
        msg.id = USB_STORAGE_MSG_DEVICE_CONNECTED;
        msg.data.address = event->device.address;
        usb_storage_post(&msg);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        msg.id = USB_STORAGE_MSG_DEVICE_DISCONNECTED;
        msg.data.device_handle = event->device.handle;
        usb_storage_post(&msg);
    } else {
        ESP_LOGW(TAG, "unsupported MSC event: %d", event->event);
    }
}

static esp_err_t usb_storage_start_host(void)
{
    if (s_host_installed) {
        return ESP_OK;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_RETURN_ON_ERROR(usb_host_install(&host_config), TAG, "install USB host");
    s_host_installed = true;

    if (xTaskCreate(usb_storage_host_task,
                    "usb_host",
                    USB_STORAGE_HOST_TASK_STACK,
                    NULL,
                    USB_STORAGE_HOST_TASK_PRIORITY,
                    &s_host_task) != pdPASS) {
        s_host_task = NULL;
        s_host_installed = false;
        (void)usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t usb_storage_start_msc(void)
{
    if (s_msc_installed) {
        return ESP_OK;
    }

    const msc_host_driver_config_t msc_config = {
        .create_backround_task = true,
        .task_priority = USB_STORAGE_MSC_TASK_PRIORITY,
        .stack_size = USB_STORAGE_MSC_TASK_STACK,
        .callback = usb_storage_msc_event_cb,
    };
    ESP_RETURN_ON_ERROR(msc_host_install(&msc_config), TAG, "install MSC host");
    s_msc_installed = true;
    return ESP_OK;
}

static void usb_storage_update_space_locked(void)
{
    uint64_t total = 0;
    uint64_t free = 0;

    if (esp_vfs_fat_info(USB_STORAGE_MOUNT_POINT, &total, &free) == ESP_OK) {
        s_status.total_bytes = total;
        s_status.free_bytes = free;
    } else {
        s_status.total_bytes = 0;
        s_status.free_bytes = 0;
    }
}

static void usb_storage_free_device(void)
{
    if (s_vfs_handle) {
        esp_err_t err = msc_host_vfs_unregister(s_vfs_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unregister USB VFS failed: %s", esp_err_to_name(err));
        }
        s_vfs_handle = NULL;
    }
    if (s_msc_device) {
        esp_err_t err = msc_host_uninstall_device(s_msc_device);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "uninstall USB MSC device failed: %s", esp_err_to_name(err));
        }
        s_msc_device = NULL;
    }

    usb_storage_lock();
    s_status.device_present = false;
    s_status.mounted = false;
    s_status.format_available = false;
    usb_storage_clear_fs_info_locked();
    usb_storage_unlock();
}

static esp_err_t usb_storage_mount_device(uint8_t address)
{
    esp_err_t err;
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = USB_STORAGE_MAX_FILES,
        .allocation_unit_size = 8192,
    };

    if (s_msc_device || s_vfs_handle) {
        usb_storage_free_device();
    }

    err = msc_host_install_device(address, &s_msc_device);
    if (err != ESP_OK) {
        usb_storage_lock();
        usb_storage_set_state_locked(USB_STORAGE_STATE_ERROR, err, "U盘设备打开失败");
        s_status.device_present = true;
        s_status.format_available = false;
        usb_storage_unlock();
        return err;
    }
    s_last_device_address = address;

    err = msc_host_vfs_register_allow_unmounted(s_msc_device,
                                                USB_STORAGE_MOUNT_POINT,
                                                &mount_config,
                                                &s_vfs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "register USB VFS failed: %s", esp_err_to_name(err));
        usb_storage_lock();
        usb_storage_probe_fs_type_locked();
        usb_storage_set_state_locked(USB_STORAGE_STATE_ERROR, err, "U盘未格式化或文件系统不支持");
        s_status.device_present = true;
        s_status.mounted = false;
        s_status.format_available = true;
        usb_storage_unlock();
        return err;
    }

    usb_storage_lock();
    s_status.device_present = true;
    s_status.mounted = true;
    s_status.format_available = false;
    usb_storage_update_space_locked();
    usb_storage_probe_fs_type_locked();
    usb_storage_set_state_locked(USB_STORAGE_STATE_MOUNTED, ESP_OK, "U盘已挂载");
    usb_storage_unlock();
    ESP_LOGI(TAG, "USB storage mounted at %s", USB_STORAGE_MOUNT_POINT);
    return ESP_OK;
}

static void usb_storage_stop_stack(void)
{
    bool waiting_host;

    usb_storage_free_device();

    if (s_msc_installed) {
        esp_err_t err = msc_host_uninstall();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "uninstall MSC host failed: %s", esp_err_to_name(err));
        }
        s_msc_installed = false;
    }

    waiting_host = s_host_installed;
    if (!waiting_host) {
        bsp_set_usb_5v_en(false);
    }
    usb_storage_lock();
    s_status.format_available = false;
    if (!waiting_host) {
        s_status.power_enabled = false;
    }
    if (waiting_host) {
        usb_storage_set_state_locked(USB_STORAGE_STATE_UNMOUNTING, ESP_OK, "正在卸载U盘");
    } else {
        usb_storage_set_state_locked(USB_STORAGE_STATE_OFF, ESP_OK, "U盘已卸载");
    }
    usb_storage_unlock();
}

static void usb_storage_handle_start(void)
{
    esp_err_t err;

    usb_storage_lock();
    if (s_status.state == USB_STORAGE_STATE_MOUNTED ||
        s_status.state == USB_STORAGE_STATE_WAITING_DEVICE ||
        s_status.state == USB_STORAGE_STATE_STARTING ||
        s_status.state == USB_STORAGE_STATE_UNMOUNTING ||
        s_status.state == USB_STORAGE_STATE_FORMATTING) {
        usb_storage_unlock();
        return;
    }
    s_status.power_enabled = true;
    usb_storage_set_state_locked(USB_STORAGE_STATE_STARTING, ESP_OK, "正在挂载U盘");
    usb_storage_unlock();

    bsp_set_usb_5v_en(true);
    vTaskDelay(pdMS_TO_TICKS(120));

    err = usb_storage_start_host();
    if (err == ESP_OK) {
        err = usb_storage_start_msc();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "USB storage start failed: %s", esp_err_to_name(err));
        if (s_msc_installed) {
            (void)msc_host_uninstall();
            s_msc_installed = false;
        }
        bsp_set_usb_5v_en(false);
        usb_storage_lock();
        s_status.power_enabled = false;
        s_status.format_available = false;
        usb_storage_set_state_locked(USB_STORAGE_STATE_ERROR, err, "U盘服务启动失败");
        usb_storage_unlock();
        return;
    }

    usb_storage_lock();
    usb_storage_set_state_locked(USB_STORAGE_STATE_WAITING_DEVICE, ESP_OK, "请插入FAT/FAT32/exFAT U盘");
    usb_storage_unlock();
}

static void usb_storage_handle_stop(void)
{
    usb_storage_lock();
    if (s_status.state == USB_STORAGE_STATE_OFF && !s_msc_installed && !s_host_installed) {
        usb_storage_unlock();
        return;
    }
    usb_storage_set_state_locked(USB_STORAGE_STATE_UNMOUNTING, ESP_OK, "正在卸载U盘");
    usb_storage_unlock();

    usb_storage_stop_stack();
}

static void usb_storage_handle_format(usb_storage_format_t format)
{
    esp_err_t err;
    esp_err_t mount_err;
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = USB_STORAGE_MAX_FILES,
        .allocation_unit_size = 8192,
    };
    msc_host_vfs_format_t vfs_format =
        format == USB_STORAGE_FORMAT_EXFAT ? MSC_HOST_VFS_FORMAT_EXFAT : MSC_HOST_VFS_FORMAT_FAT;

    usb_storage_lock();
    if (!s_msc_device || !s_vfs_handle || (!s_status.mounted && !s_status.format_available)) {
        usb_storage_set_state_locked(USB_STORAGE_STATE_ERROR, ESP_ERR_INVALID_STATE, "当前U盘无法格式化");
        usb_storage_unlock();
        return;
    }
    usb_storage_set_state_locked(USB_STORAGE_STATE_FORMATTING, ESP_OK, "正在格式化U盘");
    s_status.mounted = false;
    s_status.format_available = false;
    usb_storage_unlock();

    err = msc_host_vfs_format_ex(s_msc_device, &mount_config, s_vfs_handle, vfs_format);
    if (err != ESP_OK) {
        usb_storage_lock();
        usb_storage_set_state_locked(USB_STORAGE_STATE_ERROR, err, "U盘格式化失败");
        s_status.device_present = true;
        s_status.mounted = false;
        s_status.format_available = true;
        usb_storage_unlock();
        ESP_LOGW(TAG, "USB storage format %s failed: %s",
                 usb_storage_format_name(format),
                 esp_err_to_name(err));
        return;
    }

    if (s_vfs_handle) {
        err = msc_host_vfs_unregister(s_vfs_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unregister USB VFS after format failed: %s", esp_err_to_name(err));
        }
        s_vfs_handle = NULL;
    }

    mount_err = msc_host_vfs_register_allow_unmounted(s_msc_device,
                                                      USB_STORAGE_MOUNT_POINT,
                                                      &mount_config,
                                                      &s_vfs_handle);

    usb_storage_lock();
    if (mount_err == ESP_OK) {
        s_status.device_present = true;
        s_status.mounted = true;
        s_status.format_available = false;
        usb_storage_update_space_locked();
        usb_storage_probe_fs_type_locked();
        snprintf(s_status.message,
                 sizeof(s_status.message),
                 TRS("U盘已格式化为%s"),
                 usb_storage_format_name(format));
        s_status.state = USB_STORAGE_STATE_MOUNTED;
        s_status.last_error = ESP_OK;
        s_status.generation++;
    } else {
        s_status.device_present = true;
        s_status.mounted = false;
        s_status.format_available = true;
        usb_storage_set_state_locked(USB_STORAGE_STATE_ERROR, mount_err, "U盘格式化失败");
    }
    usb_storage_unlock();
}

static void usb_storage_manager_task(void *arg)
{
    (void)arg;

    while (true) {
        usb_storage_msg_t msg;
        if (xQueueReceive(s_usb_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (msg.id) {
        case USB_STORAGE_MSG_START:
            usb_storage_handle_start();
            break;
        case USB_STORAGE_MSG_STOP:
            usb_storage_handle_stop();
            break;
        case USB_STORAGE_MSG_FORMAT:
            usb_storage_handle_format(msg.data.format);
            break;
        case USB_STORAGE_MSG_DEVICE_CONNECTED: {
            usb_storage_lock();
            bool accept_device = s_status.state == USB_STORAGE_STATE_WAITING_DEVICE ||
                                 s_status.state == USB_STORAGE_STATE_STARTING;
            usb_storage_unlock();
            if (accept_device) {
                (void)usb_storage_mount_device(msg.data.address);
            }
            break;
        }
        case USB_STORAGE_MSG_DEVICE_DISCONNECTED:
            if (!s_msc_device || msg.data.device_handle == s_msc_device) {
                usb_storage_free_device();
                usb_storage_lock();
                if (s_status.state != USB_STORAGE_STATE_OFF &&
                    s_status.state != USB_STORAGE_STATE_UNMOUNTING) {
                    usb_storage_set_state_locked(USB_STORAGE_STATE_WAITING_DEVICE, ESP_OK, "U盘已移除");
                }
                s_status.format_available = false;
                usb_storage_unlock();
            }
            break;
        case USB_STORAGE_MSG_HOST_DONE:
            if (msg.data.error != ESP_OK) {
                ESP_LOGW(TAG, "uninstall USB host failed: %s", esp_err_to_name(msg.data.error));
            }
            bsp_set_usb_5v_en(false);
            usb_storage_lock();
            s_host_installed = false;
            s_host_task = NULL;
            s_status.power_enabled = false;
            if (s_status.state == USB_STORAGE_STATE_UNMOUNTING) {
                usb_storage_set_state_locked(USB_STORAGE_STATE_OFF, ESP_OK, "U盘已卸载");
            } else if (s_status.state == USB_STORAGE_STATE_OFF) {
                s_status.generation++;
            }
            usb_storage_unlock();
            break;
        default:
            break;
        }
    }
}

static esp_err_t usb_storage_ensure_service(void)
{
    if (!s_usb_lock) {
        s_usb_lock = xSemaphoreCreateMutex();
        if (!s_usb_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_usb_io_lock) {
        s_usb_io_lock = xSemaphoreCreateMutex();
        if (!s_usb_io_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_usb_queue) {
        s_usb_queue = xQueueCreate(USB_STORAGE_QUEUE_LEN, sizeof(usb_storage_msg_t));
        if (!s_usb_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_mgr_task) {
        if (xTaskCreate(usb_storage_manager_task,
                        "usb_storage",
                        USB_STORAGE_MGR_TASK_STACK,
                        NULL,
                        USB_STORAGE_MGR_TASK_PRIORITY,
                        &s_mgr_task) != pdPASS) {
            s_mgr_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t usb_storage_mount(void)
{
    esp_err_t err = usb_storage_ensure_service();
    if (err != ESP_OK) {
        return err;
    }

    usb_storage_msg_t msg = {.id = USB_STORAGE_MSG_START};
    if (xQueueSend(s_usb_queue, &msg, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t usb_storage_unmount(void)
{
    esp_err_t err = usb_storage_ensure_service();
    if (err != ESP_OK) {
        return err;
    }

    usb_storage_msg_t msg = {.id = USB_STORAGE_MSG_STOP};
    if (xQueueSend(s_usb_queue, &msg, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t usb_storage_format(usb_storage_format_t format)
{
    esp_err_t err = usb_storage_ensure_service();
    if (err != ESP_OK) {
        return err;
    }
    if (format != USB_STORAGE_FORMAT_FAT32 && format != USB_STORAGE_FORMAT_EXFAT) {
        return ESP_ERR_INVALID_ARG;
    }

    usb_storage_msg_t msg = {
        .id = USB_STORAGE_MSG_FORMAT,
        .data.format = format,
    };
    if (xQueueSend(s_usb_queue, &msg, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool usb_storage_is_mounted(void)
{
    bool mounted;

    usb_storage_lock();
    mounted = s_status.mounted;
    usb_storage_unlock();
    return mounted;
}

const char *usb_storage_get_mount_point(void)
{
    return USB_STORAGE_MOUNT_POINT;
}

void usb_storage_get_status(usb_storage_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    usb_storage_lock();
    *out_status = s_status;
    if (s_status.mounted) {
        bool can_probe = s_usb_io_lock &&
                         xSemaphoreTake(s_usb_io_lock, 0) == pdTRUE;
        if (can_probe) {
            usb_storage_update_space_locked();
            xSemaphoreGive(s_usb_io_lock);
            out_status->total_bytes = s_status.total_bytes;
            out_status->free_bytes = s_status.free_bytes;
        }
    }
    usb_storage_unlock();
}

esp_err_t usb_storage_begin_io(uint32_t timeout_ms)
{
    esp_err_t err = usb_storage_ensure_service();
    if (err != ESP_OK) {
        return err;
    }
    TickType_t timeout = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_usb_io_lock, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void usb_storage_end_io(void)
{
    if (s_usb_io_lock) {
        xSemaphoreGive(s_usb_io_lock);
    }
}
