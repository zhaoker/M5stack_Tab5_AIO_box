#include "screensaver/screensaver_service.h"

#include "app_config.h"
#include "bsp/m5stack_tab5.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_random.h"
#include "file_manager/file_manager_exif.h"
#include "file_manager/file_manager_media.h"
#include "fonts/MI_font_20.h"
#include "fonts/MI_font_120_time.h"
#include "fonts/MI_font_160_time.h"
#include "fonts/MI_font_200_time.h"
#include "fonts/MI_font_40_time.h"
#include "fonts/MI_font_60_time.h"
#include "fonts/MI_font_80_time.h"
#include "i18n/app_i18n.h"
#include "settings/app_settings.h"
#include "theme.hpp"
#include "ui_terminal.h"

#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "screensaver";

enum {
    SS_TIMER_MS = 250,
    SS_IMAGE_MAX = 256,
    SS_PATH_MAX = 192,
    SS_TITLE_MAX = 192,
    SS_COMPONENT_MARGIN = 12,
    SS_SCAN_DEPTH_MAX = 6,
    SS_LOADER_STACK = 8192,
    SS_LOADER_PRIORITY = 2,
};

typedef struct {
    char path[SS_PATH_MAX];
} screensaver_image_entry_t;

static lv_timer_t *s_timer;
static lv_timer_t *s_switch_timer;
static lv_timer_t *s_clock_timer;
static lv_obj_t *s_overlay;
static lv_obj_t *s_wallpaper;
static lv_obj_t *s_title_panel;
static lv_obj_t *s_title_label;
static lv_obj_t *s_time_wrap;
static lv_obj_t *s_second_wrap;
static lv_obj_t *s_date_wrap;
static lv_obj_t *s_lunar_wrap;
static lv_obj_t *s_time_label;
static lv_obj_t *s_second_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_lunar_label;
static lv_obj_t *s_time_shadow_label;
static lv_obj_t *s_second_shadow_label;
static lv_obj_t *s_date_shadow_label;
static lv_obj_t *s_lunar_shadow_label;
static lv_obj_t *s_wait_overlay;
static lv_obj_t *s_wait_label;
static file_manager_image_t s_image;
static char s_lv_image_path[SS_PATH_MAX + 4];
static uint32_t s_current_image_w;
static uint32_t s_current_image_h;
static TaskHandle_t s_loader_task;
static volatile bool s_loader_busy;
static volatile bool s_loader_task_running;
static volatile bool s_loader_async_pending;
static volatile bool s_loader_cancel;
static volatile bool s_loader_pending_next;
static QueueHandle_t s_loader_result_queue;
static uint32_t s_loader_generation;
static char s_loader_path[SS_PATH_MAX];
static time_t s_clock_last_drawn;
static screensaver_image_entry_t *s_images;
static size_t s_image_count;
static size_t s_image_pos;
static uint32_t s_last_activity_tick;
static bool s_active;
static bool s_blank_mode;
static bool s_dragging_component;
static bool s_drag_click_suppressed;
static lv_obj_t *s_drag_obj;
static app_screensaver_component_t s_drag_component;
static lv_point_t s_drag_start_point;
static lv_coord_t s_drag_start_x;
static lv_coord_t s_drag_start_y;

extern esp_lcd_touch_handle_t bsp_display_get_touch_handle(void);

static void screensaver_exit(void);
static void screensaver_enter(bool manual);
static void screensaver_load_current_image(bool show_wait);
static void screensaver_cancel_loader(bool wait);
static void screensaver_release_async_result(void *data);
static bool screensaver_ensure_loader_queue(void);
static void screensaver_poll_loader_result(void);
static void screensaver_fit_image(void);
static void screensaver_update_clock(void);
static void screensaver_update_clock_force(void);
static void ss_set_wait_visible(bool visible, const char *message);
static void ss_resize_title_panel(void);
static void ss_set_component_hidden(lv_obj_t *obj,
                                    app_screensaver_component_t component,
                                    bool hidden_by_content);
static void screensaver_timer_cb(lv_timer_t *timer);
static void ss_activity_event_cb(lv_event_t *e);

static bool screensaver_ensure_image_list(void)
{
    if (s_images) {
        return true;
    }
    s_images = (screensaver_image_entry_t *)heap_caps_calloc(SS_IMAGE_MAX,
                                                             sizeof(s_images[0]),
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_images) {
        ESP_LOGI(TAG,
                 "screensaver image list allocated in PSRAM: %u bytes",
                 (unsigned)(SS_IMAGE_MAX * sizeof(s_images[0])));
        return true;
    }
    s_images = (screensaver_image_entry_t *)heap_caps_calloc(SS_IMAGE_MAX,
                                                             sizeof(s_images[0]),
                                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_images) {
        ESP_LOGW(TAG,
                 "screensaver image list allocated in internal RAM fallback: %u bytes",
                 (unsigned)(SS_IMAGE_MAX * sizeof(s_images[0])));
        return true;
    }
    ESP_LOGE(TAG,
             "screensaver image list allocation failed: %u bytes",
             (unsigned)(SS_IMAGE_MAX * sizeof(s_images[0])));
    return false;
}

static bool screensaver_touch_pressed(void)
{
    esp_lcd_touch_handle_t tp = bsp_display_get_touch_handle();
    esp_lcd_touch_point_data_t point = {0};
    uint8_t point_count = 0;

    if (!tp) {
        return false;
    }
    return esp_lcd_touch_read_data(tp) == ESP_OK &&
           esp_lcd_touch_get_data(tp, &point, &point_count, 1) == ESP_OK &&
           point_count > 0;
}

static bool screensaver_ssh_block_active(void)
{
    return app_settings_get_screensaver_disable_during_ssh() &&
           ui_terminal_has_active_ssh_session();
}

#include "screensaver_lunar.inc"
#include "screensaver_image_list.inc"
#include "screensaver_ui.inc"

esp_err_t screensaver_service_init(void)
{
    if (s_timer) {
        return ESP_OK;
    }
    s_last_activity_tick = lv_tick_get();
    s_timer = lv_timer_create(screensaver_timer_cb, SS_TIMER_MS, NULL);
    if (!s_timer) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void screensaver_service_notify_activity(void)
{
    if (s_active) {
        if (bsp_display_lock(1000)) {
            screensaver_exit();
            bsp_display_unlock();
        } else {
            s_last_activity_tick = lv_tick_get();
        }
        return;
    }
    s_last_activity_tick = lv_tick_get();
}

void screensaver_service_enter_now(void)
{
    if (bsp_display_lock(1000)) {
        screensaver_enter(true);
        bsp_display_unlock();
    }
}

bool screensaver_service_is_active(void)
{
    return s_active;
}

void screensaver_service_reload_settings(void)
{
    if (!s_active) {
        s_last_activity_tick = lv_tick_get();
        return;
    }
    if (bsp_display_lock(1000)) {
        screensaver_exit();
        if (app_settings_get_screensaver_mode() != APP_SETTINGS_SCREENSAVER_MODE_OFF &&
            !screensaver_ssh_block_active()) {
            screensaver_enter(false);
        } else {
            s_last_activity_tick = lv_tick_get();
        }
        bsp_display_unlock();
    }
}
