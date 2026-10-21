#include "storage/tf_usb_disk_mode.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "storage/tf_card.h"
#include "storage/usb_storage.h"

#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MSC_ENABLED
#include "audio/audio_player_service.h"
#include "bsp/m5stack_tab5.h"
#include "file_manager/web_file_manager.h"
#include "file_manager/file_manager_utils.h"
#include "screenshot/screenshot_service.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "hal/usb_wrap_ll.h"
#endif
#endif

static const char *TAG = "tf_usb_disk";

enum {
    TF_USB_DISK_QUEUE_LEN = 4,
    TF_USB_DISK_TASK_STACK = 8192,
    TF_USB_DISK_TASK_PRIORITY = 5,
    TF_USB_DISK_SCREENSHOT_WAIT_MS = 1800,
};

typedef enum {
    TF_USB_DISK_MSG_START,
    TF_USB_DISK_MSG_STOP,
} tf_usb_disk_msg_id_t;

typedef struct {
    tf_usb_disk_msg_id_t id;
} tf_usb_disk_msg_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static tf_usb_disk_mode_status_t s_status = {
    .state = TF_USB_DISK_STATE_OFF,
    .message = "USB磁盘模式未开启",
};

static void tf_usb_disk_lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void tf_usb_disk_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static bool tf_usb_disk_state_blocks(tf_usb_disk_mode_state_t state)
{
    return state == TF_USB_DISK_STATE_STARTING ||
           state == TF_USB_DISK_STATE_WAITING_HOST ||
           state == TF_USB_DISK_STATE_ACTIVE ||
           state == TF_USB_DISK_STATE_STOPPING;
}

static void tf_usb_disk_set_state_locked(tf_usb_disk_mode_state_t state,
                                         esp_err_t err,
                                         const char *message)
{
    s_status.state = state;
    s_status.active = (state == TF_USB_DISK_STATE_ACTIVE);
    if (state == TF_USB_DISK_STATE_OFF || state == TF_USB_DISK_STATE_ERROR) {
        s_status.host_attached = false;
    }
    s_status.blocks_tf_access = tf_usb_disk_state_blocks(state);
    s_status.last_error = err;
    if (message) {
        strlcpy(s_status.message, message, sizeof(s_status.message));
    }
    s_status.generation++;
}

const char *tf_usb_disk_mode_state_text(tf_usb_disk_mode_state_t state)
{
    switch (state) {
    case TF_USB_DISK_STATE_STARTING:
        return "正在开启USB磁盘模式";
    case TF_USB_DISK_STATE_WAITING_HOST:
        return "USB磁盘模式已准备好，等待电脑识别TF卡";
    case TF_USB_DISK_STATE_ACTIVE:
        return "USB磁盘模式已开启：电脑正在独占访问TF卡";
    case TF_USB_DISK_STATE_STOPPING:
        return "正在关闭USB磁盘模式";
    case TF_USB_DISK_STATE_ERROR:
        return "USB磁盘模式启动失败";
    case TF_USB_DISK_STATE_OFF:
    default:
        return "USB磁盘模式未开启";
    }
}

#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MSC_ENABLED

static sdmmc_card_t *s_raw_card;
static tinyusb_msc_storage_handle_t s_storage_handle;
static bool s_driver_installed;
static bool s_msc_driver_installed;
#if CONFIG_IDF_TARGET_ESP32P4
static bool s_usb_phy_remapped;
static uint8_t s_usb_phy_prev_sw_hw;
static uint8_t s_usb_phy_prev_sw_sel;
#endif

/* TinyUSB MSC descriptors. */
#define TF_USB_DISK_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum {
    TF_USB_DISK_ITF_NUM_MSC = 0,
    TF_USB_DISK_ITF_NUM_TOTAL,
};

enum {
    TF_USB_DISK_EDPT_MSC_OUT = 0x01,
    TF_USB_DISK_EDPT_MSC_IN = 0x81,
};

static tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(s_device_desc),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_msc_fs_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1,
                          TF_USB_DISK_ITF_NUM_TOTAL,
                          0,
                          TF_USB_DISK_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          100),
    TUD_MSC_DESCRIPTOR(TF_USB_DISK_ITF_NUM_MSC,
                       0,
                       TF_USB_DISK_EDPT_MSC_OUT,
                       TF_USB_DISK_EDPT_MSC_IN,
                       64),
};

static const char *s_string_desc[] = {
    (const char[]){0x09, 0x04},
    "M5Stack",
    "Tab5 TF Card",
    "TAB5TF",
};

static void tf_usb_disk_select_usb_c_phy(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    /*
     * Tab5 routes the USB-C device D+/D- lines to GPIO25/GPIO24 (FSLS PHY0).
     * ESP-IDF's P4 OTG FS controller defaults to PHY1 (GPIO27/GPIO26), so the
     * host never sees a pull-up unless USB_WRAP is temporarily mapped to PHY0.
     */
    if (!s_usb_phy_remapped) {
        s_usb_phy_prev_sw_hw = (uint8_t)LP_SYS.usb_ctrl.sw_hw_usb_phy_sel;
        s_usb_phy_prev_sw_sel = (uint8_t)LP_SYS.usb_ctrl.sw_usb_phy_sel;
    }
    usb_wrap_ll_phy_select(&USB_WRAP, 0);
    s_usb_phy_remapped = true;
    ESP_LOGI(TAG,
             "USB FS PHY mapping for USB-C disk mode: sw_hw=%u sw_sel=%u "
             "prev_sw_hw=%u prev_sw_sel=%u "
             "(USB_WRAP/OTG1.1 -> PHY0 GPIO24/25, USJ -> PHY1 GPIO26/27)",
             (unsigned)LP_SYS.usb_ctrl.sw_hw_usb_phy_sel,
             (unsigned)LP_SYS.usb_ctrl.sw_usb_phy_sel,
             (unsigned)s_usb_phy_prev_sw_hw,
             (unsigned)s_usb_phy_prev_sw_sel);
#endif
}

static void tf_usb_disk_restore_usb_serial_jtag_phy(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (!s_usb_phy_remapped) {
        return;
    }
    LP_SYS.usb_ctrl.sw_usb_phy_sel = s_usb_phy_prev_sw_sel ? 1 : 0;
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = s_usb_phy_prev_sw_hw ? 1 : 0;
    s_usb_phy_remapped = false;
    ESP_LOGI(TAG,
             "USB FS PHY mapping restored: sw_hw=%u sw_sel=%u",
             (unsigned)LP_SYS.usb_ctrl.sw_hw_usb_phy_sel,
             (unsigned)LP_SYS.usb_ctrl.sw_usb_phy_sel);
#endif
}

static void tf_usb_disk_device_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (!event) {
        return;
    }

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(TAG, "TinyUSB attached by host: rhport=%u", (unsigned)event->rhport);
        tf_usb_disk_lock();
        s_status.host_attached = true;
        tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_ACTIVE,
                                     ESP_OK,
                                     "USB磁盘模式已开启：电脑正在独占访问TF卡");
        tf_usb_disk_unlock();
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(TAG, "TinyUSB detached by host: rhport=%u", (unsigned)event->rhport);
        tf_usb_disk_lock();
        s_status.host_attached = false;
        if (s_status.state == TF_USB_DISK_STATE_ACTIVE) {
            tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_WAITING_HOST,
                                         ESP_OK,
                                         "USB磁盘模式已准备好，等待电脑识别TF卡");
        } else {
            s_status.generation++;
        }
        tf_usb_disk_unlock();
        break;
    default:
        ESP_LOGI(TAG,
                 "TinyUSB device event: id=%d rhport=%u",
                 (int)event->id,
                 (unsigned)event->rhport);
        break;
    }
}

static void tf_usb_disk_storage_cb(tinyusb_msc_storage_handle_t handle,
                                   tinyusb_msc_event_t *event,
                                   void *arg)
{
    (void)handle;
    (void)arg;
    if (!event) {
        return;
    }

    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
        ESP_LOGI(TAG, "USB host mount start");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        ESP_LOGI(TAG,
                 "USB storage mount complete: mount_point=%s",
                 event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB ? "USB" : "APP");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGW(TAG, "USB storage mount failed or format required");
        break;
    default:
        break;
    }
}

static esp_err_t tf_usb_disk_install_driver(void)
{
    tinyusb_config_t tusb_cfg = TINYUSB_CONFIG_FULL_SPEED(tf_usb_disk_device_event_cb, NULL);
    const uint8_t pi4ioe2_in = bsp_pi4ioe2_input_state();

    tusb_cfg.task = TINYUSB_TASK_CUSTOM(8192, 9, TINYUSB_DEFAULT_TASK_AFFINITY);
    ESP_LOGI(TAG,
             "install TinyUSB MSC device prepare: port=%d speed=full msc_buf=%d usb_task_prio=9 usb_c_detect=%d pi4ioe2_in=0x%02x bit6=%d",
             (int)tusb_cfg.port,
             CONFIG_TINYUSB_MSC_BUFSIZE,
             bsp_usb_c_detect() ? 1 : 0,
             (unsigned)pi4ioe2_in,
             (pi4ioe2_in & (1U << 6)) ? 1 : 0);
    ESP_LOGI(TAG, "switching USB-C PHY to TinyUSB device; USB serial may disconnect now");
    tf_usb_disk_select_usb_c_phy();
    tusb_cfg.descriptor.device = &s_device_desc;
    tusb_cfg.descriptor.full_speed_config = s_msc_fs_desc;
    tusb_cfg.descriptor.string = s_string_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]);

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    ESP_LOGI(TAG, "tinyusb_driver_install returned %s", esp_err_to_name(err));
    if (err == ESP_OK) {
        s_driver_installed = true;
    }
    return err;
}

static esp_err_t tf_usb_disk_create_storage(sdmmc_card_t *card)
{
    tinyusb_msc_driver_config_t driver_cfg = {
        .user_flags.auto_mount_off = 1,
        .callback = tf_usb_disk_storage_cb,
    };
    tinyusb_msc_storage_config_t storage_cfg = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = {
            .base_path = NULL,
            .config.max_files = 5,
            .format_flags = 0,
        },
    };

    esp_err_t err = tinyusb_msc_install_driver(&driver_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tinyusb_msc_install_driver failed: %s", esp_err_to_name(err));
        return err;
    }
    s_msc_driver_installed = true;

    storage_cfg.medium.card = card;
    err = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &s_storage_handle);
    ESP_LOGI(TAG, "tinyusb_msc_new_storage_sdmmc returned %s handle=%p",
             esp_err_to_name(err),
             s_storage_handle);
    return err;
}

static void tf_usb_disk_cleanup_usb_stack(void)
{
    if (s_driver_installed) {
        esp_err_t err = tinyusb_driver_uninstall();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "uninstall TinyUSB driver failed: %s", esp_err_to_name(err));
        }
        s_driver_installed = false;
    }
    if (s_storage_handle) {
        esp_err_t err = tinyusb_msc_delete_storage(s_storage_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "delete TinyUSB MSC storage failed: %s", esp_err_to_name(err));
        }
        s_storage_handle = NULL;
    }
    if (s_msc_driver_installed) {
        esp_err_t err = tinyusb_msc_uninstall_driver();
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "uninstall TinyUSB MSC driver failed: %s", esp_err_to_name(err));
        }
        s_msc_driver_installed = false;
    }
    tf_usb_disk_restore_usb_serial_jtag_phy();
}

static void tf_usb_disk_deinit_raw_card(void)
{
    if (s_raw_card) {
        esp_err_t err = tf_card_deinit_raw_for_usb_disk(s_raw_card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "raw TF card deinit failed: %s", esp_err_to_name(err));
        }
        s_raw_card = NULL;
    }
}

static bool tf_usb_disk_external_usb_busy(void)
{
    usb_storage_status_t usb_status;

    memset(&usb_status, 0, sizeof(usb_status));
    usb_storage_get_status(&usb_status);
    return usb_status.state != USB_STORAGE_STATE_OFF ||
           usb_status.power_enabled ||
           usb_status.device_present ||
           usb_status.mounted;
}

static esp_err_t tf_usb_disk_preflight(void)
{
    if (!tf_card_is_vfs_mounted_for_usb_disk()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (tf_usb_disk_external_usb_busy()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (web_file_manager_is_upload_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    screenshot_service_refresh_enabled();
    int elapsed = 0;
    while (screenshot_service_is_capture_busy() && elapsed < TF_USB_DISK_SCREENSHOT_WAIT_MS) {
        vTaskDelay(pdMS_TO_TICKS(60));
        elapsed += 60;
    }
    if (screenshot_service_is_capture_busy()) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void tf_usb_disk_stop_tf_audio(void)
{
    audio_player_status_t audio_status;

    if (audio_player_get_status(&audio_status) == ESP_OK &&
        audio_status.path[0] &&
        file_manager_path_in_root(audio_status.path, tf_card_get_mount_point())) {
        ESP_LOGI(TAG, "stopping TF audio before USB disk mode: %s", audio_status.path);
        (void)audio_player_stop();
    }
}

static const char *tf_usb_disk_preflight_message(void)
{
    if (!tf_card_is_vfs_mounted_for_usb_disk()) {
        return "TF卡未挂载，无法开启USB磁盘模式";
    }
    if (tf_usb_disk_external_usb_busy()) {
        return "请先卸载外接U盘，USB磁盘模式共享的是TF卡，不是外接U盘。";
    }
    if (web_file_manager_is_upload_active()) {
        return "在线文件管理器正在上传，请稍后再试";
    }
    if (screenshot_service_is_capture_busy()) {
        return "截屏正在写入TF卡，请稍后再试";
    }
    return "USB磁盘模式启动失败";
}

static void tf_usb_disk_handle_start(void)
{
    esp_err_t err;

    tf_usb_disk_lock();
    if (tf_usb_disk_state_blocks(s_status.state)) {
        tf_usb_disk_unlock();
        return;
    }
    tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_STARTING,
                                 ESP_OK,
                                 "正在开启USB磁盘模式");
    tf_usb_disk_unlock();
    tf_card_set_usb_disk_access_blocked(true);

    err = tf_usb_disk_preflight();
    if (err != ESP_OK) {
        const char *message = tf_usb_disk_preflight_message();
        ESP_LOGW(TAG, "USB disk mode preflight failed: %s (%s)", message, esp_err_to_name(err));
        tf_card_set_usb_disk_access_blocked(false);
        tf_usb_disk_lock();
        tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_ERROR, err, message);
        tf_usb_disk_unlock();
        return;
    }

    screenshot_service_refresh_enabled();
    tf_usb_disk_stop_tf_audio();
    if (web_file_manager_is_running()) {
        ESP_LOGI(TAG, "stopping web file manager before USB disk mode");
        web_file_manager_stop();
    }

    ESP_LOGI(TAG, "starting USB disk mode");
    const uint8_t pi4ioe2_in = bsp_pi4ioe2_input_state();
    ESP_LOGI(TAG,
             "USB-C detect before device mode: %d pi4ioe2_in=0x%02x bit6=%d; external USB state is idle",
             bsp_usb_c_detect() ? 1 : 0,
             (unsigned)pi4ioe2_in,
             (pi4ioe2_in & (1U << 6)) ? 1 : 0);
    err = tf_card_unmount_for_usb_disk();
    if (err == ESP_OK) {
        err = tf_card_init_raw_for_usb_disk(&s_raw_card);
    }
    if (err == ESP_OK) {
        err = tf_usb_disk_create_storage(s_raw_card);
    }
    if (err == ESP_OK) {
        err = tf_usb_disk_install_driver();
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "USB disk mode start failed: %s", esp_err_to_name(err));
        tf_usb_disk_cleanup_usb_stack();
        tf_usb_disk_deinit_raw_card();
        tf_card_set_usb_disk_access_blocked(false);
        esp_err_t remount_err = tf_card_mount_after_usb_disk();
        screenshot_service_refresh_enabled();

        tf_usb_disk_lock();
        tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_ERROR,
                                     err,
                                     remount_err == ESP_OK
                                         ? "USB磁盘模式启动失败"
                                         : "USB磁盘模式已关闭，但TF卡恢复挂载失败，请重新插卡或重启。");
        tf_usb_disk_unlock();
        return;
    }

    tf_usb_disk_lock();
    tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_WAITING_HOST,
                                 ESP_OK,
                                 "USB磁盘模式已准备好，等待电脑识别TF卡");
    tf_usb_disk_unlock();
    ESP_LOGI(TAG, "USB disk mode waiting for host enumeration");
}

static void tf_usb_disk_handle_stop(void)
{
    tf_usb_disk_lock();
    if (s_status.state == TF_USB_DISK_STATE_OFF ||
        s_status.state == TF_USB_DISK_STATE_STOPPING) {
        tf_usb_disk_unlock();
        return;
    }
    tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_STOPPING,
                                 ESP_OK,
                                 "正在关闭USB磁盘模式");
    tf_usb_disk_unlock();

    ESP_LOGI(TAG, "stopping USB disk mode");
    tf_usb_disk_cleanup_usb_stack();
    tf_usb_disk_deinit_raw_card();
    tf_card_set_usb_disk_access_blocked(false);
    esp_err_t remount_err = tf_card_mount_after_usb_disk();
    screenshot_service_refresh_enabled();

    tf_usb_disk_lock();
    if (remount_err == ESP_OK) {
        tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_OFF,
                                     ESP_OK,
                                     "USB磁盘模式已关闭，TF卡已恢复设备端访问");
    } else {
        tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_ERROR,
                                     remount_err,
                                     "USB磁盘模式已关闭，但TF卡恢复挂载失败，请重新插卡或重启。");
    }
    tf_usb_disk_unlock();
}

#else

static void tf_usb_disk_handle_start(void)
{
    const char *message =
#if SOC_USB_OTG_SUPPORTED
        "固件未启用USB磁盘模式";
#else
        "当前芯片不支持USB OTG磁盘模式";
#endif
    tf_usb_disk_lock();
    tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_ERROR, ESP_ERR_NOT_SUPPORTED, message);
    tf_usb_disk_unlock();
    ESP_LOGW(TAG, "%s", message);
}

static void tf_usb_disk_handle_stop(void)
{
    tf_usb_disk_lock();
    tf_usb_disk_set_state_locked(TF_USB_DISK_STATE_OFF, ESP_OK, "USB磁盘模式未开启");
    tf_usb_disk_unlock();
}

#endif

static void tf_usb_disk_task(void *arg)
{
    (void)arg;
    tf_usb_disk_msg_t msg;

    while (true) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (msg.id) {
        case TF_USB_DISK_MSG_START:
            tf_usb_disk_handle_start();
            break;
        case TF_USB_DISK_MSG_STOP:
            tf_usb_disk_handle_stop();
            break;
        default:
            break;
        }
    }
}

static esp_err_t tf_usb_disk_ensure_service(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_queue) {
        s_queue = xQueueCreate(TF_USB_DISK_QUEUE_LEN, sizeof(tf_usb_disk_msg_t));
        if (!s_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_task) {
        if (xTaskCreate(tf_usb_disk_task,
                        "tf_usb_disk",
                        TF_USB_DISK_TASK_STACK,
                        NULL,
                        TF_USB_DISK_TASK_PRIORITY,
                        &s_task) != pdPASS) {
            s_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t tf_usb_disk_mode_start(void)
{
    esp_err_t err = tf_usb_disk_ensure_service();
    if (err != ESP_OK) {
        return err;
    }

    tf_usb_disk_msg_t msg = {.id = TF_USB_DISK_MSG_START};
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t tf_usb_disk_mode_stop(void)
{
    esp_err_t err = tf_usb_disk_ensure_service();
    if (err != ESP_OK) {
        return err;
    }

    tf_usb_disk_msg_t msg = {.id = TF_USB_DISK_MSG_STOP};
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool tf_usb_disk_mode_is_active(void)
{
    bool active;

    tf_usb_disk_lock();
    active = s_status.active;
    tf_usb_disk_unlock();
    return active;
}

bool tf_usb_disk_mode_blocks_tf_access(void)
{
    bool blocks;

    tf_usb_disk_lock();
    blocks = s_status.blocks_tf_access;
    tf_usb_disk_unlock();
    return blocks;
}

void tf_usb_disk_mode_get_status(tf_usb_disk_mode_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    tf_usb_disk_lock();
    *out_status = s_status;
    tf_usb_disk_unlock();
}
