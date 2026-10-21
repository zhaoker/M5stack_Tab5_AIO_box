#include "audio/audio_player_service.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "audio/audio_adf_sink.h"
#include "audio/audio_codec_sink.h"
#include "audio/audio_duration_cache.h"
#include "audio/audio_favorites.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings/app_settings.h"

#if AUDIO_ADF_SINK_AVAILABLE && defined(__has_include)
#if __has_include("mp3_decoder.h")
#include "mp3_decoder.h"
#define AUDIO_PLAYER_ADF_MP3_AVAILABLE 1
#endif
#if __has_include("flac_decoder.h")
#include "flac_decoder.h"
#define AUDIO_PLAYER_ADF_FLAC_AVAILABLE 1
#endif
#endif
#ifndef AUDIO_PLAYER_ADF_MP3_AVAILABLE
#define AUDIO_PLAYER_ADF_MP3_AVAILABLE 0
#endif
#ifndef AUDIO_PLAYER_ADF_FLAC_AVAILABLE
#define AUDIO_PLAYER_ADF_FLAC_AVAILABLE 0
#endif
#define AUDIO_PLAYER_ADF_DECODER_AVAILABLE (AUDIO_PLAYER_ADF_MP3_AVAILABLE || AUDIO_PLAYER_ADF_FLAC_AVAILABLE)

#define AUDIO_PLAYER_TASK_STACK      (12 * 1024)
#define AUDIO_PLAYER_TASK_PRIORITY   7
#define AUDIO_PLAYER_QUEUE_LEN       8
#define AUDIO_PLAYER_IO_BUF_BYTES    (16 * 1024)
#define AUDIO_PLAYER_FILE_BUF_BYTES  (64 * 1024)
#define AUDIO_PLAYER_DEFAULT_VOLUME  80
#define AUDIO_PLAYER_PCM_RATE        48000
#define AUDIO_PLAYER_PCM_BITS        16
#define AUDIO_PLAYER_PCM_CHANNELS    2
#define AUDIO_PLAYER_PROGRESS_MS     500
#define AUDIO_PLAYER_MP3_RB_BYTES    (64 * 1024)
#define AUDIO_PLAYER_FLAC_RB_BYTES   (96 * 1024)
#define AUDIO_PLAYER_PREFETCH_MAX_BYTES (2 * 1024 * 1024)
#define AUDIO_PLAYER_PREFETCH_MIN_BYTES (128 * 1024)
#define AUDIO_PLAYER_PREFETCH_READ_CHUNK_BYTES (16 * 1024)
#define AUDIO_PLAYER_PREFETCH_TASK_STACK 4096
#define AUDIO_PLAYER_PREFETCH_TASK_PRIORITY 4

static const char *TAG = "audio_player";

typedef enum {
    AUDIO_PLAYER_CMD_PLAY_PATH,
    AUDIO_PLAYER_CMD_PLAY_INDEX,
    AUDIO_PLAYER_CMD_NEXT,
    AUDIO_PLAYER_CMD_PREV,
    AUDIO_PLAYER_CMD_PAUSE,
    AUDIO_PLAYER_CMD_RESUME,
    AUDIO_PLAYER_CMD_STOP,
    AUDIO_PLAYER_CMD_SET_VOLUME,
    AUDIO_PLAYER_CMD_SET_MUTE,
    AUDIO_PLAYER_CMD_SET_PLAY_MODE,
    AUDIO_PLAYER_CMD_SCAN_FAVORITES,
    AUDIO_PLAYER_CMD_SCAN_DIR,
    AUDIO_PLAYER_CMD_SCAN_ALL,
} audio_player_cmd_type_t;

typedef struct {
    audio_player_cmd_type_t type;
    size_t index;
    uint8_t volume;
    bool muted;
    audio_player_play_mode_t play_mode;
    char path[AUDIO_LIBRARY_PATH_MAX];
} audio_player_cmd_t;

typedef enum {
    AUDIO_PLAYER_CONTROL_CONTINUE,
    AUDIO_PLAYER_CONTROL_STOP,
    AUDIO_PLAYER_CONTROL_RESTART,
} audio_player_control_t;

typedef enum {
    AUDIO_PLAYER_TRACK_FINISHED,
    AUDIO_PLAYER_TRACK_STOPPED,
    AUDIO_PLAYER_TRACK_RESTART,
    AUDIO_PLAYER_TRACK_ERROR,
} audio_player_track_result_t;

typedef struct {
    SemaphoreHandle_t lock;
    QueueHandle_t queue;
    TaskHandle_t task;
    audio_library_track_t *tracks;
    size_t track_count;
    size_t current_index;
    audio_player_status_t status;
    audio_player_status_cb_t cb;
    void *cb_user_data;
    TickType_t last_duration_publish_tick;
    bool initialized;
} audio_player_ctx_t;

typedef struct {
    uint32_t sample_rate;
    uint16_t channel_count;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_info_t;

typedef struct {
    FILE *fp;
    uint8_t *buffer;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t fill;
    uint64_t bytes_read;
    volatile bool stop;
    bool eof;
    bool error;
    bool refill_active;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
} audio_player_prefetch_t;

#if AUDIO_PLAYER_ADF_DECODER_AVAILABLE
typedef struct {
    FILE *fp;
    void *file_buf;
    audio_player_prefetch_t prefetch;
    const char *format_name;
    volatile bool abort_requested;
    bool format_ready;
    bool sink_open;
    bool pcm_started;
    uint64_t pcm_bytes;
    audio_codec_sink_format_t format;
} audio_player_adf_decoder_ctx_t;

typedef audio_element_handle_t (*audio_player_adf_decoder_init_fn_t)(void);
#endif

static audio_player_ctx_t s_player;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static esp_err_t audio_player_start_task_locked(void);

static void *audio_player_alloc_psram_buffer(size_t bytes, const char *name)
{
    void *buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
        ESP_LOGI(TAG, "%s allocated in PSRAM: %u bytes", name, (unsigned)bytes);
        return buf;
    }
    buf = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (buf) {
        ESP_LOGW(TAG, "%s allocated in internal RAM fallback: %u bytes", name, (unsigned)bytes);
        return buf;
    }
    ESP_LOGW(TAG, "%s allocation failed: %u bytes", name, (unsigned)bytes);
    return NULL;
}

static void audio_player_set_file_buffer(FILE *fp, void **owned_buf, const char *name)
{
    if (!fp || !owned_buf || *owned_buf) {
        return;
    }
    void *buf = audio_player_alloc_psram_buffer(AUDIO_PLAYER_FILE_BUF_BYTES, name);
    if (!buf) {
        return;
    }
    if (setvbuf(fp, (char *)buf, _IOFBF, AUDIO_PLAYER_FILE_BUF_BYTES) == 0) {
        *owned_buf = buf;
        return;
    }
    ESP_LOGW(TAG, "setvbuf failed for %s", name);
    free(buf);
}

#if AUDIO_PLAYER_ADF_DECODER_AVAILABLE
#include "audio_player_prefetch.inc"
#endif

static bool audio_player_play_mode_is_valid(audio_player_play_mode_t mode)
{
    return mode == AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP ||
           mode == AUDIO_PLAYER_PLAY_MODE_LIST_LOOP ||
           mode == AUDIO_PLAYER_PLAY_MODE_SHUFFLE;
}

static const char *audio_player_play_mode_message(audio_player_play_mode_t mode)
{
    switch (mode) {
    case AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP:
        return TRS("单曲循环");
    case AUDIO_PLAYER_PLAY_MODE_LIST_LOOP:
        return TRS("列表循环");
    case AUDIO_PLAYER_PLAY_MODE_SHUFFLE:
        return TRS("随机播放");
    default:
        return TRS("播放模式已切换");
    }
}

static bool audio_player_cmd_is_play_request(audio_player_cmd_type_t type)
{
    return type == AUDIO_PLAYER_CMD_PLAY_PATH || type == AUDIO_PLAYER_CMD_PLAY_INDEX ||
           type == AUDIO_PLAYER_CMD_NEXT || type == AUDIO_PLAYER_CMD_PREV;
}

static void audio_player_copy_track_to_status_locked(void)
{
    audio_player_status_t *st = &s_player.status;
    st->track_count = s_player.track_count;
    st->current_index = s_player.current_index;
    st->title[0] = '\0';
    st->artist[0] = '\0';
    st->path[0] = '\0';
    st->duration_ms = 0;

    if (s_player.tracks && s_player.track_count > 0 && s_player.current_index < s_player.track_count) {
        const audio_library_track_t *track = &s_player.tracks[s_player.current_index];
        strlcpy(st->title, track->title[0] ? track->title : track->name, sizeof(st->title));
        strlcpy(st->artist, track->artist, sizeof(st->artist));
        strlcpy(st->path, track->path, sizeof(st->path));
        st->duration_ms = track->duration_ms;
    }
    if (st->duration_ms > 0 && st->elapsed_ms > st->duration_ms) {
        st->elapsed_ms = st->duration_ms;
    }
}

static void audio_player_publish(audio_player_state_t state, const char *message)
{
    audio_player_status_cb_t cb;
    void *cb_user_data;
    audio_player_status_t snapshot;

    if (!s_player.lock) {
        return;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    s_player.status.state = state;
    if (message) {
        strlcpy(s_player.status.message, TRS(message), sizeof(s_player.status.message));
        s_player.status.message_seq++;
    }
    audio_player_copy_track_to_status_locked();
    snapshot = s_player.status;
    cb = s_player.cb;
    cb_user_data = s_player.cb_user_data;
    xSemaphoreGive(s_player.lock);

    if (cb) {
        cb(&snapshot, cb_user_data);
    }
}

#include "audio_player_duration.inc"

static void audio_player_update_track_index(size_t index)
{
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    if (index < s_player.track_count) {
        s_player.current_index = index;
        audio_player_reset_progress_locked();
    }
    xSemaphoreGive(s_player.lock);
}

static esp_err_t audio_player_alloc_tracks_locked(void)
{
    if (s_player.tracks) {
        return ESP_OK;
    }
    s_player.tracks = heap_caps_calloc(AUDIO_LIBRARY_MAX_TRACKS,
                                       sizeof(audio_library_track_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_player.tracks) {
        s_player.tracks = heap_caps_calloc(AUDIO_LIBRARY_MAX_TRACKS,
                                           sizeof(audio_library_track_t),
                                           MALLOC_CAP_8BIT);
    }
    return s_player.tracks ? ESP_OK : ESP_ERR_NO_MEM;
}

static size_t audio_player_shuffle_index_locked(void)
{
    size_t count = s_player.track_count;
    size_t current = s_player.current_index;

    if (count <= 1) {
        return current < count ? current : 0;
    }

    size_t next = (size_t)(esp_random() % (uint32_t)(count - 1));
    if (next >= current && current < count) {
        next++;
    }
    return next;
}

static void audio_player_select_manual_track_locked(bool forward)
{
    if (s_player.track_count == 0) {
        return;
    }
    if (s_player.status.play_mode == AUDIO_PLAYER_PLAY_MODE_SHUFFLE) {
        s_player.current_index = audio_player_shuffle_index_locked();
    } else if (forward) {
        s_player.current_index = (s_player.current_index + 1) % s_player.track_count;
    } else {
        s_player.current_index = s_player.current_index == 0 ? s_player.track_count - 1 : s_player.current_index - 1;
    }
    audio_player_reset_progress_locked();
}

static bool audio_player_select_finished_track_locked(void)
{
    if (s_player.track_count == 0) {
        return false;
    }

    switch (s_player.status.play_mode) {
    case AUDIO_PLAYER_PLAY_MODE_SINGLE_LOOP:
        break;
    case AUDIO_PLAYER_PLAY_MODE_SHUFFLE:
        s_player.current_index = audio_player_shuffle_index_locked();
        break;
    case AUDIO_PLAYER_PLAY_MODE_LIST_LOOP:
    default:
        s_player.current_index = (s_player.current_index + 1) % s_player.track_count;
        break;
    }
    audio_player_reset_progress_locked();
    return true;
}

static esp_err_t audio_player_enqueue(const audio_player_cmd_t *cmd)
{
    esp_err_t ret = audio_player_service_init();
    if (ret != ESP_OK) {
        return ret;
    }
    if (cmd && audio_player_cmd_is_play_request(cmd->type)) {
        audio_player_publish(AUDIO_PLAYER_STATE_QUEUED, TRS("播放请求已入队"));
    }
    if (xQueueSend(s_player.queue, cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
        if (cmd && audio_player_cmd_is_play_request(cmd->type)) {
            audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("播放请求入队失败"));
        }
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t audio_player_scan_dir_now(const char *path, bool recursive, size_t *out_index)
{
    esp_err_t ret;
    size_t count = 0;
    size_t index = 0;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_player_publish(AUDIO_PLAYER_STATE_SCANNING,
                         recursive ? TRS("扫描全卡音频") : TRS("扫描当前文件夹"));
    if (recursive) {
        ret = audio_library_scan_recursive(path, s_player.tracks, AUDIO_LIBRARY_MAX_TRACKS, &count);
    } else if (out_index) {
        ret = audio_library_scan_dir_for_file(path, s_player.tracks, AUDIO_LIBRARY_MAX_TRACKS, &count, &index);
    } else {
        ret = audio_library_scan_dir(path, s_player.tracks, AUDIO_LIBRARY_MAX_TRACKS, &count);
    }
    if (ret != ESP_OK) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("音频扫描失败"));
        return ret;
    }

    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    s_player.track_count = count;
    s_player.current_index = (out_index && index < count) ? index : 0;
    audio_player_reset_progress_locked();
    xSemaphoreGive(s_player.lock);
    audio_player_start_duration_scan(s_player.tracks, count);
    if (out_index) {
        *out_index = index;
    }
    audio_player_publish(count > 0 ? AUDIO_PLAYER_STATE_STOPPED : AUDIO_PLAYER_STATE_IDLE,
                         count > 0 ? TRS("列表已就绪") : TRS("未找到音频"));
    return count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t audio_player_scan_favorites_now(void)
{
    esp_err_t ret;
    size_t count = 0;
    size_t removed = 0;
    char message[96];

    audio_player_publish(AUDIO_PLAYER_STATE_SCANNING, TRS("正在加载收藏夹"));
    ret = audio_favorites_load(s_player.tracks, AUDIO_LIBRARY_MAX_TRACKS, &count, &removed);
    if (ret != ESP_OK) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("收藏夹加载失败"));
        return ret;
    }

    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    s_player.track_count = count;
    s_player.current_index = 0;
    audio_player_reset_progress_locked();
    xSemaphoreGive(s_player.lock);
    audio_player_start_duration_scan(s_player.tracks, count);

    if (count > 0 && removed > 0) {
        snprintf(message, sizeof(message), TRS("已忽略%u首失效"), (unsigned)removed);
    } else if (count > 0) {
        strlcpy(message, TRS("收藏夹已就绪"), sizeof(message));
    } else {
        strlcpy(message,
                removed > 0 ? TRS("收藏夹已清理") : TRS("收藏夹为空"),
                sizeof(message));
    }
    audio_player_publish(count > 0 ? AUDIO_PLAYER_STATE_STOPPED : AUDIO_PLAYER_STATE_IDLE, message);
    return count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t audio_player_prepare_single_track(const char *path)
{
    esp_err_t ret = audio_library_track_from_path(path, &s_player.tracks[0]);
    if (ret != ESP_OK) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("音频文件不可用"));
        return ret;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    s_player.track_count = 1;
    s_player.current_index = 0;
    audio_player_reset_progress_locked();
    xSemaphoreGive(s_player.lock);
    audio_player_start_duration_scan(s_player.tracks, 1);
    return ESP_OK;
}

static esp_err_t audio_player_prepare_path(const char *path)
{
    size_t index = 0;
    esp_err_t ret;

    if (!audio_library_is_supported(path)) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("不支持的音频格式"));
        return ESP_ERR_NOT_SUPPORTED;
    }

    ret = audio_player_scan_dir_now(path, false, &index);
    if (ret == ESP_OK) {
        audio_player_update_track_index(index);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "directory playlist scan failed, fallback to single track: %s", esp_err_to_name(ret));
    return audio_player_prepare_single_track(path);
}

static esp_err_t wav_read_info(FILE *fp, wav_info_t *info)
{
    uint8_t hdr[12];
    bool have_fmt = false;

    if (!fp || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return ESP_FAIL;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (!feof(fp)) {
        uint8_t chunk[8];
        uint32_t chunk_size;
        long chunk_data_pos;

        if (fread(chunk, 1, sizeof(chunk), fp) != sizeof(chunk)) {
            break;
        }
        chunk_size = read_le32(chunk + 4);
        chunk_data_pos = ftell(fp);
        if (chunk_data_pos < 0) {
            return ESP_FAIL;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk_size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), fp) != sizeof(fmt)) {
                return ESP_FAIL;
            }
            if (read_le16(fmt) != 1) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            info->channel_count = read_le16(fmt + 2);
            info->sample_rate = read_le32(fmt + 4);
            info->bits_per_sample = read_le16(fmt + 14);
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            info->data_offset = (uint32_t)chunk_data_pos;
            info->data_size = chunk_size;
            if (have_fmt) {
                return ESP_OK;
            }
        }

        long next = chunk_data_pos + chunk_size + (chunk_size & 1U);
        if (fseek(fp, next, SEEK_SET) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static audio_player_control_t audio_player_handle_command(const audio_player_cmd_t *cmd)
{
    if (!cmd) {
        return AUDIO_PLAYER_CONTROL_CONTINUE;
    }

    switch (cmd->type) {
    case AUDIO_PLAYER_CMD_SET_VOLUME:
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        s_player.status.volume = cmd->volume > 100 ? 100 : cmd->volume;
        xSemaphoreGive(s_player.lock);
        (void)audio_codec_sink_set_volume(s_player.status.volume);
        (void)app_settings_set_audio_volume(s_player.status.volume);
        audio_player_publish(s_player.status.state, TRS("音量已调整"));
        return AUDIO_PLAYER_CONTROL_CONTINUE;

    case AUDIO_PLAYER_CMD_SET_MUTE:
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        s_player.status.muted = cmd->muted;
        xSemaphoreGive(s_player.lock);
        (void)audio_codec_sink_set_mute(cmd->muted);
        audio_player_publish(s_player.status.state,
                             cmd->muted ? TRS("已静音") : TRS("已取消静音"));
        return AUDIO_PLAYER_CONTROL_CONTINUE;

    case AUDIO_PLAYER_CMD_SET_PLAY_MODE:
        if (!audio_player_play_mode_is_valid(cmd->play_mode)) {
            return AUDIO_PLAYER_CONTROL_CONTINUE;
        }
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        s_player.status.play_mode = cmd->play_mode;
        xSemaphoreGive(s_player.lock);
        audio_player_publish(s_player.status.state, audio_player_play_mode_message(cmd->play_mode));
        return AUDIO_PLAYER_CONTROL_CONTINUE;

    case AUDIO_PLAYER_CMD_SCAN_FAVORITES:
        (void)audio_codec_sink_close();
        (void)audio_player_scan_favorites_now();
        return AUDIO_PLAYER_CONTROL_STOP;

    case AUDIO_PLAYER_CMD_PAUSE:
        audio_player_publish(AUDIO_PLAYER_STATE_PAUSED, TRS("播放已暂停"));
        return AUDIO_PLAYER_CONTROL_CONTINUE;

    case AUDIO_PLAYER_CMD_RESUME:
        audio_player_publish(AUDIO_PLAYER_STATE_PLAYING, TRS("继续播放"));
        return AUDIO_PLAYER_CONTROL_CONTINUE;

    case AUDIO_PLAYER_CMD_STOP:
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        s_player.status.elapsed_ms = 0;
        xSemaphoreGive(s_player.lock);
        audio_player_publish(AUDIO_PLAYER_STATE_STOPPED, TRS("播放已停止"));
        return AUDIO_PLAYER_CONTROL_STOP;

    case AUDIO_PLAYER_CMD_NEXT:
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        audio_player_select_manual_track_locked(true);
        xSemaphoreGive(s_player.lock);
        return AUDIO_PLAYER_CONTROL_RESTART;

    case AUDIO_PLAYER_CMD_PREV:
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        audio_player_select_manual_track_locked(false);
        xSemaphoreGive(s_player.lock);
        return AUDIO_PLAYER_CONTROL_RESTART;

    case AUDIO_PLAYER_CMD_PLAY_INDEX:
        audio_player_update_track_index(cmd->index);
        return AUDIO_PLAYER_CONTROL_RESTART;

    case AUDIO_PLAYER_CMD_PLAY_PATH:
        if (audio_player_prepare_path(cmd->path) == ESP_OK) {
            return AUDIO_PLAYER_CONTROL_RESTART;
        }
        return AUDIO_PLAYER_CONTROL_STOP;

    case AUDIO_PLAYER_CMD_SCAN_DIR:
        (void)audio_codec_sink_close();
        (void)audio_player_scan_dir_now(cmd->path, false, NULL);
        return AUDIO_PLAYER_CONTROL_STOP;

    case AUDIO_PLAYER_CMD_SCAN_ALL:
        (void)audio_codec_sink_close();
        (void)audio_player_scan_dir_now(cmd->path, true, NULL);
        return AUDIO_PLAYER_CONTROL_STOP;

    default:
        return AUDIO_PLAYER_CONTROL_CONTINUE;
    }
}

static audio_player_control_t audio_player_process_pending(TickType_t wait_ticks)
{
    audio_player_cmd_t cmd;
    if (xQueueReceive(s_player.queue, &cmd, wait_ticks) == pdTRUE) {
        return audio_player_handle_command(&cmd);
    }
    return AUDIO_PLAYER_CONTROL_CONTINUE;
}

static audio_player_control_t audio_player_wait_while_paused(void)
{
    while (true) {
        audio_player_state_t state;
        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        state = s_player.status.state;
        xSemaphoreGive(s_player.lock);
        if (state != AUDIO_PLAYER_STATE_PAUSED) {
            return AUDIO_PLAYER_CONTROL_CONTINUE;
        }
        audio_player_control_t control = audio_player_process_pending(pdMS_TO_TICKS(100));
        if (control != AUDIO_PLAYER_CONTROL_CONTINUE) {
            return control;
        }
    }
}

static bool audio_player_is_paused(void)
{
    bool paused = false;
    if (!s_player.lock) {
        return false;
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    paused = s_player.status.state == AUDIO_PLAYER_STATE_PAUSED;
    xSemaphoreGive(s_player.lock);
    return paused;
}

static audio_player_track_result_t audio_player_stream_pcm(FILE *fp,
                                                          uint32_t data_size,
                                                          const audio_codec_sink_format_t *format)
{
    uint8_t *buf = heap_caps_malloc(AUDIO_PLAYER_IO_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(AUDIO_PLAYER_IO_BUF_BYTES, MALLOC_CAP_8BIT);
    }
    if (!buf) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("音频缓冲内存不足"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }

    esp_err_t ret = audio_codec_sink_open(format);
    if (ret != ESP_OK) {
        free(buf);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("音频输出初始化失败"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }

    uint32_t remaining = data_size;
    uint64_t written = 0;
    bool pcm_started = false;
    audio_player_publish_progress(0, true);
    while (remaining > 0) {
        audio_player_control_t control = audio_player_process_pending(0);
        if (control == AUDIO_PLAYER_CONTROL_STOP) {
            free(buf);
            return AUDIO_PLAYER_TRACK_STOPPED;
        }
        if (control == AUDIO_PLAYER_CONTROL_RESTART) {
            free(buf);
            return AUDIO_PLAYER_TRACK_RESTART;
        }
        control = audio_player_wait_while_paused();
        if (control == AUDIO_PLAYER_CONTROL_STOP) {
            free(buf);
            return AUDIO_PLAYER_TRACK_STOPPED;
        }
        if (control == AUDIO_PLAYER_CONTROL_RESTART) {
            free(buf);
            return AUDIO_PLAYER_TRACK_RESTART;
        }

        size_t wanted = remaining < AUDIO_PLAYER_IO_BUF_BYTES ? remaining : AUDIO_PLAYER_IO_BUF_BYTES;
        size_t got = fread(buf, 1, wanted, fp);
        if (got == 0) {
            if (ferror(fp)) {
                audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("音频文件读取失败"));
                free(buf);
                return AUDIO_PLAYER_TRACK_ERROR;
            }
            break;
        }
        ret = audio_codec_sink_write(buf, got);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "codec write failed: %s", esp_err_to_name(ret));
            audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("音频输出失败"));
            free(buf);
            return AUDIO_PLAYER_TRACK_ERROR;
        }
        if (!pcm_started) {
            pcm_started = true;
            ESP_LOGI(TAG, "PCM output started: %lu Hz, %u bit, %u ch",
                     (unsigned long)format->sample_rate,
                     format->bits_per_sample,
                     format->channel_count);
            audio_player_publish(AUDIO_PLAYER_STATE_PLAYING, TRS("正在播放PCM音乐"));
        }
        written += got;
        audio_player_publish_progress(audio_player_pcm_elapsed_ms(written,
                                                                  format->sample_rate,
                                                                  format->bits_per_sample,
                                                                  format->channel_count),
                                      false);
        remaining -= got;
    }

    audio_player_publish_progress(audio_player_pcm_elapsed_ms(written,
                                                              format->sample_rate,
                                                              format->bits_per_sample,
                                                              format->channel_count),
                                  true);
    free(buf);
    return AUDIO_PLAYER_TRACK_FINISHED;
}

static audio_player_track_result_t audio_player_play_wav(const char *path)
{
    FILE *fp = fopen(path, "rb");
    void *file_buf = NULL;
    wav_info_t info;
    esp_err_t ret;

    if (!fp) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("无法打开音频文件"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }
    audio_player_set_file_buffer(fp, &file_buf, "WAV file buffer");
    ret = wav_read_info(fp, &info);
    if (ret != ESP_OK) {
        fclose(fp);
        free(file_buf);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR,
                             ret == ESP_ERR_NOT_SUPPORTED ? TRS("WAV 编码暂不支持") : TRS("WAV 文件头无效"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }
    if (info.bits_per_sample != 16 || info.channel_count == 0 || info.channel_count > 2 || info.sample_rate == 0) {
        fclose(fp);
        free(file_buf);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("仅支持 16-bit PCM WAV"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }
    if (fseek(fp, info.data_offset, SEEK_SET) != 0) {
        fclose(fp);
        free(file_buf);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("WAV 数据定位失败"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }

    audio_codec_sink_format_t format = {
        .sample_rate = info.sample_rate,
        .bits_per_sample = (uint8_t)info.bits_per_sample,
        .channel_count = (uint8_t)info.channel_count,
    };
    audio_player_update_current_duration(audio_player_pcm_elapsed_ms(info.data_size,
                                                                     info.sample_rate,
                                                                     info.bits_per_sample,
                                                                     info.channel_count));
    audio_player_track_result_t result = audio_player_stream_pcm(fp, info.data_size, &format);
    fclose(fp);
    free(file_buf);
    return result;
}

#if AUDIO_PLAYER_ADF_DECODER_AVAILABLE
static bool audio_player_adf_decoder_format_is_valid(const audio_element_info_t *info)
{
    return info && info->sample_rates > 0 &&
           (info->bits == 8 || info->bits == 16 || info->bits == 24 || info->bits == 32) &&
           info->channels > 0 && info->channels <= 2;
}

static esp_err_t audio_player_adf_decoder_open_sink(audio_player_adf_decoder_ctx_t *ctx)
{
    if (!ctx || !ctx->format_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ctx->sink_open) {
        return ESP_OK;
    }
    esp_err_t ret = audio_codec_sink_open(&ctx->format);
    if (ret == ESP_OK) {
        ctx->sink_open = true;
    }
    return ret;
}

static esp_err_t audio_player_adf_decoder_event_cb(audio_element_handle_t el,
                                                   audio_event_iface_msg_t *event,
                                                   void *user_data)
{
    audio_player_adf_decoder_ctx_t *ctx = (audio_player_adf_decoder_ctx_t *)user_data;
    if (!ctx || !event || event->source_type != AUDIO_ELEMENT_TYPE_ELEMENT ||
        event->source != (void *)el) {
        return ESP_OK;
    }

    if (event->cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
        audio_element_info_t info = {0};
        audio_element_getinfo(el, &info);
        ESP_LOGI(TAG, "ADF %s info: %d Hz, %d bit, %d ch",
                 ctx->format_name, info.sample_rates, info.bits, info.channels);
        if (!audio_player_adf_decoder_format_is_valid(&info)) {
            ESP_LOGE(TAG, "unsupported %s PCM format: %d Hz, %d bit, %d ch",
                     ctx->format_name,
                     info.sample_rates, info.bits, info.channels);
            char msg[64];
            snprintf(msg, sizeof(msg), TRS("%s PCM 格式暂不支持"), ctx->format_name);
            audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
            ctx->abort_requested = true;
            return ESP_FAIL;
        }
        ctx->format.sample_rate = (uint32_t)info.sample_rates;
        ctx->format.bits_per_sample = (uint8_t)info.bits;
        ctx->format.channel_count = (uint8_t)info.channels;
        ctx->format_ready = true;
    }
    return ESP_OK;
}

static bool audio_player_adf_decoder_wait_if_paused(audio_player_adf_decoder_ctx_t *ctx)
{
    while (ctx && !ctx->abort_requested && audio_player_is_paused()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ctx && !ctx->abort_requested;
}

static int audio_player_adf_decoder_read_cb(audio_element_handle_t el,
                                            char *buf,
                                            int len,
                                            TickType_t wait_time,
                                            void *user_data)
{
    (void)el;
    (void)wait_time;
    audio_player_adf_decoder_ctx_t *ctx = (audio_player_adf_decoder_ctx_t *)user_data;
    if (!ctx || !ctx->fp || !buf || len <= 0) {
        return AEL_IO_FAIL;
    }
    if (!audio_player_adf_decoder_wait_if_paused(ctx)) {
        return AEL_IO_ABORT;
    }

    size_t got;
    if (ctx->prefetch.buffer) {
        return audio_player_prefetch_read(&ctx->prefetch, buf, len, &ctx->abort_requested);
    }

    got = fread(buf, 1, (size_t)len, ctx->fp);
    if (got > 0) {
        return (int)got;
    }
    if (ferror(ctx->fp)) {
        ESP_LOGE(TAG, "%s file read failed", ctx->format_name);
        return AEL_IO_FAIL;
    }
    return AEL_IO_DONE;
}

static int audio_player_adf_decoder_write_cb(audio_element_handle_t el,
                                             char *buf,
                                             int len,
                                             TickType_t wait_time,
                                             void *user_data)
{
    (void)el;
    (void)wait_time;
    audio_player_adf_decoder_ctx_t *ctx = (audio_player_adf_decoder_ctx_t *)user_data;
    if (!ctx || !buf || len < 0) {
        return AEL_IO_FAIL;
    }
    if (ctx->abort_requested) {
        return AEL_IO_ABORT;
    }
    if (len == 0) {
        return AEL_IO_OK;
    }
    if (!audio_player_adf_decoder_wait_if_paused(ctx)) {
        return AEL_IO_ABORT;
    }
    if (audio_player_adf_decoder_open_sink(ctx) != ESP_OK) {
        char msg[64];
        ctx->abort_requested = true;
        snprintf(msg, sizeof(msg), TRS("%s 输出初始化失败"), ctx->format_name);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
        return AEL_IO_FAIL;
    }
    esp_err_t ret = audio_codec_sink_write(buf, (size_t)len);
    if (ret != ESP_OK) {
        char msg[64];
        ESP_LOGE(TAG, "%s PCM write failed: %s", ctx->format_name, esp_err_to_name(ret));
        ctx->abort_requested = true;
        snprintf(msg, sizeof(msg), TRS("%s 输出失败"), ctx->format_name);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
        return AEL_IO_FAIL;
    }
    if (!ctx->pcm_started) {
        ctx->pcm_started = true;
        char msg[64];
        ESP_LOGI(TAG, "ADF %s PCM output started: %lu Hz, %u bit, %u ch",
                 ctx->format_name, (unsigned long)ctx->format.sample_rate,
                 ctx->format.bits_per_sample,
                 ctx->format.channel_count);
        snprintf(msg, sizeof(msg), TRS("正在播放%s音乐"), ctx->format_name);
        audio_player_publish(AUDIO_PLAYER_STATE_PLAYING, msg);
    }
    ctx->pcm_bytes += (uint32_t)len;
    audio_player_publish_progress(audio_player_pcm_elapsed_ms(ctx->pcm_bytes,
                                                              ctx->format.sample_rate,
                                                              ctx->format.bits_per_sample,
                                                              ctx->format.channel_count),
                                  false);
    return len;
}

static audio_player_track_result_t audio_player_play_adf_decoder(const char *path,
                                                                 const char *format_name,
                                                                 audio_player_adf_decoder_init_fn_t init_fn)
{
    audio_player_adf_decoder_ctx_t ctx = {
        .format_name = format_name,
    };
    audio_element_handle_t decoder = NULL;
    audio_player_track_result_t result = AUDIO_PLAYER_TRACK_ERROR;
    char msg[64];

    ctx.fp = fopen(path, "rb");
    if (!ctx.fp) {
        snprintf(msg, sizeof(msg), TRS("无法打开 %s 文件"), format_name);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
        return AUDIO_PLAYER_TRACK_ERROR;
    }
    audio_player_set_file_buffer(ctx.fp, &ctx.file_buf, "ADF file buffer");
    if (audio_player_prefetch_start(&ctx.prefetch, ctx.fp, format_name) != ESP_OK) {
        ESP_LOGW(TAG, "ADF %s prefetch unavailable, fallback to direct FILE read", format_name);
    }

    decoder = init_fn ? init_fn() : NULL;
    if (!decoder) {
        audio_player_prefetch_stop(&ctx.prefetch);
        fclose(ctx.fp);
        free(ctx.file_buf);
        snprintf(msg, sizeof(msg), TRS("%s 解码器初始化失败"), format_name);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
        return AUDIO_PLAYER_TRACK_ERROR;
    }

    audio_element_set_read_cb(decoder, audio_player_adf_decoder_read_cb, &ctx);
    audio_element_set_write_cb(decoder, audio_player_adf_decoder_write_cb, &ctx);
    audio_element_set_event_callback(decoder, audio_player_adf_decoder_event_cb, &ctx);

    ESP_LOGI(TAG, "ADF %s decode start: %s", format_name, path);
    if (audio_element_run(decoder) != ESP_OK ||
        audio_element_resume(decoder, 0, pdMS_TO_TICKS(2000)) != ESP_OK) {
        snprintf(msg, sizeof(msg), TRS("%s 解码任务启动失败"), format_name);
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
        goto cleanup;
    }

    while (true) {
        audio_player_control_t control = audio_player_process_pending(pdMS_TO_TICKS(50));
        if (control == AUDIO_PLAYER_CONTROL_STOP || control == AUDIO_PLAYER_CONTROL_RESTART) {
            ctx.abort_requested = true;
            (void)audio_element_stop(decoder);
            (void)audio_element_wait_for_stop_ms(decoder, pdMS_TO_TICKS(2000));
            result = control == AUDIO_PLAYER_CONTROL_STOP ? AUDIO_PLAYER_TRACK_STOPPED
                                                          : AUDIO_PLAYER_TRACK_RESTART;
            break;
        }

        audio_element_state_t state = audio_element_get_state(decoder);
        if (state == AEL_STATE_FINISHED) {
            result = ctx.pcm_started ? AUDIO_PLAYER_TRACK_FINISHED : AUDIO_PLAYER_TRACK_ERROR;
            if (!ctx.pcm_started) {
                snprintf(msg, sizeof(msg), TRS("%s 未产生 PCM"), format_name);
                audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
            }
            break;
        }
        if (state == AEL_STATE_ERROR) {
            snprintf(msg, sizeof(msg), TRS("%s 解码失败"), format_name);
            audio_player_publish(AUDIO_PLAYER_STATE_ERROR, msg);
            result = AUDIO_PLAYER_TRACK_ERROR;
            break;
        }
        if (state == AEL_STATE_STOPPED) {
            result = ctx.abort_requested ? AUDIO_PLAYER_TRACK_STOPPED : AUDIO_PLAYER_TRACK_FINISHED;
            break;
        }
    }

cleanup:
    ctx.abort_requested = true;
    if (decoder) {
        (void)audio_element_stop(decoder);
        (void)audio_element_wait_for_stop_ms(decoder, pdMS_TO_TICKS(2000));
        audio_element_deinit(decoder);
    }
    audio_player_prefetch_stop(&ctx.prefetch);
    fclose(ctx.fp);
    free(ctx.file_buf);
    return result;
}

#if AUDIO_PLAYER_ADF_MP3_AVAILABLE
static audio_element_handle_t audio_player_init_mp3_decoder(void)
{
    mp3_decoder_cfg_t cfg = DEFAULT_MP3_DECODER_CONFIG();
    cfg.task_prio = AUDIO_PLAYER_TASK_PRIORITY;
    cfg.out_rb_size = AUDIO_PLAYER_MP3_RB_BYTES;
    return mp3_decoder_init(&cfg);
}
#endif

#if AUDIO_PLAYER_ADF_FLAC_AVAILABLE
static audio_element_handle_t audio_player_init_flac_decoder(void)
{
    flac_decoder_cfg_t cfg = DEFAULT_FLAC_DECODER_CONFIG();
    cfg.task_prio = AUDIO_PLAYER_TASK_PRIORITY;
    cfg.out_rb_size = AUDIO_PLAYER_FLAC_RB_BYTES;
    return flac_decoder_init(&cfg);
}
#endif
#endif

static audio_player_track_result_t audio_player_play_mp3_adf(const char *path)
{
#if AUDIO_PLAYER_ADF_MP3_AVAILABLE
    return audio_player_play_adf_decoder(path, "MP3", audio_player_init_mp3_decoder);
#else
    ESP_LOGW(TAG,
             "MP3 requires ESP-ADF decoder support in this build; no PCM was produced, so audio_sink will not open/write: %s",
             path);
    audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("MP3 需要 ESP-ADF 解码器"));
    return AUDIO_PLAYER_TRACK_ERROR;
#endif
}

static audio_player_track_result_t audio_player_play_flac_adf(const char *path)
{
#if AUDIO_PLAYER_ADF_FLAC_AVAILABLE
    return audio_player_play_adf_decoder(path, "FLAC", audio_player_init_flac_decoder);
#else
    ESP_LOGW(TAG,
             "FLAC requires ESP-ADF decoder support in this build; no PCM was produced, so audio_sink will not open/write: %s",
             path);
    audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("FLAC 需要 ESP-ADF 解码器"));
    return AUDIO_PLAYER_TRACK_ERROR;
#endif
}

static audio_player_track_result_t audio_player_play_raw_pcm(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("无法打开音频文件"));
        return AUDIO_PLAYER_TRACK_ERROR;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return AUDIO_PLAYER_TRACK_ERROR;
    }
    long file_size = ftell(fp);
    if (file_size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return AUDIO_PLAYER_TRACK_ERROR;
    }

    audio_codec_sink_format_t format = {
        .sample_rate = AUDIO_PLAYER_PCM_RATE,
        .bits_per_sample = AUDIO_PLAYER_PCM_BITS,
        .channel_count = AUDIO_PLAYER_PCM_CHANNELS,
    };
    audio_player_update_current_duration(audio_player_pcm_elapsed_ms((uint64_t)file_size,
                                                                     AUDIO_PLAYER_PCM_RATE,
                                                                     AUDIO_PLAYER_PCM_BITS,
                                                                     AUDIO_PLAYER_PCM_CHANNELS));
    audio_player_track_result_t result = audio_player_stream_pcm(fp, (uint32_t)file_size, &format);
    fclose(fp);
    return result;
}

static audio_player_track_result_t audio_player_play_current_once(void)
{
    audio_library_track_t track;

    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    if (!s_player.tracks || s_player.track_count == 0 || s_player.current_index >= s_player.track_count) {
        xSemaphoreGive(s_player.lock);
        audio_player_publish(AUDIO_PLAYER_STATE_IDLE, TRS("没有可播放的音频"));
        return AUDIO_PLAYER_TRACK_STOPPED;
    }
    track = s_player.tracks[s_player.current_index];
    xSemaphoreGive(s_player.lock);

    audio_player_publish(AUDIO_PLAYER_STATE_PREPARING, TRS("正在准备音频"));
    ESP_LOGI(TAG, "play %u/%u: %s",
             (unsigned)(s_player.current_index + 1),
             (unsigned)s_player.track_count,
             track.path);

    if (strcasecmp(track.format, "WAV") == 0) {
        return audio_player_play_wav(track.path);
    }
    if (strcasecmp(track.format, "MP3") == 0) {
        return audio_player_play_mp3_adf(track.path);
    }
    if (strcasecmp(track.format, "FLAC") == 0) {
        return audio_player_play_flac_adf(track.path);
    }
    if (strcasecmp(track.format, "PCM") == 0) {
        return audio_player_play_raw_pcm(track.path);
    }

    ESP_LOGW(TAG,
             "%s requires a decoder in this build; no PCM was produced, so audio_sink will not open/write: %s",
             track.format[0] ? track.format : "audio format",
             track.path);
    audio_player_publish(AUDIO_PLAYER_STATE_ERROR, TRS("该格式需要解码器，未产生 PCM"));
    return AUDIO_PLAYER_TRACK_ERROR;
}

static void audio_player_play_loop(void)
{
    while (true) {
        audio_player_track_result_t result = audio_player_play_current_once();

        if (result == AUDIO_PLAYER_TRACK_RESTART) {
            continue;
        }
        if (result == AUDIO_PLAYER_TRACK_STOPPED || result == AUDIO_PLAYER_TRACK_ERROR) {
            (void)audio_codec_sink_close();
            return;
        }

        xSemaphoreTake(s_player.lock, portMAX_DELAY);
        bool has_next = audio_player_select_finished_track_locked();
        xSemaphoreGive(s_player.lock);

        if (!has_next) {
            (void)audio_codec_sink_close();
            audio_player_publish(AUDIO_PLAYER_STATE_STOPPED, TRS("播放完成"));
            return;
        }
    }
}

static void audio_player_task(void *arg)
{
    (void)arg;
    while (true) {
        audio_player_cmd_t cmd;
        if (xQueueReceive(s_player.queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        audio_player_control_t control = audio_player_handle_command(&cmd);
        if (control == AUDIO_PLAYER_CONTROL_RESTART) {
            audio_player_play_loop();
        } else if (control == AUDIO_PLAYER_CONTROL_STOP) {
            (void)audio_codec_sink_close();
        }
    }
}

static esp_err_t audio_player_start_task_locked(void)
{
    if (s_player.task) {
        return ESP_OK;
    }
    if (xTaskCreate(audio_player_task,
                    "audio_player",
                    AUDIO_PLAYER_TASK_STACK,
                    NULL,
                    AUDIO_PLAYER_TASK_PRIORITY,
                    &s_player.task) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_player_service_init(void)
{
    if (s_player.initialized) {
        return ESP_OK;
    }
    if (!s_player.lock) {
        s_player.lock = xSemaphoreCreateMutex();
        if (!s_player.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_player.lock, portMAX_DELAY);
    esp_err_t ret = audio_player_alloc_tracks_locked();
    if (ret == ESP_OK && !s_player.queue) {
        s_player.queue = xQueueCreate(AUDIO_PLAYER_QUEUE_LEN, sizeof(audio_player_cmd_t));
        if (!s_player.queue) {
            ret = ESP_ERR_NO_MEM;
        }
    }
    if (ret == ESP_OK) {
        uint8_t saved_volume = (uint8_t)app_settings_get_audio_volume();
        s_player.status.state = AUDIO_PLAYER_STATE_IDLE;
        s_player.status.play_mode = AUDIO_PLAYER_PLAY_MODE_LIST_LOOP;
        s_player.status.volume = saved_volume > 100 ? AUDIO_PLAYER_DEFAULT_VOLUME : saved_volume;
        s_player.status.muted = false;
        (void)audio_codec_sink_set_volume(s_player.status.volume);
        strlcpy(s_player.status.message, TRS("音频服务已就绪"), sizeof(s_player.status.message));
        ret = audio_player_start_task_locked();
    }
    if (ret == ESP_OK) {
        s_player.initialized = true;
    }
    xSemaphoreGive(s_player.lock);
    return ret;
}

#include "audio_player_api.inc"
