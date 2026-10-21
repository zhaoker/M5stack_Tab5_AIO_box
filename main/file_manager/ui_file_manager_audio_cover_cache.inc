enum {
    /*
     * 音乐封面解码缓存数量。每张封面会保存一份 RGB565 图片到 PSRAM，
     * 切出音乐 UI 后再切回来可以直接复用，避免重新读取 MP3/FLAC 封面导致播放卡顿。
     */
    FM_AUDIO_COVER_CACHE_MAX = 4,
    /* 封面缓存总量上限，超过后按最久未使用淘汰，避免图片过大时长期占用过多 PSRAM。 */
    FM_AUDIO_COVER_CACHE_MAX_BYTES = 4 * 1024 * 1024,
};

typedef struct {
    bool valid;
    uint32_t age;
    char track_path[AUDIO_LIBRARY_PATH_MAX];
    char cover_path[AUDIO_LIBRARY_PATH_MAX];
    char lv_path[AUDIO_LIBRARY_PATH_MAX + 4];
    file_manager_image_t image;
} fm_audio_cover_cache_entry_t;

static fm_audio_cover_cache_entry_t s_audio_cover_cache[FM_AUDIO_COVER_CACHE_MAX];
static uint32_t s_audio_cover_cache_clock;
static size_t s_audio_cover_cache_bytes;

static void fm_audio_cover_cache_release_entry(fm_audio_cover_cache_entry_t *entry)
{
    size_t bytes;

    if (!entry || !entry->valid) {
        return;
    }
    bytes = entry->image.dsc.data ? entry->image.dsc.data_size : 0;
    if (bytes > 0) {
        s_audio_cover_cache_bytes = bytes <= s_audio_cover_cache_bytes
                                    ? s_audio_cover_cache_bytes - bytes
                                    : 0;
    }
    if (entry->image.dsc.data) {
        lv_image_cache_drop(&entry->image.dsc);
        file_manager_media_release_image(&entry->image);
    }
    if (entry->lv_path[0]) {
        lv_image_cache_drop(entry->lv_path);
    }
    memset(entry, 0, sizeof(*entry));
}

static void fm_audio_cover_cache_trim_for(size_t add_bytes)
{
    while (add_bytes > 0 &&
           s_audio_cover_cache_bytes + add_bytes > FM_AUDIO_COVER_CACHE_MAX_BYTES) {
        fm_audio_cover_cache_entry_t *victim = NULL;

        for (size_t i = 0; i < FM_AUDIO_COVER_CACHE_MAX; i++) {
            if (!s_audio_cover_cache[i].valid) {
                continue;
            }
            if (!victim || s_audio_cover_cache[i].age < victim->age) {
                victim = &s_audio_cover_cache[i];
            }
        }
        if (!victim) {
            break;
        }
        fm_audio_cover_cache_release_entry(victim);
    }
}

static fm_audio_cover_cache_entry_t *fm_audio_cover_cache_find(const char *track_path)
{
    if (!track_path || !track_path[0]) {
        return NULL;
    }
    for (size_t i = 0; i < FM_AUDIO_COVER_CACHE_MAX; i++) {
        if (s_audio_cover_cache[i].valid &&
            strcmp(s_audio_cover_cache[i].track_path, track_path) == 0) {
            s_audio_cover_cache[i].age = ++s_audio_cover_cache_clock;
            return &s_audio_cover_cache[i];
        }
    }
    return NULL;
}

static fm_audio_cover_cache_entry_t *fm_audio_cover_cache_pick_slot(void)
{
    size_t oldest = 0;

    for (size_t i = 0; i < FM_AUDIO_COVER_CACHE_MAX; i++) {
        if (!s_audio_cover_cache[i].valid) {
            return &s_audio_cover_cache[i];
        }
        if (s_audio_cover_cache[i].age < s_audio_cover_cache[oldest].age) {
            oldest = i;
        }
    }
    return &s_audio_cover_cache[oldest];
}

static esp_err_t fm_audio_cover_cache_load(const char *track_path,
                                           fm_audio_cover_cache_entry_t **out_entry)
{
    fm_audio_cover_cache_entry_t *entry;
    file_manager_image_t image;
    char cover_path[AUDIO_LIBRARY_PATH_MAX];
    char lv_path[AUDIO_LIBRARY_PATH_MAX + 4];
    char status[96];
    esp_err_t ret;

    if (out_entry) {
        *out_entry = NULL;
    }
    if (!track_path || !track_path[0] || !out_entry) {
        return ESP_ERR_INVALID_ARG;
    }
    entry = fm_audio_cover_cache_find(track_path);
    if (entry) {
        *out_entry = entry;
        return ESP_OK;
    }

    cover_path[0] = '\0';
    ret = audio_cover_extract_to_file(track_path, cover_path, sizeof(cover_path));
    if (ret != ESP_OK) {
        return ret;
    }

    memset(&image, 0, sizeof(image));
    ret = file_manager_media_load_image(cover_path, &image, status, sizeof(status));
    if (ret == ESP_OK) {
        fm_audio_cover_cache_trim_for(image.dsc.data ? image.dsc.data_size : 0);
        entry = fm_audio_cover_cache_pick_slot();
        fm_audio_cover_cache_release_entry(entry);
        entry->valid = true;
        entry->age = ++s_audio_cover_cache_clock;
        strlcpy(entry->track_path, track_path, sizeof(entry->track_path));
        strlcpy(entry->cover_path, cover_path, sizeof(entry->cover_path));
        entry->lv_path[0] = '\0';
        entry->image = image;
        s_audio_cover_cache_bytes += image.dsc.data ? image.dsc.data_size : 0;
        *out_entry = entry;
        return ESP_OK;
    }

    if (ret == ESP_ERR_NOT_SUPPORTED &&
        snprintf(lv_path, sizeof(lv_path), "A:%s", cover_path) > 0 &&
        lv_image_decoder_get_info(lv_path, &(lv_image_header_t){0}) == LV_RESULT_OK) {
        entry = fm_audio_cover_cache_pick_slot();
        fm_audio_cover_cache_release_entry(entry);
        entry->valid = true;
        entry->age = ++s_audio_cover_cache_clock;
        strlcpy(entry->track_path, track_path, sizeof(entry->track_path));
        strlcpy(entry->cover_path, cover_path, sizeof(entry->cover_path));
        strlcpy(entry->lv_path, lv_path, sizeof(entry->lv_path));
        memset(&entry->image, 0, sizeof(entry->image));
        *out_entry = entry;
        return ESP_OK;
    }

    return ret;
}
