#include "file_manager/file_manager_exif.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "i18n/app_i18n.h"

static const char *TAG = "fm_exif";

enum {
    EXIF_TAG_IMAGE_DESCRIPTION = 0x010E,
    EXIF_TAG_MAKE = 0x010F,
    EXIF_TAG_MODEL = 0x0110,
    EXIF_TAG_DOCUMENT_NAME = 0x010D,
    EXIF_TAG_IMAGE_WIDTH = 0x0100,
    EXIF_TAG_IMAGE_HEIGHT = 0x0101,
    EXIF_TAG_EXIF_IFD = 0x8769,
    EXIF_TAG_DATETIME_ORIGINAL = 0x9003,
    EXIF_TAG_USER_COMMENT = 0x9286,
    EXIF_TAG_FNUMBER = 0x829D,
    EXIF_TAG_EXPOSURE_TIME = 0x829A,
    EXIF_TAG_ISO = 0x8827,
    EXIF_TAG_FOCAL_LENGTH = 0x920A,
    EXIF_TAG_EXPOSURE_BIAS = 0x9204,
    EXIF_TAG_LENS_MODEL = 0xA434,
    EXIF_TAG_PIXEL_X_DIMENSION = 0xA002,
    EXIF_TAG_PIXEL_Y_DIMENSION = 0xA003,
    EXIF_TAG_XP_TITLE = 0x9C9B,
    EXIF_TAG_XP_COMMENT = 0x9C9C,
};

static const char *tag_name(uint16_t tag)
{
    switch (tag) {
    case EXIF_TAG_DOCUMENT_NAME:
        return "DocumentName";
    case EXIF_TAG_IMAGE_DESCRIPTION:
        return "ImageDescription";
    case EXIF_TAG_MAKE:
        return "Make";
    case EXIF_TAG_MODEL:
        return "Model";
    case EXIF_TAG_IMAGE_WIDTH:
        return "ImageWidth";
    case EXIF_TAG_IMAGE_HEIGHT:
        return "ImageHeight";
    case EXIF_TAG_EXIF_IFD:
        return "ExifIFD";
    case EXIF_TAG_DATETIME_ORIGINAL:
        return "DateTimeOriginal";
    case EXIF_TAG_FNUMBER:
        return "FNumber";
    case EXIF_TAG_EXPOSURE_TIME:
        return "ExposureTime";
    case EXIF_TAG_ISO:
        return "ISO";
    case EXIF_TAG_FOCAL_LENGTH:
        return "FocalLength";
    case EXIF_TAG_EXPOSURE_BIAS:
        return "ExposureBias";
    case EXIF_TAG_USER_COMMENT:
        return "UserComment";
    case EXIF_TAG_LENS_MODEL:
        return "LensModel";
    case EXIF_TAG_PIXEL_X_DIMENSION:
        return "PixelXDimension";
    case EXIF_TAG_PIXEL_Y_DIMENSION:
        return "PixelYDimension";
    case EXIF_TAG_XP_TITLE:
        return "XPTitle";
    case EXIF_TAG_XP_COMMENT:
        return "XPComment";
    default:
        return "Unknown";
    }
}

static bool fm_exif_is_jpeg(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && (strcasecmp(dot + 1, "jpg") == 0 || strcasecmp(dot + 1, "jpeg") == 0);
}

static uint16_t rd16(const uint8_t *p, bool le)
{
    return le ? ((uint16_t)p[0] | ((uint16_t)p[1] << 8))
              : (((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p, bool le)
{
    return le ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24))
              : (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}

static uint32_t type_size(uint16_t type)
{
    switch (type) {
    case 1:
    case 2:
    case 7:
        return 1;
    case 3:
        return 2;
    case 4:
    case 9:
        return 4;
    case 5:
    case 10:
        return 8;
    default:
        return 0;
    }
}

static const uint8_t *entry_data(const uint8_t *tiff,
                                 size_t tiff_len,
                                 const uint8_t *entry,
                                 bool le,
                                 uint16_t type,
                                 uint32_t count,
                                 size_t *out_len)
{
    uint32_t unit = type_size(type);
    size_t len;
    uint32_t offset;

    if (out_len) {
        *out_len = 0;
    }
    if (unit == 0 || count == 0 || count > SIZE_MAX / unit) {
        return NULL;
    }
    len = (size_t)unit * count;
    if (out_len) {
        *out_len = len;
    }
    if (len <= 4) {
        return entry + 8;
    }
    offset = rd32(entry + 8, le);
    if (offset > tiff_len || len > tiff_len - offset) {
        return NULL;
    }
    return tiff + offset;
}

static void copy_ascii(char *dst, size_t dst_len, const uint8_t *src, size_t len)
{
    size_t n;

    if (!dst || dst_len == 0 || !src || len == 0) {
        return;
    }
    while (len > 0 && src[len - 1] == '\0') {
        len--;
    }
    n = len < dst_len - 1 ? len : dst_len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) {
        dst[--n] = '\0';
    }
}

static bool text_is_empty(const char *s)
{
    return !s || !s[0];
}

static void trim_text(char *s)
{
    size_t len;

    if (!s) {
        return;
    }
    len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
}

static bool copy_ascii_nonempty(char *dst, size_t dst_len, const uint8_t *src, size_t len)
{
    char tmp[160] = {0};

    if (!dst || dst_len == 0) {
        return false;
    }
    copy_ascii(tmp, sizeof(tmp), src, len);
    trim_text(tmp);
    if (!tmp[0]) {
        return false;
    }
    strlcpy(dst, tmp, dst_len);
    return true;
}

static bool normalize_exif_datetime(char *dst, size_t dst_len, const char *raw)
{
    char tmp[40] = {0};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed = 0;
    char sep1 = '\0';
    char sep2 = '\0';
    const char *time_part;

    if (!dst || dst_len == 0 || !raw || !raw[0]) {
        return false;
    }
    strlcpy(tmp, raw, sizeof(tmp));
    trim_text(tmp);
    if (sscanf(tmp, "%4d%c%2d%c%2d%n", &year, &sep1, &month, &sep2, &day, &consumed) != 5 ||
        (sep1 != ':' && sep1 != '-' && sep1 != '/') ||
        (sep2 != ':' && sep2 != '-' && sep2 != '/') ||
        year <= 0 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    time_part = tmp + consumed;
    while (*time_part == ' ' || *time_part == '\t' || *time_part == 'T') {
        time_part++;
    }
    if (*time_part) {
        int parsed_hour = 0;
        int parsed_minute = 0;
        int parsed_second = 0;
        if (sscanf(time_part, "%2d:%2d:%2d", &parsed_hour, &parsed_minute, &parsed_second) == 3 &&
            parsed_hour >= 0 && parsed_hour <= 23 &&
            parsed_minute >= 0 && parsed_minute <= 59 &&
            parsed_second >= 0 && parsed_second <= 60) {
            hour = parsed_hour;
            minute = parsed_minute;
            second = parsed_second;
        }
    }

    if (hour + minute + second == 0) {
        snprintf(dst, dst_len, "%04d-%02d-%02d", year, month, day);
    } else {
        snprintf(dst, dst_len, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
    }
    return true;
}

static size_t append_utf8(char *dst, size_t dst_len, size_t pos, uint32_t cp)
{
    if (!dst || dst_len == 0 || pos >= dst_len - 1) {
        return pos;
    }
    if (cp == '\r' || cp == '\n') {
        cp = ' ';
    }
    if (cp < 0x80) {
        dst[pos++] = (char)cp;
    } else if (cp < 0x800 && pos + 2 < dst_len) {
        dst[pos++] = (char)(0xC0 | (cp >> 6));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000 && pos + 3 < dst_len) {
        dst[pos++] = (char)(0xE0 | (cp >> 12));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF && pos + 4 < dst_len) {
        dst[pos++] = (char)(0xF0 | (cp >> 18));
        dst[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    dst[pos] = '\0';
    return pos;
}

static bool copy_utf16_text(char *dst, size_t dst_len, const uint8_t *src, size_t len, bool le)
{
    size_t out = 0;

    if (!dst || dst_len == 0 || !src || len < 2) {
        return false;
    }
    dst[0] = '\0';
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint16_t w1 = rd16(src + i, le);
        uint32_t cp = w1;
        if (w1 == 0) {
            break;
        }
        if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 3 < len) {
            uint16_t w2 = rd16(src + i + 2, le);
            if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                cp = 0x10000U + ((((uint32_t)w1 - 0xD800U) << 10) | ((uint32_t)w2 - 0xDC00U));
                i += 2;
            }
        }
        out = append_utf8(dst, dst_len, out, cp);
        if (out >= dst_len - 1) {
            break;
        }
    }
    trim_text(dst);
    return dst[0] != '\0';
}

static bool copy_xp_text(char *dst, size_t dst_len, const uint8_t *src, size_t len)
{
    return copy_utf16_text(dst, dst_len, src, len, true);
}

static bool copy_user_comment(char *dst, size_t dst_len, const uint8_t *src, size_t len, bool le)
{
    if (!dst || dst_len == 0 || !src || len == 0) {
        return false;
    }
    if (len >= 8 && memcmp(src, "ASCII\0\0\0", 8) == 0) {
        return copy_ascii_nonempty(dst, dst_len, src + 8, len - 8);
    }
    if (len >= 8 && memcmp(src, "UNICODE\0", 8) == 0) {
        return copy_utf16_text(dst, dst_len, src + 8, len - 8, le);
    }
    return copy_ascii_nonempty(dst, dst_len, src, len);
}

static void log_text_tag(const char *ifd_name, uint16_t tag, const char *value)
{
    ESP_LOGI(TAG, "EXIF %s %s(0x%04X) text=\"%s\"",
             ifd_name ? ifd_name : "IFD",
             tag_name(tag),
             (unsigned)tag,
             value && value[0] ? value : "");
}

static bool should_log_tag(uint16_t tag)
{
    switch (tag) {
    case EXIF_TAG_DOCUMENT_NAME:
    case EXIF_TAG_IMAGE_DESCRIPTION:
    case EXIF_TAG_MAKE:
    case EXIF_TAG_MODEL:
    case EXIF_TAG_IMAGE_WIDTH:
    case EXIF_TAG_IMAGE_HEIGHT:
    case EXIF_TAG_EXIF_IFD:
    case EXIF_TAG_DATETIME_ORIGINAL:
    case EXIF_TAG_FNUMBER:
    case EXIF_TAG_EXPOSURE_TIME:
    case EXIF_TAG_ISO:
    case EXIF_TAG_FOCAL_LENGTH:
    case EXIF_TAG_EXPOSURE_BIAS:
    case EXIF_TAG_USER_COMMENT:
    case EXIF_TAG_LENS_MODEL:
    case EXIF_TAG_PIXEL_X_DIMENSION:
    case EXIF_TAG_PIXEL_Y_DIMENSION:
    case EXIF_TAG_XP_TITLE:
    case EXIF_TAG_XP_COMMENT:
        return true;
    default:
        return false;
    }
}

static uint32_t get_u32_value(const uint8_t *tiff,
                              size_t tiff_len,
                              const uint8_t *entry,
                              bool le,
                              uint16_t type,
                              uint32_t count)
{
    size_t len = 0;
    const uint8_t *p = entry_data(tiff, tiff_len, entry, le, type, count, &len);

    if (!p || len == 0) {
        return 0;
    }
    if (type == 3 && len >= 2) {
        return rd16(p, le);
    }
    if ((type == 4 || type == 9) && len >= 4) {
        return rd32(p, le);
    }
    return 0;
}

static bool get_rational(const uint8_t *tiff,
                         size_t tiff_len,
                         const uint8_t *entry,
                         bool le,
                         uint16_t type,
                         uint32_t count,
                         double *out,
                         int32_t *out_num,
                         int32_t *out_den)
{
    size_t len = 0;
    const uint8_t *p = entry_data(tiff, tiff_len, entry, le, type, count, &len);
    int32_t num;
    int32_t den;

    if (!p || count == 0 || len < 8 || (type != 5 && type != 10)) {
        return false;
    }
    num = type == 10 ? (int32_t)rd32(p, le) : (int32_t)(uint32_t)rd32(p, le);
    den = type == 10 ? (int32_t)rd32(p + 4, le) : (int32_t)(uint32_t)rd32(p + 4, le);
    if (den == 0) {
        return false;
    }
    if (out) {
        *out = (double)num / (double)den;
    }
    if (out_num) {
        *out_num = num;
    }
    if (out_den) {
        *out_den = den;
    }
    return true;
}

static void format_aperture(char *dst, size_t len, double value)
{
    if (value > 0.0) {
        snprintf(dst, len, "f/%.1f", value);
    }
}

static void format_focal(char *dst, size_t len, double value)
{
    if (value > 0.0) {
        snprintf(dst, len, "%.0f mm", value);
    }
}

static void format_bias(char *dst, size_t len, double value)
{
    if (fabs(value) > 0.005) {
        snprintf(dst, len, "%+.1f EV", value);
    }
}

static void format_shutter(char *dst, size_t len, int32_t num, int32_t den, double value)
{
    if (num <= 0 || den <= 0) {
        return;
    }
    if (value > 0.0 && value < 1.0 && den % num == 0) {
        snprintf(dst, len, "1/%ld s", (long)(den / num));
    } else if (value > 0.0) {
        snprintf(dst, len, "%.2f s", value);
    }
}

static void parse_ifd(const uint8_t *tiff,
                      size_t tiff_len,
                      uint32_t ifd_offset,
                      bool le,
                      file_manager_exif_info_t *info,
                      const char *ifd_name,
                      uint32_t *exif_ifd)
{
    if (!tiff || !info || ifd_offset + 2 > tiff_len) {
        ESP_LOGW(TAG, "EXIF %s invalid offset=%lu len=%lu",
                 ifd_name ? ifd_name : "IFD",
                 (unsigned long)ifd_offset,
                 (unsigned long)tiff_len);
        return;
    }
    uint16_t count = rd16(tiff + ifd_offset, le);
    const uint8_t *entries = tiff + ifd_offset + 2;

    if ((size_t)ifd_offset + 2U + (size_t)count * 12U > tiff_len) {
        ESP_LOGW(TAG, "EXIF %s entries overflow offset=%lu count=%u len=%lu",
                 ifd_name ? ifd_name : "IFD",
                 (unsigned long)ifd_offset,
                 (unsigned)count,
                 (unsigned long)tiff_len);
        return;
    }
    ESP_LOGI(TAG, "EXIF %s offset=%lu entries=%u",
             ifd_name ? ifd_name : "IFD",
             (unsigned long)ifd_offset,
             (unsigned)count);

    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *entry = entries + (size_t)i * 12U;
        uint16_t tag = rd16(entry, le);
        uint16_t type = rd16(entry + 2, le);
        uint32_t value_count = rd32(entry + 4, le);
        size_t len = 0;
        const uint8_t *p = entry_data(tiff, tiff_len, entry, le, type, value_count, &len);
        double d = 0.0;
        int32_t num = 0;
        int32_t den = 0;

        if (should_log_tag(tag)) {
            ESP_LOGI(TAG, "EXIF %s entry %u/%u %s(0x%04X) type=%u count=%lu len=%lu data=%s",
                     ifd_name ? ifd_name : "IFD",
                     (unsigned)i + 1U,
                     (unsigned)count,
                     tag_name(tag),
                     (unsigned)tag,
                     (unsigned)type,
                     (unsigned long)value_count,
                     (unsigned long)len,
                     p ? "ok" : "NULL");
        }
        switch (tag) {
        case EXIF_TAG_DOCUMENT_NAME:
        case EXIF_TAG_IMAGE_DESCRIPTION: {
            char value[160] = {0};
            if (copy_ascii_nonempty(value, sizeof(value), p, len)) {
                if (text_is_empty(info->title)) {
                    strlcpy(info->title, value, sizeof(info->title));
                }
                log_text_tag(ifd_name, tag, value);
            } else {
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) empty or unsupported",
                         ifd_name ? ifd_name : "IFD",
                         tag_name(tag),
                         (unsigned)tag);
            }
            break;
        }
        case EXIF_TAG_MAKE:
            copy_ascii(info->camera, sizeof(info->camera), p, len);
            log_text_tag(ifd_name, tag, info->camera);
            break;
        case EXIF_TAG_MODEL:
            if (info->camera[0]) {
                char model[48] = {0};
                copy_ascii(model, sizeof(model), p, len);
                log_text_tag(ifd_name, tag, model);
                if (model[0] && strstr(info->camera, model) == NULL) {
                    strlcat(info->camera, " ", sizeof(info->camera));
                    strlcat(info->camera, model, sizeof(info->camera));
                }
            } else {
                copy_ascii(info->camera, sizeof(info->camera), p, len);
                log_text_tag(ifd_name, tag, info->camera);
            }
            break;
        case EXIF_TAG_IMAGE_WIDTH:
        case EXIF_TAG_PIXEL_X_DIMENSION:
            info->width = get_u32_value(tiff, tiff_len, entry, le, type, value_count);
            ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%lu",
                     ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, (unsigned long)info->width);
            break;
        case EXIF_TAG_IMAGE_HEIGHT:
        case EXIF_TAG_PIXEL_Y_DIMENSION:
            info->height = get_u32_value(tiff, tiff_len, entry, le, type, value_count);
            ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%lu",
                     ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, (unsigned long)info->height);
            break;
        case EXIF_TAG_EXIF_IFD:
            if (exif_ifd) {
                *exif_ifd = get_u32_value(tiff, tiff_len, entry, le, type, value_count);
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) offset=%lu",
                         ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, (unsigned long)*exif_ifd);
            }
            break;
        case EXIF_TAG_DATETIME_ORIGINAL:
            copy_ascii(info->datetime, sizeof(info->datetime), p, len);
            if (!normalize_exif_datetime(info->datetime, sizeof(info->datetime), info->datetime)) {
                trim_text(info->datetime);
            }
            log_text_tag(ifd_name, tag, info->datetime);
            break;
        case EXIF_TAG_LENS_MODEL:
            copy_ascii(info->lens, sizeof(info->lens), p, len);
            log_text_tag(ifd_name, tag, info->lens);
            break;
        case EXIF_TAG_FNUMBER:
            if (get_rational(tiff, tiff_len, entry, le, type, value_count, &d, NULL, NULL)) {
                format_aperture(info->aperture, sizeof(info->aperture), d);
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%s raw=%.4f",
                         ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, info->aperture, d);
            }
            break;
        case EXIF_TAG_EXPOSURE_TIME:
            if (get_rational(tiff, tiff_len, entry, le, type, value_count, &d, &num, &den)) {
                format_shutter(info->shutter, sizeof(info->shutter), num, den, d);
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%s raw=%ld/%ld",
                         ifd_name ? ifd_name : "IFD",
                         tag_name(tag),
                         (unsigned)tag,
                         info->shutter,
                         (long)num,
                         (long)den);
            }
            break;
        case EXIF_TAG_ISO: {
            uint32_t iso = get_u32_value(tiff, tiff_len, entry, le, type, value_count);
            if (iso > 0) {
                snprintf(info->iso, sizeof(info->iso), "ISO %lu", (unsigned long)iso);
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%s",
                         ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, info->iso);
            }
            break;
        }
        case EXIF_TAG_FOCAL_LENGTH:
            if (get_rational(tiff, tiff_len, entry, le, type, value_count, &d, NULL, NULL)) {
                format_focal(info->focal_length, sizeof(info->focal_length), d);
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%s raw=%.4f",
                         ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, info->focal_length, d);
            }
            break;
        case EXIF_TAG_EXPOSURE_BIAS:
            if (get_rational(tiff, tiff_len, entry, le, type, value_count, &d, NULL, NULL)) {
                format_bias(info->exposure_bias, sizeof(info->exposure_bias), d);
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) value=%s raw=%.4f",
                         ifd_name ? ifd_name : "IFD", tag_name(tag), (unsigned)tag, info->exposure_bias, d);
            }
            break;
        case EXIF_TAG_USER_COMMENT: {
            char value[160] = {0};
            if (copy_user_comment(value, sizeof(value), p, len, le)) {
                strlcpy(info->comment, value, sizeof(info->comment));
                log_text_tag(ifd_name, tag, value);
            } else {
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) empty or unsupported",
                         ifd_name ? ifd_name : "IFD",
                         tag_name(tag),
                         (unsigned)tag);
            }
            break;
        }
        case EXIF_TAG_XP_TITLE: {
            char value[160] = {0};
            if (copy_xp_text(value, sizeof(value), p, len)) {
                strlcpy(info->title, value, sizeof(info->title));
                log_text_tag(ifd_name, tag, value);
            } else {
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) empty or unsupported",
                         ifd_name ? ifd_name : "IFD",
                         tag_name(tag),
                         (unsigned)tag);
            }
            break;
        }
        case EXIF_TAG_XP_COMMENT: {
            char value[160] = {0};
            if (copy_xp_text(value, sizeof(value), p, len)) {
                if (text_is_empty(info->comment)) {
                    strlcpy(info->comment, value, sizeof(info->comment));
                }
                log_text_tag(ifd_name, tag, value);
            } else {
                ESP_LOGI(TAG, "EXIF %s %s(0x%04X) empty or unsupported",
                         ifd_name ? ifd_name : "IFD",
                         tag_name(tag),
                         (unsigned)tag);
            }
            break;
        }
        default:
            break;
        }
    }
}

static esp_err_t parse_exif_payload(const uint8_t *data, size_t len, file_manager_exif_info_t *info)
{
    if (!data || len < 14 || memcmp(data, "Exif\0\0", 6) != 0 || !info) {
        return ESP_ERR_NOT_FOUND;
    }
    const uint8_t *tiff = data + 6;
    size_t tiff_len = len - 6;
    bool le;
    uint32_t ifd0;
    uint32_t exif_ifd = 0;

    if (tiff_len < 8) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (tiff[0] == 'I' && tiff[1] == 'I') {
        le = true;
    } else if (tiff[0] == 'M' && tiff[1] == 'M') {
        le = false;
    } else {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (rd16(tiff + 2, le) != 42) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ifd0 = rd32(tiff + 4, le);
    ESP_LOGI(TAG, "EXIF payload len=%lu byte_order=%s ifd0=%lu",
             (unsigned long)len,
             le ? "II" : "MM",
             (unsigned long)ifd0);
    parse_ifd(tiff, tiff_len, ifd0, le, info, "IFD0", &exif_ifd);
    if (exif_ifd > 0) {
        parse_ifd(tiff, tiff_len, exif_ifd, le, info, "ExifIFD", NULL);
    } else {
        ESP_LOGI(TAG, "EXIF ExifIFD not present");
    }
    return ESP_OK;
}

esp_err_t file_manager_exif_read(const char *path, file_manager_exif_info_t *info)
{
    FILE *fp;
    uint8_t hdr[4];
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    if (!path || !info || !fm_exif_is_jpeg(path)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    fp = fopen(path, "rb");
    if (!fp) {
        return ESP_FAIL;
    }
    if (fread(hdr, 1, 2, fp) != 2 || hdr[0] != 0xFF || hdr[1] != 0xD8) {
        fclose(fp);
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (fread(hdr, 1, 4, fp) == 4) {
        if (hdr[0] != 0xFF) {
            ESP_LOGW(TAG, "read exif %s marker prefix invalid: 0x%02X", path, (unsigned)hdr[0]);
            break;
        }
        uint8_t marker = hdr[1];
        uint16_t seg_len = ((uint16_t)hdr[2] << 8) | hdr[3];
        if (seg_len < 2) {
            ESP_LOGW(TAG, "read exif %s invalid segment len=%u marker=0x%02X", path, (unsigned)seg_len, (unsigned)marker);
            break;
        }
        if (marker == 0xDA || marker == 0xD9) {
            ESP_LOGI(TAG, "read exif %s stop at marker=0x%02X before EXIF", path, (unsigned)marker);
            break;
        }
        size_t payload_len = seg_len - 2U;
        ESP_LOGI(TAG, "read exif %s segment marker=0x%02X payload=%lu", path, (unsigned)marker, (unsigned long)payload_len);
        if (marker == 0xE1 && payload_len > 6) {
            uint8_t *payload = (uint8_t *)malloc(payload_len);
            if (!payload) {
                ret = ESP_ERR_NO_MEM;
                break;
            }
            if (fread(payload, 1, payload_len, fp) != payload_len) {
                free(payload);
                ret = ESP_FAIL;
                break;
            }
            ret = parse_exif_payload(payload, payload_len, info);
            free(payload);
            if (ret == ESP_OK) {
                break;
            }
            ESP_LOGI(TAG, "read exif %s APP1 not EXIF or parse failed: %s", path, esp_err_to_name(ret));
        } else if (fseek(fp, payload_len, SEEK_CUR) != 0) {
            ret = ESP_FAIL;
            break;
        }
    }
    fclose(fp);
    ESP_LOGI(TAG,
             "read exif %s: %s title=\"%s\" comment=\"%s\" camera=\"%s\" lens=\"%s\" datetime=\"%s\" aperture=\"%s\" shutter=\"%s\" iso=\"%s\" focal=\"%s\" bias=\"%s\" size=%lux%lu",
             path,
             esp_err_to_name(ret),
             info->title,
             info->comment,
             info->camera,
             info->lens,
             info->datetime,
             info->aperture,
             info->shutter,
             info->iso,
             info->focal_length,
             info->exposure_bias,
             (unsigned long)info->width,
             (unsigned long)info->height);
    return ret;
}

static void append_item(char *out, size_t out_len, const char *title, const char *value)
{
    if (!out || out_len == 0 || !value || !value[0]) {
        return;
    }
    size_t used = strlen(out);
    if (used >= out_len - 1) {
        return;
    }
    snprintf(out + used, out_len - used, "%s%s：%s", used ? "\n" : "", title, value);
}

bool file_manager_exif_format_lines(const file_manager_exif_info_t *info, char *out, size_t out_len)
{
    char resolution[40] = {0};

    if (!info || !out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    append_item(out, out_len, TRS("备注"), info->comment);
    append_item(out, out_len, TRS("相机"), info->camera);
    append_item(out, out_len, TRS("镜头"), info->lens);
    append_item(out, out_len, TRS("时间"), info->datetime);
    append_item(out, out_len, TRS("光圈"), info->aperture);
    append_item(out, out_len, TRS("快门"), info->shutter);
    append_item(out, out_len, TRS("感光度"), info->iso);
    append_item(out, out_len, TRS("焦距"), info->focal_length);
    append_item(out, out_len, TRS("曝光补偿"), info->exposure_bias);
    if (info->width > 0 && info->height > 0) {
        snprintf(resolution, sizeof(resolution), "%lux%lu",
                 (unsigned long)info->width,
                 (unsigned long)info->height);
        append_item(out, out_len, TRS("分辨率"), resolution);
    }
    return out[0] != '\0';
}
