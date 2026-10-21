/**
 * @file ui_wifi.c
 * @brief WiFi 管理界面实现
 *
 * 功能：
 *   - 显示当前连接状态
 *   - 管理多个已保存 WiFi
 *   - 扫描当前可用 WiFi 列表
 *   - 手动输入密码并连接
 */
#include "ui_wifi.h"
#include "wifi_manager.h"
#include "hw/tab5_keyboard.h"
#include "pinyin/pinyin_dict_custom.h"
#include "ui_hw_pinyin_input.h"
#include "ui_hw_textarea_input.h"
#include "ui_pinyin_ime_ext.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_wifi";

#define COLOR_BG           lv_color_hex(0x111827)
#define COLOR_PANEL        lv_color_hex(0x1F2937)
#define COLOR_SURFACE      lv_color_hex(0x0F172A)
#define COLOR_BORDER       lv_color_hex(0x334155)
#define COLOR_TEXT         lv_color_hex(0xE5E7EB)
#define COLOR_TEXT_SUB     lv_color_hex(0x94A3B8)
#define COLOR_PRIMARY      lv_color_hex(0x2563EB)
#define COLOR_PRIMARY_DARK lv_color_hex(0x1D4ED8)
#define COLOR_DANGER       lv_color_hex(0xDC2626)
#define COLOR_SUCCESS      lv_color_hex(0x16A34A)
#define COLOR_WARNING      lv_color_hex(0xF59E0B)
#define COLOR_MUTED        lv_color_hex(0x64748B)
#define COLOR_INPUT        lv_color_hex(0x020617)

#define WIFI_PANEL_MIN_W   900
#define WIFI_PANEL_H       620
#define WIFI_TITLE_H       52
#define WIFI_BUTTON_H      42
#define WIFI_INPUT_H       44
#define WIFI_SECTION_H     180
#define WIFI_LEFT_COL_W    400
#define WIFI_SIDE_PAD      16
#define WIFI_GAP           12
#define WIFI_SMALL_GAP     8
#define WIFI_MANUAL_PAD    14
#define WIFI_FIELD_LABEL_W 58
#define WIFI_FIELD_GAP     10
#define WIFI_FIELD_INPUT_W (WIFI_LEFT_COL_W - (WIFI_MANUAL_PAD * 2) - WIFI_FIELD_LABEL_W - WIFI_FIELD_GAP)
#define HW_KEYBOARD_POLL_MS 20

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_panel = NULL;
static lv_obj_t *s_status_card = NULL;
static lv_obj_t *s_manual_title = NULL;
static lv_obj_t *s_lbl_state = NULL;
static lv_obj_t *s_lbl_ipv4 = NULL;
static lv_obj_t *s_lbl_ipv6 = NULL;
static lv_obj_t *s_scan_hint = NULL;
static lv_obj_t *s_ta_ssid = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_kb = NULL;
static lv_obj_t *s_pinyin_kb = NULL;
#if LV_USE_IME_PINYIN != 0
static lv_obj_t *s_pinyin_ime = NULL;
static lv_obj_t *s_pinyin_cand_panel = NULL;
static ui_pinyin_ime_ext_t *s_pinyin_ext = NULL;
#endif
static lv_obj_t *s_saved_list = NULL;
static lv_obj_t *s_scan_list = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static lv_timer_t *s_scan_timer = NULL;
static lv_timer_t *s_hw_keyboard_timer = NULL;
static lv_obj_t *s_hw_active_ta = NULL;
static ui_hw_pinyin_input_t *s_hw_pinyin = NULL;
static bool s_hw_keyboard_present = false;

static wifi_saved_network_t s_saved_networks[WIFI_MAX_SAVED_NETWORKS];
static size_t s_saved_count = 0;
static wifi_scan_result_t s_scan_results[WIFI_MAX_SCAN_RESULTS];
static size_t s_scan_count = 0;

static void update_state_label(void);
static void refresh_saved_networks(void);
static void rebuild_saved_networks_list(void);
static void rebuild_scan_list(void);
static void scan_and_refresh_networks(void);
static void request_scan(void);
static void clear_container_children(lv_obj_t *container);
static const char *authmode_to_text(uint8_t authmode);
static lv_obj_t *create_text_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_event_cb_t cb, void *user_data);
static void on_scan_row_action(lv_event_t *e);
static void on_scan_saved_delete(lv_event_t *e);
static bool saved_network_is_scanned(const char *ssid);
static void hw_keyboard_timer_cb(lv_timer_t *timer);

/* Private implementation shards. Keep this file as the public UI entry point. */
#include "wifi/ui_wifi_widgets.inc"
#include "wifi/ui_wifi_input.inc"
#include "wifi/ui_wifi_actions.inc"
#include "wifi/ui_wifi_lists.inc"
#include "wifi/ui_wifi_create.inc"
