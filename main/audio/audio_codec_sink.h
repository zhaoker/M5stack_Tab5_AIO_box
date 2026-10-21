#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;
    uint8_t bits_per_sample;
    uint8_t channel_count;
} audio_codec_sink_format_t;

typedef struct {
    uint8_t volume;
    bool muted;
} audio_codec_sink_config_t;

esp_err_t audio_codec_sink_init(const audio_codec_sink_config_t *config);
esp_err_t audio_codec_sink_open(const audio_codec_sink_format_t *format);
esp_err_t audio_codec_sink_write(const void *data, size_t len);
esp_err_t audio_codec_sink_close(void);
esp_err_t audio_codec_sink_set_volume(uint8_t volume);
esp_err_t audio_codec_sink_set_mute(bool muted);
bool audio_codec_sink_is_open(void);

#ifdef __cplusplus
}
#endif
