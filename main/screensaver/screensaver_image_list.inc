static bool ss_has_image_ext(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    if (!dot || !dot[1]) {
        return false;
    }
    return strcasecmp(dot + 1, "jpg") == 0 ||
           strcasecmp(dot + 1, "jpeg") == 0 ||
           strcasecmp(dot + 1, "bmp") == 0;
}

static bool ss_join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    size_t dir_len;

    if (!out || out_len == 0 || !dir || !name || !name[0]) {
        return false;
    }
    dir_len = strlen(dir);
    if (dir_len == 0) {
        return false;
    }
    if (dir_len > 1 && dir[dir_len - 1] == '/') {
        return snprintf(out, out_len, "%s%s", dir, name) > 0 && strlen(out) < out_len;
    }
    return snprintf(out, out_len, "%s/%s", dir, name) > 0 && strlen(out) < out_len;
}

static void ss_add_image_path(const char *path)
{
    if (!path || !path[0] || s_image_count >= SS_IMAGE_MAX) {
        return;
    }
    strlcpy(s_images[s_image_count].path, path, sizeof(s_images[s_image_count].path));
    s_image_count++;
}

static void ss_scan_dir(const char *dir, int depth)
{
    DIR *dp;
    struct dirent *entry;

    if (!dir || depth > SS_SCAN_DEPTH_MAX || s_image_count >= SS_IMAGE_MAX) {
        return;
    }
    dp = opendir(dir);
    if (!dp) {
        ESP_LOGW(TAG, "open wallpaper dir failed: %s errno=%d", dir, errno);
        return;
    }
    while ((entry = readdir(dp)) != NULL && s_image_count < SS_IMAGE_MAX) {
        char path[SS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!ss_join_path(path, sizeof(path), dir, entry->d_name)) {
            continue;
        }
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            ss_scan_dir(path, depth + 1);
        } else if (S_ISREG(st.st_mode) && ss_has_image_ext(path)) {
            ss_add_image_path(path);
        }
    }
    closedir(dp);
}

static void ss_collect_images(void)
{
    s_image_count = 0;
    s_image_pos = 0;
    if (!screensaver_ensure_image_list()) {
        return;
    }
    ss_scan_dir(app_settings_get_screensaver_wallpaper_dir(), 0);
    if (s_image_count > 1 &&
        app_settings_get_screensaver_order() == APP_SETTINGS_SCREENSAVER_ORDER_RANDOM) {
        s_image_pos = esp_random() % s_image_count;
    }
    ESP_LOGI(TAG,
             "screensaver image scan: dir=%s count=%u pos=%u",
             app_settings_get_screensaver_wallpaper_dir(),
             (unsigned)s_image_count,
             (unsigned)s_image_pos);
}

static void ss_select_next_image(void)
{
    if (s_image_count <= 1) {
        return;
    }
    if (app_settings_get_screensaver_order() == APP_SETTINGS_SCREENSAVER_ORDER_RANDOM) {
        size_t next = esp_random() % s_image_count;
        if (next == s_image_pos) {
            next = (next + 1) % s_image_count;
        }
        s_image_pos = next;
    } else {
        s_image_pos = (s_image_pos + 1) % s_image_count;
    }
}

static const char *ss_file_basename(const char *path)
{
    const char *slash;
    if (!path) {
        return "";
    }
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void ss_title_for_image(const char *path, char *out, size_t out_len)
{
    file_manager_exif_info_t info;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (app_settings_get_screensaver_component_visible(APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE) &&
        file_manager_exif_read(path, &info) == ESP_OK &&
        info.title[0]) {
        strlcpy(out, info.title, out_len);
        return;
    }
    strlcpy(out, ss_file_basename(path), out_len);
}
