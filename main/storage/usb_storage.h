#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_STORAGE_MOUNT_POINT "/usb0"

typedef enum {
    USB_STORAGE_STATE_OFF = 0,
    USB_STORAGE_STATE_STARTING,
    USB_STORAGE_STATE_WAITING_DEVICE,
    USB_STORAGE_STATE_MOUNTED,
    USB_STORAGE_STATE_UNMOUNTING,
    USB_STORAGE_STATE_FORMATTING,
    USB_STORAGE_STATE_ERROR,
} usb_storage_state_t;

typedef enum {
    USB_STORAGE_FORMAT_FAT32 = 0,
    USB_STORAGE_FORMAT_EXFAT,
} usb_storage_format_t;

typedef struct {
    usb_storage_state_t state;
    bool power_enabled;
    bool device_present;
    bool mounted;
    bool format_available;
    uint32_t generation;
    char mount_point[16];
    char fs_type[16];
    char message[96];
    uint64_t total_bytes;
    uint64_t free_bytes;
    esp_err_t last_error;
} usb_storage_status_t;

esp_err_t usb_storage_mount(void);
esp_err_t usb_storage_unmount(void);
esp_err_t usb_storage_format(usb_storage_format_t format);
bool usb_storage_is_mounted(void);
const char *usb_storage_get_mount_point(void);
void usb_storage_get_status(usb_storage_status_t *out_status);
esp_err_t usb_storage_begin_io(uint32_t timeout_ms);
void usb_storage_end_io(void);

#ifdef __cplusplus
}
#endif
