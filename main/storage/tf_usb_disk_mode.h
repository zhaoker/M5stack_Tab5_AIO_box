#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TF_USB_DISK_STATE_OFF = 0,
    TF_USB_DISK_STATE_STARTING,
    TF_USB_DISK_STATE_WAITING_HOST,
    TF_USB_DISK_STATE_ACTIVE,
    TF_USB_DISK_STATE_STOPPING,
    TF_USB_DISK_STATE_ERROR,
} tf_usb_disk_mode_state_t;

typedef struct {
    tf_usb_disk_mode_state_t state;
    bool active;
    bool host_attached;
    bool blocks_tf_access;
    uint32_t generation;
    esp_err_t last_error;
    char message[128];
} tf_usb_disk_mode_status_t;

esp_err_t tf_usb_disk_mode_start(void);
esp_err_t tf_usb_disk_mode_stop(void);
bool tf_usb_disk_mode_is_active(void);
bool tf_usb_disk_mode_blocks_tf_access(void);
void tf_usb_disk_mode_get_status(tf_usb_disk_mode_status_t *out_status);
const char *tf_usb_disk_mode_state_text(tf_usb_disk_mode_state_t state);

#ifdef __cplusplus
}
#endif
