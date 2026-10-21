/**
 * @file ui_terminal.c
 * @brief SSH 终端界面实现
 *
 * 架构：
 *   ┌──────────────────────────────────────────────────────┐
 *   │  lv_canvas（全屏 1280×702，ANSI 终端画面）             │
 *   ├──────────────────────────────────────────────────────┤
 *   │  状态栏               │
 *   └──────────────────────────────────────────────────────┘
 *   软键盘：触摸屏幕 → 从底部弹出 lv_keyboard，
 *           隐藏时终端占满全屏
 *
 * 线程模型：
 *   - SSH 接收任务（libssh 栈）：调用 ssh_client.c 后台任务
 *   - LVGL 渲染：通过 lv_timer（30ms）定期锁屏刷新
 *   - term_state 受 FreeRTOS mutex 保护
 */
#include "ui_terminal.h"
#include "ui_audio_background.h"
#include "ui_server_list.h"
#include "theme.hpp"
#include "pinyin/pinyin_dict_custom.h"
#include "pinyin/pinyin_phrase_dict.h"
#include "settings/app_settings.h"
#include "terminal/ansi_parser.h"
#include "terminal/term_renderer.h"
#include "ssh/ssh_client.h"
#include "hw/tab5_keyboard.h"
#include "bsp/m5stack_tab5.h"
#if LV_USE_IME_PINYIN != 0
#include "others/ime/lv_ime_pinyin_private.h"
#endif
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static const char *TAG = "ui_term";

/* SSH 连接任务栈大小（需容纳 libssh known_hosts 深调用栈） */
#define SSH_CONN_TASK_STACK  16384
#define SSH_CONN_TASK_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define SSH_CONN_TASK_PRIO   4
#define STATUSBAR_H          APP_TERM_STATUSBAR_H
#define STATUS_HOTZONE_H     APP_TERM_HOTZONE_TOP_H
#define LEFT_HOTZONE_W       APP_TERM_HOTZONE_LEFT_W
#define RIGHT_HOTZONE_W      APP_TERM_HOTZONE_RIGHT_W
#define CLOSE_BTN_W          96
#define CLOSE_BTN_H          40
#define CONTROL_BAR_H        APP_TERM_CONTROL_BAR_H
#define KEYBOARD_FALLBACK_H  240
#define IME_CAND_PANEL_H     52
#define IME_INPUT_BAR_H      38
#define IME_HW_CAND_DIGIT_COUNT 10
#define IME_HW_CAND_DIGIT_FONT (&lv_font_montserrat_16)
#define IME_PHRASE_CAND_MAX  128
#define TERMINAL_BOTTOM_GAP  15
#define TERMINAL_CURSOR_SAFE_OFFSET APP_TERM_CURSOR_SAFE_OFFSET
#define HW_KEYBOARD_POLL_MS  20
#define IME_TOAST_TOP_Y      80
#define IME_TOAST_HIDE_MS    2000
#define IME_TOAST_FADE_MS    350
#define HW_MOD_CTRL          0x01U
#define HW_MOD_ALT           0x04U

/* ─── 全局状态 ─── */
typedef struct terminal_session {
    uint32_t id;
    uint32_t ref_count;
    server_entry_t server;
    term_state_t term;
    SemaphoreHandle_t term_mutex;
    ssh_client_t *ssh;
    TaskHandle_t connect_task;
    bool connect_started;
    bool ssh_runtime_ref;
    bool connected;
    bool failed;
    bool minimized;
    bool banner_injected;
    bool destroy_requested;
    struct terminal_session *next;
} terminal_session_t;

static terminal_session_t *s_sessions = NULL;
static terminal_session_t *s_active_session = NULL;
static uint32_t s_next_session_id = 1;
static portMUX_TYPE s_session_ref_mux = portMUX_INITIALIZER_UNLOCKED;

static lv_obj_t   *s_screen       = NULL;  /* 终端屏幕根对象 */
static lv_obj_t   *s_viewport     = NULL;  /* Scrollable terminal viewport */
static lv_obj_t   *s_status_hotzone = NULL; /* Top tap zone for hidden statusbar */
static lv_obj_t   *s_left_hotzone  = NULL;  /* Left tap zone for control bar */
static lv_obj_t   *s_right_hotzone = NULL;  /* Right tap zone for keyboard */
static lv_obj_t   *s_canvas       = NULL;  /* 终端 canvas */
static lv_obj_t   *s_statusbar    = NULL;  /* 顶部状态栏 */
static lv_obj_t   *s_lbl_status   = NULL;  /* 状态文字 */
static lv_obj_t   *s_keyboard     = NULL;  /* LVGL 软键盘 */
#if LV_USE_IME_PINYIN != 0
static lv_obj_t   *s_pinyin_ime   = NULL;
static lv_obj_t   *s_pinyin_cand_panel = NULL;
static lv_obj_t   *s_pinyin_input_bar = NULL;
static lv_obj_t   *s_pinyin_input_label = NULL;
static lv_obj_t   *s_hw_pinyin_digit_labels[IME_HW_CAND_DIGIT_COUNT];
#endif
static lv_timer_t *s_render_timer = NULL;  /* 渲染刷新定时器 */
static lv_timer_t *s_hw_keyboard_timer = NULL;

static lv_obj_t   *s_control_bar  = NULL;  /* 终端控制按键行 */
static bool        s_ctrl_bar_visible = false;
static bool        s_ctrl_active  = false;
static bool        s_alt_active   = false;
static lv_obj_t   *s_btn_ctrl     = NULL;
static lv_obj_t   *s_btn_alt      = NULL;
static lv_obj_t   *s_btn_ime_mode = NULL;
static lv_obj_t   *s_lbl_ime_mode = NULL;
static lv_timer_t *s_statusbar_timer = NULL;
static lv_obj_t   *s_msgbox       = NULL;
static lv_obj_t   *s_ime_toast    = NULL;
static lv_timer_t *s_ime_toast_timer = NULL;
static bool        s_kb_value_is_pinyin_comp = false;
static bool        s_kb_value_is_pinyin_backspace = false;
static bool        s_vta_internal_update = false;
static bool        s_hw_keyboard_present = false;
#if LV_USE_IME_PINYIN != 0
static bool        s_pinyin_enabled = true;
static bool        s_pinyin_committed_in_ready = false;
typedef enum {
    PINYIN_CAND_MAP_DEFAULT = 0,
    PINYIN_CAND_MAP_HARDWARE,
    PINYIN_CAND_MAP_PHRASE,
} pinyin_cand_map_kind_t;
static char        s_hw_pinyin_cand_buf[LV_IME_PINYIN_CAND_TEXT_NUM][8];
static const char *s_hw_pinyin_cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 3];
static const char * const *s_pinyin_default_cand_map = NULL;
static const char *s_pinyin_phrase_words[IME_PHRASE_CAND_MAX];
static uint16_t    s_pinyin_phrase_count = 0;
static pinyin_cand_map_kind_t s_pinyin_cand_map_kind = PINYIN_CAND_MAP_DEFAULT;
static bool        s_hw_pinyin_composing = false;
static bool        s_hw_pinyin_cand_map_active = false;
#endif

typedef struct {
    ssh_event_t event;
    terminal_session_t *session;
    char reason[192];
} ssh_event_info_t;

static void on_terminal_msgbox_ok(lv_event_t *e);
static void on_minimize_click(lv_event_t *e);
static void apply_hotzone_style(lv_obj_t *obj);
static void reposition_elements(void);
static void toggle_control_bar_visible(void);
static void toggle_keyboard_visible(void);
static bool is_ascii_letters_only(const char *txt);
static bool keyboard_text_is_mode_key(const char *txt);
static bool keyboard_text_is_direct_ssh_key(const char *txt);
static bool terminal_key_text_to_sequence(const char *txt, size_t len, const char **seq, size_t *seq_len);
static bool send_terminal_key_text(const char *txt, size_t len);
static void clear_vta_text(void);
static void ssh_send_terminal_text(const char *txt, size_t len);
static void position_ime_mode_toast(void);
static void show_ime_mode_toast(void);
static void toggle_ime_mode(void);
static void hw_keyboard_timer_cb(lv_timer_t *t);
static void hw_keyboard_handle_text(const char *txt, size_t len, uint8_t modifier);
static bool terminal_active_ssh_is_connected(void);
static int terminal_active_ssh_write(const char *data, size_t len);
static void terminal_return_to_server_list(void);
static void terminal_destroy_view(void);
static void terminal_session_destroy(terminal_session_t *session);
static bool terminal_session_is_listed(const terminal_session_t *session);
static bool terminal_session_retain(terminal_session_t *session);
static void terminal_session_release(terminal_session_t *session);
static void terminal_session_set_ssh_runtime_ref(terminal_session_t *session, bool active);
static bool terminal_session_consume_ssh_runtime_ref(terminal_session_t *session);
#if LV_USE_IME_PINYIN != 0
static bool pinyin_ime_has_composition(void);
static bool pinyin_cand_visible(void);
static uint8_t pinyin_candidate_count_on_page(const lv_ime_pinyin_t *ime);
static void pinyin_ime_reset_composition(void);
static bool pinyin_commit_raw_input(void);
static void update_ime_mode_label(void);
static void pinyin_hide_hardware_digit_labels(void);
static void pinyin_position_hardware_digit_labels(uint8_t count);
static void pinyin_create_hardware_digit_labels(void);
static void pinyin_restore_default_candidate_map(void);
static bool pinyin_handle_hardware_char(char ch);
static bool pinyin_handle_page_key_text(const char *txt, size_t len);
static bool pinyin_commit_candidate_index(uint8_t idx);
static bool pinyin_select_candidate_by_digit(char digit);
static void on_pinyin_cand_panel_preprocess(lv_event_t *e);
#endif

#define s_term (s_active_session->term)
#define s_term_mutex (s_active_session ? s_active_session->term_mutex : NULL)
#define s_server (s_active_session->server)
#define ssh_client_is_connected() terminal_active_ssh_is_connected()
#define ssh_client_write(data, len) terminal_active_ssh_write((data), (len))

/* Private implementation shards. Keep this file as the public UI entry point. */
#include "terminal/ui_terminal_feedback_ssh.inc"
#include "terminal/ui_terminal_layout.inc"
#include "terminal/ui_terminal_keymap.inc"
#include "terminal/ui_terminal_pinyin.inc"
#include "terminal/ui_terminal_input_events.inc"
#include "terminal/ui_terminal_lifecycle.inc"
