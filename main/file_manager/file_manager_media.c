#include "file_manager/file_manager_media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "lvgl.h"
#include "file_manager/web_file_manager.h"
#include "wifi_manager.h"

#if LV_USE_TJPGD
#include "src/libs/tjpgd/tjpgd.h"
#endif

#if CONFIG_SOC_JPEG_DECODE_SUPPORTED
#include "driver/jpeg_decode.h"
#endif

#if CONFIG_SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif

static const char *TAG = "fm_media";

#define FM_MEDIA_MAX_HW_JPEG_BYTES (8U * 1024U * 1024U)
#define FM_MEDIA_PREVIEW_MAX_W 1280U
#define FM_MEDIA_PREVIEW_MAX_H 720U
#define FM_MEDIA_HW_DIRECT_MAX_W 1920U
#define FM_MEDIA_HW_DIRECT_MAX_H 1080U
#define FM_MEDIA_VIEWER_CHROME_H 0U
#define FM_MEDIA_PPA_ALIGN_FALLBACK 128U
#define FM_MEDIA_HW_JPEG_HEADROOM_BYTES (1024U * 1024U)
/* JPEG 硬解优先，但按系统状态动态保留 DMA：离线最激进，WiFi 忙时更保守。 */
#define FM_MEDIA_HW_DMA_OFFLINE_FREE_MIN_BYTES      (20U * 1024U)
#define FM_MEDIA_HW_DMA_OFFLINE_LARGEST_MIN_BYTES   (4U * 1024U)
#define FM_MEDIA_HW_DMA_CONNECTED_FREE_MIN_BYTES    (48U * 1024U)
#define FM_MEDIA_HW_DMA_CONNECTED_LARGEST_MIN_BYTES (16U * 1024U)
#define FM_MEDIA_HW_DMA_CONNECTING_FREE_MIN_BYTES   (64U * 1024U)
#define FM_MEDIA_HW_DMA_CONNECTING_LARGEST_MIN_BYTES (24U * 1024U)

#if CONFIG_SOC_PPA_SUPPORTED
static ppa_client_handle_t s_ppa_srm;
#endif

static uint32_t s_target_area_w;
static uint32_t s_target_area_h;
static TaskHandle_t s_target_area_owner;
static SemaphoreHandle_t s_target_area_lock;

typedef struct {
    size_t free_min;
    size_t largest_min;
    const char *mode;
} fm_media_dma_policy_t;

static bool fm_media_has_ext(const char *path, const char *ext)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && strcasecmp(dot + 1, ext) == 0;
}

static bool fm_media_is_jpeg(const char *path)
{
    return fm_media_has_ext(path, "jpg") || fm_media_has_ext(path, "jpeg");
}

static bool fm_media_is_bmp(const char *path)
{
    return fm_media_has_ext(path, "bmp");
}

static bool fm_media_is_1080p_or_smaller(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return false;
    }
    return (width <= FM_MEDIA_HW_DIRECT_MAX_W && height <= FM_MEDIA_HW_DIRECT_MAX_H) ||
           (width <= FM_MEDIA_HW_DIRECT_MAX_H && height <= FM_MEDIA_HW_DIRECT_MAX_W);
}

static bool fm_media_jpeg_has_segment_len(uint8_t marker)
{
    if (marker == 0x01 || marker == 0xD8 || marker == 0xD9 || marker == 0xDA) {
        return false;
    }
    return marker < 0xD0 || marker > 0xD7;
}

static bool fm_media_jpeg_is_sof(uint8_t marker)
{
    return marker >= 0xC0 && marker <= 0xCF &&
           marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

static esp_err_t fm_media_jpeg_read_main_size(const uint8_t *buf,
                                              size_t len,
                                              uint32_t *out_w,
                                              uint32_t *out_h,
                                              uint8_t *out_marker)
{
    size_t pos = 2;

    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    if (out_marker) {
        *out_marker = 0;
    }
    if (!buf || len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    while (pos < len) {
        while (pos < len && buf[pos] != 0xFF) {
            pos++;
        }
        while (pos < len && buf[pos] == 0xFF) {
            pos++;
        }
        if (pos >= len) {
            break;
        }
        uint8_t marker = buf[pos++];
        if (marker == 0xDA || marker == 0xD9) {
            break;
        }
        if (!fm_media_jpeg_has_segment_len(marker)) {
            continue;
        }
        if (pos + 2U > len) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint16_t seg_len = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
        if (seg_len < 2 || pos + seg_len > len) {
            ESP_LOGW(TAG,
                     "JPEG segment invalid while scanning main size: marker=0x%02X pos=%lu len=%u file=%lu",
                     (unsigned)marker,
                     (unsigned long)(pos - 2U),
                     (unsigned)seg_len,
                     (unsigned long)len);
            return ESP_ERR_INVALID_SIZE;
        }
        if (fm_media_jpeg_is_sof(marker)) {
            if (seg_len < 8) {
                return ESP_ERR_INVALID_SIZE;
            }
            uint32_t h = ((uint32_t)buf[pos + 3] << 8) | buf[pos + 4];
            uint32_t w = ((uint32_t)buf[pos + 5] << 8) | buf[pos + 6];
            if (w == 0 || h == 0) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (out_w) {
                *out_w = w;
            }
            if (out_h) {
                *out_h = h;
            }
            if (out_marker) {
                *out_marker = marker;
            }
            return ESP_OK;
        }
        pos += seg_len;
    }
    return ESP_ERR_NOT_FOUND;
}

static void fm_media_set_status(char *status, size_t status_len, const char *text)
{
    if (status && status_len > 0) {
        snprintf(status, status_len, "%s", text ? text : "");
    }
}

static fm_media_dma_policy_t fm_media_jpeg_dma_policy(void)
{
    wifi_state_t state = wifi_manager_get_state();

    if (state == WIFI_STATE_CONNECTING) {
        return (fm_media_dma_policy_t) {
            .free_min = FM_MEDIA_HW_DMA_CONNECTING_FREE_MIN_BYTES,
            .largest_min = FM_MEDIA_HW_DMA_CONNECTING_LARGEST_MIN_BYTES,
            .mode = "wifi-connecting",
        };
    }
    if (state == WIFI_STATE_CONNECTED) {
        return (fm_media_dma_policy_t) {
            .free_min = FM_MEDIA_HW_DMA_CONNECTED_FREE_MIN_BYTES,
            .largest_min = FM_MEDIA_HW_DMA_CONNECTED_LARGEST_MIN_BYTES,
            .mode = "wifi-connected",
        };
    }
    return (fm_media_dma_policy_t) {
        .free_min = FM_MEDIA_HW_DMA_OFFLINE_FREE_MIN_BYTES,
        .largest_min = FM_MEDIA_HW_DMA_OFFLINE_LARGEST_MIN_BYTES,
        .mode = "offline",
    };
}

static bool fm_media_dma_watermark_ok(const char *reason)
{
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    fm_media_dma_policy_t policy = fm_media_jpeg_dma_policy();

    if (free_dma < policy.free_min || largest_dma < policy.largest_min) {
        ESP_LOGW(TAG,
                 "DMA low, use JPEG software path: reason=%s mode=%s free=%lu largest=%lu need_free=%lu need_largest=%lu",
                 reason ? reason : "",
                 policy.mode,
                 (unsigned long)free_dma,
                 (unsigned long)largest_dma,
                 (unsigned long)policy.free_min,
                 (unsigned long)policy.largest_min);
        return false;
    }
    ESP_LOGI(TAG,
             "DMA ok for JPEG hardware: reason=%s mode=%s free=%lu largest=%lu need_free=%lu need_largest=%lu",
             reason ? reason : "",
             policy.mode,
             (unsigned long)free_dma,
             (unsigned long)largest_dma,
             (unsigned long)policy.free_min,
             (unsigned long)policy.largest_min);
    return true;
}

static size_t fm_media_ppa_alignment(void)
{
    size_t alignment = FM_MEDIA_PPA_ALIGN_FALLBACK;

#if CONFIG_SOC_PPA_SUPPORTED
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment) != ESP_OK ||
        alignment == 0) {
        alignment = FM_MEDIA_PPA_ALIGN_FALLBACK;
    }
#endif
    return alignment;
}

static uint8_t *fm_media_alloc_pixels(size_t size)
{
    uint8_t *pixels = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        pixels = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!pixels) {
        pixels = (uint8_t *)malloc(size);
    }
    return pixels;
}

static uint8_t *fm_media_alloc_ppa_pixels(size_t size, size_t *out_alloc_size)
{
    size_t alignment = fm_media_ppa_alignment();
    size_t alloc_size = (size + alignment - 1U) & ~(alignment - 1U);
    uint8_t *pixels;

    if (alloc_size == 0) {
        alloc_size = alignment;
    }
    pixels = (uint8_t *)heap_caps_aligned_alloc(alignment,
                                                alloc_size,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        pixels = (uint8_t *)heap_caps_aligned_alloc(alignment,
                                                    alloc_size,
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pixels && out_alloc_size) {
        *out_alloc_size = alloc_size;
    }
    return pixels;
}

static uint32_t fm_media_fit_dimension(uint32_t src, uint32_t other, uint32_t max_src, uint32_t max_other)
{
    uint64_t w = src;
    uint64_t h = other;

    if (src == 0 || other == 0) {
        return src;
    }
    if (w > max_src) {
        h = (h * max_src) / w;
        w = max_src;
    }
    if (h > max_other) {
        w = (w * max_other) / h;
        h = max_other;
    }
    if (w == 0) {
        w = 1;
    }
    return (uint32_t)w;
}

static uint32_t fm_media_scaled_height(uint32_t src_w, uint32_t src_h, uint32_t dst_w)
{
    uint32_t dst_h;

    if (src_w == 0 || src_h == 0 || dst_w == 0) {
        return 0;
    }
    dst_h = (uint32_t)(((uint64_t)src_h * dst_w) / src_w);
    return dst_h ? dst_h : 1;
}

static size_t fm_media_ppa_temp_size(uint32_t src_w,
                                     uint32_t src_h,
                                     uint32_t dst_w,
                                     uint32_t dst_h)
{
    uint32_t scale_x_q;
    uint32_t scale_y_q;
    uint32_t hw_w;
    uint32_t hw_h;

    if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
        return 0;
    }
    scale_x_q = (uint32_t)(((uint64_t)dst_w * 16U + src_w - 1U) / src_w);
    scale_y_q = (uint32_t)(((uint64_t)dst_h * 16U + src_h - 1U) / src_h);
    if (scale_x_q == 0) {
        scale_x_q = 1;
    }
    if (scale_y_q == 0) {
        scale_y_q = 1;
    }
    hw_w = (uint32_t)(((uint64_t)src_w * scale_x_q) / 16U);
    hw_h = (uint32_t)(((uint64_t)src_h * scale_y_q) / 16U);
    while (hw_w < dst_w) {
        scale_x_q++;
        hw_w = (uint32_t)(((uint64_t)src_w * scale_x_q) / 16U);
    }
    while (hw_h < dst_h) {
        scale_y_q++;
        hw_h = (uint32_t)(((uint64_t)src_h * scale_y_q) / 16U);
    }
    size_t size = (size_t)hw_w * hw_h * 2U;
    size_t alignment = fm_media_ppa_alignment();
    return (size + alignment - 1U) & ~(alignment - 1U);
}

static bool fm_media_should_use_full_hw_jpeg(uint32_t src_w,
                                             uint32_t src_h,
                                             uint32_t out_w,
                                             uint32_t out_h,
                                             uint32_t preview_w,
                                             uint32_t preview_h,
                                             size_t *out_required,
                                             size_t *out_scratch,
                                             size_t *out_free,
                                             size_t *out_largest)
{
    size_t full_size = (size_t)out_w * out_h * 2U;
    size_t preview_size = (size_t)preview_w * preview_h * 2U;
    size_t ppa_size = fm_media_ppa_temp_size(src_w, src_h, preview_w, preview_h);
    size_t scratch = ppa_size > preview_size ? ppa_size : preview_size;
    size_t required = full_size + scratch + FM_MEDIA_HW_JPEG_HEADROOM_BYTES;
    size_t free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    if (out_required) {
        *out_required = required;
    }
    if (out_scratch) {
        *out_scratch = scratch;
    }
    if (out_free) {
        *out_free = free_size;
    }
    if (out_largest) {
        *out_largest = largest;
    }
    return largest >= full_size && free_size >= required;
}

static inline uint16_t fm_media_read_rgb565_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void fm_media_write_rgb565_le(uint8_t *p, uint16_t c)
{
    p[0] = (uint8_t)(c & 0xFFU);
    p[1] = (uint8_t)(c >> 8);
}

static uint16_t fm_media_mix_rgb565(uint16_t c00,
                                    uint16_t c10,
                                    uint16_t c01,
                                    uint16_t c11,
                                    uint32_t wx,
                                    uint32_t wy)
{
    uint32_t ix = 256U - wx;
    uint32_t iy = 256U - wy;
    uint32_t r0 = ((((c00 >> 11) & 0x1FU) * ix) + (((c10 >> 11) & 0x1FU) * wx) + 128U) >> 8;
    uint32_t r1 = ((((c01 >> 11) & 0x1FU) * ix) + (((c11 >> 11) & 0x1FU) * wx) + 128U) >> 8;
    uint32_t g0 = ((((c00 >> 5) & 0x3FU) * ix) + (((c10 >> 5) & 0x3FU) * wx) + 128U) >> 8;
    uint32_t g1 = ((((c01 >> 5) & 0x3FU) * ix) + (((c11 >> 5) & 0x3FU) * wx) + 128U) >> 8;
    uint32_t b0 = (((c00 & 0x1FU) * ix) + ((c10 & 0x1FU) * wx) + 128U) >> 8;
    uint32_t b1 = (((c01 & 0x1FU) * ix) + ((c11 & 0x1FU) * wx) + 128U) >> 8;
    uint32_t r = (r0 * iy + r1 * wy + 128U) >> 8;
    uint32_t g = (g0 * iy + g1 * wy + 128U) >> 8;
    uint32_t b = (b0 * iy + b1 * wy + 128U) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint8_t *fm_media_scale_rgb565_bilinear(const uint8_t *src,
                                               uint32_t src_w,
                                               uint32_t src_h,
                                               uint32_t src_stride_px,
                                               uint32_t dst_w,
                                               uint32_t dst_h)
{
    size_t dst_stride = (size_t)dst_w * 2U;
    size_t dst_size = dst_stride * dst_h;
    uint8_t *dst = fm_media_alloc_pixels(dst_size);

    if (!src || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0 || !dst) {
        free(dst);
        return NULL;
    }
    for (uint32_t y = 0; y < dst_h; y++) {
        uint64_t gy = (dst_h > 1 && src_h > 1) ? ((uint64_t)y * (src_h - 1U) * 256U) / (dst_h - 1U) : 0;
        uint32_t y0 = (uint32_t)(gy >> 8);
        uint32_t y1 = y0 + 1U < src_h ? y0 + 1U : y0;
        uint32_t wy = (uint32_t)(gy & 0xFFU);
        const uint8_t *src_row0 = src + (size_t)y0 * src_stride_px * 2U;
        const uint8_t *src_row1 = src + (size_t)y1 * src_stride_px * 2U;
        uint8_t *dst_row = dst + (size_t)y * dst_stride;
        for (uint32_t x = 0; x < dst_w; x++) {
            uint64_t gx = (dst_w > 1 && src_w > 1) ? ((uint64_t)x * (src_w - 1U) * 256U) / (dst_w - 1U) : 0;
            uint32_t x0 = (uint32_t)(gx >> 8);
            uint32_t x1 = x0 + 1U < src_w ? x0 + 1U : x0;
            uint32_t wx = (uint32_t)(gx & 0xFFU);
            uint16_t c00 = fm_media_read_rgb565_le(src_row0 + (size_t)x0 * 2U);
            uint16_t c10 = fm_media_read_rgb565_le(src_row0 + (size_t)x1 * 2U);
            uint16_t c01 = fm_media_read_rgb565_le(src_row1 + (size_t)x0 * 2U);
            uint16_t c11 = fm_media_read_rgb565_le(src_row1 + (size_t)x1 * 2U);
            fm_media_write_rgb565_le(dst_row + (size_t)x * 2U, fm_media_mix_rgb565(c00, c10, c01, c11, wx, wy));
        }
    }
    return dst;
}

static uint8_t *fm_media_scale_rgb565_ppa(const uint8_t *src,
                                          uint32_t src_w,
                                          uint32_t src_h,
                                          uint32_t src_stride_px,
                                          uint32_t dst_w,
                                          uint32_t dst_h,
                                          size_t *out_size,
                                          const char *tag)
{
#if CONFIG_SOC_PPA_SUPPORTED
    if (!src || src_w == 0 || src_h == 0 || src_stride_px < src_w || dst_w == 0 || dst_h == 0) {
        return NULL;
    }
    if (!file_manager_media_prepare_ppa(NULL, 0)) {
        ESP_LOGW(TAG, "PPA scale unavailable: %s", tag ? tag : "");
        return NULL;
    }

    uint32_t scale_x_q = (uint32_t)(((uint64_t)dst_w * 16U + src_w - 1U) / src_w);
    uint32_t scale_y_q = (uint32_t)(((uint64_t)dst_h * 16U + src_h - 1U) / src_h);
    if (scale_x_q == 0) {
        scale_x_q = 1;
    }
    if (scale_y_q == 0) {
        scale_y_q = 1;
    }
    float scale_x = (float)scale_x_q / 16.0f;
    float scale_y = (float)scale_y_q / 16.0f;
    uint32_t hw_w = (uint32_t)(((uint64_t)src_w * scale_x_q) / 16U);
    uint32_t hw_h = (uint32_t)(((uint64_t)src_h * scale_y_q) / 16U);
    while (hw_w < dst_w) {
        scale_x_q++;
        scale_x = (float)scale_x_q / 16.0f;
        hw_w = (uint32_t)(((uint64_t)src_w * scale_x_q) / 16U);
    }
    while (hw_h < dst_h) {
        scale_y_q++;
        scale_y = (float)scale_y_q / 16.0f;
        hw_h = (uint32_t)(((uint64_t)src_h * scale_y_q) / 16U);
    }
    if (hw_w == 0 || hw_h == 0) {
        return NULL;
    }

    size_t ppa_size = (size_t)hw_w * hw_h * 2U;
    size_t ppa_alloc_size = 0;
    uint8_t *ppa_pixels = fm_media_alloc_ppa_pixels(ppa_size, &ppa_alloc_size);
    if (!ppa_pixels) {
        ESP_LOGW(TAG,
                 "PPA scale alloc failed: %s %lux%lu -> %lux%lu bytes=%lu",
                 tag ? tag : "",
                 (unsigned long)src_w,
                 (unsigned long)src_h,
                 (unsigned long)hw_w,
                 (unsigned long)hw_h,
                 (unsigned long)ppa_size);
        return NULL;
    }

    ppa_srm_oper_config_t cfg = {
        .in.buffer = src,
        .in.pic_w = src_stride_px,
        .in.pic_h = src_h,
        .in.block_w = src_w,
        .in.block_h = src_h,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = ppa_pixels,
        .out.buffer_size = (uint32_t)ppa_alloc_size,
        .out.pic_w = hw_w,
        .out.pic_h = hw_h,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_LOGI(TAG,
             "PPA scale start: %s %lux%lu stride=%lu -> target=%lux%lu hw=%lux%lu factor=%.4fx%.4f alloc=%lu",
             tag ? tag : "",
             (unsigned long)src_w,
             (unsigned long)src_h,
             (unsigned long)src_stride_px,
             (unsigned long)dst_w,
             (unsigned long)dst_h,
             (unsigned long)hw_w,
             (unsigned long)hw_h,
             (double)scale_x,
             (double)scale_y,
             (unsigned long)ppa_alloc_size);
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PPA scale failed: %s err=%s", tag ? tag : "", esp_err_to_name(err));
        free(ppa_pixels);
        return NULL;
    }

    if (hw_w == dst_w && hw_h == dst_h) {
        if (out_size) {
            *out_size = ppa_size;
        }
        ESP_LOGI(TAG, "PPA scale done: %s out=%lux%lu bytes=%lu",
                 tag ? tag : "",
                 (unsigned long)hw_w,
                 (unsigned long)hw_h,
                 (unsigned long)ppa_size);
        return ppa_pixels;
    }

    ESP_LOGI(TAG,
             "PPA scale needs final trim: %s hw=%lux%lu -> final=%lux%lu",
             tag ? tag : "",
             (unsigned long)hw_w,
             (unsigned long)hw_h,
             (unsigned long)dst_w,
             (unsigned long)dst_h);
    uint8_t *final_pixels = fm_media_scale_rgb565_bilinear(ppa_pixels,
                                                           hw_w,
                                                           hw_h,
                                                           hw_w,
                                                           dst_w,
                                                           dst_h);
    free(ppa_pixels);
    if (!final_pixels) {
        ESP_LOGW(TAG, "PPA final trim alloc failed: %s", tag ? tag : "");
        return NULL;
    }
    if (out_size) {
        *out_size = (size_t)dst_w * dst_h * 2U;
    }
    return final_pixels;
#else
    (void)src;
    (void)src_w;
    (void)src_h;
    (void)src_stride_px;
    (void)dst_w;
    (void)dst_h;
    (void)out_size;
    (void)tag;
    return NULL;
#endif
}

static uint8_t *fm_media_scale_rgb565_preview(const uint8_t *src,
                                              uint32_t src_w,
                                              uint32_t src_h,
                                              uint32_t src_stride_px,
                                              uint32_t dst_w,
                                              uint32_t dst_h,
                                              bool *used_ppa,
                                              const char *tag)
{
    size_t out_size = 0;
    uint8_t *dst = fm_media_scale_rgb565_ppa(src,
                                             src_w,
                                             src_h,
                                             src_stride_px,
                                             dst_w,
                                             dst_h,
                                             &out_size,
                                             tag);
    if (dst) {
        if (used_ppa) {
            *used_ppa = true;
        }
        (void)out_size;
        return dst;
    }

    ESP_LOGI(TAG,
             "SW bilinear scale fallback: %s %lux%lu -> %lux%lu",
             tag ? tag : "",
             (unsigned long)src_w,
             (unsigned long)src_h,
             (unsigned long)dst_w,
             (unsigned long)dst_h);
    dst = fm_media_scale_rgb565_bilinear(src, src_w, src_h, src_stride_px, dst_w, dst_h);
    if (used_ppa) {
        *used_ppa = false;
    }
    return dst;
}

static void fm_media_get_viewer_area(uint32_t *out_w, uint32_t *out_h)
{
    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    int32_t screen_h = lv_display_get_vertical_resolution(NULL);

    if (screen_w <= 0) {
        screen_w = 720;
    }
    if (screen_h <= (int32_t)FM_MEDIA_VIEWER_CHROME_H) {
        screen_h = 1280;
    }
    if (out_w) {
        *out_w = (uint32_t)screen_w;
    }
    if (out_h) {
        *out_h = (uint32_t)(screen_h - FM_MEDIA_VIEWER_CHROME_H);
    }
}

static void fm_media_set_target_area(uint32_t area_w, uint32_t area_h)
{
    s_target_area_w = area_w;
    s_target_area_h = area_h;
    s_target_area_owner = xTaskGetCurrentTaskHandle();
}

static void fm_media_clear_target_area(void)
{
    s_target_area_w = 0;
    s_target_area_h = 0;
    s_target_area_owner = NULL;
}

static void fm_media_get_target_area(uint32_t *out_w, uint32_t *out_h)
{
    if (s_target_area_owner == xTaskGetCurrentTaskHandle() &&
        s_target_area_w > 0 &&
        s_target_area_h > 0) {
        if (out_w) {
            *out_w = s_target_area_w;
        }
        if (out_h) {
            *out_h = s_target_area_h;
        }
        return;
    }
    fm_media_get_viewer_area(out_w, out_h);
}

static uint16_t fm_media_rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

static uint16_t fm_media_bmp_read_pixel_rgb565(const uint8_t *src, uint16_t bpp)
{
    if (bpp == 16) {
        return fm_media_read_rgb565_le(src);
    }
    if (bpp == 24 || bpp == 32) {
        return fm_media_rgb888_to_rgb565(src[2], src[1], src[0]);
    }
    return 0;
}

static esp_err_t fm_media_decode_bmp(const char *path,
                                     file_manager_image_t *image,
                                     char *status,
                                     size_t status_len)
{
    FILE *fp = fopen(path, "rb");
    uint8_t header[54];
    uint32_t pixel_offset;
    int32_t width_signed;
    int32_t height_signed;
    uint32_t width;
    uint32_t height;
    uint16_t bpp;
    uint32_t compression;
    uint32_t row_size;
    uint8_t *row = NULL;
    uint8_t *pixels = NULL;
    uint8_t *preview = NULL;

    if (!fp) {
        fm_media_set_status(status, status_len, TRS("BMP 文件无法读取"));
        return ESP_FAIL;
    }
    if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
        header[0] != 'B' || header[1] != 'M') {
        fclose(fp);
        fm_media_set_status(status, status_len, TRS("BMP 文件头无效"));
        return ESP_ERR_INVALID_RESPONSE;
    }

    memcpy(&pixel_offset, header + 10, sizeof(pixel_offset));
    memcpy(&width_signed, header + 18, sizeof(width_signed));
    memcpy(&height_signed, header + 22, sizeof(height_signed));
    memcpy(&bpp, header + 28, sizeof(bpp));
    memcpy(&compression, header + 30, sizeof(compression));

    if (width_signed <= 0 || height_signed == 0 || compression != 0 ||
        (bpp != 16 && bpp != 24 && bpp != 32)) {
        fclose(fp);
        ESP_LOGW(TAG,
                 "BMP unsupported: %s w=%ld h=%ld bpp=%u compression=%lu",
                 path,
                 (long)width_signed,
                 (long)height_signed,
                 (unsigned)bpp,
                 (unsigned long)compression);
        fm_media_set_status(status, status_len, TRS("BMP 格式暂不支持"));
        return ESP_ERR_NOT_SUPPORTED;
    }

    width = (uint32_t)width_signed;
    height = height_signed < 0 ? (uint32_t)(-height_signed) : (uint32_t)height_signed;
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        fclose(fp);
        fm_media_set_status(status, status_len, TRS("BMP 尺寸无效"));
        return ESP_ERR_INVALID_SIZE;
    }

    row_size = ((uint32_t)bpp * width + 31U) / 32U * 4U;
    size_t pixel_size = (size_t)width * height * 2U;
    row = (uint8_t *)malloc(row_size);
    pixels = fm_media_alloc_pixels(pixel_size);
    if (!row || !pixels) {
        free(row);
        free(pixels);
        fclose(fp);
        fm_media_set_status(status, status_len, TRS("BMP 解码内存不足"));
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "BMP decode start: %s %lux%lu bpp=%u row=%lu bytes=%lu top_down=%d",
             path,
             (unsigned long)width,
             (unsigned long)height,
             (unsigned)bpp,
             (unsigned long)row_size,
             (unsigned long)pixel_size,
             height_signed < 0 ? 1 : 0);

    for (uint32_t src_y = 0; src_y < height; src_y++) {
        uint32_t dst_y = height_signed < 0 ? src_y : (height - 1U - src_y);
        long row_pos = (long)pixel_offset + (long)((uint64_t)src_y * row_size);
        if (fseek(fp, row_pos, SEEK_SET) != 0 ||
            fread(row, 1, row_size, fp) != row_size) {
            free(row);
            free(pixels);
            fclose(fp);
            fm_media_set_status(status, status_len, TRS("BMP 像素读取失败"));
            return ESP_FAIL;
        }
        uint8_t *dst = pixels + (size_t)dst_y * width * 2U;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t *src = row + (size_t)x * (bpp / 8U);
            fm_media_write_rgb565_le(dst + (size_t)x * 2U, fm_media_bmp_read_pixel_rgb565(src, bpp));
        }
    }
    free(row);
    fclose(fp);

    uint32_t preview_w = fm_media_fit_dimension(width, height,
                                                FM_MEDIA_PREVIEW_MAX_W,
                                                FM_MEDIA_PREVIEW_MAX_H);
    uint32_t preview_h = fm_media_scaled_height(width, height, preview_w);
    size_t preview_stride = (size_t)preview_w * 2U;
    size_t preview_size = preview_stride * preview_h;
    bool used_ppa = false;

    if (preview_w != width || preview_h != height) {
        preview = fm_media_scale_rgb565_preview(pixels,
                                                width,
                                                height,
                                                width,
                                                preview_w,
                                                preview_h,
                                                &used_ppa,
                                                "bmp-preview");
        free(pixels);
        if (!preview) {
            fm_media_set_status(status, status_len, TRS("BMP 预览缩放内存不足"));
            return ESP_ERR_NO_MEM;
        }
    } else {
        preview = pixels;
    }

    image->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    image->dsc.header.flags = 0;
    image->dsc.header.w = (uint16_t)preview_w;
    image->dsc.header.h = (uint16_t)preview_h;
    image->dsc.header.stride = (uint16_t)preview_stride;
    image->dsc.data_size = preview_size;
    image->dsc.data = preview;
    image->source_w = preview_w;
    image->source_h = preview_h;
    image->hardware_jpeg = false;
    image->ppa_ready = used_ppa;
    fm_media_set_status(status,
                        status_len,
                        used_ppa ? "BMP 解码 + PPA 预览显示" : "BMP 解码预览显示");
    ESP_LOGI(TAG,
             "BMP decode done: %s src=%lux%lu preview=%lux%lu bytes=%lu method=%s",
             path,
             (unsigned long)width,
             (unsigned long)height,
             (unsigned long)preview_w,
             (unsigned long)preview_h,
             (unsigned long)preview_size,
             used_ppa ? "ppa" : "sw");
    return ESP_OK;
}

#include "file_manager_media_jpeg.inc"
