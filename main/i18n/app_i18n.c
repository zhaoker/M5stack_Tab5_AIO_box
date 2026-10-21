#include "i18n/app_i18n.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "i18n/lang/app_lang_en.h"
#include "i18n/lang/app_lang_zh_cn.h"
#include "i18n/lang/app_lang_zh_tw.h"

static const char *TAG = "app_i18n";

#define I18N_NVS_NAMESPACE "app_settings"
#define I18N_NVS_LANGUAGE_KEY "language"

typedef struct {
    const char *source;
    app_i18n_key_t key;
} app_i18n_source_map_t;

#include "i18n/app_i18n_map.inc"

static app_language_t s_language = APP_LANGUAGE_UNSET;
static bool s_language_selected;

#define I18N_COMPOSE_SLOTS 4
#define I18N_COMPOSE_LEN   256

static char s_compose_buf[I18N_COMPOSE_SLOTS][I18N_COMPOSE_LEN];
static uint8_t s_compose_slot;

static app_language_t clamp_language(uint8_t language)
{
    switch ((app_language_t)language) {
    case APP_LANGUAGE_ZH_CN:
    case APP_LANGUAGE_ZH_TW:
    case APP_LANGUAGE_EN:
        return (app_language_t)language;
    case APP_LANGUAGE_UNSET:
    default:
        return APP_LANGUAGE_UNSET;
    }
}

static const char *const *current_table(void)
{
    switch (s_language) {
    case APP_LANGUAGE_ZH_TW:
        return s_i18n_zh_tw;
    case APP_LANGUAGE_EN:
        return s_i18n_en;
    case APP_LANGUAGE_ZH_CN:
    case APP_LANGUAGE_UNSET:
    default:
        return s_i18n_zh_cn;
    }
}

static bool is_cjk_codepoint(uint32_t cp)
{
    return (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF);
}

static bool utf8_read_codepoint(const char *text, size_t pos, uint32_t *out_cp, size_t *out_step)
{
    const unsigned char *s = (const unsigned char *)text;
    unsigned char c = s[pos];
    size_t remaining = strlen(text + pos);

    if (c < 0x80) {
        *out_cp = c;
        *out_step = 1;
        return true;
    }
    if (remaining >= 2 && (c & 0xE0) == 0xC0 && (s[pos + 1] & 0xC0) == 0x80) {
        *out_cp = ((uint32_t)(c & 0x1F) << 6) |
                  (uint32_t)(s[pos + 1] & 0x3F);
        *out_step = 2;
        return true;
    }
    if (remaining >= 3 &&
        (c & 0xF0) == 0xE0 &&
        (s[pos + 1] & 0xC0) == 0x80 &&
        (s[pos + 2] & 0xC0) == 0x80) {
        *out_cp = ((uint32_t)(c & 0x0F) << 12) |
                  ((uint32_t)(s[pos + 1] & 0x3F) << 6) |
                  (uint32_t)(s[pos + 2] & 0x3F);
        *out_step = 3;
        return true;
    }
    if (remaining >= 4 &&
        (c & 0xF8) == 0xF0 &&
        (s[pos + 1] & 0xC0) == 0x80 &&
        (s[pos + 2] & 0xC0) == 0x80 &&
        (s[pos + 3] & 0xC0) == 0x80) {
        *out_cp = ((uint32_t)(c & 0x07) << 18) |
                  ((uint32_t)(s[pos + 1] & 0x3F) << 12) |
                  ((uint32_t)(s[pos + 2] & 0x3F) << 6) |
                  (uint32_t)(s[pos + 3] & 0x3F);
        *out_step = 4;
        return true;
    }
    *out_cp = c;
    *out_step = 1;
    return false;
}

static app_i18n_key_t find_source_key(const char *source_text)
{
    for (size_t i = 0; i < sizeof(s_i18n_source_map) / sizeof(s_i18n_source_map[0]); i++) {
        if (strcmp(source_text, s_i18n_source_map[i].source) == 0) {
            return s_i18n_source_map[i].key;
        }
    }
    return I18N_KEY_COUNT;
}

static const char *compose_prefixed_translation(const char *source_text,
                                                size_t tail_pos,
                                                const char *translated_tail)
{
    char *buf = s_compose_buf[s_compose_slot++ % I18N_COMPOSE_SLOTS];
    snprintf(buf, I18N_COMPOSE_LEN, "%.*s%s", (int)tail_pos, source_text, translated_tail);
    return buf;
}

esp_err_t app_i18n_init(void)
{
    nvs_handle_t h;
    uint8_t saved = 0;
    esp_err_t err = nvs_open(I18N_NVS_NAMESPACE, NVS_READONLY, &h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_language = APP_LANGUAGE_UNSET;
        s_language_selected = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open language setting failed: %s", esp_err_to_name(err));
        s_language = APP_LANGUAGE_UNSET;
        s_language_selected = false;
        return err;
    }

    err = nvs_get_u8(h, I18N_NVS_LANGUAGE_KEY, &saved);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_language = APP_LANGUAGE_UNSET;
        s_language_selected = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "load language setting failed: %s", esp_err_to_name(err));
        s_language = APP_LANGUAGE_UNSET;
        s_language_selected = false;
        return err;
    }

    s_language = clamp_language(saved);
    s_language_selected = s_language != APP_LANGUAGE_UNSET;
    return ESP_OK;
}

app_language_t app_i18n_get_language(void)
{
    return s_language_selected ? s_language : APP_LANGUAGE_ZH_CN;
}

bool app_i18n_language_is_selected(void)
{
    return s_language_selected;
}

esp_err_t app_i18n_set_language(app_language_t language)
{
    nvs_handle_t h;
    esp_err_t err;

    language = clamp_language((uint8_t)language);
    if (language == APP_LANGUAGE_UNSET) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(I18N_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open language setting for save failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u8(h, I18N_NVS_LANGUAGE_KEY, (uint8_t)language);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        s_language = language;
        s_language_selected = true;
    } else {
        ESP_LOGW(TAG, "save language setting failed: %s", esp_err_to_name(err));
    }
    return err;
}

const char *app_i18n_language_name(app_language_t language)
{
    switch (language) {
    case APP_LANGUAGE_ZH_CN:
        return "简体中文";
    case APP_LANGUAGE_ZH_TW:
        return "繁體中文";
    case APP_LANGUAGE_EN:
        return "English";
    case APP_LANGUAGE_UNSET:
    default:
        return "未选择";
    }
}

const char *app_i18n_text(app_i18n_key_t key)
{
    const char *const *table = current_table();

    if (key < 0 || key >= I18N_KEY_COUNT) {
        return "";
    }
    if (table[key]) {
        return table[key];
    }
    return s_i18n_zh_cn[key] ? s_i18n_zh_cn[key] : "";
}

const char *app_i18n_translate(const char *source_text)
{
    app_i18n_key_t key;

    if (!source_text || !source_text[0]) {
        return source_text ? source_text : "";
    }
    key = find_source_key(source_text);
    if (key != I18N_KEY_COUNT) {
        return app_i18n_text(key);
    }

    for (size_t pos = 0; source_text[pos];) {
        uint32_t cp = 0;
        size_t step = 1;
        if (!utf8_read_codepoint(source_text, pos, &cp, &step)) {
            pos += step;
            continue;
        }
        if (is_cjk_codepoint(cp)) {
            size_t tail_pos = pos;
            while (tail_pos > 0 && source_text[tail_pos - 1] == ' ') {
                tail_pos--;
            }
            if (tail_pos > 0) {
                key = find_source_key(source_text + tail_pos);
                if (key != I18N_KEY_COUNT) {
                    return compose_prefixed_translation(source_text, tail_pos, app_i18n_text(key));
                }
                key = find_source_key(source_text + pos);
                if (key != I18N_KEY_COUNT) {
                    return compose_prefixed_translation(source_text, pos, app_i18n_text(key));
                }
            }
            break;
        }
        pos += step;
    }
    return source_text;
}

bool app_i18n_default_chinese_input(void)
{
    app_language_t language = app_i18n_get_language();
    return language == APP_LANGUAGE_ZH_CN || language == APP_LANGUAGE_ZH_TW;
}
