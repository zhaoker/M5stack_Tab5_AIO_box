#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_audio_background_init(void);
void ui_audio_background_set_music_ui_visible(bool visible);
void ui_audio_background_set_terminal_status_visible(bool visible);
void ui_audio_background_refresh_geometry(void);

#ifdef __cplusplus
}
#endif
