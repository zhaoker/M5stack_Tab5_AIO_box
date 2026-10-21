#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "i18n/app_i18n_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_LANGUAGE_UNSET = 0,
    APP_LANGUAGE_ZH_CN = 1,
    APP_LANGUAGE_ZH_TW = 2,
    APP_LANGUAGE_EN = 3,
} app_language_t;

esp_err_t app_i18n_init(void);
app_language_t app_i18n_get_language(void);
bool app_i18n_language_is_selected(void);
esp_err_t app_i18n_set_language(app_language_t language);
const char *app_i18n_language_name(app_language_t language);
const char *app_i18n_text(app_i18n_key_t key);
const char *app_i18n_translate(const char *source_text);
bool app_i18n_default_chinese_input(void);

#define TR(key) app_i18n_text((key))
#define TRS(text) app_i18n_translate((text))

#ifdef __cplusplus
}
#endif
