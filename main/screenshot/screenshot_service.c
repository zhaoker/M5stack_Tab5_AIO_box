#include "screenshot/screenshot_service.h"

#include "bsp/m5stack_tab5.h"
#include "driver/jpeg_encode.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i18n/app_i18n.h"
#include "settings/app_settings.h"
#include "storage/tf_card.h"
#include "storage/tf_usb_disk_mode.h"
#include "theme.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "lvgl.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern esp_lcd_touch_handle_t bsp_display_get_touch_handle(void);

static const char *TAG = "screenshot";

#define SCREENSHOT_DIR_NAME           "ScreenShots"
#define SCREENSHOT_TASK_STACK         12288
#define SCREENSHOT_TASK_STACK_CAPS    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define SCREENSHOT_GESTURE_STACK      4096
#define SCREENSHOT_GESTURE_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define SCREENSHOT_QUEUE_LEN          2
#define SCREENSHOT_TOUCH_MAX_POINTS   5
#define SCREENSHOT_TRIGGER_POINTS     3
#define SCREENSHOT_SAMPLE_MS          28
#define SCREENSHOT_TRIGGER_DISTANCE   80.0f
#define SCREENSHOT_MAX_SPREAD_DELTA   160.0f
#define SCREENSHOT_COOLDOWN_MS        1600
#define SCREENSHOT_MIN_ACTIVE_MS      80
#define SCREENSHOT_JPEG_QUALITY       88
#define SCREENSHOT_JPEG_TIMEOUT_MS    4000
#define SCREENSHOT_JPEG_DMA_MIN_FREE  (24U * 1024U)
#define SCREENSHOT_JPEG_DMA_MIN_BLOCK (18U * 1024U)
#define SCREENSHOT_NAME_MAX_TRIES     702
#define SCREENSHOT_TOAST_VISIBLE_MS   2000
#define SCREENSHOT_TOAST_FADE_MS      360

typedef struct {
    bool tracking;
    bool fired;
    int64_t start_us;
    float start_cx;
    float start_cy;
    float start_spread;
} gesture_state_t;

typedef struct {
    char dir[160];
    char name[96];
} screenshot_toast_msg_t;

static TaskHandle_t s_gesture_task;
static TaskHandle_t s_capture_task;
static QueueHandle_t s_capture_queue;
static volatile bool s_started;
static volatile bool s_enabled_runtime;
static volatile bool s_capture_busy;
static int64_t s_last_capture_us;
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static bool service_should_enable(void)
{
    return app_settings_get_three_finger_screenshot_enabled() &&
           tf_card_is_mounted() &&
           !tf_usb_disk_mode_blocks_tf_access();
}

void screenshot_service_refresh_enabled(void)
{
    s_enabled_runtime = service_should_enable();
}

bool screenshot_service_is_capture_busy(void)
{
    return s_capture_busy;
}

static void build_screenshot_dir(char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", tf_card_get_mount_point(), SCREENSHOT_DIR_NAME);
}

static const char *basename_from_path(const char *path)
{
    const char *slash;

    if (!path) {
        return "";
    }

    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool path_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

static esp_err_t ensure_screenshot_dir(char *dir, size_t dir_len)
{
    build_screenshot_dir(dir, dir_len);
    if (mkdir(dir, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "mkdir %s failed: errno=%d", dir, errno);
    return ESP_FAIL;
}

static void make_suffix(int index, char *out, size_t out_len)
{
    char tmp[8];
    size_t len = 0;

    if (out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (index <= 0) {
        return;
    }

    index--;
    do {
        tmp[len++] = (char)('A' + (index % 26));
        index = (index / 26) - 1;
    } while (index >= 0 && len < sizeof(tmp));

    for (size_t i = 0; i < len && i + 1 < out_len; ++i) {
        out[i] = tmp[len - 1 - i];
        out[i + 1] = '\0';
    }
}

static void format_time_stem(char *out, size_t out_len)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (!out || out_len == 0) {
        return;
    }

    if (!localtime_r(&now, &tm_now) || strftime(out, out_len, "%Y%m%d_%H%M%S", &tm_now) == 0) {
        snprintf(out, out_len, "19700101_%06lld", (long long)(esp_timer_get_time() / 1000000) % 1000000LL);
    }
}

static esp_err_t make_unique_path_with_ext(char *path, size_t path_len, const char *ext)
{
    char dir[160];
    char stem[48];
    char suffix[8];

    if (!ext || !ext[0]) {
        ext = "jpg";
    }
    ESP_RETURN_ON_ERROR(ensure_screenshot_dir(dir, sizeof(dir)), TAG, "screenshot dir unavailable");
    format_time_stem(stem, sizeof(stem));

    for (int i = 0; i < SCREENSHOT_NAME_MAX_TRIES; ++i) {
        make_suffix(i, suffix, sizeof(suffix));
        int written = snprintf(path, path_len, "%s/%s%s.%s", dir, stem, suffix, ext);
        if (written < 0 || (size_t)written >= path_len) {
            return ESP_ERR_NO_MEM;
        }
        if (!path_exists(path)) {
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t make_unique_path(char *path, size_t path_len)
{
    return make_unique_path_with_ext(path, path_len, "jpg");
}

static esp_err_t write_jpeg_file(const char *path, const uint8_t *data, size_t len)
{
    char tmp_path[256];
    FILE *fp = NULL;

    int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof(tmp_path)) {
        return ESP_ERR_NO_MEM;
    }

    fp = fopen(tmp_path, "wb");
    if (!fp) {
        ESP_LOGW(TAG, "open %s failed: errno=%d", tmp_path, errno);
        return ESP_FAIL;
    }

    size_t out = fwrite(data, 1, len, fp);
    int close_ret = fclose(fp);
    fp = NULL;
    if (out != len || close_ret != 0) {
        unlink(tmp_path);
        ESP_LOGW(TAG, "write %s failed: wrote=%u expected=%u errno=%d",
                 tmp_path,
                 (unsigned)out,
                 (unsigned)len,
                 errno);
        return ESP_FAIL;
    }

    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        ESP_LOGW(TAG, "rename screenshot failed: errno=%d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void screenshot_write_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)(value >> 8);
}

static void screenshot_write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void screenshot_rgb565_to_bgr888(uint16_t px, uint8_t *dst)
{
    uint8_t r5 = (uint8_t)((px >> 11) & 0x1FU);
    uint8_t g6 = (uint8_t)((px >> 5) & 0x3FU);
    uint8_t b5 = (uint8_t)(px & 0x1FU);

    dst[0] = (uint8_t)((b5 << 3) | (b5 >> 2));
    dst[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    dst[2] = (uint8_t)((r5 << 3) | (r5 >> 2));
}

static esp_err_t write_bmp_file(const char *path,
                                const uint8_t *raw_buf,
                                uint32_t raw_size,
                                uint32_t width,
                                uint32_t height)
{
    char tmp_path[256];
    uint8_t header[54] = {0};
    uint8_t *row = NULL;
    FILE *fp = NULL;
    uint32_t src_stride;
    uint32_t bmp_row_size;
    uint32_t bmp_data_size;
    uint32_t bmp_file_size;
    esp_err_t ret = ESP_OK;

    if (!path || !raw_buf || raw_size == 0 || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    src_stride = raw_size / height;
    if (src_stride < width * 2U) {
        return ESP_ERR_INVALID_SIZE;
    }
    bmp_row_size = ((width * 3U) + 3U) & ~3U;
    if (height > (UINT32_MAX - sizeof(header)) / bmp_row_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    bmp_data_size = bmp_row_size * height;
    bmp_file_size = (uint32_t)sizeof(header) + bmp_data_size;

    int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof(tmp_path)) {
        return ESP_ERR_NO_MEM;
    }

    row = heap_caps_malloc(bmp_row_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!row) {
        row = heap_caps_malloc(bmp_row_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!row) {
        return ESP_ERR_NO_MEM;
    }

    header[0] = 'B';
    header[1] = 'M';
    screenshot_write_u32_le(header + 2, bmp_file_size);
    screenshot_write_u32_le(header + 10, (uint32_t)sizeof(header));
    screenshot_write_u32_le(header + 14, 40U);
    screenshot_write_u32_le(header + 18, width);
    screenshot_write_u32_le(header + 22, height);
    screenshot_write_u16_le(header + 26, 1U);
    screenshot_write_u16_le(header + 28, 24U);
    screenshot_write_u32_le(header + 34, bmp_data_size);

    fp = fopen(tmp_path, "wb");
    if (!fp) {
        ESP_LOGW(TAG, "open %s failed: errno=%d", tmp_path, errno);
        ret = ESP_FAIL;
        goto out;
    }
    if (fwrite(header, 1, sizeof(header), fp) != sizeof(header)) {
        ret = ESP_FAIL;
        goto out;
    }

    for (uint32_t out_y = 0; out_y < height; out_y++) {
        uint32_t src_y = height - 1U - out_y;
        const uint8_t *src = raw_buf + (size_t)src_y * src_stride;
        memset(row, 0, bmp_row_size);
        for (uint32_t x = 0; x < width; x++) {
            uint16_t px = (uint16_t)src[(size_t)x * 2U] |
                          ((uint16_t)src[(size_t)x * 2U + 1U] << 8);
            screenshot_rgb565_to_bgr888(px, row + (size_t)x * 3U);
        }
        if (fwrite(row, 1, bmp_row_size, fp) != bmp_row_size) {
            ret = ESP_FAIL;
            goto out;
        }
    }

out:
    if (fp) {
        if (fclose(fp) != 0 && ret == ESP_OK) {
            ret = ESP_FAIL;
        }
    }
    heap_caps_free(row);
    if (ret != ESP_OK) {
        unlink(tmp_path);
        ESP_LOGW(TAG, "write bmp screenshot failed: errno=%d", errno);
        return ret;
    }

    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        ESP_LOGW(TAG, "rename bmp screenshot failed: errno=%d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void log_screenshot_dma(const char *stage)
{
    ESP_LOGI(TAG,
             "screenshot dma %s: free=%lu largest=%lu",
             stage ? stage : "",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

static bool screenshot_jpeg_dma_ready(void)
{
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    if (free_dma < SCREENSHOT_JPEG_DMA_MIN_FREE ||
        largest_dma < SCREENSHOT_JPEG_DMA_MIN_BLOCK) {
        ESP_LOGW(TAG,
                 "screenshot jpeg skipped: dma tight free=%u largest=%u need_free=%u need_largest=%u",
                 (unsigned)free_dma,
                 (unsigned)largest_dma,
                 (unsigned)SCREENSHOT_JPEG_DMA_MIN_FREE,
                 (unsigned)SCREENSHOT_JPEG_DMA_MIN_BLOCK);
        return false;
    }
    return true;
}

static void delete_screenshot_toast(void)
{
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) {
        lv_obj_del(s_toast);
        s_toast = NULL;
    }
}

static void delete_screenshot_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    delete_screenshot_toast();
}

static void show_screenshot_toast_locked(const char *dir, const char *name)
{
    char line[256];

    delete_screenshot_toast();

    s_toast = lv_obj_create(lv_layer_top());
    if (!s_toast) {
        return;
    }
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(s_toast, 620);
    lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, LV_PCT(82), 0);
    lv_obj_set_style_pad_top(s_toast, 18, 0);
    lv_obj_set_style_pad_bottom(s_toast, 18, 0);
    lv_obj_set_style_pad_left(s_toast, 24, 0);
    lv_obj_set_style_pad_right(s_toast, 24, 0);
    lv_obj_set_style_pad_row(s_toast, 8, 0);
    lv_obj_set_flex_flow(s_toast, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_toast,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_toast, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_radius(s_toast, 12, 0);
    lv_obj_set_style_shadow_width(s_toast, 18, 0);
    lv_obj_set_style_shadow_opa(s_toast, 80, 0);
    lv_obj_set_style_shadow_color(s_toast, lv_color_hex(0x000000), 0);

    lv_obj_t *title = lv_label_create(s_toast);
    lv_label_set_text(title, TRS("截屏已保存到 TF 卡"));
    lv_obj_set_style_text_font(title, &APP_FONT, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    snprintf(line, sizeof(line), "%s\n%s", dir ? dir : "", name ? name : "");
    lv_obj_t *detail = lv_label_create(s_toast);
    lv_label_set_text(detail, line);
    lv_obj_set_width(detail, LV_PCT(100));
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(detail, &APP_FONT, 0);
    lv_obj_set_style_text_color(detail, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_center(s_toast);
    lv_obj_move_foreground(s_toast);
    lv_obj_fade_out(s_toast, SCREENSHOT_TOAST_FADE_MS, SCREENSHOT_TOAST_VISIBLE_MS);

    s_toast_timer = lv_timer_create(delete_screenshot_toast_timer_cb,
                                    SCREENSHOT_TOAST_VISIBLE_MS + SCREENSHOT_TOAST_FADE_MS + 80,
                                    NULL);
    if (s_toast_timer) {
        lv_timer_set_repeat_count(s_toast_timer, 1);
    }
}

static void show_screenshot_toast_async_cb(void *user_data)
{
    screenshot_toast_msg_t *msg = (screenshot_toast_msg_t *)user_data;

    if (!msg) {
        return;
    }
    show_screenshot_toast_locked(msg->dir, msg->name);
    free(msg);
}

static void show_screenshot_toast(const char *path)
{
    screenshot_toast_msg_t *msg = calloc(1, sizeof(*msg));

    if (!msg) {
        return;
    }
    build_screenshot_dir(msg->dir, sizeof(msg->dir));
    strlcpy(msg->name, basename_from_path(path), sizeof(msg->name));
    if (lv_async_call(show_screenshot_toast_async_cb, msg) != LV_RESULT_OK) {
        free(msg);
    }
}

static esp_err_t take_lvgl_snapshot(uint8_t **out_buf,
                                    uint32_t *out_size,
                                    uint32_t *out_width,
                                    uint32_t *out_height)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *screen = NULL;
    lv_draw_buf_t draw_buf = {0};
    uint8_t *buf = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t size = 0;

    if (!out_buf || !out_size || !out_width || !out_height) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_buf = NULL;
    *out_size = 0;
    *out_width = 0;
    *out_height = 0;

    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    screen = lv_scr_act();
    lv_obj_update_layout(screen);
    width = (uint32_t)lv_obj_get_width(screen);
    height = (uint32_t)lv_obj_get_height(screen);
    stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    size = LV_DRAW_BUF_SIZE(width, height, LV_COLOR_FORMAT_RGB565);
    buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ret = ESP_ERR_NO_MEM;
        goto out_unlock;
    }

    if (lv_draw_buf_init(&draw_buf,
                         width,
                         height,
                         LV_COLOR_FORMAT_RGB565,
                         LV_STRIDE_AUTO,
                         buf,
                         size) != LV_RESULT_OK) {
        ret = ESP_FAIL;
        goto out_free;
    }
    if (draw_buf.data != buf) {
        ESP_LOGW(TAG, "snapshot buffer alignment adjusted unexpectedly");
        ret = ESP_ERR_INVALID_STATE;
        goto out_free;
    }

    if (lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, &draw_buf) != LV_RESULT_OK) {
        ret = ESP_FAIL;
        goto out_free;
    }

    if (draw_buf.header.w != width || draw_buf.header.h != height ||
        draw_buf.header.cf != LV_COLOR_FORMAT_RGB565 || draw_buf.header.stride != stride) {
        ESP_LOGW(TAG,
                 "snapshot layout unexpected: %ux%u stride=%u cf=%u expected %ux%u stride=%u",
                 (unsigned)draw_buf.header.w,
                 (unsigned)draw_buf.header.h,
                 (unsigned)draw_buf.header.stride,
                 (unsigned)draw_buf.header.cf,
                 (unsigned)width,
                 (unsigned)height,
                 (unsigned)stride);
    }

    *out_buf = draw_buf.data;
    *out_size = draw_buf.header.stride * draw_buf.header.h;
    *out_width = draw_buf.header.w;
    *out_height = draw_buf.header.h;
    ret = ESP_OK;
    goto out_unlock;

out_free:
    heap_caps_free(buf);
out_unlock:
    bsp_display_unlock();
    return ret;
}

static esp_err_t encode_snapshot_to_jpeg(const uint8_t *raw_buf,
                                         uint32_t raw_size,
                                         uint32_t width,
                                         uint32_t height,
                                         uint8_t **out_jpeg,
                                         uint32_t *out_jpeg_size)
{
    jpeg_encoder_handle_t encoder = NULL;
    uint8_t *jpg_buf = NULL;
    size_t jpg_alloc_size = 0;
    uint32_t jpg_size = 0;
    esp_err_t ret;
    jpeg_encode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = SCREENSHOT_JPEG_TIMEOUT_MS,
    };
    jpeg_encode_memory_alloc_cfg_t jpg_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    jpeg_encode_cfg_t encode_cfg = {
        .height = height,
        .width = width,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = SCREENSHOT_JPEG_QUALITY,
    };

    if (!raw_buf || !out_jpeg || !out_jpeg_size || raw_size == 0 || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_jpeg = NULL;
    *out_jpeg_size = 0;

    log_screenshot_dma("jpeg-start");
    ret = jpeg_new_encoder_engine(&engine_cfg, &encoder);
    if (ret != ESP_OK) {
        log_screenshot_dma("jpeg-engine-failed");
        return ret;
    }

    jpg_buf = jpeg_alloc_encoder_mem(raw_size, &jpg_mem_cfg, &jpg_alloc_size);
    if (!jpg_buf) {
        log_screenshot_dma("jpeg-output-alloc-failed");
        ret = ESP_ERR_NO_MEM;
        goto out;
    }

    ret = jpeg_encoder_process(encoder,
                               &encode_cfg,
                               raw_buf,
                               raw_size,
                               jpg_buf,
                               (uint32_t)jpg_alloc_size,
                               &jpg_size);
    if (ret == ESP_OK) {
        *out_jpeg = jpg_buf;
        *out_jpeg_size = jpg_size;
        jpg_buf = NULL;
    }

out:
    if (jpg_buf) {
        heap_caps_free(jpg_buf);
    }
    jpeg_del_encoder_engine(encoder);
    return ret;
}

static esp_err_t capture_once(void)
{
    uint8_t *raw_buf = NULL;
    uint8_t *jpg_buf = NULL;
    uint32_t raw_size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t jpg_size = 0;
    char path[256];
    esp_err_t ret;

    if (!service_should_enable()) {
        screenshot_service_refresh_enabled();
        return ESP_ERR_INVALID_STATE;
    }

    ret = make_unique_path(path, sizeof(path));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = take_lvgl_snapshot(&raw_buf, &raw_size, &width, &height);
    if (ret != ESP_OK) {
        goto out;
    }

    if (screenshot_jpeg_dma_ready()) {
        ret = encode_snapshot_to_jpeg(raw_buf, raw_size, width, height, &jpg_buf, &jpg_size);
    } else {
        ret = ESP_ERR_NO_MEM;
    }
    if (ret != ESP_OK) {
        esp_err_t jpg_ret = ret;
        log_screenshot_dma("jpeg-fallback");
        ESP_LOGW(TAG, "jpeg screenshot unavailable: %s, save bmp fallback", esp_err_to_name(jpg_ret));
        ret = make_unique_path_with_ext(path, sizeof(path), "bmp");
        if (ret == ESP_OK) {
            ret = write_bmp_file(path, raw_buf, raw_size, width, height);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "screenshot saved as bmp fallback: %s (%ux%u, %u bytes raw)",
                         path,
                         (unsigned)width,
                         (unsigned)height,
                         (unsigned)raw_size);
                show_screenshot_toast(path);
            }
        }
        goto out;
    }

    ret = write_jpeg_file(path, jpg_buf, jpg_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "screenshot saved: %s (%ux%u, %u bytes)",
                 path,
                 (unsigned)width,
                 (unsigned)height,
                 (unsigned)jpg_size);
        show_screenshot_toast(path);
    }

out:
    if (raw_buf) {
        heap_caps_free(raw_buf);
    }
    if (jpg_buf) {
        heap_caps_free(jpg_buf);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "screenshot failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t screenshot_service_request_capture(void)
{
    uint8_t token = 1;
    int64_t now_us = esp_timer_get_time();

    if (!s_capture_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    screenshot_service_refresh_enabled();
    if (!s_enabled_runtime) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_capture_busy ||
        (s_last_capture_us > 0 &&
         now_us - s_last_capture_us < SCREENSHOT_COOLDOWN_MS * 1000LL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_capture_queue, &token, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_last_capture_us = now_us;
    return ESP_OK;
}

static bool read_touch_points(esp_lcd_touch_point_data_t *points, uint8_t *point_count)
{
    esp_lcd_touch_handle_t tp = bsp_display_get_touch_handle();

    if (!tp || !points || !point_count) {
        return false;
    }

    *point_count = 0;
    if (esp_lcd_touch_read_data(tp) != ESP_OK) {
        return false;
    }
    if (esp_lcd_touch_get_data(tp,
                               points,
                               point_count,
                               SCREENSHOT_TOUCH_MAX_POINTS) != ESP_OK) {
        *point_count = 0;
        return false;
    }
    return true;
}

static void calc_touch_centroid(const esp_lcd_touch_point_data_t *points,
                                uint8_t count,
                                float *cx,
                                float *cy,
                                float *spread)
{
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    float max_dist = 0.0f;

    for (uint8_t i = 0; i < count; ++i) {
        sum_x += points[i].x;
        sum_y += points[i].y;
    }

    *cx = sum_x / count;
    *cy = sum_y / count;

    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t j = i + 1; j < count; ++j) {
            float dx = (float)points[i].x - (float)points[j].x;
            float dy = (float)points[i].y - (float)points[j].y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > max_dist) {
                max_dist = dist;
            }
        }
    }
    *spread = max_dist;
}

static void reset_gesture(gesture_state_t *gesture)
{
    memset(gesture, 0, sizeof(*gesture));
}

static void update_gesture(gesture_state_t *gesture,
                           const esp_lcd_touch_point_data_t *points,
                           uint8_t point_count)
{
    float cx = 0.0f;
    float cy = 0.0f;
    float spread = 0.0f;
    float dx;
    float dy;
    float distance;
    int64_t now_us = esp_timer_get_time();

    if (point_count != SCREENSHOT_TRIGGER_POINTS) {
        reset_gesture(gesture);
        return;
    }

    calc_touch_centroid(points, point_count, &cx, &cy, &spread);

    if (!gesture->tracking) {
        gesture->tracking = true;
        gesture->start_us = now_us;
        gesture->start_cx = cx;
        gesture->start_cy = cy;
        gesture->start_spread = spread;
        gesture->fired = false;
        return;
    }

    if (gesture->fired) {
        return;
    }

    dx = cx - gesture->start_cx;
    dy = cy - gesture->start_cy;
    distance = sqrtf(dx * dx + dy * dy);
    if (distance >= SCREENSHOT_TRIGGER_DISTANCE &&
        fabsf(spread - gesture->start_spread) <= SCREENSHOT_MAX_SPREAD_DELTA &&
        now_us - gesture->start_us >= SCREENSHOT_MIN_ACTIVE_MS * 1000LL) {
        if (screenshot_service_request_capture() == ESP_OK) {
            gesture->fired = true;
        }
    }
}

static void screenshot_gesture_task(void *arg)
{
    gesture_state_t gesture = {0};
    esp_lcd_touch_point_data_t points[SCREENSHOT_TOUCH_MAX_POINTS];
    uint8_t point_count = 0;

    (void)arg;

    while (true) {
        screenshot_service_refresh_enabled();
        if (!s_enabled_runtime) {
            reset_gesture(&gesture);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (read_touch_points(points, &point_count)) {
            update_gesture(&gesture, points, point_count);
        } else {
            reset_gesture(&gesture);
        }

        vTaskDelay(pdMS_TO_TICKS(SCREENSHOT_SAMPLE_MS));
    }
}

static void screenshot_capture_task(void *arg)
{
    uint8_t token;

    (void)arg;

    while (true) {
        if (xQueueReceive(s_capture_queue, &token, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_capture_busy = true;
        (void)capture_once();
        s_capture_busy = false;
    }
}

esp_err_t screenshot_service_start(void)
{
    if (s_started) {
        screenshot_service_refresh_enabled();
        return ESP_OK;
    }

    s_capture_queue = xQueueCreate(SCREENSHOT_QUEUE_LEN, sizeof(uint8_t));
    if (!s_capture_queue) {
        return ESP_ERR_NO_MEM;
    }

    screenshot_service_refresh_enabled();

    if (xTaskCreateWithCaps(screenshot_capture_task,
                            "screenshot_cap",
                            SCREENSHOT_TASK_STACK,
                            NULL,
                            4,
                            &s_capture_task,
                            SCREENSHOT_TASK_STACK_CAPS) != pdPASS) {
        vQueueDelete(s_capture_queue);
        s_capture_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreateWithCaps(screenshot_gesture_task,
                            "screenshot_gst",
                            SCREENSHOT_GESTURE_STACK,
                            NULL,
                            4,
                            &s_gesture_task,
                            SCREENSHOT_GESTURE_STACK_CAPS) != pdPASS) {
        vTaskDeleteWithCaps(s_capture_task);
        s_capture_task = NULL;
        vQueueDelete(s_capture_queue);
        s_capture_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG,
             "three finger screenshot service started (%s)",
             s_enabled_runtime ? "enabled" : "disabled");
    return ESP_OK;
}
