#include "file_manager/file_manager_text.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define FM_GBK_LEAD_MIN 0x81
#define FM_GBK_LEAD_MAX 0xFE
#define FM_GBK_TRAIL_COUNT 190

static const uint16_t s_gbk_to_unicode[FM_GBK_TRAIL_COUNT * (FM_GBK_LEAD_MAX - FM_GBK_LEAD_MIN + 1)] = {
#include "file_manager_gbk_table.inc"
};

static size_t fm_utf8_encode(uint32_t cp, char *out)
{
    if (cp == 0) {
        cp = ' ';
    }
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    out[0] = '?';
    return 1;
}

static bool fm_utf8_is_valid(const uint8_t *data, size_t len, size_t offset)
{
    size_t i = offset;

    while (i < len) {
        uint8_t c = data[i];
        uint32_t cp;
        uint32_t min_cp;
        size_t need;

        if (c == 0) {
            i++;
            continue;
        }
        if (c < 0x80) {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            cp = c & 0x1F;
            min_cp = 0x80;
            need = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            cp = c & 0x0F;
            min_cp = 0x800;
            need = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            cp = c & 0x07;
            min_cp = 0x10000;
            need = 3;
        } else {
            return false;
        }
        if (i + need >= len) {
            return false;
        }
        for (size_t j = 1; j <= need; j++) {
            if ((data[i + j] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (data[i + j] & 0x3F);
        }
        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += need + 1;
    }
    return true;
}

static char *fm_copy_utf8_text(const uint8_t *data, size_t len, size_t offset, size_t *out_len)
{
    size_t text_len = len > offset ? len - offset : 0;
    char *out = (char *)malloc(text_len + 1);

    if (!out) {
        return NULL;
    }
    memcpy(out, data + offset, text_len);
    for (size_t i = 0; i < text_len; i++) {
        if (out[i] == '\0') {
            out[i] = ' ';
        }
    }
    out[text_len] = '\0';
    if (out_len) {
        *out_len = text_len;
    }
    return out;
}

static char *fm_decode_utf16_text(const uint8_t *data, size_t len, bool little_endian, size_t offset, size_t *out_len)
{
    char *out = (char *)malloc((len - offset) * 2 + 1);
    size_t pos = 0;

    if (!out) {
        return NULL;
    }
    for (size_t i = offset; i + 1 < len;) {
        uint16_t unit = little_endian
                            ? ((uint16_t)data[i] | ((uint16_t)data[i + 1] << 8))
                            : (((uint16_t)data[i] << 8) | data[i + 1]);
        uint32_t cp = unit;
        i += 2;

        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < len) {
            uint16_t low = little_endian
                               ? ((uint16_t)data[i] | ((uint16_t)data[i + 1] << 8))
                               : (((uint16_t)data[i] << 8) | data[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((((uint32_t)unit - 0xD800) << 10) | ((uint32_t)low - 0xDC00));
                i += 2;
            } else {
                cp = '?';
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            cp = '?';
        }
        pos += fm_utf8_encode(cp, out + pos);
    }
    out[pos] = '\0';
    if (out_len) {
        *out_len = pos;
    }
    return out;
}

static int fm_gbk_trail_index(uint8_t trail)
{
    if (trail >= 0x40 && trail <= 0x7E) {
        return trail - 0x40;
    }
    if (trail >= 0x80 && trail <= 0xFE) {
        return 0x3F + (trail - 0x80);
    }
    return -1;
}

static uint32_t fm_gbk_lookup(uint8_t lead, uint8_t trail)
{
    int trail_idx = fm_gbk_trail_index(trail);

    if (lead < FM_GBK_LEAD_MIN || lead > FM_GBK_LEAD_MAX || trail_idx < 0) {
        return '?';
    }
    uint16_t cp = s_gbk_to_unicode[((size_t)lead - FM_GBK_LEAD_MIN) * FM_GBK_TRAIL_COUNT + (size_t)trail_idx];
    return cp ? cp : '?';
}

static char *fm_decode_gbk_text(const uint8_t *data, size_t len, size_t *out_len)
{
    char *out = (char *)malloc(len * 3 + 1);
    size_t pos = 0;

    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < len;) {
        uint8_t c = data[i++];
        uint32_t cp;

        if (c == 0) {
            cp = ' ';
        } else if (c < 0x80) {
            cp = c;
        } else if (c == 0x80) {
            cp = 0x20AC;
        } else if (c >= FM_GBK_LEAD_MIN && c <= FM_GBK_LEAD_MAX) {
            if (i >= len) {
                break;
            }
            cp = fm_gbk_lookup(c, data[i++]);
        } else {
            cp = '?';
        }
        pos += fm_utf8_encode(cp, out + pos);
    }
    out[pos] = '\0';
    if (out_len) {
        *out_len = pos;
    }
    return out;
}

char *file_manager_text_decode_to_utf8(const uint8_t *data,
                                       size_t data_len,
                                       size_t *out_len,
                                       const char **out_encoding)
{
    if (out_len) {
        *out_len = 0;
    }
    if (out_encoding) {
        *out_encoding = "unknown";
    }
    if (!data) {
        return NULL;
    }
    if (data_len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        if (out_encoding) {
            *out_encoding = "UTF-8 BOM";
        }
        return fm_copy_utf8_text(data, data_len, 3, out_len);
    }
    if (data_len >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        if (out_encoding) {
            *out_encoding = "UTF-16LE BOM";
        }
        return fm_decode_utf16_text(data, data_len, true, 2, out_len);
    }
    if (data_len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        if (out_encoding) {
            *out_encoding = "UTF-16BE BOM";
        }
        return fm_decode_utf16_text(data, data_len, false, 2, out_len);
    }
    if (fm_utf8_is_valid(data, data_len, 0)) {
        if (out_encoding) {
            *out_encoding = "UTF-8";
        }
        return fm_copy_utf8_text(data, data_len, 0, out_len);
    }
    if (out_encoding) {
        *out_encoding = "GBK";
    }
    return fm_decode_gbk_text(data, data_len, out_len);
}
