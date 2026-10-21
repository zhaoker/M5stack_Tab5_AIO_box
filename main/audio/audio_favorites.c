#include "audio/audio_favorites.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "storage/tf_card.h"

/* 收藏夹文件统一收进 TF 卡根目录 .sys，避免系统文件散落在用户可见根目录。 */
#define AUDIO_FAVORITES_PATH TF_CARD_SYS_DIR "/tab5_audio_favorites.txt"
/* 旧版本曾写在 TF 卡根目录，保留迁移路径，首次访问时会自动搬进 .sys 并删除旧文件。 */
#define AUDIO_FAVORITES_LEGACY_PATH TF_CARD_MOUNT_POINT "/.tab5_audio_favorites.txt"
#define AUDIO_FAVORITES_TMP_PATH TF_CARD_SYS_DIR "/tab5_audio_favorites.tmp"

static const char *TAG = "audio_favorites";

static esp_err_t audio_favorites_prepare_storage(void)
{
    FILE *legacy;
    FILE *current;
    bool current_exists = false;

    esp_err_t ret = tf_card_ensure_sys_dir();
    if (ret != ESP_OK) {
        return ret;
    }

    current = fopen(AUDIO_FAVORITES_PATH, "rb");
    if (current) {
        current_exists = true;
        fclose(current);
    }

    legacy = fopen(AUDIO_FAVORITES_LEGACY_PATH, "rb");
    if (!legacy) {
        return ESP_OK;
    }
    fclose(legacy);

    if (!current_exists && rename(AUDIO_FAVORITES_LEGACY_PATH, AUDIO_FAVORITES_PATH) == 0) {
        ESP_LOGI(TAG, "favorites migrated: %s -> %s", AUDIO_FAVORITES_LEGACY_PATH, AUDIO_FAVORITES_PATH);
        return ESP_OK;
    }

    legacy = fopen(AUDIO_FAVORITES_LEGACY_PATH, "rb");
    current = fopen(AUDIO_FAVORITES_PATH, current_exists ? "ab" : "wb");
    if (!legacy || !current) {
        if (legacy) {
            fclose(legacy);
        }
        if (current) {
            fclose(current);
        }
        ESP_LOGW(TAG,
                 "favorites migrate failed: legacy=%s current=%s errno=%d",
                 AUDIO_FAVORITES_LEGACY_PATH,
                 AUDIO_FAVORITES_PATH,
                 errno);
        return ESP_FAIL;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), legacy)) > 0) {
        if (fwrite(buf, 1, n, current) != n) {
            fclose(legacy);
            fclose(current);
            return ESP_FAIL;
        }
    }
    fclose(legacy);
    fclose(current);
    remove(AUDIO_FAVORITES_LEGACY_PATH);
    ESP_LOGI(TAG, "favorites legacy file merged into %s", AUDIO_FAVORITES_PATH);
    return ESP_OK;
}

static void audio_favorites_trim_line(char *line)
{
    size_t len;

    if (!line) {
        return;
    }
    len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }
}

static bool audio_favorites_contains_path(const audio_library_track_t *tracks,
                                          size_t count,
                                          const char *path)
{
    if (!tracks || !path) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(tracks[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t audio_favorites_rewrite(const audio_library_track_t *tracks, size_t count)
{
    (void)audio_favorites_prepare_storage();

    FILE *fp = fopen(AUDIO_FAVORITES_PATH, "wb");
    if (!fp) {
        ESP_LOGW(TAG, "rewrite failed: %s errno=%d", AUDIO_FAVORITES_PATH, errno);
        return ESP_FAIL;
    }
    for (size_t i = 0; tracks && i < count; i++) {
        fprintf(fp, "%s\n", tracks[i].path);
    }
    fclose(fp);
    return ESP_OK;
}

esp_err_t audio_favorites_add(const char *path)
{
    audio_library_track_t track;
    char line[AUDIO_LIBRARY_PATH_MAX + 8];

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_library_track_from_path(path, &track) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (audio_favorites_prepare_storage() != ESP_OK) {
        return ESP_FAIL;
    }

    FILE *fp = fopen(AUDIO_FAVORITES_PATH, "rb");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            audio_favorites_trim_line(line);
            if (strcmp(line, track.path) == 0) {
                fclose(fp);
                return ESP_OK;
            }
        }
        fclose(fp);
    }

    fp = fopen(AUDIO_FAVORITES_PATH, "ab");
    if (!fp) {
        ESP_LOGW(TAG, "append failed: %s errno=%d", AUDIO_FAVORITES_PATH, errno);
        return ESP_FAIL;
    }
    fprintf(fp, "%s\n", track.path);
    fclose(fp);
    return ESP_OK;
}

esp_err_t audio_favorites_remove(const char *path)
{
    FILE *fp;
    FILE *tmp;
    audio_library_track_t track;
    char line[AUDIO_LIBRARY_PATH_MAX + 8];
    const char *target = path;
    bool removed = false;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_library_track_from_path(path, &track) == ESP_OK) {
        target = track.path;
    }

    if (audio_favorites_prepare_storage() != ESP_OK) {
        return ESP_FAIL;
    }
    fp = fopen(AUDIO_FAVORITES_PATH, "rb");
    if (!fp) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    tmp = fopen(AUDIO_FAVORITES_TMP_PATH, "wb");
    if (!tmp) {
        (void)tf_card_ensure_sys_dir();
        tmp = fopen(AUDIO_FAVORITES_TMP_PATH, "wb");
    }
    if (!tmp) {
        fclose(fp);
        ESP_LOGW(TAG, "open tmp failed: %s errno=%d", AUDIO_FAVORITES_TMP_PATH, errno);
        return ESP_FAIL;
    }

    while (fgets(line, sizeof(line), fp)) {
        audio_library_track_t keep_track;

        audio_favorites_trim_line(line);
        if (!line[0]) {
            continue;
        }
        if (strcmp(line, target) == 0) {
            removed = true;
            continue;
        }
        if (audio_library_track_from_path(line, &keep_track) == ESP_OK) {
            fprintf(tmp, "%s\n", keep_track.path);
        }
    }
    fclose(fp);
    fclose(tmp);

    remove(AUDIO_FAVORITES_PATH);
    if (rename(AUDIO_FAVORITES_TMP_PATH, AUDIO_FAVORITES_PATH) != 0) {
        ESP_LOGW(TAG, "rename tmp failed: %s errno=%d", AUDIO_FAVORITES_TMP_PATH, errno);
        remove(AUDIO_FAVORITES_TMP_PATH);
        return ESP_FAIL;
    }
    return removed ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool audio_favorites_contains(const char *path)
{
    FILE *fp;
    audio_library_track_t track;
    char line[AUDIO_LIBRARY_PATH_MAX + 8];
    const char *target = path;
    bool found = false;

    if (!path || !path[0]) {
        return false;
    }
    if (audio_library_track_from_path(path, &track) == ESP_OK) {
        target = track.path;
    }

    if (audio_favorites_prepare_storage() != ESP_OK) {
        return false;
    }
    fp = fopen(AUDIO_FAVORITES_PATH, "rb");
    if (!fp) {
        return false;
    }
    while (fgets(line, sizeof(line), fp)) {
        audio_favorites_trim_line(line);
        if (strcmp(line, target) == 0) {
            found = true;
            break;
        }
    }
    fclose(fp);
    return found;
}

esp_err_t audio_favorites_load(audio_library_track_t *tracks,
                               size_t max_tracks,
                               size_t *out_count,
                               size_t *out_removed_count)
{
    FILE *fp;
    char line[AUDIO_LIBRARY_PATH_MAX + 8];
    size_t count = 0;
    size_t removed = 0;

    if (out_count) {
        *out_count = 0;
    }
    if (out_removed_count) {
        *out_removed_count = 0;
    }
    if (!tracks || max_tracks == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (audio_favorites_prepare_storage() != ESP_OK) {
        return ESP_FAIL;
    }
    fp = fopen(AUDIO_FAVORITES_PATH, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "open failed: %s errno=%d", AUDIO_FAVORITES_PATH, errno);
        return ESP_FAIL;
    }

    while (fgets(line, sizeof(line), fp)) {
        audio_library_track_t track;

        audio_favorites_trim_line(line);
        if (!line[0]) {
            continue;
        }
        if (count >= max_tracks) {
            removed++;
            continue;
        }
        if (audio_library_track_from_path(line, &track) != ESP_OK ||
            audio_favorites_contains_path(tracks, count, track.path)) {
            removed++;
            continue;
        }
        tracks[count++] = track;
    }
    fclose(fp);

    if (removed > 0) {
        (void)audio_favorites_rewrite(tracks, count);
    }
    *out_count = count;
    if (out_removed_count) {
        *out_removed_count = removed;
    }
    return ESP_OK;
}
