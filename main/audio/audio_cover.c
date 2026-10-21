#include "audio/audio_cover.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "storage/tf_card.h"

#define AUDIO_COVER_MAX_BYTES (4U * 1024U * 1024U)

static const char *TAG = "audio_cover";

static uint32_t cover_read_be24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t cover_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

static uint32_t cover_read_syncsafe32(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7FU) << 21) |
           ((uint32_t)(p[1] & 0x7FU) << 14) |
           ((uint32_t)(p[2] & 0x7FU) << 7) |
           (uint32_t)(p[3] & 0x7FU);
}

static uint32_t cover_hash_path(const char *path)
{
    uint32_t h = 2166136261U;
    while (path && *path) {
        h ^= (uint8_t)*path++;
        h *= 16777619U;
    }
    return h;
}

static esp_err_t cover_cached_path(const char *audio_path, const char *ext, char *out_path, size_t out_len)
{
    int n;

    if (!audio_path || !ext || !out_path || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    n = snprintf(out_path,
                 out_len,
                 "%s/tab5_audio_cover_%08lx.%s",
                 TF_CARD_SYS_DIR,
                 (unsigned long)cover_hash_path(audio_path),
                 ext);
    return n > 0 && (size_t)n < out_len ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t cover_find_cached(const char *audio_path, char *out_path, size_t out_len)
{
    static const char *const exts[] = {"jpg", "png"};
    struct stat st;

    if (!audio_path || !out_path || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (cover_cached_path(audio_path, exts[i], out_path, out_len) == ESP_OK &&
            stat(out_path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            return ESP_OK;
        }
    }
    out_path[0] = '\0';
    return ESP_ERR_NOT_FOUND;
}

static uint8_t *cover_alloc(size_t size)
{
    uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return buf;
}

static const char *cover_ext_for_image(const char *mime, const uint8_t *data, size_t len)
{
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "jpg";
    }
    if (len >= 8 && memcmp(data, "\x89PNG\r\n\x1A\n", 8) == 0) {
        return "png";
    }
    if (mime) {
        if (strcasecmp(mime, "image/jpeg") == 0 || strcasecmp(mime, "image/jpg") == 0 ||
            strcasecmp(mime, "JPG") == 0 || strcasecmp(mime, "JPEG") == 0) {
            return "jpg";
        }
        if (strcasecmp(mime, "image/png") == 0 || strcasecmp(mime, "PNG") == 0) {
            return "png";
        }
    }
    return NULL;
}

static esp_err_t cover_write_image(const char *audio_path,
                                   const char *mime,
                                   const uint8_t *data,
                                   size_t len,
                                   bool unsync,
                                   char *out_path,
                                   size_t out_len)
{
    const char *ext;
    FILE *fp;

    if (!audio_path || !data || len == 0 || !out_path || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > AUDIO_COVER_MAX_BYTES) {
        ESP_LOGW(TAG, "cover too large: %lu", (unsigned long)len);
        return ESP_ERR_INVALID_SIZE;
    }
    ext = cover_ext_for_image(mime, data, len);
    if (!ext) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    /* 歌曲内嵌封面解出的图片属于显示缓存，统一写到 TF 根目录 .sys，避免污染用户音乐目录。 */
    (void)tf_card_ensure_sys_dir();
    ESP_RETURN_ON_ERROR(cover_cached_path(audio_path, ext, out_path, out_len), TAG, "cover path too long");
    fp = fopen(out_path, "wb");
    if (!fp) {
        return ESP_FAIL;
    }
    if (!unsync) {
        if (fwrite(data, 1, len, fp) != len) {
            fclose(fp);
            return ESP_FAIL;
        }
    } else {
        for (size_t i = 0; i < len; i++) {
            if (i > 0 && data[i] == 0x00 && data[i - 1] == 0xFF) {
                continue;
            }
            if (fputc(data[i], fp) == EOF) {
                fclose(fp);
                return ESP_FAIL;
            }
        }
    }
    fclose(fp);
    ESP_LOGI(TAG, "cover extracted: %s -> %s", audio_path, out_path);
    return ESP_OK;
}

static size_t cover_skip_text(const uint8_t *data, size_t len, size_t pos, uint8_t encoding)
{
    if (encoding == 1 || encoding == 2) {
        while (pos + 1 < len) {
            if (data[pos] == 0x00 && data[pos + 1] == 0x00) {
                return pos + 2;
            }
            pos++;
        }
        return len;
    }
    while (pos < len && data[pos] != 0x00) {
        pos++;
    }
    return pos < len ? pos + 1 : len;
}

static esp_err_t cover_parse_apic(const char *audio_path,
                                  const uint8_t *data,
                                  size_t len,
                                  bool unsync,
                                  char *out_path,
                                  size_t out_len)
{
    char mime[48] = {0};
    size_t pos = 1;
    size_t mime_start;
    size_t mime_len;

    if (len < 5) {
        return ESP_ERR_INVALID_SIZE;
    }
    mime_start = pos;
    while (pos < len && data[pos] != 0x00) {
        pos++;
    }
    if (pos >= len) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    mime_len = pos - mime_start;
    if (mime_len >= sizeof(mime)) {
        mime_len = sizeof(mime) - 1;
    }
    memcpy(mime, data + mime_start, mime_len);
    pos++;
    if (pos >= len) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    pos++;
    pos = cover_skip_text(data, len, pos, data[0]);
    if (pos >= len) {
        return ESP_ERR_NOT_FOUND;
    }
    return cover_write_image(audio_path, mime, data + pos, len - pos, unsync, out_path, out_len);
}

static esp_err_t cover_parse_pic(const char *audio_path,
                                 const uint8_t *data,
                                 size_t len,
                                 bool unsync,
                                 char *out_path,
                                 size_t out_len)
{
    char fmt[4] = {0};
    size_t pos = 5;

    if (len < 8) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(fmt, data + 1, 3);
    pos = cover_skip_text(data, len, pos, data[0]);
    if (pos >= len) {
        return ESP_ERR_NOT_FOUND;
    }
    return cover_write_image(audio_path, fmt, data + pos, len - pos, unsync, out_path, out_len);
}

static esp_err_t cover_extract_id3(FILE *fp, const char *audio_path, char *out_path, size_t out_len)
{
    uint8_t hdr[10];
    uint32_t tag_size;
    uint32_t pos = 0;
    uint8_t ver;
    bool unsync;

    if (fseek(fp, 0, SEEK_SET) != 0 || fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return ESP_FAIL;
    }
    if (memcmp(hdr, "ID3", 3) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    ver = hdr[3];
    if (ver < 2 || ver > 4) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    tag_size = cover_read_syncsafe32(hdr + 6);
    unsync = (hdr[5] & 0x80U) != 0;

    if (hdr[5] & 0x40U) {
        uint8_t ext[4];
        uint32_t ext_size;
        if (fread(ext, 1, sizeof(ext), fp) != sizeof(ext)) {
            return ESP_FAIL;
        }
        ext_size = ver == 4 ? cover_read_syncsafe32(ext) : cover_read_be32(ext);
        if (ver == 4 && ext_size >= 4) {
            ext_size -= 4;
        }
        if (fseek(fp, (long)ext_size, SEEK_CUR) != 0) {
            return ESP_FAIL;
        }
        pos += 4 + ext_size;
    }

    while (pos < tag_size) {
        uint8_t frame_hdr[10];
        char id[5] = {0};
        uint32_t frame_size;
        size_t header_size = ver == 2 ? 6U : 10U;
        uint8_t *payload;
        esp_err_t ret;

        if (pos + header_size > tag_size || fread(frame_hdr, 1, header_size, fp) != header_size) {
            break;
        }
        if (frame_hdr[0] == 0) {
            break;
        }
        if (ver == 2) {
            memcpy(id, frame_hdr, 3);
            frame_size = cover_read_be24(frame_hdr + 3);
        } else {
            memcpy(id, frame_hdr, 4);
            frame_size = ver == 4 ? cover_read_syncsafe32(frame_hdr + 4) : cover_read_be32(frame_hdr + 4);
        }
        pos += header_size;
        if (frame_size == 0 || pos + frame_size > tag_size) {
            break;
        }
        if ((ver == 2 && strcmp(id, "PIC") != 0) || (ver != 2 && strcmp(id, "APIC") != 0)) {
            if (fseek(fp, (long)frame_size, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
            pos += frame_size;
            continue;
        }
        payload = cover_alloc(frame_size);
        if (!payload) {
            return ESP_ERR_NO_MEM;
        }
        if (fread(payload, 1, frame_size, fp) != frame_size) {
            free(payload);
            return ESP_FAIL;
        }
        ret = ver == 2 ? cover_parse_pic(audio_path, payload, frame_size, unsync, out_path, out_len)
                       : cover_parse_apic(audio_path, payload, frame_size, unsync, out_path, out_len);
        free(payload);
        return ret;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t cover_extract_flac(FILE *fp, const char *audio_path, char *out_path, size_t out_len)
{
    uint8_t sig[4];
    bool last = false;

    if (fseek(fp, 0, SEEK_SET) != 0 || fread(sig, 1, sizeof(sig), fp) != sizeof(sig)) {
        return ESP_FAIL;
    }
    if (memcmp(sig, "fLaC", 4) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    while (!last) {
        uint8_t hdr[4];
        uint32_t len;
        uint8_t type;

        if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
            return ESP_FAIL;
        }
        last = (hdr[0] & 0x80U) != 0;
        type = hdr[0] & 0x7FU;
        len = cover_read_be24(hdr + 1);
        if (type != 6) {
            if (fseek(fp, (long)len, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
            continue;
        }
        if (len > AUDIO_COVER_MAX_BYTES + 256U) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint8_t *buf = cover_alloc(len);
        if (!buf) {
            return ESP_ERR_NO_MEM;
        }
        if (fread(buf, 1, len, fp) != len) {
            free(buf);
            return ESP_FAIL;
        }
        size_t pos = 4;
        if (pos + 4 > len) {
            free(buf);
            return ESP_ERR_INVALID_RESPONSE;
        }
        uint32_t mime_len = cover_read_be32(buf + pos);
        pos += 4;
        if (pos + mime_len + 4 > len || mime_len >= 64) {
            free(buf);
            return ESP_ERR_INVALID_RESPONSE;
        }
        char mime[64] = {0};
        memcpy(mime, buf + pos, mime_len);
        pos += mime_len;
        uint32_t desc_len = cover_read_be32(buf + pos);
        pos += 4 + desc_len;
        if (pos + 20 > len) {
            free(buf);
            return ESP_ERR_INVALID_RESPONSE;
        }
        pos += 16;
        uint32_t data_len = cover_read_be32(buf + pos);
        pos += 4;
        if (pos + data_len > len) {
            free(buf);
            return ESP_ERR_INVALID_RESPONSE;
        }
        esp_err_t ret = cover_write_image(audio_path, mime, buf + pos, data_len, false, out_path, out_len);
        free(buf);
        return ret;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t audio_cover_extract_to_file(const char *audio_path, char *out_path, size_t out_len)
{
    FILE *fp;
    esp_err_t ret;

    if (out_path && out_len > 0) {
        out_path[0] = '\0';
    }
    if (!audio_path || !audio_path[0] || !out_path || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cover_find_cached(audio_path, out_path, out_len) == ESP_OK) {
        return ESP_OK;
    }
    fp = fopen(audio_path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }
    ret = cover_extract_id3(fp, audio_path, out_path, out_len);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = cover_extract_flac(fp, audio_path, out_path, out_len);
    }
    fclose(fp);
    return ret;
}
