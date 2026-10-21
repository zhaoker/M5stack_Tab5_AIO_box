#include "audio/audio_adf_sink.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "audio_adf_sink";

#if AUDIO_ADF_SINK_AVAILABLE

typedef struct {
    audio_codec_sink_format_t format;
    bool has_format;
} audio_adf_codec_sink_ctx_t;

static esp_err_t audio_adf_sink_open(audio_element_handle_t self)
{
    audio_adf_codec_sink_ctx_t *ctx = (audio_adf_codec_sink_ctx_t *)audio_element_getdata(self);
    if (!ctx || !ctx->has_format) {
        audio_element_info_t info = {0};
        audio_element_getinfo(self, &info);
        if (info.sample_rates > 0 && info.bits > 0 && info.channels > 0) {
            ctx->format.sample_rate = info.sample_rates;
            ctx->format.bits_per_sample = info.bits;
            ctx->format.channel_count = info.channels;
            ctx->has_format = true;
        }
    }
    return (ctx && ctx->has_format) ? audio_codec_sink_open(&ctx->format) : ESP_ERR_INVALID_STATE;
}

static int audio_adf_sink_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    audio_adf_codec_sink_ctx_t *ctx = (audio_adf_codec_sink_ctx_t *)audio_element_getdata(self);
    if (!ctx || in_len <= 0) {
        return in_len;
    }
    int read_len = audio_element_input(self, in_buffer, in_len);
    if (read_len <= 0) {
        return read_len;
    }
    esp_err_t ret = audio_codec_sink_write(in_buffer, (size_t)read_len);
    return ret == ESP_OK ? read_len : AEL_IO_FAIL;
}

static esp_err_t audio_adf_sink_close(audio_element_handle_t self)
{
    (void)self;
    return audio_codec_sink_close();
}

static esp_err_t audio_adf_sink_destroy(audio_element_handle_t self)
{
    audio_adf_codec_sink_ctx_t *ctx = (audio_adf_codec_sink_ctx_t *)audio_element_getdata(self);
    free(ctx);
    return ESP_OK;
}

audio_element_handle_t audio_adf_codec_sink_init(const audio_codec_sink_config_t *config)
{
    audio_adf_codec_sink_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = audio_adf_sink_open;
    cfg.process = audio_adf_sink_process;
    cfg.close = audio_adf_sink_close;
    cfg.destroy = audio_adf_sink_destroy;
    cfg.task_stack = 4096;
    cfg.task_prio = 4;
    cfg.tag = "codec_sink";

    audio_element_handle_t element = audio_element_init(&cfg);
    if (!element) {
        free(ctx);
        return NULL;
    }
    ESP_LOGI(TAG, "ESP-ADF codec sink element available");
    audio_element_setdata(element, ctx);
    (void)audio_codec_sink_init(config);
    return element;
}

esp_err_t audio_adf_codec_sink_set_format(audio_element_handle_t element,
                                          const audio_codec_sink_format_t *format)
{
    if (!element || !format) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_adf_codec_sink_ctx_t *ctx = (audio_adf_codec_sink_ctx_t *)audio_element_getdata(element);
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }
    ctx->format = *format;
    ctx->has_format = true;
    return ESP_OK;
}

#else

audio_element_handle_t audio_adf_codec_sink_init(const audio_codec_sink_config_t *config)
{
    (void)config;
    ESP_LOGW(TAG, "ESP-ADF audio_element is not available in this build");
    return NULL;
}

esp_err_t audio_adf_codec_sink_set_format(audio_element_handle_t element,
                                          const audio_codec_sink_format_t *format)
{
    (void)element;
    (void)format;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
