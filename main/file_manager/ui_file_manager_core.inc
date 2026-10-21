static void fm_style_surface(lv_obj_t *obj, lv_color_t bg, lv_color_t border, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

static lv_obj_t *fm_label(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, TRS(text ? text : ""));
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *fm_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t border)
{
    lv_obj_t *btn = lv_button_create(parent);
    fm_style_surface(btn, bg, border, 10);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 12, 0);
    lv_obj_set_style_pad_top(btn, 8, 0);
    lv_obj_set_style_pad_bottom(btn, 8, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = fm_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *fm_status_chip(lv_obj_t *parent, const char *text, lv_obj_t **out_label)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(chip, 54, 48);
    fm_style_surface(chip, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    label = fm_label(chip, text, THEME_TEXT_SECONDARY);
    lv_obj_center(label);

    if (out_label) {
        *out_label = label;
        lv_obj_null_on_delete(out_label);
    }

    return chip;
}

static lv_color_t fm_power_color_for_status(const power_monitor_status_t *status)
{
    if (!status || !status->ina226_present) {
        return THEME_TEXT_SECONDARY;
    }
    if (!status->battery_present) {
        return status->usb_c_present ? THEME_ACCENT_CYAN : THEME_TEXT_SECONDARY;
    }
    if (status->battery_percent <= 15.0f) {
        return THEME_DANGER_LIGHT;
    }
    if (status->battery_percent <= 35.0f) {
        return THEME_WARNING_LIGHT;
    }
    if (status->charging) {
        return THEME_ACCENT_CYAN;
    }
    return THEME_SUCCESS_LIGHT;
}

static lv_obj_t *fm_create_power_chip(lv_obj_t *parent)
{
    lv_obj_t *chip = lv_obj_create(parent);

    lv_obj_set_size(chip, 82, 54);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    s_power_body = lv_obj_create(chip);
    lv_obj_null_on_delete(&s_power_body);
    lv_obj_set_size(s_power_body, 52, 30);
    lv_obj_align(s_power_body, LV_ALIGN_CENTER, -3, 0);
    lv_obj_set_style_bg_opa(s_power_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_power_body, 3, 0);
    lv_obj_set_style_border_color(s_power_body, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_radius(s_power_body, 6, 0);
    lv_obj_set_style_pad_all(s_power_body, 0, 0);
    lv_obj_clear_flag(s_power_body, LV_OBJ_FLAG_SCROLLABLE);

    s_power_cap = lv_obj_create(chip);
    lv_obj_null_on_delete(&s_power_cap);
    lv_obj_set_size(s_power_cap, 6, 16);
    lv_obj_align_to(s_power_cap, s_power_body, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_set_style_bg_color(s_power_cap, THEME_TEXT_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_power_cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_power_cap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_power_cap, 2, 0);
    lv_obj_clear_flag(s_power_cap, LV_OBJ_FLAG_SCROLLABLE);

    s_power_percent = lv_label_create(s_power_body);
    lv_obj_null_on_delete(&s_power_percent);
    lv_label_set_text(s_power_percent, "--");
    lv_obj_set_style_text_font(s_power_percent, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_power_percent, THEME_TEXT_SECONDARY, 0);
    lv_obj_center(s_power_percent);

    return chip;
}

static bool fm_has_ext(const char *name, const char *const *exts, size_t count)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    if (!dot || dot == name) {
        return false;
    }
    dot++;
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(dot, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool fm_has_single_ext(const char *name, const char *ext)
{
    return fm_has_ext(name, &ext, 1);
}

static fm_file_type_t fm_file_type(const char *name, bool is_dir)
{
    static const char *const image_exts[] = {"jpg", "jpeg", "png", "bmp", "gif"};
    static const char *const audio_exts[] = {"mp3", "wav", "flac", "aac", "m4a", "ogg", "opus", "amr", "pcm"};
    static const char *const video_exts[] = {"avi", "mjpg", "mp4"};
    static const char *const text_exts[] = {
        "txt", "text", "log", "md", "markdown", "json", "jsonc", "ini", "cfg", "conf", "cnf",
        "yaml", "yml", "toml", "xml", "csv", "tsv", "html", "htm", "css", "js", "mjs", "cjs",
        "ts", "tsx", "jsx", "c", "h", "cpp", "hpp", "cc", "hh", "cxx", "hxx", "ino", "py",
        "sh", "bash", "zsh", "bat", "cmd", "ps1", "sql", "lua", "rs", "go", "java", "kt",
        "swift", "php", "rb", "pl", "cmake", "mk", "mak", "properties", "env"
    };

    if (is_dir) {
        return FM_TYPE_DIR;
    }
    if (fm_has_ext(name, image_exts, sizeof(image_exts) / sizeof(image_exts[0]))) {
        return FM_TYPE_IMAGE;
    }
    if (fm_has_ext(name, audio_exts, sizeof(audio_exts) / sizeof(audio_exts[0]))) {
        return FM_TYPE_AUDIO;
    }
    if (fm_has_ext(name, video_exts, sizeof(video_exts) / sizeof(video_exts[0]))) {
        return FM_TYPE_VIDEO;
    }
    if (fm_has_ext(name, text_exts, sizeof(text_exts) / sizeof(text_exts[0]))) {
        return FM_TYPE_TEXT;
    }
    return FM_TYPE_OTHER;
}

static const char *fm_type_symbol(fm_file_type_t type)
{
    switch (type) {
    case FM_TYPE_DIR:
        return LV_SYMBOL_DIRECTORY;
    case FM_TYPE_IMAGE:
        return LV_SYMBOL_IMAGE;
    case FM_TYPE_AUDIO:
        return LV_SYMBOL_AUDIO;
    case FM_TYPE_VIDEO:
        return LV_SYMBOL_VIDEO;
    case FM_TYPE_TEXT:
        return LV_SYMBOL_FILE;
    default:
        return LV_SYMBOL_FILE;
    }
}

static lv_color_t fm_type_color(fm_file_type_t type)
{
    switch (type) {
    case FM_TYPE_DIR:
        return THEME_WARNING_LIGHT;
    case FM_TYPE_IMAGE:
        return THEME_SUCCESS_LIGHT;
    case FM_TYPE_AUDIO:
        return THEME_ACCENT_CYAN;
    case FM_TYPE_VIDEO:
        return lv_color_hex(0xFB7185);
    case FM_TYPE_TEXT:
        return THEME_PRIMARY_LIGHT;
    default:
        return THEME_TEXT_TERTIARY;
    }
}

static bool fm_snprintf_ok(int n, size_t out_len)
{
    return file_manager_snprintf_ok(n, out_len);
}

static bool fm_join_path(char *out, size_t out_len, const char *base, const char *name)
{
    return file_manager_join_path(out, out_len, base, name);
}

static bool fm_path_in_root(const char *path)
{
    return file_manager_path_in_root(path, s_root_path);
}

static bool fm_is_safe_name(const char *name)
{
    return file_manager_is_safe_name(name) && !strstr(name, "..");
}

static bool fm_is_root(void)
{
    return strcmp(s_current_path, s_root_path) == 0;
}

static const char *fm_display_path(void)
{
    if (fm_is_root()) {
        return "/";
    }
    size_t root_len = strlen(s_root_path);
    if (strncmp(s_current_path, s_root_path, root_len) == 0) {
        return s_current_path + root_len;
    }
    return s_current_path;
}

static void fm_format_size(uint64_t bytes, char *out, size_t out_len)
{
    file_manager_format_size(bytes, out, out_len);
}

static int fm_entry_compare(const void *lhs, const void *rhs)
{
    const fm_entry_t *a = (const fm_entry_t *)lhs;
    const fm_entry_t *b = (const fm_entry_t *)rhs;
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    return strcasecmp(a->name, b->name);
}

static bool fm_load_entries(void)
{
    DIR *dir;
    s_entry_count = 0;

    if (s_storage_picker_active || !fm_volume_is_mounted(s_current_volume)) {
        return false;
    }
    dir = opendir(s_current_path);
    if (!dir) {
        ESP_LOGW(TAG, "opendir failed: %s errno=%d", s_current_path, errno);
        return false;
    }

    while (s_entry_count < FM_MAX_ENTRIES) {
        struct dirent *ent = readdir(dir);
        char full[FM_PATH_MAX];
        struct stat st;
        bool is_dir;
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!fm_join_path(full, sizeof(full), s_current_path, ent->d_name)) {
            continue;
        }
        if (stat(full, &st) != 0) {
            continue;
        }
        is_dir = S_ISDIR(st.st_mode);
        strlcpy(s_entries[s_entry_count].name, ent->d_name, sizeof(s_entries[s_entry_count].name));
        s_entries[s_entry_count].is_dir = is_dir;
        s_entries[s_entry_count].size = is_dir ? 0 : (uint64_t)st.st_size;
        s_entries[s_entry_count].type = fm_file_type(ent->d_name, is_dir);
        s_entry_count++;
    }
    closedir(dir);
    qsort(s_entries, s_entry_count, sizeof(s_entries[0]), fm_entry_compare);

    size_t total_pages = s_entry_count == 0 ? 1 : ((s_entry_count - 1) / FM_PAGE_SIZE + 1);
    if (s_page >= total_pages) {
        s_page = total_pages - 1;
    }
    return true;
}

static void fm_clamp_selection_to_entries(void)
{
    if (s_entry_count == 0) {
        s_selected_index = 0;
        s_all_active = false;
        s_selection_status_active = false;
        return;
    }
    if (s_selected_index >= s_entry_count) {
        s_selected_index = s_entry_count - 1;
    }
}

static void fm_select_index(size_t index, bool redraw)
{
    if (s_entry_count == 0) {
        s_selected_index = 0;
        s_all_active = false;
        s_selection_status_active = false;
        if (redraw) {
            fm_render();
        }
        return;
    }
    if (index >= s_entry_count) {
        index = s_entry_count - 1;
    }
    s_selected_index = index;
    s_all_active = false;
    s_selection_status_active = false;
    s_page = s_selected_index / FM_PAGE_SIZE;
    s_scroll_active_into_view = true;
    if (redraw) {
        fm_render();
    }
}

static void fm_select_relative(int delta)
{
    int next;

    if (s_entry_count == 0) {
        return;
    }
    next = (int)s_selected_index + delta;
    if (next < 0) {
        next = 0;
    } else if ((size_t)next >= s_entry_count) {
        next = (int)s_entry_count - 1;
    }
    if ((size_t)next != s_selected_index || s_all_active) {
        fm_select_index((size_t)next, true);
    }
}

static void fm_select_page_start(void)
{
    size_t start = s_page * FM_PAGE_SIZE;

    if (s_entry_count == 0) {
        s_selected_index = 0;
        s_all_active = false;
        s_selection_status_active = false;
        return;
    }
    if (start >= s_entry_count) {
        start = s_entry_count - 1;
    }
    s_selected_index = start;
    s_all_active = false;
    s_selection_status_active = false;
    s_scroll_active_into_view = true;
}

static size_t fm_first_active_index(void)
{
    return s_all_active ? 0 : s_selected_index;
}

static bool fm_entry_active(size_t index)
{
    return index < s_entry_count && (s_all_active || index == s_selected_index);
}

static size_t fm_active_count(void)
{
    if (s_entry_count == 0) {
        return 0;
    }
    return s_all_active ? s_entry_count : 1;
}

static bool fm_set_active_from_index(size_t index)
{
    if (index >= s_entry_count) {
        return false;
    }
    strlcpy(s_active_name, s_entries[index].name, sizeof(s_active_name));
    if (!fm_join_path(s_active_path, sizeof(s_active_path), s_current_path, s_active_name)) {
        fm_set_status(TRS("路径过长"), THEME_DANGER_LIGHT);
        return false;
    }
    return true;
}

static bool fm_set_active_from_first_active(void)
{
    if (s_entry_count == 0) {
        fm_set_status(TRS("当前文件夹为空"), THEME_TEXT_SECONDARY);
        return false;
    }
    return fm_set_active_from_index(fm_first_active_index());
}

static void fm_open_selected(void)
{
    if (!fm_set_active_from_first_active()) {
        return;
    }
    fm_open_active();
}

static void fm_select_all_active(void)
{
    if (s_entry_count == 0) {
        s_all_active = false;
        s_selection_status_active = false;
        fm_set_status(TRS("当前文件夹为空"), THEME_TEXT_SECONDARY);
        return;
    }
    s_all_active = true;
    s_selection_status_active = true;
    s_status_override_active = false;
    s_selected_index = 0;
    s_page = 0;
    s_scroll_active_into_view = true;
    fm_render();
    fm_update_selection_status();
}

static void fm_clear_delete_selection(void)
{
    if (s_delete_selection) {
        free(s_delete_selection);
        s_delete_selection = NULL;
    }
}

static bool fm_prepare_delete_selection_from_entries(void)
{
    fm_delete_selection_t *selection;
    size_t count;
    size_t written = 0;
    size_t first;

    count = fm_active_count();
    if (count == 0) {
        fm_set_status(TRS("当前文件夹为空"), THEME_TEXT_SECONDARY);
        return false;
    }
    first = fm_first_active_index();
    if (!fm_set_active_from_index(first)) {
        return false;
    }

    fm_clear_delete_selection();
    selection = calloc(1, sizeof(*selection) + count * sizeof(selection->names[0]));
    if (!selection) {
        fm_set_status(TRS("删除列表内存不足"), THEME_DANGER_LIGHT);
        return false;
    }

    strlcpy(selection->dir, s_current_path, sizeof(selection->dir));
    if (s_all_active) {
        for (size_t i = 0; i < s_entry_count && written < count; i++) {
            strlcpy(selection->names[written++], s_entries[i].name, sizeof(selection->names[0]));
        }
    } else {
        strlcpy(selection->names[written++], s_entries[first].name, sizeof(selection->names[0]));
    }

    if (written == 0) {
        free(selection);
        fm_set_status(TRS("当前文件夹为空"), THEME_TEXT_SECONDARY);
        return false;
    }
    selection->count = written;
    s_delete_selection = selection;
    return true;
}

static bool fm_prepare_delete_single_active(void)
{
    fm_delete_selection_t *selection;
    char path[FM_PATH_MAX];

    if (s_active_name[0] == '\0' || !fm_join_path(path, sizeof(path), s_current_path, s_active_name)) {
        fm_set_status(TRS("路径过长"), THEME_DANGER_LIGHT);
        return false;
    }

    fm_clear_delete_selection();
    selection = calloc(1, sizeof(*selection) + sizeof(selection->names[0]));
    if (!selection) {
        fm_set_status(TRS("删除列表内存不足"), THEME_DANGER_LIGHT);
        return false;
    }

    selection->count = 1;
    strlcpy(selection->dir, s_current_path, sizeof(selection->dir));
    strlcpy(selection->names[0], s_active_name, sizeof(selection->names[0]));
    strlcpy(s_active_path, path, sizeof(s_active_path));
    s_delete_selection = selection;
    return true;
}

static void fm_delete_first_active(void)
{
    if (!fm_prepare_delete_selection_from_entries()) {
        return;
    }
    fm_show_delete_confirm();
}

static void fm_show_first_active_action_sheet(void)
{
    size_t index;

    if (s_entry_count == 0) {
        fm_set_status(TRS("当前文件夹为空"), THEME_TEXT_SECONDARY);
        return;
    }
    index = fm_first_active_index();
    s_all_active = false;
    s_selection_status_active = false;
    fm_show_action_sheet(index);
}

static bool fm_remove_recursive(const char *path)
{
    struct stat st;
    if (!path || path[0] == '\0' || fm_path_is_volume_root(path) ||
        !fm_path_in_known_volume(path) || stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        return false;
    }
    while (true) {
        struct dirent *ent = readdir(dir);
        char child[FM_PATH_MAX];
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!fm_join_path(child, sizeof(child), path, ent->d_name) ||
            !fm_remove_recursive(child)) {
            closedir(dir);
            return false;
        }
    }
    closedir(dir);
    return rmdir(path) == 0;
}

static bool fm_path_is_same_or_child(const char *path, const char *base)
{
    size_t base_len;

    if (!path || !base || !path[0] || !base[0]) {
        return false;
    }
    base_len = strlen(base);
    return strncmp(path, base, base_len) == 0 &&
           (path[base_len] == '\0' || path[base_len] == '/');
}

static void fm_clear_clipboard(void)
{
    s_clipboard_mode = FM_CLIPBOARD_NONE;
    s_clipboard_path[0] = '\0';
    s_clipboard_name[0] = '\0';
    s_clipboard_dir[0] = '\0';
    s_clipboard_count = 0;
    if (s_clipboard_names) {
        memset(s_clipboard_names, 0, fm_clipboard_names_bytes());
    }
    fm_update_paste_button();
}

static bool fm_clipboard_source_path(char *out, size_t out_len, size_t index)
{
    if (!out || index >= s_clipboard_count || !s_clipboard_names[index][0]) {
        return false;
    }
    if (s_clipboard_count == 1 && s_clipboard_path[0]) {
        strlcpy(out, s_clipboard_path, out_len);
        return true;
    }
    return fm_join_path(out, out_len, s_clipboard_dir, s_clipboard_names[index]);
}

static bool fm_paste_job_source_path(const fm_paste_job_t *job, char *out, size_t out_len, size_t index)
{
    if (!job || !out || index >= job->count || !job->names[index][0]) {
        return false;
    }
    if (job->count == 1 && job->single_source_path[0]) {
        strlcpy(out, job->single_source_path, out_len);
        return true;
    }
    return fm_join_path(out, out_len, job->source_dir, job->names[index]);
}

static bool fm_copy_file(const char *src, const char *dst)
{
    FILE *in = NULL;
    FILE *out = NULL;
    uint8_t *buf = NULL;
    bool ok = false;

    in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    buf = (uint8_t *)malloc(FM_COPY_BUF_BYTES);
    if (!buf) {
        fclose(out);
        fclose(in);
        unlink(dst);
        return false;
    }

    ok = true;
    while (true) {
        size_t got = fread(buf, 1, FM_COPY_BUF_BYTES, in);
        if (got > 0 && fwrite(buf, 1, got, out) != got) {
            ok = false;
            break;
        }
        if (got > 0) {
            vTaskDelay(1);
        }
        if (got < FM_COPY_BUF_BYTES) {
            if (ferror(in)) {
                ok = false;
            }
            break;
        }
    }

    free(buf);
    if (fclose(out) != 0) {
        ok = false;
    }
    fclose(in);
    if (!ok) {
        unlink(dst);
    }
    return ok;
}

static bool fm_copy_recursive(const char *src, const char *dst)
{
    struct stat st;

    if (!src || !dst || !fm_path_in_known_volume(src) || !fm_path_in_root(dst) ||
        stat(src, &st) != 0 || access(dst, F_OK) == 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return fm_copy_file(src, dst);
    }
    if (fm_path_is_same_or_child(dst, src) || mkdir(dst, 0775) != 0) {
        return false;
    }

    DIR *dir = opendir(src);
    if (!dir) {
        rmdir(dst);
        return false;
    }
    while (true) {
        struct dirent *ent = readdir(dir);
        char child_src[FM_PATH_MAX];
        char child_dst[FM_PATH_MAX];
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!fm_join_path(child_src, sizeof(child_src), src, ent->d_name) ||
            !fm_join_path(child_dst, sizeof(child_dst), dst, ent->d_name) ||
            !fm_copy_recursive(child_src, child_dst)) {
            closedir(dir);
            fm_remove_recursive(dst);
            return false;
        }
    }
    closedir(dir);
    return true;
}

static bool fm_copy_recursive_overwrite(const char *src, const char *dst, bool overwrite)
{
    if (strcmp(src, dst) == 0) {
        return true;
    }
    if (access(dst, F_OK) == 0) {
        if (!overwrite || !fm_remove_recursive(dst)) {
            return false;
        }
    }
    return fm_copy_recursive(src, dst);
}

static bool fm_clipboard_has_conflict(void)
{
    char src[FM_PATH_MAX];
    char dst[FM_PATH_MAX];

    for (size_t i = 0; i < s_clipboard_count; i++) {
        if (!fm_clipboard_source_path(src, sizeof(src), i) ||
            !fm_join_path(dst, sizeof(dst), s_current_path, s_clipboard_names[i])) {
            return true;
        }
        if (s_clipboard_mode == FM_CLIPBOARD_MOVE && strcmp(src, dst) == 0) {
            continue;
        }
        if (access(dst, F_OK) == 0) {
            return true;
        }
    }
    return false;
}

static bool fm_move_clipboard_item(const char *src, const char *dst, const struct stat *st, bool overwrite)
{
    if (strcmp(src, dst) == 0) {
        return true;
    }
    if (S_ISDIR(st->st_mode) && fm_path_is_same_or_child(dst, src)) {
        return false;
    }
    if (access(dst, F_OK) == 0) {
        if (!overwrite || !fm_remove_recursive(dst)) {
            return false;
        }
    }
    if (rename(src, dst) == 0) {
        return true;
    }
    if (!fm_copy_recursive(src, dst)) {
        return false;
    }
    return fm_remove_recursive(src);
}

static bool __attribute__((unused)) fm_make_unique_copy_path(char *out,
                                                             size_t out_len,
                                                             const char *dir,
                                                             const char *name)
{
    char candidate_name[FM_NAME_MAX];
    char stem[FM_NAME_MAX];
    char ext[FM_NAME_MAX];
    const char *dot;
    size_t stem_len;

    if (!out || !dir || !name || !name[0]) {
        return false;
    }
    if (!fm_join_path(out, out_len, dir, name)) {
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
                    ? snprintf(candidate_name, sizeof(candidate_name), "%s copy%s", stem, ext)
                    : snprintf(candidate_name, sizeof(candidate_name), "%s copy %u%s", stem, i + 1, ext);
        if (!fm_snprintf_ok(n, sizeof(candidate_name))) {
            continue;
        }
        if (!fm_join_path(out, out_len, dir, candidate_name)) {
            return false;
        }
        if (access(out, F_OK) != 0) {
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

static void fm_set_status(const char *text, lv_color_t color)
{
    if (!s_status_label) {
        return;
    }
    lv_label_set_text(s_status_label, TRS(text ? text : ""));
    lv_obj_set_style_text_color(s_status_label, color, 0);
    s_status_override_active = true;
    s_status_override_until = lv_tick_get() + FM_STATUS_OVERRIDE_MS;
}

static void fm_set_web_status(const char *text, lv_color_t color)
{
    if (!s_web_status_label) {
        return;
    }
    lv_label_set_text(s_web_status_label, TRS(text ? text : ""));
    lv_obj_set_style_text_color(s_web_status_label, color, 0);
}

static uint64_t fm_current_folder_file_size(void)
{
    uint64_t total = 0;

    for (size_t i = 0; i < s_entry_count; i++) {
        if (!s_entries[i].is_dir) {
            total += s_entries[i].size;
        }
    }
    return total;
}

static void fm_update_selection_status(void)
{
    char text[96];

    if (!s_status_label || !s_selection_status_active || !s_all_active || s_entry_count == 0) {
        return;
    }
    snprintf(text, sizeof(text), TRS("已选择 %u 项"), (unsigned)s_entry_count);
    lv_label_set_text(s_status_label, text);
    lv_obj_set_style_text_color(s_status_label, THEME_SUCCESS_LIGHT, 0);
}

static void fm_update_folder_status(void)
{
    char size_text[32];
    char text[96];

    if (!s_status_label) {
        return;
    }
    if (s_status_override_active) {
        uint32_t now = lv_tick_get();
        if ((int32_t)(now - s_status_override_until) < 0) {
            return;
        }
        s_status_override_active = false;
    }
    if (s_selection_status_active && s_all_active && s_entry_count > 0) {
        fm_update_selection_status();
        return;
    }
    if (s_storage_picker_active) {
        lv_label_set_text(s_status_label, TRS("选择存储器"));
        lv_obj_set_style_text_color(s_status_label, THEME_TEXT_TERTIARY, 0);
        return;
    }
    if (!fm_volume_is_mounted(s_current_volume)) {
        lv_label_set_text(s_status_label,
                          s_current_volume == FM_VOLUME_USB ? TRS("U盘未挂载") : TRS("TF卡未挂载"));
        lv_obj_set_style_text_color(s_status_label, THEME_WARNING_LIGHT, 0);
        return;
    }
    file_manager_format_size_fixed2(fm_current_folder_file_size(), size_text, sizeof(size_text));
    snprintf(text, sizeof(text), TRS("%u 项，总大小 %s"), (unsigned)s_entry_count, size_text);
    lv_label_set_text(s_status_label, text);
    lv_obj_set_style_text_color(s_status_label, THEME_TEXT_TERTIARY, 0);
}

static void fm_show_busy_toast(const char *text)
{
    lv_obj_t *panel;

    if (!s_overlay) {
        return;
    }
    if (!s_busy_toast) {
        s_busy_toast = lv_obj_create(s_overlay);
        lv_obj_null_on_delete(&s_busy_toast);
        lv_obj_set_size(s_busy_toast, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(s_busy_toast, 0, 0);
        lv_obj_set_style_bg_color(s_busy_toast, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_busy_toast, LV_OPA_40, 0);
        lv_obj_set_style_border_opa(s_busy_toast, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(s_busy_toast, 0, 0);
        lv_obj_set_style_pad_all(s_busy_toast, 0, 0);
        lv_obj_add_flag(s_busy_toast, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_busy_toast, LV_OBJ_FLAG_SCROLLABLE);

        panel = lv_obj_create(s_busy_toast);
        lv_obj_set_size(panel, 360, 118);
        lv_obj_center(panel);
        fm_style_surface(panel, THEME_BG_PANEL, THEME_PRIMARY, 12);
        lv_obj_set_style_pad_all(panel, 18, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        s_busy_toast_label = fm_label(panel, "", THEME_TEXT_PRIMARY);
        lv_obj_null_on_delete(&s_busy_toast_label);
        lv_obj_set_width(s_busy_toast_label, LV_PCT(100));
        lv_label_set_long_mode(s_busy_toast_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_busy_toast_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (s_busy_toast_label) {
        lv_label_set_text(s_busy_toast_label, text ? TRS(text) : TRS("文件操作正在进行，请稍候..."));
    }
    lv_obj_move_foreground(s_busy_toast);
    lv_obj_clear_flag(s_busy_toast, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL);
}

static void fm_hide_busy_toast(void)
{
    if (s_busy_toast) {
        lv_obj_del(s_busy_toast);
    }
    s_busy_toast = NULL;
    s_busy_toast_label = NULL;
}

static bool fm_show_web_status_in_footer(void)
{
    char url[96];
    char reason[96];
    bool running = web_file_manager_is_running();
    bool can_start = web_file_manager_can_start(reason, sizeof(reason));

    if (running && web_file_manager_get_url(url, sizeof(url))) {
        char text[144];
        snprintf(text, sizeof(text), TRS("在线文件管理器：%s"), url);
        fm_set_web_status(text, THEME_SUCCESS_LIGHT);
        return true;
    }
    if (can_start) {
        fm_set_web_status(TRS("在线文件管理器：已就绪"), THEME_TEXT_SECONDARY);
    } else {
        char text[128];
        snprintf(text, sizeof(text), TRS("在线文件管理器：%s"), reason[0] ? reason : TRS("未就绪"));
        fm_set_web_status(text, THEME_WARNING_LIGHT);
    }
    return false;
}
