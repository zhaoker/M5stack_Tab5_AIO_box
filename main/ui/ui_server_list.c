/**
 * @file ui_server_list.c
 * @brief 服务器列表主界面
 */
#include "ui_server_list.h"
#include "ui_terminal.h"
#include "ui_wifi.h"
#include "ui_settings.h"
#include "file_manager/ui_file_manager.h"
#include "pinyin/pinyin_dict_custom.h"
#include "screensaver/screensaver_service.h"
#include "ui_hw_pinyin_input.h"
#include "ui_hw_textarea_input.h"
#include "ui_pinyin_ime_ext.h"
#include "theme.hpp"
#include "hw/tab5_keyboard.h"
#include "power/power_monitor.h"
#include "settings/app_settings.h"
#include "storage/server_config.h"
#include "storage/tf_card.h"
#include "wifi_manager.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ui_srv_list";

enum {
    TOPBAR_HEIGHT = 80,
    SIDE_MARGIN = 20,
    CONTENT_TOP_GAP = 12,
    CONTENT_BOTTOM_GAP = 20,
    GRID_COLUMNS = 3,
    GRID_GAP = 16,
    CARD_RATIO_W = 5,
    CARD_RATIO_H = 3,
    MODAL_MARGIN = 18,
    FORM_HEADER_H = 64,
    FORM_FOOTER_H = 60,
    FORM_DIALOG_PAD = 14,
    FORM_DIALOG_GAP = 4,
    FORM_ROW_GAP = 8,
    FORM_FIELD_GAP = 4,
    FORM_FIELD_H = 50,
    FORM_ACTION_BTN_H = 48,
    ACTION_INFO_GAP = 5,
    ACTION_PANEL_W = 420,
    ACTION_PANEL_H = 340,
    ACTION_SECTION_GAP = 24,
    ACTION_BUTTON_GAP = 30,
    ACTION_CANCEL_TOP_GAP = 30,
    DELETE_PANEL_W = 470,
    DELETE_PANEL_H = 250,
    INFO_PANEL_W = 470,
    INFO_PANEL_H = 240,
    NET_INFO_IPV4_PANEL_W = 560,
    NET_INFO_IPV6_PANEL_W = 900,
    NET_INFO_PANEL_MIN_H = 300,
    NET_INFO_PANEL_MAX_H = 520,
    NET_INFO_ROW_H = 46,
    POWER_PANEL_W = 620,
    POWER_PANEL_H = 610,
    HW_KEYBOARD_POLL_MS = 20,
    PUBLIC_IPV4_TASK_STACK = 6144,
    SESSION_BUTTON_W = 160,
    SESSION_BUTTON_H = 48,
    SESSION_BUTTON_TEXT_H = 20,
    SESSION_BUTTON_VISUAL_LINE_GAP = 0,
};

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *dialog;
    lv_obj_t *body;
    lv_obj_t *keyboard;
#if LV_USE_IME_PINYIN != 0
    lv_obj_t *pinyin_ime;
    lv_obj_t *pinyin_cand_panel;
    ui_pinyin_ime_ext_t *pinyin_ext;
#endif
    lv_obj_t *name_ta;
    lv_obj_t *host_ta;
    lv_obj_t *port_ta;
    lv_obj_t *user_ta;
    lv_obj_t *pass_ta;
    lv_obj_t *cmd_ta;
    lv_obj_t *active_ta;
    ui_hw_pinyin_input_t *hw_pinyin;
    lv_timer_t *hw_keyboard_timer;
    int edit_index;
    bool keyboard_visible;
    bool hw_keyboard_present;
} server_form_t;

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_list_scroll = NULL;
static lv_obj_t *s_list_grid = NULL;
static lv_obj_t *s_wifi_chip = NULL;
static lv_obj_t *s_wifi_label = NULL;
static lv_obj_t *s_summary_label = NULL;
static lv_obj_t *s_statusbar_bottom = NULL;
static lv_obj_t *s_file_manager_button = NULL;
static lv_obj_t *s_session_row = NULL;
static lv_obj_t *s_ipv4_chip = NULL;
static lv_obj_t *s_ipv4_label = NULL;
static lv_obj_t *s_ipv6_chip = NULL;
static lv_obj_t *s_ipv6_label = NULL;
static lv_obj_t *s_power_chip = NULL;
static lv_obj_t *s_power_hotzone = NULL;
static lv_obj_t *s_power_body = NULL;
static lv_obj_t *s_power_cap = NULL;
static lv_obj_t *s_power_percent = NULL;
static lv_obj_t *s_power_overlay = NULL;
static lv_obj_t *s_boot_auto_charge_slider = NULL;
static lv_obj_t *s_boot_auto_charge_off_label = NULL;
static lv_obj_t *s_boot_auto_charge_on_label = NULL;
static lv_obj_t *s_charge_mode_slider = NULL;
static lv_obj_t *s_charge_standard_label = NULL;
static lv_obj_t *s_charge_fast_label = NULL;
static lv_obj_t *s_icon_picker_overlay = NULL;
static lv_obj_t *s_icon_picker_panel = NULL;
static lv_obj_t *s_icon_picker_preview = NULL;
static lv_obj_t *s_icon_picker_button = NULL;
static uint8_t s_selected_icon_id = 0;

static lv_obj_t *s_action_overlay = NULL;
static lv_obj_t *s_delete_overlay = NULL;
static lv_obj_t *s_info_overlay = NULL;
static lv_obj_t *s_net_info_public_ipv4_label = NULL;
static lv_timer_t *s_wifi_timer = NULL;
static lv_timer_t *s_power_timer = NULL;
static lv_timer_t *s_power_detail_timer = NULL;
static uint32_t s_net_info_generation = 0;

typedef struct {
    uint32_t generation;
} public_ipv4_task_arg_t;

typedef struct {
    uint32_t generation;
    char public_ipv4[32];
} public_ipv4_result_t;

typedef struct {
    const char *name;
    const char *url;
} public_ipv4_node_t;

static const public_ipv4_node_t s_public_ipv4_nodes[] = {
    {"4.ipw.cn", "http://4.ipw.cn"},
    {"ip.3322.net", "http://ip.3322.net"},
    {"members.3322.org", "http://members.3322.org/dyndns/getip"},
    {"myip.ipip.net", "http://myip.ipip.net"},
    {"pv.sohu.com", "http://pv.sohu.com/cityjson?ie=utf-8"},
};

enum {
    POWER_DETAIL_BATTERY,
    POWER_DETAIL_VOLTAGE,
    POWER_DETAIL_CURRENT,
    POWER_DETAIL_POWER,
    POWER_DETAIL_PERCENT,
    POWER_DETAIL_SOURCE,
    POWER_DETAIL_USB_C,
    POWER_DETAIL_COUNT,
};

static lv_obj_t *s_power_detail_values[POWER_DETAIL_COUNT] = {NULL};

static server_form_t s_form = {
    .edit_index = -1,
};

static lv_style_t s_style_card;
static lv_style_t s_style_card_pressed;
static lv_style_t s_style_modal_panel;
static lv_style_t s_style_primary_btn;
static lv_style_t s_style_primary_btn_pressed;
static lv_style_t s_style_secondary_btn;
static lv_style_t s_style_secondary_btn_pressed;
static lv_style_t s_style_danger_btn;
static lv_style_t s_style_danger_btn_pressed;
static lv_style_t s_style_textarea;
static lv_style_t s_style_textarea_focused;
static lv_style_t s_style_icon_picker_btn;
static lv_style_t s_style_icon_picker_btn_pressed;
static bool s_styles_init = false;

static lv_coord_t s_card_w = 360;
static lv_coord_t s_card_h = 216;

typedef struct {
    const char *glyph;
    const char *name;
    uint32_t color_hex;
} server_icon_def_t;

static const server_icon_def_t s_server_icons[] = {
    {"CN", "CN", 0xEF4444},
    {"JP", "JP", 0xF97316},
    {"SG", "SG", 0x22C55E},
    {"US", "US", 0x3B82F6},
    {"HK", "HK", 0xEC4899},
    {"EU", "EU", 0x8B5CF6},
    {LV_SYMBOL_WIFI, "WiFi", 0xFBBF24},
    {LV_SYMBOL_HOME, "Home", 0x60A5FA},
    {LV_SYMBOL_DRIVE, "Drive", 0x38BDF8},
    {LV_SYMBOL_FILE, "File", 0x34D399},
    {LV_SYMBOL_KEYBOARD, "Keyboard", 0xA78BFA},
    {LV_SYMBOL_GPS, "GPS", 0xF472B6},
    {LV_SYMBOL_UPLOAD, "Upload", 0x93C5FD},
    {LV_SYMBOL_SETTINGS, "Gear", 0xCBD5E1},
    {LV_SYMBOL_REFRESH, "Refresh", 0x2DD4BF},
    {LV_SYMBOL_WARNING, "Alert", 0xFB7185},
    {LV_SYMBOL_CHARGE, "Bolt", 0xFACC15},
    {LV_SYMBOL_EYE_OPEN, "Eye", 0xC084FC},
    {LV_SYMBOL_CALL, "Call", 0x4ADE80},
    {LV_SYMBOL_OK, "Check", 0x22D3EE},
    {LV_SYMBOL_BELL, "Bell", 0xFDE68A},
    {LV_SYMBOL_LIST, "List", 0x94A3B8},
    {LV_SYMBOL_DIRECTORY, "Folder", 0xF59E0B},
    {LV_SYMBOL_POWER, "Power", 0x5EEAD4},
    {LV_SYMBOL_AUDIO, "Audio", 0xF59E0B},
    {LV_SYMBOL_VIDEO, "Video", 0xFB7185},
    {LV_SYMBOL_IMAGE, "Image", 0x34D399},
    {LV_SYMBOL_TINT, "Tint", 0x06B6D4},
    {LV_SYMBOL_PREV, "Prev", 0xA78BFA},
    {LV_SYMBOL_PLAY, "Play", 0x22C55E},
    {LV_SYMBOL_PAUSE, "Pause", 0xFBBF24},
    {LV_SYMBOL_STOP, "Stop", 0xEF4444},
    {LV_SYMBOL_NEXT, "Next", 0x60A5FA},
    {LV_SYMBOL_EJECT, "Eject", 0x94A3B8},
    {LV_SYMBOL_LEFT, "Left", 0x38BDF8},
    {LV_SYMBOL_RIGHT, "Right", 0x38BDF8},
    {LV_SYMBOL_PLUS, "Plus", 0x22C55E},
    {LV_SYMBOL_MINUS, "Minus", 0xF87171},
    {LV_SYMBOL_SHUFFLE, "Shuffle", 0xC084FC},
    {LV_SYMBOL_UP, "Up", 0x93C5FD},
    {LV_SYMBOL_DOWN, "Down", 0x93C5FD},
    {LV_SYMBOL_LOOP, "Loop", 0x2DD4BF},
    {LV_SYMBOL_DOWNLOAD, "Download", 0x60A5FA},
    {LV_SYMBOL_MUTE, "Mute", 0x64748B},
    {LV_SYMBOL_VOLUME_MID, "Volume", 0xFBBF24},
    {LV_SYMBOL_VOLUME_MAX, "Loud", 0xF59E0B},
    {LV_SYMBOL_BATTERY_FULL, "Battery+", 0x22C55E},
    {LV_SYMBOL_BATTERY_EMPTY, "Battery-", 0xEF4444},
    {"WIN", "Windows", 0x0078D4},
    {"LNX", "Linux", 0xFCC624},
    {"UBU", "Ubuntu", 0xE95420},
    {"DEB", "Debian", 0xA80030},
    {"RHL", "Red Hat", 0xEE0000},
    {"BSD", "FreeBSD", 0xAB2B28},
};

#define SERVER_ICON_COUNT ((uint8_t)(sizeof(s_server_icons) / sizeof(s_server_icons[0])))

static void init_styles(void);
static void layout_main_screen(void);
static void refresh_list(void);
static void refresh_session_buttons(void);
static void update_summary_label(int count);
static void update_wifi_label(void);
static void update_power_chip(void);
static void show_power_detail(void);
static void close_power_overlay(void);
static void update_boot_auto_charge_controls(bool enabled);
static void update_charge_mode_controls(bool fast_charge_enabled);
static void close_server_form(void);
static void close_action_overlay(void);
static void close_delete_overlay(void);
static void close_info_overlay(void);
static void close_transient_overlays(void);
static void show_server_form(int edit_index);
static void show_card_actions(int index);
static void show_delete_confirm(int index);
static void show_info_dialog(const char *title, const char *message);
static void show_ipv4_info_dialog(void);
static void show_ipv6_info_dialog(void);
static lv_obj_t *create_net_info_row(lv_obj_t *parent, const char *text);
static lv_obj_t *create_form_row(lv_obj_t *parent);
static lv_obj_t *create_modal_overlay(lv_event_cb_t on_click);
static lv_obj_t *create_modal_panel(lv_obj_t *overlay, lv_coord_t width, lv_coord_t height);
static void close_icon_picker(void);
static void hw_keyboard_timer_cb(lv_timer_t *timer);
static void power_status_timer_cb(lv_timer_t *timer);
static void power_detail_timer_cb(lv_timer_t *timer);
static void on_boot_auto_charge_changed(lv_event_t *e);
static void on_charge_mode_changed(lv_event_t *e);

static void on_title_screensaver_click(lv_event_t *e)
{
    (void)e;
    screensaver_service_enter_now();
}

/* Private implementation shards. Keep this file as the public UI entry point. */
#include "server_list/ui_server_list_core.inc"
#include "server_list/ui_server_list_overlays.inc"
#include "server_list/ui_server_list_form.inc"
#include "server_list/ui_server_list_actions.inc"
#include "server_list/ui_server_list_main.inc"
