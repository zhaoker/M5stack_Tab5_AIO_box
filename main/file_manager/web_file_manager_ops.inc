static bool webfm_append_json(char **json, size_t *len, size_t *cap, const char *text)
{
    size_t add;
    size_t need;
    size_t next_cap;
    char *next;

    if (!json || !len || !cap || !text) {
        return false;
    }
    add = strlen(text);
    if (add > SIZE_MAX - *len - 1) {
        return false;
    }
    need = *len + add + 1;
    if (need > *cap) {
        next_cap = *cap ? *cap : 512;
        while (next_cap < need) {
            if (next_cap > SIZE_MAX / 2) {
                return false;
            }
            next_cap *= 2;
        }
        next = realloc(*json, next_cap);
        if (!next) {
            return false;
        }
        *json = next;
        *cap = next_cap;
    }
    memcpy(*json + *len, text, add);
    *len += add;
    (*json)[*len] = '\0';
    return true;
}

static esp_err_t webfm_list_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char *json = NULL;
    size_t json_len = 0;
    size_t json_cap = 0;
    DIR *dir;
    bool first = true;
    size_t item_count = 0;
    bool usb_locked = false;
    bool usb_path = false;
    bool read_failed = false;
    int read_errno = 0;
    int64_t start_us = 0;
    esp_err_t ret = ESP_OK;

    (void)webfm_query_decoded(req, "dir", rel, sizeof(rel));
    if (webfm_rel_is_virtual_root(rel)) {
        char escaped[WEBFM_JSON_NAME_LEN];
        char item[512];

        ESP_LOGI(TAG, "Web list start: virtual root");
        httpd_resp_set_type(req, "application/json; charset=utf-8");
        if (httpd_resp_sendstr_chunk(req, "{\"items\":[") != ESP_OK) {
            return ESP_FAIL;
        }
        webfm_json_escape(webfm_tf_label(), escaped, sizeof(escaped));
        snprintf(item,
                 sizeof(item),
                 "{\"name\":\"%s\",\"dir\":true,\"root\":true,\"size\":0}",
                 escaped);
        if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) {
            return ESP_FAIL;
        }
        webfm_json_escape(webfm_usb_label(), escaped, sizeof(escaped));
        snprintf(item,
                 sizeof(item),
                 ",{\"name\":\"%s\",\"dir\":true,\"root\":true,\"size\":0}",
                 escaped);
        if (httpd_resp_sendstr_chunk(req, item) != ESP_OK ||
            httpd_resp_sendstr_chunk(req, "]}") != ESP_OK) {
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Web list done: virtual root items=2");
        return httpd_resp_sendstr_chunk(req, NULL);
    }
    if (!webfm_build_path(rel, path, sizeof(path))) {
        ESP_LOGW(TAG, "Web list failed: invalid rel='%s'", rel);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }

    usb_path = webfm_path_uses_usb(path);
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Web list start: rel='%s' path='%s' usb=%s", rel, path, usb_path ? "yes" : "no");
    dir = opendir(path);
    if (!dir) {
        ESP_LOGW(TAG, "Web list failed: opendir rel='%s' path='%s' errno=%d", rel, path, errno);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 404, TRS("目录打开失败"));
    }

    if (!webfm_append_json(&json, &json_len, &json_cap, "{\"items\":[")) {
        closedir(dir);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }
    while (true) {
        struct dirent *ent;
        struct stat st;
        char full[WEBFM_MAX_PATH_LEN];
        char escaped[WEBFM_JSON_NAME_LEN];
        char item[512];
        bool is_dir;
        int n;

        errno = 0;
        ent = readdir(dir);
        if (!ent) {
            if (errno != 0) {
                read_failed = true;
                read_errno = errno;
            }
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        errno = 0;
        if (!webfm_snprintf_ok(snprintf(full, sizeof(full), "%s/%s", path, ent->d_name), sizeof(full)) ||
            stat(full, &st) != 0) {
            ESP_LOGW(TAG,
                     "Web list skip: rel='%s' entry='%s' path='%s' errno=%d",
                     rel,
                     ent->d_name,
                     full,
                     errno);
            if (usb_path) {
                read_failed = true;
                read_errno = errno;
                break;
            }
            continue;
        }
        is_dir = S_ISDIR(st.st_mode);
        webfm_json_escape(ent->d_name, escaped, sizeof(escaped));
        n = snprintf(item,
                     sizeof(item),
                     "%s{\"name\":\"%s\",\"dir\":%s,\"root\":false,\"size\":%llu}",
                     first ? "" : ",",
                     escaped,
                     is_dir ? "true" : "false",
                     is_dir ? 0ULL : (unsigned long long)st.st_size);
        if (webfm_snprintf_ok(n, sizeof(item))) {
            if (!webfm_append_json(&json, &json_len, &json_cap, item)) {
                closedir(dir);
                webfm_end_usb_io(usb_locked);
                free(json);
                return webfm_send_error(req, 500, TRS("内存不足"));
            }
            item_count++;
            first = false;
        }
    }
    closedir(dir);
    if (usb_path && !read_failed && item_count == 0) {
        uint64_t total = 0;
        uint64_t free_bytes = 0;
        if (esp_vfs_fat_info(usb_storage_get_mount_point(), &total, &free_bytes) != ESP_OK) {
            read_failed = true;
            read_errno = errno;
        }
    }
    if (read_failed) {
        ESP_LOGW(TAG,
                 "Web list failed: readdir rel='%s' path='%s' items=%u errno=%d",
                 rel,
                 path,
                 (unsigned)item_count,
                 read_errno);
        webfm_end_usb_io(usb_locked);
        free(json);
        return webfm_send_error(req, 500, TRS("目录读取失败"));
    }
    if (!webfm_append_json(&json, &json_len, &json_cap, "]}")) {
        webfm_end_usb_io(usb_locked);
        free(json);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }
    webfm_end_usb_io(usb_locked);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    ret = httpd_resp_send(req, json, json_len);
    free(json);
    ESP_LOGI(TAG,
             "Web list done: rel='%s' items=%u usb=%s elapsed=%lld ms ret=%s",
             rel,
             (unsigned)item_count,
             usb_path ? "yes" : "no",
             (long long)((esp_timer_get_time() - start_us) / 1000),
             esp_err_to_name(ret));
    return ret;
}

static esp_err_t webfm_download_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char inline_q[8] = {0};
    char disposition[192];
    char filename[128];
    char *buf;
    size_t io_buf_size;
    FILE *fp;
    struct stat st;
    bool usb_locked = false;
    bool usb_path;
    const char *content_type;
    uint64_t sent = 0;
    uint32_t chunks = 0;
    int64_t start_us;
    esp_err_t ret = ESP_OK;

    if (!webfm_query_decoded(req, "path", rel, sizeof(rel))) {
        return webfm_send_error(req, 400, TRS("缺少路径"));
    }
    if (!webfm_build_path(rel, path, sizeof(path))) {
        ESP_LOGW(TAG, "Web download rejected: invalid rel='%s'", rel);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    usb_path = webfm_path_uses_usb(path);
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "Web download failed: not a file rel='%s' path='%s' errno=%d", rel, path, errno);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 404, TRS("文件打开失败"));
    }
    fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Web download failed: fopen rel='%s' path='%s' errno=%d", rel, path, errno);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 404, TRS("文件打开失败"));
    }
    if (usb_path) {
        setvbuf(fp, NULL, _IONBF, 0);
    }
    io_buf_size = usb_path ? WEBFM_USB_IO_BUF_SIZE : WEBFM_IO_BUF_SIZE;
    buf = malloc(io_buf_size);
    if (!buf) {
        fclose(fp);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }

    (void)webfm_query_decoded(req, "inline", inline_q, sizeof(inline_q));
    content_type = webfm_content_type_for_path(path);
    httpd_resp_set_type(req, content_type);
    webfm_sanitize_header_filename(webfm_basename(path), filename, sizeof(filename));
    snprintf(disposition,
             sizeof(disposition),
             "%s; filename=\"%s\"",
             inline_q[0] == '1' ? "inline" : "attachment",
             filename[0] ? filename : "download.bin");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Web download start: rel='%s' path='%s' size=%llu inline=%s usb=%s type=%s buf=%u",
             rel,
             path,
             (unsigned long long)st.st_size,
             inline_q[0] == '1' ? "yes" : "no",
             usb_path ? "yes" : "no",
             content_type,
             (unsigned)io_buf_size);

    while (true) {
        size_t got = fread(buf, 1, io_buf_size, fp);
        if (got > 0) {
            ret = httpd_resp_send_chunk(req, buf, got);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG,
                         "Web download send failed: rel='%s' sent=%llu/%llu chunks=%u err=%s",
                         rel,
                         (unsigned long long)sent,
                         (unsigned long long)st.st_size,
                         (unsigned)chunks,
                         esp_err_to_name(ret));
                break;
            }
            sent += got;
            chunks++;
            if (usb_path) {
                vTaskDelay(pdMS_TO_TICKS(WEBFM_USB_READ_DELAY_MS));
            }
        }
        if (got < io_buf_size) {
            if (ferror(fp)) {
                ESP_LOGW(TAG,
                         "Web download read failed: rel='%s' path='%s' sent=%llu/%llu errno=%d",
                         rel,
                         path,
                         (unsigned long long)sent,
                         (unsigned long long)st.st_size,
                         errno);
                ret = ESP_FAIL;
            }
            break;
        }
    }
    free(buf);
    fclose(fp);
    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    webfm_end_usb_io(usb_locked);
    ESP_LOGI(TAG,
             "Web download done: rel='%s' sent=%llu/%llu chunks=%u inline=%s usb=%s elapsed=%lld ms ret=%s",
             rel,
             (unsigned long long)sent,
             (unsigned long long)st.st_size,
             (unsigned)chunks,
             inline_q[0] == '1' ? "yes" : "no",
             usb_path ? "yes" : "no",
             (long long)((esp_timer_get_time() - start_us) / 1000),
             esp_err_to_name(ret));
    return ret;
}

static esp_err_t webfm_mkdir_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char name[128] = {0};
    char parent[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};

    (void)webfm_query_decoded(req, "dir", rel, sizeof(rel));
    if (!webfm_query_decoded(req, "name", name, sizeof(name))) {
        return webfm_send_error(req, 400, TRS("缺少名称"));
    }
    if (!webfm_is_safe_name(name)) {
        return webfm_send_error(req, 400, TRS("名称无效"));
    }
    struct stat st;

    if (!webfm_build_path(rel, parent, sizeof(parent)) ||
        stat(parent, &st) != 0 || !S_ISDIR(st.st_mode) ||
        !webfm_snprintf_ok(snprintf(path, sizeof(path), "%s/%s", parent, name), sizeof(path))) {
        ESP_LOGW(TAG, "Web mkdir rejected: dir='%s' name='%s'", rel, name);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Web mkdir failed: dir='%s' name='%s' path='%s' errno=%d", rel, name, path, errno);
        return webfm_send_error(req, 500, TRS("新建文件夹失败"));
    }
    ESP_LOGI(TAG, "Web mkdir: dir='%s' name='%s' path='%s'", rel, name, path);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t webfm_newfile_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char name[128] = {0};
    char parent[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    struct stat st;
    FILE *fp;

    (void)webfm_query_decoded(req, "dir", rel, sizeof(rel));
    if (!webfm_query_decoded(req, "name", name, sizeof(name))) {
        return webfm_send_error(req, 400, TRS("缺少名称"));
    }
    if (!webfm_is_safe_name(name)) {
        return webfm_send_error(req, 400, TRS("名称无效"));
    }
    if (!webfm_build_path(rel, parent, sizeof(parent)) ||
        stat(parent, &st) != 0 || !S_ISDIR(st.st_mode) ||
        !webfm_snprintf_ok(snprintf(path, sizeof(path), "%s/%s", parent, name), sizeof(path))) {
        ESP_LOGW(TAG, "Web newfile rejected: dir='%s' name='%s'", rel, name);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    if (stat(path, &st) == 0) {
        ESP_LOGW(TAG, "Web newfile rejected: exists dir='%s' name='%s' path='%s'", rel, name, path);
        return webfm_send_error(req, 409, TRS("目标已存在"));
    }
    fp = fopen(path, "wb");
    if (!fp) {
        ESP_LOGW(TAG, "Web newfile failed: dir='%s' name='%s' path='%s' errno=%d", rel, name, path, errno);
        return webfm_send_error(req, 500, TRS("新建文件失败"));
    }
    fclose(fp);
    ESP_LOGI(TAG, "Web newfile: dir='%s' name='%s' path='%s'", rel, name, path);
    return httpd_resp_sendstr(req, "ok");
}

static bool webfm_remove_recursive(const char *path)
{
    struct stat st;
    DIR *dir;

    if (!path || stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0;
    }
    dir = opendir(path);
    if (!dir) {
        return false;
    }
    while (true) {
        struct dirent *ent = readdir(dir);
        char child[WEBFM_MAX_PATH_LEN];
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!webfm_snprintf_ok(snprintf(child, sizeof(child), "%s/%s", path, ent->d_name), sizeof(child)) ||
            !webfm_remove_recursive(child)) {
            closedir(dir);
            return false;
        }
    }
    closedir(dir);
    return rmdir(path) == 0;
}

static bool webfm_copy_file(const char *src, const char *dst)
{
    FILE *in = NULL;
    FILE *out = NULL;
    char *buf = NULL;
    bool ok = false;

    in = fopen(src, "rb");
    if (!in) {
        ESP_LOGW(TAG, "Web copy failed: open source src='%s' errno=%d", src, errno);
        return false;
    }
    out = fopen(dst, "wb");
    if (!out) {
        ESP_LOGW(TAG, "Web copy failed: open dest dst='%s' errno=%d", dst, errno);
        fclose(in);
        return false;
    }
    buf = malloc(WEBFM_IO_BUF_SIZE);
    if (!buf) {
        fclose(out);
        fclose(in);
        unlink(dst);
        return false;
    }

    ok = true;
    while (true) {
        size_t got = fread(buf, 1, WEBFM_IO_BUF_SIZE, in);
        if (got > 0 && fwrite(buf, 1, got, out) != got) {
            ESP_LOGW(TAG, "Web copy failed: write src='%s' dst='%s' errno=%d", src, dst, errno);
            ok = false;
            break;
        }
        if (got > 0) {
            vTaskDelay(pdMS_TO_TICKS(WEBFM_COPY_IO_DELAY_MS));
        }
        if (got < WEBFM_IO_BUF_SIZE) {
            if (ferror(in)) {
                ESP_LOGW(TAG, "Web copy failed: read src='%s' dst='%s' errno=%d", src, dst, errno);
                ok = false;
            }
            break;
        }
    }

    free(buf);
    if (fclose(out) != 0) {
        ESP_LOGW(TAG, "Web copy failed: close dest dst='%s' errno=%d", dst, errno);
        ok = false;
    }
    fclose(in);
    if (!ok) {
        unlink(dst);
    }
    return ok;
}

static bool webfm_copy_recursive(const char *src, const char *dst)
{
    struct stat st;

    if (!src || !dst || stat(src, &st) != 0 || access(dst, F_OK) == 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return webfm_copy_file(src, dst);
    }
    if (webfm_path_is_same_or_child(dst, src) || mkdir(dst, 0775) != 0) {
        return false;
    }

    DIR *dir = opendir(src);
    if (!dir) {
        rmdir(dst);
        return false;
    }
    while (true) {
        struct dirent *ent = readdir(dir);
        char child_src[WEBFM_MAX_PATH_LEN];
        char child_dst[WEBFM_MAX_PATH_LEN];
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!webfm_snprintf_ok(snprintf(child_src, sizeof(child_src), "%s/%s", src, ent->d_name), sizeof(child_src)) ||
            !webfm_snprintf_ok(snprintf(child_dst, sizeof(child_dst), "%s/%s", dst, ent->d_name), sizeof(child_dst)) ||
            !webfm_copy_recursive(child_src, child_dst)) {
            closedir(dir);
            webfm_remove_recursive(dst);
            return false;
        }
    }
    closedir(dir);
    return true;
}

static bool webfm_move_path(const char *src, const char *dst)
{
    if (!src || !dst) {
        return false;
    }
    if (rename(src, dst) == 0) {
        return true;
    }
    if (!webfm_copy_recursive(src, dst)) {
        return false;
    }
    if (!webfm_remove_recursive(src)) {
        int saved_errno = errno;
        (void)webfm_remove_recursive(dst);
        errno = saved_errno;
        return false;
    }
    return true;
}

static bool webfm_make_unique_copy_path(char *out, size_t out_len, const char *dir, const char *name)
{
    char candidate_name[128];
    char stem[128];
    char ext[128];
    const char *dot;
    size_t stem_len;

    if (!out || !dir || !name || !name[0]) {
        return false;
    }
    if (!webfm_snprintf_ok(snprintf(out, out_len, "%s/%s", dir, name), out_len)) {
        return false;
    }
    if (access(out, F_OK) != 0) {
        return true;
    }

    dot = strrchr(name, '.');
    if (dot && dot != name) {
        stem_len = (size_t)(dot - name);
        if (stem_len >= sizeof(stem)) {
            stem_len = sizeof(stem) - 1;
        }
        memcpy(stem, name, stem_len);
        stem[stem_len] = '\0';
        strlcpy(ext, dot, sizeof(ext));
    } else {
        strlcpy(stem, name, sizeof(stem));
        ext[0] = '\0';
    }

    for (unsigned i = 0; i < 100; i++) {
        int n = i == 0
                    ? snprintf(candidate_name, sizeof(candidate_name), TRS("%s 副本%s"), stem, ext)
                    : snprintf(candidate_name, sizeof(candidate_name), TRS("%s 副本 %u%s"), stem, i + 1, ext);
        if (!webfm_snprintf_ok(n, sizeof(candidate_name))) {
            continue;
        }
        if (!webfm_snprintf_ok(snprintf(out, out_len, "%s/%s", dir, candidate_name), out_len)) {
            return false;
        }
        if (access(out, F_OK) != 0) {
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

static esp_err_t webfm_delete_post_handler(httpd_req_t *req)
{
    char *body = webfm_receive_body_alloc(req, WEBFM_FORM_LIMIT_BYTES);
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    esp_err_t ret = ESP_OK;
    bool usb_locked = false;

    if (!body ||
        !webfm_body_value(body, "path", rel, sizeof(rel)) ||
        rel[0] == '\0' ||
        !webfm_build_path(rel, path, sizeof(path)) ||
        webfm_path_is_storage_root(path)) {
        ESP_LOGW(TAG, "Web delete rejected: rel='%s'", rel);
        free(body);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        free(body);
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (!webfm_remove_recursive(path)) {
        ESP_LOGW(TAG, "Web delete failed: rel='%s' path='%s' errno=%d", rel, path, errno);
        ret = webfm_send_error(req, 500, TRS("删除失败"));
    } else {
        ESP_LOGI(TAG, "Web delete: rel='%s' path='%s'", rel, path);
        ret = httpd_resp_sendstr(req, "ok");
    }
    webfm_end_usb_io(usb_locked);
    free(body);
    return ret;
}

static esp_err_t webfm_rmdir_post_handler(httpd_req_t *req)
{
    char *body = webfm_receive_body_alloc(req, WEBFM_FORM_LIMIT_BYTES);
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    esp_err_t ret = ESP_OK;
    bool usb_locked = false;

    if (!body ||
        !webfm_body_value(body, "path", rel, sizeof(rel)) ||
        rel[0] == '\0' ||
        !webfm_build_path(rel, path, sizeof(path)) ||
        webfm_path_is_storage_root(path)) {
        ESP_LOGW(TAG, "Web rmdir rejected: rel='%s'", rel);
        free(body);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        free(body);
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (rmdir(path) != 0) {
        ESP_LOGW(TAG, "Web rmdir failed: rel='%s' path='%s' errno=%d", rel, path, errno);
        ret = webfm_send_error(req, 500, TRS("删除文件夹失败"));
    } else {
        ESP_LOGI(TAG, "Web rmdir: rel='%s' path='%s'", rel, path);
        ret = httpd_resp_sendstr(req, "ok");
    }
    webfm_end_usb_io(usb_locked);
    free(body);
    return ret;
}

static esp_err_t webfm_rename_post_handler(httpd_req_t *req)
{
    char *body = webfm_receive_body_alloc(req, WEBFM_FORM_LIMIT_BYTES);
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char name[128] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char dest[WEBFM_MAX_PATH_LEN] = {0};
    char *slash;
    struct stat st;
    esp_err_t ret = ESP_OK;
    bool usb_locked = false;

    if (!body ||
        !webfm_body_value(body, "path", rel, sizeof(rel)) ||
        !webfm_body_value(body, "name", name, sizeof(name)) ||
        rel[0] == '\0' ||
        !webfm_build_path(rel, path, sizeof(path)) ||
        webfm_path_is_storage_root(path) ||
        !webfm_is_safe_name(name)) {
        ESP_LOGW(TAG, "Web rename rejected: rel='%s' new_name='%s'", rel, name);
        free(body);
        return webfm_send_error(req, 400, TRS("请求无效"));
    }

    strlcpy(dest, path, sizeof(dest));
    slash = strrchr(dest, '/');
    if (!slash) {
        free(body);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    slash[1] = '\0';
    if (strlcat(dest, name, sizeof(dest)) >= sizeof(dest)) {
        free(body);
        return webfm_send_error(req, 400, TRS("目标位置无效"));
    }
    if (!webfm_begin_usb_io_for_paths(path, dest, &usb_locked)) {
        free(body);
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (stat(dest, &st) == 0) {
        ESP_LOGW(TAG, "Web rename rejected: target exists rel='%s' dest='%s'", rel, dest);
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 409, TRS("目标已存在"));
    }
    if (rename(path, dest) != 0) {
        ESP_LOGW(TAG, "Web rename failed: rel='%s' path='%s' dest='%s' errno=%d", rel, path, dest, errno);
        ret = webfm_send_error(req, 500, TRS("重命名失败"));
    } else {
        ESP_LOGI(TAG, "Web rename: rel='%s' path='%s' dest='%s'", rel, path, dest);
        ret = httpd_resp_sendstr(req, "ok");
    }
    webfm_end_usb_io(usb_locked);
    free(body);
    return ret;
}

static esp_err_t webfm_move_post_handler(httpd_req_t *req)
{
    char *body = webfm_receive_body_alloc(req, WEBFM_FORM_LIMIT_BYTES);
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char dest_rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char dest_dir[WEBFM_MAX_PATH_LEN] = {0};
    char dest[WEBFM_MAX_PATH_LEN] = {0};
    const char *base;
    struct stat st;
    esp_err_t ret = ESP_OK;
    bool usb_locked = false;

    if (!body ||
        !webfm_body_value(body, "path", rel, sizeof(rel)) ||
        !webfm_body_value(body, "dest", dest_rel, sizeof(dest_rel)) ||
        rel[0] == '\0' ||
        !webfm_build_path(rel, path, sizeof(path)) ||
        !webfm_build_path(dest_rel, dest_dir, sizeof(dest_dir)) ||
        webfm_path_is_storage_root(path)) {
        ESP_LOGW(TAG, "Web move rejected: rel='%s' dest_dir='%s'", rel, dest_rel);
        free(body);
        return webfm_send_error(req, 400, TRS("请求无效"));
    }
    if (!webfm_begin_usb_io_for_paths(path, dest_dir, &usb_locked)) {
        free(body);
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (stat(path, &st) != 0) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 404, TRS("来源不存在"));
    }
    if (S_ISDIR(st.st_mode) && rel[0] && strncmp(dest_rel, rel, strlen(rel)) == 0 &&
        (dest_rel[strlen(rel)] == '\0' || dest_rel[strlen(rel)] == '/')) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 400, TRS("不能把文件夹移动到自身内部"));
    }
    if (stat(dest_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 404, TRS("目标文件夹不存在"));
    }
    base = webfm_basename(path);
    if (!webfm_snprintf_ok(snprintf(dest, sizeof(dest), "%s/%s", dest_dir, base), sizeof(dest))) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 400, TRS("目标位置无效"));
    }
    if (stat(dest, &st) == 0) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 409, TRS("目标已存在"));
    }
    if (!webfm_move_path(path, dest)) {
        ESP_LOGW(TAG, "Web move failed: rel='%s' dest_dir='%s' path='%s' dest='%s' errno=%d",
                 rel, dest_rel, path, dest, errno);
        ret = webfm_send_error(req, 500, TRS("移动失败"));
    } else {
        ESP_LOGI(TAG, "Web move: rel='%s' dest_dir='%s' path='%s' dest='%s'", rel, dest_rel, path, dest);
        ret = httpd_resp_sendstr(req, "ok");
    }
    webfm_end_usb_io(usb_locked);
    free(body);
    return ret;
}

static esp_err_t webfm_copy_post_handler(httpd_req_t *req)
{
    char *body = webfm_receive_body_alloc(req, WEBFM_FORM_LIMIT_BYTES);
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char dest_rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char dest_dir[WEBFM_MAX_PATH_LEN] = {0};
    char dest[WEBFM_MAX_PATH_LEN] = {0};
    const char *base;
    struct stat st;
    esp_err_t ret = ESP_OK;
    bool usb_locked = false;

    if (!body ||
        !webfm_body_value(body, "path", rel, sizeof(rel)) ||
        !webfm_body_value(body, "dest", dest_rel, sizeof(dest_rel)) ||
        rel[0] == '\0' ||
        !webfm_build_path(rel, path, sizeof(path)) ||
        !webfm_build_path(dest_rel, dest_dir, sizeof(dest_dir)) ||
        webfm_path_is_storage_root(path)) {
        ESP_LOGW(TAG, "Web copy rejected: rel='%s' dest_dir='%s'", rel, dest_rel);
        free(body);
        return webfm_send_error(req, 400, TRS("请求无效"));
    }
    if (!webfm_begin_usb_io_for_paths(path, dest_dir, &usb_locked)) {
        free(body);
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (stat(path, &st) != 0) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 404, TRS("来源不存在"));
    }
    if (stat(dest_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 404, TRS("目标文件夹不存在"));
    }
    base = webfm_basename(path);
    if (!webfm_make_unique_copy_path(dest, sizeof(dest), dest_dir, base)) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 400, TRS("目标位置无效"));
    }
    if (webfm_path_is_same_or_child(dest, path)) {
        webfm_end_usb_io(usb_locked);
        free(body);
        return webfm_send_error(req, 400, TRS("不能把文件夹复制到自身内部"));
    }
    if (!webfm_copy_recursive(path, dest)) {
        ESP_LOGW(TAG, "Web copy failed: rel='%s' dest_dir='%s' path='%s' dest='%s' errno=%d",
                 rel, dest_rel, path, dest, errno);
        ret = webfm_send_error(req, 500, TRS("复制失败"));
    } else {
        ESP_LOGI(TAG, "Web copy: rel='%s' dest_dir='%s' path='%s' dest='%s'", rel, dest_rel, path, dest);
        ret = httpd_resp_sendstr(req, "ok");
    }
    webfm_end_usb_io(usb_locked);
    free(body);
    return ret;
}

static bool webfm_query_u64(httpd_req_t *req, const char *name, uint64_t *out)
{
    char raw[32] = {0};
    char *end = NULL;
    unsigned long long value;
    if (!webfm_query_decoded(req, name, raw, sizeof(raw)) || raw[0] == '\0') {
        return false;
    }
    errno = 0;
    value = strtoull(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static esp_err_t webfm_upload_post_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char name[128] = {0};
    char parent[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char *buf = NULL;
    FILE *fp = NULL;
    struct stat st;
    uint64_t offset = 0;
    uint64_t total = 0;
    bool has_offset;
    bool has_total;
    bool chunked;
    int remaining;
    bool usb_locked = false;

    httpd_resp_set_hdr(req, "Connection", "close");
    (void)webfm_query_decoded(req, "dir", rel, sizeof(rel));
    if (!webfm_query_decoded(req, "name", name, sizeof(name))) {
        return webfm_send_error(req, 400, TRS("缺少名称"));
    }
    if (!webfm_is_safe_name(name) ||
        !webfm_build_path(rel, parent, sizeof(parent)) ||
        !webfm_snprintf_ok(snprintf(path, sizeof(path), "%s/%s", parent, name), sizeof(path))) {
        ESP_LOGW(TAG, "Web upload rejected: dir='%s' name='%s' size=%d", rel, name, req->content_len);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    has_offset = webfm_query_u64(req, "offset", &offset);
    has_total = webfm_query_u64(req, "total", &total);
    if (has_offset != has_total) {
        return webfm_send_error(req, 400, TRS("片段参数无效"));
    }
    chunked = has_offset && has_total;
    if (!chunked) {
        total = (uint64_t)req->content_len;
    }
    if (chunked && (uint64_t)req->content_len > WEBFM_UPLOAD_CHUNK_LIMIT_BYTES) {
        return webfm_send_error(req, 413, TRS("片段过大"));
    }
    if (!chunked && (uint64_t)req->content_len > WEBFM_UPLOAD_CHUNK_LIMIT_BYTES) {
        return webfm_send_error(req, 413, TRS("请刷新页面后上传"));
    }
    if (total > 0x7fffffffULL || offset > total || (uint64_t)req->content_len > total - offset) {
        return webfm_send_error(req, 400, TRS("上传范围无效"));
    }
    s_webfm_upload_active++;
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        s_webfm_upload_active--;
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (chunked && offset > 0) {
        if (stat(path, &st) == 0 &&
            (uint64_t)st.st_size >= offset + (uint64_t)req->content_len) {
            ESP_LOGI(TAG,
                     "Web upload duplicate chunk: dir='%s' name='%s' path='%s' offset=%llu chunk=%d total=%llu",
                     rel,
                     name,
                     path,
                     (unsigned long long)offset,
                     req->content_len,
                     (unsigned long long)total);
            webfm_end_usb_io(usb_locked);
            s_webfm_upload_active--;
            return httpd_resp_sendstr(req, "ok");
        }
        if (stat(path, &st) != 0 || (uint64_t)st.st_size != offset) {
            ESP_LOGW(TAG, "Web upload rejected: non-contiguous chunk path='%s' offset=%llu errno=%d",
                     path,
                     (unsigned long long)offset,
                     errno);
            webfm_end_usb_io(usb_locked);
            s_webfm_upload_active--;
            return webfm_send_error(req, 409, TRS("片段顺序错误"));
        }
    }

    fp = fopen(path, (chunked && offset > 0) ? "r+b" : "wb");
    if (!fp) {
        webfm_end_usb_io(usb_locked);
        s_webfm_upload_active--;
        ESP_LOGW(TAG, "Web upload failed: fopen dir='%s' name='%s' path='%s' errno=%d",
                 rel, name, path, errno);
        return webfm_send_error(req, 500, TRS("打开失败"));
    }
    if (offset > 0 && fseek(fp, (long)offset, SEEK_SET) != 0) {
        fclose(fp);
        webfm_end_usb_io(usb_locked);
        s_webfm_upload_active--;
        ESP_LOGW(TAG, "Web upload failed: seek path='%s' offset=%llu errno=%d",
                 path,
                 (unsigned long long)offset,
                 errno);
        return webfm_send_error(req, 500, TRS("定位失败"));
    }
    buf = heap_caps_malloc(WEBFM_IO_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(WEBFM_IO_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        fclose(fp);
        webfm_end_usb_io(usb_locked);
        s_webfm_upload_active--;
        return webfm_send_error(req, 500, TRS("内存不足"));
    }

    ESP_LOGI(TAG,
             "Web upload begin: dir='%s' name='%s' path='%s' offset=%llu chunk=%d total=%llu dma_free=%u",
             rel,
             name,
             path,
             (unsigned long long)offset,
             req->content_len,
             (unsigned long long)total,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    remaining = req->content_len;
    while (remaining > 0) {
        int want = remaining > WEBFM_IO_BUF_SIZE ? WEBFM_IO_BUF_SIZE : remaining;
        int got = webfm_recv(req, buf, (size_t)want);
        if (got <= 0 || fwrite(buf, 1, got, fp) != (size_t)got) {
            ESP_LOGW(TAG, "Web upload failed: write dir='%s' name='%s' path='%s' remaining=%d errno=%d",
                     rel, name, path, remaining, errno);
            free(buf);
            fclose(fp);
            if (chunked) {
                (void)truncate(path, (off_t)offset);
            } else {
                unlink(path);
            }
            webfm_end_usb_io(usb_locked);
            s_webfm_upload_active--;
            return webfm_send_error(req, 500, TRS("上传失败"));
        }
        remaining -= got;
        size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
        vTaskDelay(pdMS_TO_TICKS(WEBFM_UPLOAD_RECV_DELAY_MS));
        if (dma_free < WEBFM_UPLOAD_CRITICAL_DMA_BYTES) {
            vTaskDelay(pdMS_TO_TICKS(WEBFM_UPLOAD_CRITICAL_DMA_DELAY_MS));
        } else if (dma_free < WEBFM_UPLOAD_LOW_DMA_BYTES) {
            vTaskDelay(pdMS_TO_TICKS(WEBFM_UPLOAD_LOW_DMA_DELAY_MS));
        }
    }
    free(buf);
    fclose(fp);
    webfm_end_usb_io(usb_locked);
    s_webfm_upload_active--;
    ESP_LOGI(TAG,
             "Web upload: dir='%s' name='%s' path='%s' offset=%llu chunk=%d total=%llu done=%d",
             rel,
             name,
             path,
             (unsigned long long)offset,
             req->content_len,
             (unsigned long long)total,
             (int)(offset + (uint64_t)req->content_len >= total));
    return httpd_resp_sendstr(req, "ok");
}
