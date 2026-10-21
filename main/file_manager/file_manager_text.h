#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

char *file_manager_text_decode_to_utf8(const uint8_t *data,
                                       size_t data_len,
                                       size_t *out_len,
                                       const char **out_encoding);

#ifdef __cplusplus
}
#endif
