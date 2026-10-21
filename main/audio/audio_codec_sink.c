#include "audio/audio_codec_sink.h"

#include <limits.h>
#include <string.h>

#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio_sink";

#define AUDIO_SINK_WRITE_LOG_STEP_BYTES (512 * 1024U)
/* 音乐播放优先：DMA 余量只做告警，最终由 I2S/codec 驱动返回真实分配结果。 */
#define AUDIO_SINK_INIT_DMA_FREE_WARN_BYTES (12U * 1024U)
#define AUDIO_SINK_INIT_DMA_LARGEST_WARN_BYTES (4U * 1024U)
#define AUDIO_SINK_OPEN_DMA_FREE_WARN_BYTES (8U * 1024U)
#define AUDIO_SINK_OPEN_DMA_LARGEST_WARN_BYTES (4U * 1024U)
#define AUDIO_SINK_HEADPHONE_POLL_MS 300U
#define AUDIO_SINK_HEADPHONE_TASK_STACK 2048
#define AUDIO_SINK_HEADPHONE_TASK_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define AUDIO_SINK_HEADPHONE_TASK_STACK_FALLBACK_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define AUDIO_SINK_HEADPHONE_TASK_PRIORITY 2

static SemaphoreHandle_t s_lock;
static esp_codec_dev_handle_t s_codec;
static audio_codec_sink_format_t s_format;
static uint8_t s_volume = 80;
static bool s_muted;
static bool s_open;
static uint64_t s_written_bytes;
static uint64_t s_next_write_log_bytes;
static TaskHandle_t s_headphone_task;
static volatile bool s_headphone_stop_requested;

static esp_err_t audio_codec_sink_lock_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t audio_codec_sink_ensure_codec_locked(void)
{
    if (s_codec) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "BSP speaker init begin");
    s_codec = bsp_audio_codec_speaker_init();
    if (!s_codec) {
        ESP_LOGE(TAG, "speaker codec init failed");
        return ESP_FAIL;
    }
    (void)esp_codec_set_disable_when_closed(s_codec, false);
    ESP_LOGI(TAG, "BSP speaker init done: codec=%p", s_codec);
    return ESP_OK;
}

static void audio_codec_sink_log_dma_state(const char *stage, size_t warn_free, size_t warn_largest)
{
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    if (free_dma < warn_free || largest_dma < warn_largest) {
        ESP_LOGW(TAG,
                 "%s: DMA memory low, free=%u largest=%u warn_free=%u warn_largest=%u; trying codec driver",
                 stage ? stage : "codec",
                 (unsigned)free_dma,
                 (unsigned)largest_dma,
                 (unsigned)warn_free,
                 (unsigned)warn_largest);
        return;
    }
    ESP_LOGI(TAG,
             "%s: DMA memory ready, free=%u largest=%u",
             stage ? stage : "codec",
             (unsigned)free_dma,
             (unsigned)largest_dma);
}

static esp_err_t audio_codec_sink_apply_headphone_route(bool headphone_inserted, const char *reason)
{
    esp_err_t ret = bsp_set_speaker_enable(!headphone_inserted);
    ESP_LOGI(TAG,
             "headphone route: %s headphone=%u speaker=%s ret=%s",
             reason ? reason : "update",
             headphone_inserted ? 1U : 0U,
             headphone_inserted ? "off" : "on",
             esp_err_to_name(ret));
    return ret;
}

static void audio_codec_sink_headphone_task(void *arg)
{
    (void)arg;
    bool last_valid = false;
    bool last_inserted = false;

    while (!s_headphone_stop_requested) {
        bool inserted = bsp_headphone_detect();
        if (!last_valid || inserted != last_inserted) {
            (void)audio_codec_sink_apply_headphone_route(inserted, "monitor");
            last_inserted = inserted;
            last_valid = true;
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_SINK_HEADPHONE_POLL_MS));
    }

    s_headphone_task = NULL;
    s_headphone_stop_requested = false;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t audio_codec_sink_start_headphone_monitor(void)
{
    if (s_headphone_task) {
        return ESP_OK;
    }
    s_headphone_stop_requested = false;
    BaseType_t task_ret = xTaskCreateWithCaps(audio_codec_sink_headphone_task,
                                              "hp_detect",
                                              AUDIO_SINK_HEADPHONE_TASK_STACK,
                                              NULL,
                                              AUDIO_SINK_HEADPHONE_TASK_PRIORITY,
                                              &s_headphone_task,
                                              AUDIO_SINK_HEADPHONE_TASK_STACK_CAPS);
    if (task_ret != pdPASS) {
        ESP_LOGW(TAG, "headphone monitor PSRAM stack create failed, trying internal");
        task_ret = xTaskCreateWithCaps(audio_codec_sink_headphone_task,
                                       "hp_detect",
                                       AUDIO_SINK_HEADPHONE_TASK_STACK,
                                       NULL,
                                       AUDIO_SINK_HEADPHONE_TASK_PRIORITY,
                                       &s_headphone_task,
                                       AUDIO_SINK_HEADPHONE_TASK_STACK_FALLBACK_CAPS);
        if (task_ret != pdPASS) {
            s_headphone_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void audio_codec_sink_stop_headphone_monitor(void)
{
    TaskHandle_t task = s_headphone_task;

    if (task) {
        s_headphone_stop_requested = true;
        xTaskNotifyGive(task);
        for (int i = 0; i < 100 && s_headphone_task == task; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (s_headphone_task == task) {
        ESP_LOGW(TAG, "headphone monitor did not stop within timeout");
    } else {
        s_headphone_stop_requested = false;
    }
    (void)audio_codec_sink_apply_headphone_route(false, "close");
}

esp_err_t audio_codec_sink_init(const audio_codec_sink_config_t *config)
{
    esp_err_t ret = audio_codec_sink_lock_init();
    if (ret != ESP_OK) {
        return ret;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (config) {
        s_volume = config->volume > 100 ? 100 : config->volume;
        s_muted = config->muted;
        ESP_LOGI(TAG, "init config: volume=%u muted=%u", s_volume, s_muted ? 1U : 0U);
    }
    if (!s_codec) {
        audio_codec_sink_log_dma_state("codec init",
                                       AUDIO_SINK_INIT_DMA_FREE_WARN_BYTES,
                                       AUDIO_SINK_INIT_DMA_LARGEST_WARN_BYTES);
    }
    ret = audio_codec_sink_ensure_codec_locked();
    if (ret == ESP_OK) {
        esp_err_t mute_ret = esp_codec_dev_set_out_mute(s_codec, s_muted);
        ESP_LOGI(TAG, "apply initial mute=%u ret=%s", s_muted ? 1U : 0U, esp_err_to_name(mute_ret));
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t audio_codec_sink_open(const audio_codec_sink_format_t *format)
{
    esp_err_t ret;
    esp_codec_dev_sample_info_t fs;

    if (!format || format->sample_rate == 0 ||
        format->bits_per_sample == 0 || format->channel_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (format->bits_per_sample != 8 && format->bits_per_sample != 16 &&
        format->bits_per_sample != 24 && format->bits_per_sample != 32) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ret = audio_codec_sink_init(NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_open && memcmp(&s_format, format, sizeof(s_format)) == 0) {
        esp_err_t vol_ret = esp_codec_dev_set_out_vol(s_codec, s_volume);
        esp_err_t mute_ret = esp_codec_dev_set_out_mute(s_codec, s_muted);
        bool headphone_inserted = bsp_headphone_detect();
        esp_err_t route_ret = audio_codec_sink_apply_headphone_route(headphone_inserted, "reuse");
        esp_err_t monitor_ret = audio_codec_sink_start_headphone_monitor();
        ESP_LOGI(TAG,
                 "open reused existing codec: %lu Hz, %u bit, %u ch, volume=%u ret=%s, mute=%u ret=%s, hp_route=%s, hp_monitor=%s, written=%llu bytes",
                 (unsigned long)format->sample_rate,
                 format->bits_per_sample,
                 format->channel_count,
                 s_volume,
                 esp_err_to_name(vol_ret),
                 s_muted ? 1U : 0U,
                 esp_err_to_name(mute_ret),
                 esp_err_to_name(route_ret),
                 esp_err_to_name(monitor_ret),
                 (unsigned long long)s_written_bytes);
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    if (s_open) {
        ESP_LOGI(TAG, "closing previous codec stream before reopen, written=%llu bytes",
                 (unsigned long long)s_written_bytes);
        (void)esp_codec_dev_close(s_codec);
        s_open = false;
    }

    fs = (esp_codec_dev_sample_info_t) {
        .bits_per_sample = format->bits_per_sample,
        .channel = format->channel_count,
        .channel_mask = 0,
        .sample_rate = format->sample_rate,
        .mclk_multiple = 0,
    };
    ESP_LOGI(TAG, "codec open begin: %lu Hz, %u bit, %u ch",
             (unsigned long)format->sample_rate,
             format->bits_per_sample,
             format->channel_count);
    audio_codec_sink_log_dma_state("codec open",
                                   AUDIO_SINK_OPEN_DMA_FREE_WARN_BYTES,
                                   AUDIO_SINK_OPEN_DMA_LARGEST_WARN_BYTES);
    ret = esp_codec_dev_open(s_codec, &fs);
    if (ret == ESP_CODEC_DEV_OK) {
        s_format = *format;
        s_open = true;
        s_written_bytes = 0;
        s_next_write_log_bytes = AUDIO_SINK_WRITE_LOG_STEP_BYTES;
        esp_err_t vol_ret = esp_codec_dev_set_out_vol(s_codec, s_volume);
        esp_err_t mute_ret = esp_codec_dev_set_out_mute(s_codec, s_muted);
        ESP_LOGI(TAG, "opened %lu Hz, %u bit, %u ch",
                 (unsigned long)format->sample_rate,
                 format->bits_per_sample,
                 format->channel_count);
        bool headphone_inserted = bsp_headphone_detect();
        esp_err_t route_ret = audio_codec_sink_apply_headphone_route(headphone_inserted, "open");
        esp_err_t monitor_ret = audio_codec_sink_start_headphone_monitor();
        ESP_LOGI(TAG, "apply volume=%u ret=%s, mute=%u ret=%s",
                 s_volume,
                 esp_err_to_name(vol_ret),
                 s_muted ? 1U : 0U,
                 esp_err_to_name(mute_ret));
        ESP_LOGI(TAG, "headphone monitor start ret=%s route_ret=%s",
                 esp_err_to_name(monitor_ret),
                 esp_err_to_name(route_ret));
    } else {
        ESP_LOGE(TAG,
                 "codec open failed: %s, dma_free=%u dma_largest=%u",
                 esp_err_to_name(ret),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t audio_codec_sink_write(const void *data, size_t len)
{
    esp_err_t ret;

    if (!data && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (len > INT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!s_lock || !s_codec) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_open) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    ret = esp_codec_dev_write(s_codec, (void *)data, (int)len);
    if (ret == ESP_OK) {
        s_written_bytes += len;
        if (s_written_bytes >= s_next_write_log_bytes) {
            ESP_LOGI(TAG, "write cumulative=%llu bytes", (unsigned long long)s_written_bytes);
            do {
                s_next_write_log_bytes += AUDIO_SINK_WRITE_LOG_STEP_BYTES;
            } while (s_written_bytes >= s_next_write_log_bytes);
        }
    } else {
        ESP_LOGE(TAG, "codec write failed at cumulative=%llu len=%u ret=%s",
                 (unsigned long long)s_written_bytes,
                 (unsigned)len,
                 esp_err_to_name(ret));
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t audio_codec_sink_close(void)
{
    esp_err_t ret = ESP_OK;
    bool closed_stream = false;

    if (!s_lock) {
        return ESP_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_codec && s_open) {
        ESP_LOGI(TAG, "codec close begin, total_written=%llu bytes",
                 (unsigned long long)s_written_bytes);
        ret = esp_codec_dev_close(s_codec);
        s_open = false;
        closed_stream = true;
        ESP_LOGI(TAG, "codec close ret=%s", esp_err_to_name(ret));
    }
    xSemaphoreGive(s_lock);
    if (closed_stream) {
        audio_codec_sink_stop_headphone_monitor();
    }
    return ret;
}

esp_err_t audio_codec_sink_set_volume(uint8_t volume)
{
    esp_err_t ret = ESP_OK;

    if (volume > 100) {
        volume = 100;
    }
    if (audio_codec_sink_lock_init() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_volume = volume;
    if (s_codec && s_open) {
        ret = esp_codec_dev_set_out_vol(s_codec, s_volume);
    }
    ESP_LOGI(TAG, "set volume=%u ret=%s open=%u", s_volume, esp_err_to_name(ret), s_open ? 1U : 0U);
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t audio_codec_sink_set_mute(bool muted)
{
    esp_err_t ret = ESP_OK;

    if (audio_codec_sink_lock_init() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_muted = muted;
    if (s_codec && s_open) {
        ret = esp_codec_dev_set_out_mute(s_codec, muted);
    }
    ESP_LOGI(TAG, "set mute=%u ret=%s open=%u", muted ? 1U : 0U, esp_err_to_name(ret), s_open ? 1U : 0U);
    xSemaphoreGive(s_lock);
    return ret;
}

bool audio_codec_sink_is_open(void)
{
    bool open = false;

    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    open = s_open;
    xSemaphoreGive(s_lock);
    return open;
}
