#include "audio/audio_duration_cache.h"

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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define AUDIO_DURATION_CACHE_MAX        384
#define AUDIO_DURATION_SCAN_TASK_STACK  (8 * 1024)
#define AUDIO_DURATION_SCAN_TASK_PRIO   2
#define AUDIO_DURATION_SCAN_IDLE_MS     25
#define AUDIO_DURATION_SCAN_MAX_TRACKS  AUDIO_LIBRARY_MAX_TRACKS
#define AUDIO_DURATION_PCM_RATE         48000U
#define AUDIO_DURATION_PCM_BITS         16U
#define AUDIO_DURATION_PCM_CHANNELS     2U

static const char *TAG = "audio_duration";

typedef struct {
    bool valid;
    char path[AUDIO_LIBRARY_PATH_MAX];
    uint64_t size;
    uint64_t mtime;
    uint32_t duration_ms;
    uint32_t age;
} audio_duration_cache_entry_t;

typedef struct {
    char path[AUDIO_LIBRARY_PATH_MAX];
    uint64_t size;
    uint64_t mtime;
} audio_duration_scan_item_t;

typedef struct {
    size_t count;
    audio_duration_cache_update_cb_t cb;
    void *user_data;
    audio_duration_scan_item_t items[];
} audio_duration_scan_job_t;

static audio_duration_cache_entry_t *s_cache;
static SemaphoreHandle_t s_cache_lock;
static uint32_t s_cache_age;

static uint16_t duration_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t duration_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t duration_read_be64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

static uint32_t duration_ms_from_rate(uint64_t units, uint32_t units_per_second)
{
    if (units_per_second == 0) {
        return 0;
    }
    uint64_t ms = (units * 1000ULL) / units_per_second;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static uint32_t duration_ms_from_pcm_bytes(uint64_t bytes,
                                           uint32_t sample_rate,
                                           uint16_t channels,
                                           uint16_t bits_per_sample)
{
    uint64_t bytes_per_second = (uint64_t)sample_rate * channels * bits_per_sample / 8ULL;
    return duration_ms_from_rate(bytes, (uint32_t)bytes_per_second);
}

static esp_err_t duration_cache_ensure_lock(void)
{
    if (!s_cache_lock) {
        s_cache_lock = xSemaphoreCreateMutex();
        if (!s_cache_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_cache) {
        s_cache = heap_caps_calloc(AUDIO_DURATION_CACHE_MAX,
                                   sizeof(audio_duration_cache_entry_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_cache) {
            s_cache = heap_caps_calloc(AUDIO_DURATION_CACHE_MAX,
                                       sizeof(audio_duration_cache_entry_t),
                                       MALLOC_CAP_8BIT);
        }
        if (!s_cache) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static int duration_cache_find_locked(const char *path, uint64_t size, uint64_t mtime)
{
    for (size_t i = 0; i < AUDIO_DURATION_CACHE_MAX; i++) {
        if (s_cache[i].valid &&
            s_cache[i].size == size &&
            s_cache[i].mtime == mtime &&
            strcmp(s_cache[i].path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t duration_cache_put(const char *path,
                                    uint64_t size,
                                    uint64_t mtime,
                                    uint32_t duration_ms)
{
    int slot = -1;
    uint32_t oldest_age = UINT32_MAX;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(duration_cache_ensure_lock(), TAG, "cache lock init failed");

    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    slot = duration_cache_find_locked(path, size, mtime);
    if (slot < 0) {
        for (size_t i = 0; i < AUDIO_DURATION_CACHE_MAX; i++) {
            if (!s_cache[i].valid) {
                slot = (int)i;
                break;
            }
            if (s_cache[i].age < oldest_age) {
                oldest_age = s_cache[i].age;
                slot = (int)i;
            }
        }
    }
    if (slot >= 0) {
        s_cache[slot].valid = true;
        strlcpy(s_cache[slot].path, path, sizeof(s_cache[slot].path));
        s_cache[slot].size = size;
        s_cache[slot].mtime = mtime;
        s_cache[slot].duration_ms = duration_ms;
        s_cache[slot].age = ++s_cache_age;
    }
    xSemaphoreGive(s_cache_lock);
    return slot >= 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t duration_skip_id3v2(FILE *fp, uint64_t file_size, uint64_t *out_offset)
{
    uint8_t hdr[10];

    if (out_offset) {
        *out_offset = 0;
    }
    if (!fp) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return ESP_FAIL;
    }
    if (memcmp(hdr, "ID3", 3) != 0) {
        return ESP_OK;
    }
    if ((hdr[6] & 0x80) || (hdr[7] & 0x80) || (hdr[8] & 0x80) || (hdr[9] & 0x80)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint64_t tag_size = ((uint64_t)hdr[6] << 21) |
                        ((uint64_t)hdr[7] << 14) |
                        ((uint64_t)hdr[8] << 7) |
                        hdr[9];
    uint64_t offset = 10ULL + tag_size + ((hdr[5] & 0x10) ? 10ULL : 0ULL);
    if (offset >= file_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_offset) {
        *out_offset = offset;
    }
    return ESP_OK;
}

static esp_err_t duration_measure_wav(const char *path, uint32_t *out_duration_ms)
{
    FILE *fp = fopen(path, "rb");
    uint8_t hdr[12];
    bool have_fmt = false;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;

    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) ||
        memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(fp);
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (!feof(fp)) {
        uint8_t chunk[8];
        uint32_t chunk_size;
        long data_pos;

        if (fread(chunk, 1, sizeof(chunk), fp) != sizeof(chunk)) {
            break;
        }
        chunk_size = duration_read_le32(chunk + 4);
        data_pos = ftell(fp);
        if (data_pos < 0) {
            fclose(fp);
            return ESP_FAIL;
        }
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk_size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), fp) != sizeof(fmt)) {
                fclose(fp);
                return ESP_FAIL;
            }
            sample_rate = duration_read_le32(fmt + 4);
            channels = duration_read_le16(fmt + 2);
            bits_per_sample = duration_read_le16(fmt + 14);
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_size = chunk_size;
            if (have_fmt) {
                break;
            }
        }
        if (fseek(fp, data_pos + chunk_size + (chunk_size & 1U), SEEK_SET) != 0) {
            fclose(fp);
            return ESP_FAIL;
        }
    }
    fclose(fp);

    if (!have_fmt || data_size == 0 || sample_rate == 0 || channels == 0 || bits_per_sample == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_duration_ms) {
        *out_duration_ms = duration_ms_from_pcm_bytes(data_size, sample_rate, channels, bits_per_sample);
    }
    return ESP_OK;
}

static bool duration_mp3_parse_bitrate(uint32_t hdr, uint32_t *out_bitrate_bps)
{
    static const uint16_t v1_l1[] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};
    static const uint16_t v1_l2[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
    static const uint16_t v1_l3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t v2_l1[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0};
    static const uint16_t v2_l23[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    uint8_t version = (hdr >> 19) & 0x03;
    uint8_t layer = (hdr >> 17) & 0x03;
    uint8_t bitrate_index = (hdr >> 12) & 0x0F;
    uint8_t sample_index = (hdr >> 10) & 0x03;
    uint16_t kbps = 0;

    if ((hdr & 0xFFE00000U) != 0xFFE00000U ||
        version == 1 || layer == 0 ||
        bitrate_index == 0 || bitrate_index == 15 ||
        sample_index == 3) {
        return false;
    }
    if (version == 3) {
        kbps = layer == 3 ? v1_l1[bitrate_index] :
               layer == 2 ? v1_l2[bitrate_index] : v1_l3[bitrate_index];
    } else {
        kbps = layer == 3 ? v2_l1[bitrate_index] : v2_l23[bitrate_index];
    }
    if (kbps == 0) {
        return false;
    }
    if (out_bitrate_bps) {
        *out_bitrate_bps = (uint32_t)kbps * 1000U;
    }
    return true;
}

static esp_err_t duration_measure_mp3(const char *path, uint64_t file_size, uint32_t *out_duration_ms)
{
    FILE *fp = fopen(path, "rb");
    uint64_t offset = 0;
    uint8_t buf[4096];
    uint64_t scanned = 0;
    esp_err_t ret;

    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }
    ret = duration_skip_id3v2(fp, file_size, &offset);
    if (ret != ESP_OK) {
        fclose(fp);
        return ret;
    }
    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
        fclose(fp);
        return ESP_FAIL;
    }

    while (scanned < 64U * 1024U) {
        size_t got = fread(buf, 1, sizeof(buf), fp);
        if (got < 4) {
            break;
        }
        for (size_t i = 0; i + 3 < got; i++) {
            uint32_t hdr = ((uint32_t)buf[i] << 24) |
                           ((uint32_t)buf[i + 1] << 16) |
                           ((uint32_t)buf[i + 2] << 8) |
                           buf[i + 3];
            uint32_t bitrate_bps = 0;
            if (duration_mp3_parse_bitrate(hdr, &bitrate_bps)) {
                uint64_t frame_offset = offset + scanned + i;
                uint64_t audio_bytes = file_size > frame_offset ? file_size - frame_offset : 0;
                fclose(fp);
                if (bitrate_bps == 0 || audio_bytes == 0) {
                    return ESP_ERR_INVALID_RESPONSE;
                }
                if (out_duration_ms) {
                    *out_duration_ms = duration_ms_from_rate(audio_bytes * 8ULL, bitrate_bps);
                }
                return ESP_OK;
            }
        }
        if (got < sizeof(buf)) {
            break;
        }
        scanned += got - 3;
        vTaskDelay(pdMS_TO_TICKS(5));
        if (fseek(fp, (long)(offset + scanned), SEEK_SET) != 0) {
            fclose(fp);
            return ESP_FAIL;
        }
    }
    fclose(fp);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t duration_measure_flac(const char *path, uint64_t file_size, uint32_t *out_duration_ms)
{
    FILE *fp = fopen(path, "rb");
    uint64_t offset = 0;
    uint8_t marker[4];
    esp_err_t ret;

    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }
    ret = duration_skip_id3v2(fp, file_size, &offset);
    if (ret != ESP_OK) {
        fclose(fp);
        return ret;
    }
    if (fseek(fp, (long)offset, SEEK_SET) != 0 ||
        fread(marker, 1, sizeof(marker), fp) != sizeof(marker) ||
        memcmp(marker, "fLaC", 4) != 0) {
        fclose(fp);
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (!feof(fp)) {
        uint8_t header[4];
        uint32_t block_len;
        uint8_t block_type;
        bool last;

        if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
            break;
        }
        last = (header[0] & 0x80) != 0;
        block_type = header[0] & 0x7F;
        block_len = ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];
        if (block_type == 0) {
            uint8_t streaminfo[34];
            if (block_len < sizeof(streaminfo) ||
                fread(streaminfo, 1, sizeof(streaminfo), fp) != sizeof(streaminfo)) {
                fclose(fp);
                return ESP_FAIL;
            }
            uint64_t packed = duration_read_be64(streaminfo + 10);
            uint32_t sample_rate = (uint32_t)((packed >> 44) & 0xFFFFF);
            uint64_t total_samples = packed & 0xFFFFFFFFFULL;
            fclose(fp);
            if (sample_rate == 0 || total_samples == 0) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (out_duration_ms) {
                *out_duration_ms = duration_ms_from_rate(total_samples, sample_rate);
            }
            return ESP_OK;
        }
        if (fseek(fp, (long)block_len, SEEK_CUR) != 0) {
            fclose(fp);
            return ESP_FAIL;
        }
        if (last) {
            break;
        }
    }
    fclose(fp);
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t duration_measure_path(const char *path,
                                       uint64_t size,
                                       uint32_t *out_duration_ms)
{
    const char *format = audio_library_format_for_path(path);

    if (out_duration_ms) {
        *out_duration_ms = 0;
    }
    if (!format) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcasecmp(format, "WAV") == 0) {
        return duration_measure_wav(path, out_duration_ms);
    }
    if (strcasecmp(format, "PCM") == 0) {
        if (out_duration_ms) {
            *out_duration_ms = duration_ms_from_pcm_bytes(size,
                                                          AUDIO_DURATION_PCM_RATE,
                                                          AUDIO_DURATION_PCM_CHANNELS,
                                                          AUDIO_DURATION_PCM_BITS);
        }
        return ESP_OK;
    }
    if (strcasecmp(format, "MP3") == 0) {
        return duration_measure_mp3(path, size, out_duration_ms);
    }
    if (strcasecmp(format, "FLAC") == 0) {
        return duration_measure_flac(path, size, out_duration_ms);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_duration_cache_get(const char *path,
                                   uint64_t size,
                                   uint64_t mtime,
                                   uint32_t *out_duration_ms)
{
    int slot;

    if (out_duration_ms) {
        *out_duration_ms = 0;
    }
    if (!path || !path[0] || !out_duration_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(duration_cache_ensure_lock(), TAG, "cache lock init failed");

    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    slot = duration_cache_find_locked(path, size, mtime);
    if (slot >= 0) {
        s_cache[slot].age = ++s_cache_age;
        *out_duration_ms = s_cache[slot].duration_ms;
    }
    xSemaphoreGive(s_cache_lock);
    return slot >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t audio_duration_cache_get_or_measure(const char *path,
                                              uint64_t size,
                                              uint64_t mtime,
                                              uint32_t *out_duration_ms)
{
    struct stat st;
    uint32_t duration_ms = 0;
    esp_err_t ret;

    if (!path || !path[0] || !out_duration_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_duration_ms = 0;
    if ((size == 0 || mtime == 0) && stat(path, &st) == 0) {
        size = (uint64_t)st.st_size;
        mtime = (uint64_t)st.st_mtime;
    }
    ret = audio_duration_cache_get(path, size, mtime, out_duration_ms);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

    ret = duration_measure_path(path, size, &duration_ms);
    if (ret == ESP_OK || ret == ESP_ERR_NOT_SUPPORTED) {
        *out_duration_ms = duration_ms;
        (void)duration_cache_put(path, size, mtime, duration_ms);
        return ESP_OK;
    }
    ESP_LOGD(TAG, "duration unavailable: %s (%s)", path, esp_err_to_name(ret));
    return ret;
}

static void audio_duration_cache_scan_task(void *arg)
{
    audio_duration_scan_job_t *job = (audio_duration_scan_job_t *)arg;

    if (!job) {
        vTaskDelete(NULL);
        return;
    }
    for (size_t i = 0; i < job->count; i++) {
        uint32_t duration_ms = 0;
        esp_err_t ret = audio_duration_cache_get_or_measure(job->items[i].path,
                                                            job->items[i].size,
                                                            job->items[i].mtime,
                                                            &duration_ms);
        if (ret == ESP_OK && job->cb) {
            job->cb(job->items[i].path,
                    job->items[i].size,
                    job->items[i].mtime,
                    duration_ms,
                    job->user_data);
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_DURATION_SCAN_IDLE_MS));
    }
    free(job);
    vTaskDelete(NULL);
}

esp_err_t audio_duration_cache_scan_async(const audio_library_track_t *tracks,
                                          size_t count,
                                          audio_duration_cache_update_cb_t cb,
                                          void *user_data)
{
    audio_duration_scan_job_t *job;
    size_t alloc_count = count;
    size_t written = 0;

    if (!tracks && count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (alloc_count > AUDIO_DURATION_SCAN_MAX_TRACKS) {
        alloc_count = AUDIO_DURATION_SCAN_MAX_TRACKS;
    }
    if (alloc_count == 0) {
        return ESP_OK;
    }

    size_t alloc_size = sizeof(*job) + alloc_count * sizeof(job->items[0]);
    job = heap_caps_calloc(1, alloc_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!job) {
        job = heap_caps_calloc(1, alloc_size, MALLOC_CAP_8BIT);
    }
    if (!job) {
        return ESP_ERR_NO_MEM;
    }
    job->cb = cb;
    job->user_data = user_data;
    for (size_t i = 0; i < alloc_count; i++) {
        if (!tracks[i].path[0]) {
            continue;
        }
        strlcpy(job->items[written].path, tracks[i].path, sizeof(job->items[written].path));
        job->items[written].size = tracks[i].size;
        job->items[written].mtime = tracks[i].mtime;
        written++;
    }
    job->count = written;
    if (job->count == 0) {
        free(job);
        return ESP_OK;
    }
    if (xTaskCreate(audio_duration_cache_scan_task,
                    "aud_dur_scan",
                    AUDIO_DURATION_SCAN_TASK_STACK,
                    job,
                    AUDIO_DURATION_SCAN_TASK_PRIO,
                    NULL) != pdPASS) {
        free(job);
        return ESP_FAIL;
    }
    return ESP_OK;
}
