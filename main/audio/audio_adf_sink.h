#pragma once

#include "audio/audio_codec_sink.h"
#include "esp_err.h"

#if defined(__has_include)
#if __has_include("audio_element.h")
#include "audio_element.h"
#define AUDIO_ADF_SINK_AVAILABLE 1
#else
#define AUDIO_ADF_SINK_AVAILABLE 0
typedef void *audio_element_handle_t;
#endif
#else
#define AUDIO_ADF_SINK_AVAILABLE 0
typedef void *audio_element_handle_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

audio_element_handle_t audio_adf_codec_sink_init(const audio_codec_sink_config_t *config);
esp_err_t audio_adf_codec_sink_set_format(audio_element_handle_t element,
                                          const audio_codec_sink_format_t *format);

#ifdef __cplusplus
}
#endif
