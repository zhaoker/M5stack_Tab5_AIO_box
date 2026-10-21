#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *py;
    uint32_t first_word;
    uint16_t word_count;
} pinyin_phrase_entry_t;

const pinyin_phrase_entry_t *pinyin_phrase_lookup(const char *py);
uint16_t pinyin_phrase_collect(const char *query, const char **out_words, uint16_t max_words);
const char *pinyin_phrase_get_word(const pinyin_phrase_entry_t *entry, uint16_t index);
size_t pinyin_phrase_entry_count(void);
size_t pinyin_phrase_word_count(void);

#ifdef __cplusplus
}
#endif
