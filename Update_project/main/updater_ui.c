#include "updater_ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/m5stack_tab5.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "updater_config.h"

#define LCD_NATIVE_W BSP_LCD_H_RES
#define LCD_NATIVE_H BSP_LCD_V_RES
#define UI_W BSP_LCD_V_RES
#define UI_H BSP_LCD_H_RES

#define BAR_W 900
#define BAR_H 72
#define BAR_X ((UI_W - BAR_W) / 2)
#define BAR_Y ((UI_H - BAR_H) / 2)
#define BAR_BORDER 4

#define COLOR_BG 0x0841
#define COLOR_BAR_BG 0x0000
#define COLOR_BORDER 0x87F0
#define COLOR_GREEN 0x07E0
#define COLOR_ERROR 0xF800

static const char *TAG = "updater_ui";

static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static bool s_update_mode;
static int s_percent = -1;
static int s_drawn_percent = -1;
static int s_progress_fill_w;

static inline uint16_t rgb565(uint16_t color)
{
    return color;
}

static esp_err_t draw_native_rect(int x, int y, int w, int h, uint16_t color)
{
    enum { MAX_PIXELS = LCD_NATIVE_W * 16 };
    static uint16_t line_buf[MAX_PIXELS];

    if (!s_panel || w <= 0 || h <= 0) {
        return ESP_OK;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > LCD_NATIVE_W) {
        w = LCD_NATIVE_W - x;
    }
    if (y + h > LCD_NATIVE_H) {
        h = LCD_NATIVE_H - y;
    }
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    for (int i = 0; i < w; ++i) {
        line_buf[i] = rgb565(color);
    }

    while (h > 0) {
        const int rows = h > 16 ? 16 : h;
        for (int row = 1; row < rows; ++row) {
            memcpy(&line_buf[row * w], line_buf, (size_t)w * sizeof(line_buf[0]));
        }

        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + rows, line_buf),
                            TAG, "draw rect failed");
        y += rows;
        h -= rows;
        vTaskDelay(1);
    }

    return ESP_OK;
}

static esp_err_t draw_ui_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > UI_W) {
        w = UI_W - x;
    }
    if (y + h > UI_H) {
        h = UI_H - y;
    }
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    const int native_x = y;
    const int native_y = LCD_NATIVE_H - (x + w);
    return draw_native_rect(native_x, native_y, h, w, color);
}

static esp_err_t draw_progress_frame(void)
{
    s_progress_fill_w = 0;
    ESP_RETURN_ON_ERROR(draw_ui_rect(BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_BORDER),
                        TAG,
                        "draw border failed");
    return draw_ui_rect(BAR_X + BAR_BORDER,
                        BAR_Y + BAR_BORDER,
                        BAR_W - BAR_BORDER * 2,
                        BAR_H - BAR_BORDER * 2,
                        COLOR_BAR_BG);
}

static esp_err_t draw_progress(int percent)
{
    if (percent < 1) {
        percent = 1;
    } else if (percent > 100) {
        percent = 100;
    }

    const int inner_w = BAR_W - BAR_BORDER * 2;
    const int fill_w = (inner_w * percent) / 100;
    if (fill_w <= s_progress_fill_w) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(draw_ui_rect(BAR_X + BAR_BORDER + s_progress_fill_w,
                                     BAR_Y + BAR_BORDER,
                                     fill_w - s_progress_fill_w,
                                     BAR_H - BAR_BORDER * 2,
                                     COLOR_GREEN),
                        TAG,
                        "draw bar fill failed");
    s_progress_fill_w = fill_w;
    return ESP_OK;
}

esp_err_t updater_ui_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    bsp_lcd_handles_t handles = {0};

    ESP_LOGI(TAG, "init raw Tab5 display");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");
    bsp_io_expander_pi4ioe_init(bsp_i2c_get_handle());
    bsp_reset_tp();

    ESP_RETURN_ON_ERROR(bsp_display_new_tab5(NULL, &handles), TAG, "LCD init failed");
    s_panel = handles.panel;
    ESP_RETURN_ON_ERROR(draw_native_rect(0, 0, LCD_NATIVE_W, LCD_NATIVE_H, COLOR_BG),
                        TAG,
                        "clear screen failed");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "backlight failed");

    s_ready = true;
    s_update_mode = false;
    s_percent = -1;
    s_drawn_percent = -1;
    s_progress_fill_w = 0;
    ESP_LOGI(TAG, "raw display ready: panel=%s", bsp_display_get_panel_ic());
    return ESP_OK;
}

bool updater_ui_is_ready(void)
{
    return s_ready;
}

void updater_ui_force_backlight_off(void)
{
    if (!s_ready) {
        return;
    }
    ESP_LOGI(TAG, "backlight off");
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_off());
}

void updater_ui_show_update_mode(const char *status)
{
    (void)status;
    if (!s_ready) {
        return;
    }
    ESP_LOGI(TAG, "show raw update progress");
    s_update_mode = true;
    s_percent = 0;
    s_drawn_percent = 0;
    ESP_ERROR_CHECK_WITHOUT_ABORT(draw_progress_frame());
}

void updater_ui_set_status(const char *status)
{
    ESP_LOGI(TAG, "status: %s", status && status[0] ? status : "");
}

void updater_ui_set_progress(int percent)
{
    if (!s_ready || !s_update_mode) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    if (percent == s_percent) {
        return;
    }

    const bool force_draw = s_drawn_percent < 0 || percent <= 0 || percent >= 96;
    const bool enough_step = percent - s_drawn_percent >= UPDATER_UI_PROGRESS_MIN_STEP ||
                             s_drawn_percent - percent >= UPDATER_UI_PROGRESS_MIN_STEP;

    s_percent = percent;
    if (!force_draw && !enough_step) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(draw_progress(percent));
    s_drawn_percent = percent;
}

void updater_ui_set_error(const char *message)
{
    ESP_LOGE(TAG, "error: %s", message && message[0] ? message : "error");
    if (!s_ready) {
        return;
    }
    s_update_mode = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(draw_native_rect(0, 0, LCD_NATIVE_W, LCD_NATIVE_H, COLOR_ERROR));
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_on());
}

void updater_ui_blackout(void)
{
    if (!s_ready) {
        return;
    }
    ESP_LOGI(TAG, "blackout display");
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_off());
    vTaskDelay(pdMS_TO_TICKS(30));
}

void updater_ui_pump(void)
{
    vTaskDelay(pdMS_TO_TICKS(UPDATER_UI_PUMP_MS));
}
