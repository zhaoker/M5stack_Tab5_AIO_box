#include "audio/audio_library.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

#include "audio/audio_duration_cache.h"
#include "esp_log.h"

static const char *TAG = "audio_library";

typedef struct {
    const char *ext;
    const char *format;
} audio_ext_map_t;

static const audio_ext_map_t s_audio_exts[] = {
    {"mp3", "MP3"},
    {"wav", "WAV"},
    {"flac", "FLAC"},
    {"aac", "AAC"},
    {"m4a", "AAC"},
    {"ogg", "OGG"},
    {"opus", "OPUS"},
    {"amr", "AMR"},
    {"pcm", "PCM"},
};

static const char *audio_library_ext(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return (dot && dot != path) ? dot + 1 : NULL;
}

const char *audio_library_format_for_path(const char *path)
{
    const char *ext = audio_library_ext(path);
    if (!ext) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_audio_exts) / sizeof(s_audio_exts[0]); i++) {
        if (strcasecmp(ext, s_audio_exts[i].ext) == 0) {
            return s_audio_exts[i].format;
        }
    }
    return NULL;
}

bool audio_library_is_supported(const char *path)
{
    return audio_library_format_for_path(path) != NULL;
}

static bool audio_join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    int n;
    if (!out || out_len == 0 || !dir || !name || !dir[0] || !name[0]) {
        return false;
    }
    n = snprintf(out, out_len, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < out_len;
}

static void audio_split_path(const char *path, char *dir, size_t dir_len, char *name, size_t name_len)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
    if (dir && dir_len > 0) {
        dir[0] = '\0';
    }
    if (name && name_len > 0) {
        name[0] = '\0';
    }
    if (!path || !path[0]) {
        return;
    }
    if (!slash) {
        if (dir && dir_len > 0) {
            strlcpy(dir, ".", dir_len);
        }
        if (name && name_len > 0) {
            strlcpy(name, path, name_len);
        }
        return;
    }
    if (dir && dir_len > 0) {
        size_t len = (size_t)(slash - path);
        if (len == 0) {
            len = 1;
        }
        if (len >= dir_len) {
            len = dir_len - 1;
        }
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
    if (name && name_len > 0) {
        strlcpy(name, slash + 1, name_len);
    }
}

static void audio_title_from_name(const char *name, char *title, size_t title_len, char *artist, size_t artist_len)
{
    char tmp[AUDIO_LIBRARY_NAME_MAX];
    char *dot;
    char *sep;

    if (title && title_len > 0) {
        title[0] = '\0';
    }
    if (artist && artist_len > 0) {
        artist[0] = '\0';
    }
    if (!name || !name[0]) {
        return;
    }

    strlcpy(tmp, name, sizeof(tmp));
    dot = strrchr(tmp, '.');
    if (dot) {
        *dot = '\0';
    }
    sep = strstr(tmp, " - ");
    if (sep) {
        *sep = '\0';
        if (artist && artist_len > 0) {
            strlcpy(artist, tmp, artist_len);
        }
        if (title && title_len > 0) {
            strlcpy(title, sep + 3, title_len);
        }
    } else {
        if (title && title_len > 0) {
            strlcpy(title, tmp, title_len);
        }
    }
}

esp_err_t audio_library_track_from_path(const char *path, audio_library_track_t *out_track)
{
    struct stat st;
    const char *format;

    if (!path || !path[0] || !out_track) {
        return ESP_ERR_INVALID_ARG;
    }
    format = audio_library_format_for_path(path);
    if (!format) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(out_track, 0, sizeof(*out_track));
    strlcpy(out_track->path, path, sizeof(out_track->path));
    audio_split_path(path, out_track->dir, sizeof(out_track->dir),
                     out_track->name, sizeof(out_track->name));
    audio_title_from_name(out_track->name, out_track->title, sizeof(out_track->title),
                          out_track->artist, sizeof(out_track->artist));
    strlcpy(out_track->format, format, sizeof(out_track->format));
    out_track->size = (uint64_t)st.st_size;
    out_track->mtime = (uint64_t)st.st_mtime;
    (void)audio_duration_cache_get(path,
                                    out_track->size,
                                    out_track->mtime,
                                    &out_track->duration_ms);
    return ESP_OK;
}

static int audio_track_compare(const void *lhs, const void *rhs)
{
    const audio_library_track_t *a = (const audio_library_track_t *)lhs;
    const audio_library_track_t *b = (const audio_library_track_t *)rhs;
    return strcasecmp(a->path, b->path);
}

static esp_err_t audio_scan_dir_internal(const char *dir_path,
                                         audio_library_track_t *tracks,
                                         size_t max_tracks,
                                         size_t *count,
                                         bool recursive,
                                         int depth)
{
    DIR *dir;
    if (!dir_path || !tracks || !count || *count >= max_tracks || depth > 10) {
        return ESP_OK;
    }
    dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "opendir failed: %s", dir_path);
        return ESP_FAIL;
    }

    while (*count < max_tracks) {
        struct dirent *ent = readdir(dir);
        char full[AUDIO_LIBRARY_PATH_MAX];
        struct stat st;
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || ent->d_name[0] == '.') {
            continue;
        }
        if (!audio_join_path(full, sizeof(full), dir_path, ent->d_name) || stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (recursive) {
                (void)audio_scan_dir_internal(full, tracks, max_tracks, count, true, depth + 1);
            }
            continue;
        }
        if (audio_library_track_from_path(full, &tracks[*count]) == ESP_OK) {
            (*count)++;
        }
    }
    closedir(dir);
    return ESP_OK;
}

esp_err_t audio_library_scan_dir(const char *dir_path,
                                 audio_library_track_t *tracks,
                                 size_t max_tracks,
                                 size_t *out_count)
{
    size_t count = 0;
    esp_err_t ret;
    if (!out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (!dir_path || !tracks || max_tracks == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ret = audio_scan_dir_internal(dir_path, tracks, max_tracks, &count, false, 0);
    qsort(tracks, count, sizeof(tracks[0]), audio_track_compare);
    *out_count = count;
    return ret;
}

esp_err_t audio_library_scan_recursive(const char *root_path,
                                       audio_library_track_t *tracks,
                                       size_t max_tracks,
                                       size_t *out_count)
{
    size_t count = 0;
    esp_err_t ret;
    if (!out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (!root_path || !tracks || max_tracks == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ret = audio_scan_dir_internal(root_path, tracks, max_tracks, &count, true, 0);
    qsort(tracks, count, sizeof(tracks[0]), audio_track_compare);
    *out_count = count;
    return ret;
}

esp_err_t audio_library_scan_dir_for_file(const char *file_path,
                                          audio_library_track_t *tracks,
                                          size_t max_tracks,
                                          size_t *out_count,
                                          size_t *out_index)
{
    char dir[AUDIO_LIBRARY_PATH_MAX];
    char name[AUDIO_LIBRARY_NAME_MAX];
    esp_err_t ret;
    size_t count = 0;

    if (out_index) {
        *out_index = 0;
    }
    audio_split_path(file_path, dir, sizeof(dir), name, sizeof(name));
    ret = audio_library_scan_dir(dir, tracks, max_tracks, &count);
    if (out_count) {
        *out_count = count;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(tracks[i].path, file_path) == 0) {
            if (out_index) {
                *out_index = i;
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
