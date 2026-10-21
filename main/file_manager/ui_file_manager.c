#include "file_manager/ui_file_manager.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "audio/audio_cover.h"
#include "audio/audio_favorites.h"
#include "audio/audio_player_service.h"
#include "diagnostics/mem_diag.h"
#include "settings/app_settings.h"
#include "file_manager/file_manager_exif.h"
#include "file_manager/file_manager_media.h"
#include "file_manager/file_manager_text.h"
#include "file_manager/file_manager_utils.h"
#include "file_manager/web_file_manager.h"
#include "misc/cache/lv_image_cache.h"
#include "hw/tab5_keyboard.h"
#include "power/power_monitor.h"
#include "pinyin/pinyin_dict_custom.h"
#include "storage/tf_card.h"
#include "storage/tf_usb_disk_mode.h"
#include "storage/usb_storage.h"
#include "theme.hpp"
#include "ui_hw_pinyin_input.h"
#include "ui_hw_textarea_input.h"
#include "ui_audio_background.h"
#include "ui_pinyin_ime_ext.h"
#include "ui_server_list.h"
#include "ui_settings.h"
#include "wifi_manager.h"

static const char *TAG = "ui_file_mgr";

extern esp_lcd_touch_handle_t bsp_display_get_touch_handle(void);

enum {
    FM_TOPBAR_H = 64,
    FM_TOOLBAR_H = 64,
    FM_PAD = 18,
    FM_GAP = 12,
    FM_PAGE_SIZE = 16,
    FM_MAX_ENTRIES = 256,
    FM_PATH_MAX = 512,
    FM_NAME_MAX = 160,
    FM_CLIPBOARD_MAX_ITEMS = FM_MAX_ENTRIES,
    FM_KEYBOARD_POLL_MS = 20,
    FM_STATUS_OVERRIDE_MS = 4000,
    FM_PASTE_TASK_STACK = 12 * 1024,
    FM_PASTE_TASK_PRIORITY = 2,
    FM_LIST_KEYBOARD_COLUMNS = 2,
    FM_TEXT_KEYBOARD_SCROLL_LINES = 8,
    FM_TEXT_MAX_BYTES = 24 * 1024,
    FM_COPY_BUF_BYTES = 4096,
    FM_TEXT_STATUS_H = 34,
    FM_IMAGE_SCALE_MIN = 64,
    FM_IMAGE_SCALE_MAX = 1024,
    FM_VIEWER_TOUCH_MS = 20,
    FM_VIEWER_CONTROLS_HIDE_MS = 3000,
    FM_GIF_INTERACTION_RESUME_MS = 800,
    FM_GIF_MAX_W = 512,
    FM_GIF_MAX_H = 512,
    FM_GIF_MAX_PIXELS = 512 * 512,
};

typedef enum {
    FM_TYPE_DIR,
    FM_TYPE_IMAGE,
    FM_TYPE_AUDIO,
    FM_TYPE_VIDEO,
    FM_TYPE_TEXT,
    FM_TYPE_OTHER,
} fm_file_type_t;

typedef enum {
    FM_INPUT_MKDIR,
    FM_INPUT_RENAME,
} fm_input_mode_t;

typedef enum {
    FM_TEXT_THEME_DARK,
    FM_TEXT_THEME_LIGHT,
    FM_TEXT_THEME_EYE,
} fm_text_theme_t;

typedef enum {
    FM_CLIPBOARD_NONE,
    FM_CLIPBOARD_COPY,
    FM_CLIPBOARD_MOVE,
} fm_clipboard_mode_t;

typedef enum {
    FM_MODAL_ENTER_NONE,
    FM_MODAL_ENTER_DELETE,
} fm_modal_enter_action_t;

typedef enum {
    FM_VOLUME_NONE,
    FM_VOLUME_TF,
    FM_VOLUME_USB,
} fm_volume_t;

typedef struct {
    char name[FM_NAME_MAX];
    bool is_dir;
    uint64_t size;
    fm_file_type_t type;
} fm_entry_t;

typedef char fm_clipboard_name_t[FM_NAME_MAX];

typedef struct {
    fm_clipboard_mode_t mode;
    bool overwrite;
    uint32_t generation;
    size_t count;
    size_t ok_count;
    char dest_dir[FM_PATH_MAX];
    char source_dir[FM_PATH_MAX];
    char single_source_path[FM_PATH_MAX];
    char names[FM_CLIPBOARD_MAX_ITEMS][FM_NAME_MAX];
} fm_paste_job_t;

typedef struct {
    size_t count;
    char dir[FM_PATH_MAX];
    char names[][FM_NAME_MAX];
} fm_delete_selection_t;

static lv_obj_t *s_overlay;
static lv_obj_t *s_title_label;
static lv_obj_t *s_path_label;
static lv_obj_t *s_count_label;
static lv_obj_t *s_web_status_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_list;
static lv_obj_t *s_wifi_chip;
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_power_chip;
static lv_obj_t *s_power_body;
static lv_obj_t *s_power_cap;
static lv_obj_t *s_power_percent;
static lv_obj_t *s_back_button;
static lv_obj_t *s_web_button;
static lv_obj_t *s_web_button_label;
static lv_obj_t *s_paste_button;
static lv_obj_t *s_busy_toast;
static lv_obj_t *s_busy_toast_label;
static lv_obj_t *s_modal;
static lv_obj_t *s_input_textarea;
static lv_obj_t *s_input_status;
static lv_obj_t *s_input_keyboard;
#if LV_USE_IME_PINYIN != 0
static lv_obj_t *s_input_pinyin_ime;
static lv_obj_t *s_input_pinyin_cand_panel;
#endif
static lv_obj_t *s_viewer;
static lv_obj_t *s_viewer_canvas;
static lv_obj_t *s_viewer_img;
static lv_obj_t *s_viewer_gif;
static lv_obj_t *s_viewer_info_panel;
static lv_obj_t *s_viewer_exif_top_panel;
static lv_obj_t *s_viewer_exif_top_label;
static lv_obj_t *s_viewer_exif_panel;
static lv_obj_t *s_viewer_exif_label;
static lv_obj_t *s_viewer_status;
static lv_obj_t *s_viewer_prev_btn;
static lv_obj_t *s_viewer_next_btn;
static lv_obj_t *s_viewer_rotate_btn;
static lv_obj_t *s_viewer_close_btn;
static lv_obj_t *s_audio_viewer;
static lv_obj_t *s_audio_title;
static lv_obj_t *s_audio_artist;
static lv_obj_t *s_audio_status;
static lv_obj_t *s_audio_index;
static lv_obj_t *s_audio_play_label;
static lv_obj_t *s_audio_mode_icon;
static lv_obj_t *s_audio_mode_label;
static lv_obj_t *s_audio_progress;
static lv_obj_t *s_audio_elapsed;
static lv_obj_t *s_audio_duration;
static lv_obj_t *s_audio_volume;
static lv_obj_t *s_audio_volume_value;
static lv_obj_t *s_audio_playlist;
static lv_obj_t *s_text_viewer;
static lv_obj_t *s_text_bar;
static lv_obj_t *s_text_title;
static lv_obj_t *s_text_theme_label;
static lv_obj_t *s_text_textarea;
static lv_obj_t *s_text_status;
static lv_timer_t *s_status_timer;
static lv_timer_t *s_hw_keyboard_timer;
static lv_timer_t *s_viewer_touch_timer;
static TaskHandle_t s_paste_task;
static fm_paste_job_t *s_active_paste_job;
static fm_delete_selection_t *s_delete_selection;
static ui_hw_pinyin_input_t *s_input_hw_pinyin;
static ui_pinyin_ime_ext_t *s_input_pinyin_ext;
static file_manager_image_t s_viewer_image;

static char s_root_path[FM_PATH_MAX] = TF_CARD_MOUNT_POINT;
static char s_current_path[FM_PATH_MAX] = TF_CARD_MOUNT_POINT;
static char s_active_path[FM_PATH_MAX];
static char s_active_name[FM_NAME_MAX];
static char s_viewer_path[FM_PATH_MAX];
static char s_clipboard_path[FM_PATH_MAX];
static char s_clipboard_name[FM_NAME_MAX];
static char s_clipboard_dir[FM_PATH_MAX];
static fm_clipboard_name_t *s_clipboard_names;
static char s_text_encoding[24] = "unknown";
static fm_entry_t *s_entries;
static size_t *s_viewer_image_indices;
static size_t s_entry_count;
static size_t s_viewer_image_count;
static size_t s_viewer_image_pos;
static size_t s_page;
static size_t s_selected_index;
static size_t s_clipboard_count;
static size_t s_text_raw_len;
static fm_input_mode_t s_input_mode;
static fm_clipboard_mode_t s_clipboard_mode;
static fm_modal_enter_action_t s_modal_enter_action;
static fm_text_theme_t s_text_theme = FM_TEXT_THEME_EYE;
static fm_volume_t s_current_volume = FM_VOLUME_NONE;
static uint32_t s_image_scale = 256;
static uint32_t s_image_fit_scale = 256;
static uint32_t s_pinch_start_scale = 256;
static uint32_t s_viewer_controls_last_touch;
static uint32_t s_status_override_until;
static uint32_t s_ui_generation;
static uint32_t s_usb_status_generation;
static int32_t s_pinch_start_distance;
static int32_t s_image_rotation;
static bool s_input_hw_keyboard_present;
static bool s_all_active;
static bool s_status_override_active;
static bool s_selection_status_active;
static bool s_scroll_active_into_view;
static bool s_paste_busy;
static bool s_pinch_active;
static bool s_viewer_controls_visible;
static bool s_viewer_gif_paused_for_controls;
static bool s_viewer_exif_title_has_content;
static bool s_viewer_exif_has_content;
static bool s_audio_status_cb_registered;
static bool s_storage_picker_active = true;

static void fm_render(void);
static void fm_close_modal(void);
static void fm_close_viewer(void);
static void fm_close_text_viewer(void);
static void fm_close_audio_player(void);
static void fm_show_info_dialog(const char *title_text, const char *body_text);
static void fm_show_input_dialog(fm_input_mode_t mode, const char *initial);
static void fm_set_status(const char *text, lv_color_t color);
static void fm_set_web_status(const char *text, lv_color_t color);
static void fm_update_selection_status(void);
static void fm_update_folder_status(void);
static void fm_update_navigation_controls(void);
static void fm_update_web_controls(void);
static void fm_update_status_bar(void);
static void fm_update_paste_button(void);
static void fm_set_button_text(lv_obj_t *btn, const char *text);
static bool fm_volume_is_mounted(fm_volume_t volume);
static bool fm_path_in_known_volume(const char *path);
static bool fm_path_is_volume_root(const char *path);
static void fm_log_heap(const char *where);
static void fm_show_busy_toast(const char *text);
static void fm_hide_busy_toast(void);
static void fm_release_viewer_image(void);
static void fm_viewer_set_controls_visible(bool visible);
static void fm_open_active(void);
static void fm_show_action_sheet(size_t index);
static void fm_show_delete_confirm(void);
static void fm_confirm_delete_selection(void);
static void fm_set_clipboard(fm_clipboard_mode_t mode);
static void fm_paste_clipboard(bool overwrite);
static void fm_on_select_all(lv_event_t *e);
static void fm_on_copy_selection(lv_event_t *e);
static void fm_on_cut_selection(lv_event_t *e);
static void fm_on_delete_selection(lv_event_t *e);

static void *fm_calloc_prefer_psram(size_t count, size_t size, const char *name)
{
    void *ptr;
    size_t bytes = count * size;

    ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        ESP_LOGI(TAG, "%s allocated in PSRAM: %u bytes", name, (unsigned)bytes);
        return ptr;
    }
    ptr = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr) {
        ESP_LOGW(TAG, "%s allocated in internal RAM fallback: %u bytes", name, (unsigned)bytes);
        return ptr;
    }
    ESP_LOGE(TAG, "%s allocation failed: %u bytes", name, (unsigned)bytes);
    return NULL;
}

static bool fm_ensure_large_buffers(void)
{
    if (!s_entries) {
        s_entries = (fm_entry_t *)fm_calloc_prefer_psram(FM_MAX_ENTRIES,
                                                         sizeof(s_entries[0]),
                                                         "file entries");
    }
    if (!s_clipboard_names) {
        s_clipboard_names = (fm_clipboard_name_t *)fm_calloc_prefer_psram(FM_CLIPBOARD_MAX_ITEMS,
                                                                          sizeof(s_clipboard_names[0]),
                                                                          "clipboard names");
    }
    if (!s_viewer_image_indices) {
        s_viewer_image_indices = (size_t *)fm_calloc_prefer_psram(FM_MAX_ENTRIES,
                                                                  sizeof(s_viewer_image_indices[0]),
                                                                  "image viewer indices");
    }
    return s_entries && s_clipboard_names && s_viewer_image_indices;
}

static size_t fm_clipboard_names_bytes(void)
{
    return FM_CLIPBOARD_MAX_ITEMS * sizeof(s_clipboard_names[0]);
}


#include "ui_file_manager_core.inc"
#include "ui_file_manager_storage.inc"
#include "ui_file_manager_text.inc"
#include "ui_file_manager_image_core.inc"
#include "ui_file_manager_image_load.inc"
#include "ui_file_manager_audio_cover_cache.inc"
#include "ui_file_manager_audio_helpers.inc"
#include "ui_file_manager_image_ui.inc"
#include "ui_file_manager_input_keyboard.inc"
#include "ui_file_manager_actions.inc"
#include "ui_file_manager_layout.inc"
