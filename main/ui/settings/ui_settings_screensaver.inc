static void set_screensaver_status(const char *message, lv_color_t color)
{
    if (!s_screensaver_status_label) {
        return;
    }
    lv_label_set_text(s_screensaver_status_label, message ? TRS(message) : "");
    lv_obj_set_style_text_color(s_screensaver_status_label, color, 0);
}

static void set_screensaver_status_range(const char *format,
                                         int min_value,
                                         int max_value,
                                         lv_color_t color)
{
    char text[128];

    if (!s_screensaver_status_label) {
        return;
    }
    snprintf(text, sizeof(text), TRS(format), min_value, max_value);
    lv_label_set_text(s_screensaver_status_label, text);
    lv_obj_set_style_text_color(s_screensaver_status_label, color, 0);
}

static lv_obj_t *screensaver_create_raw_label(lv_obj_t *parent,
                                              const char *text,
                                              lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, &APP_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static bool screensaver_dir_is_tf_ready(void)
{
    return tf_card_is_mounted() && !tf_usb_disk_mode_blocks_tf_access();
}

static bool screensaver_dir_is_storage_root(const char *path)
{
    return path &&
           ((screensaver_dir_is_tf_ready() &&
             strcmp(path, tf_card_get_mount_point()) == 0) ||
            (usb_storage_is_mounted() &&
             strcmp(path, usb_storage_get_mount_point()) == 0));
}

static void screensaver_dir_set_pending(const char *path)
{
    if (!path || path[0] != '/') {
        return;
    }
    snprintf(s_screensaver_dir_pending, sizeof(s_screensaver_dir_pending), "%s", path);
    if (s_screensaver_dir_value_label) {
        lv_label_set_text(s_screensaver_dir_value_label, s_screensaver_dir_pending);
    }
}

static bool screensaver_dir_join(char *out,
                                 size_t out_len,
                                 const char *dir,
                                 const char *name)
{
    int written;

    if (!out || out_len == 0 || !dir || !name) {
        return false;
    }
    written = snprintf(out,
                       out_len,
                       "%s%s%s",
                       dir,
                       dir[strlen(dir) - 1] == '/' ? "" : "/",
                       name);
    return written > 0 && (size_t)written < out_len;
}

static bool screensaver_dir_parent(char *out, size_t out_len, const char *path)
{
    char tmp[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN];
    char *slash;
    size_t len;

    if (!out || out_len == 0 || !path || path[0] != '/' ||
        screensaver_dir_is_storage_root(path)) {
        return false;
    }
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }
    slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return false;
    }
    *slash = '\0';
    snprintf(out, out_len, "%s", tmp);
    return true;
}

static bool screensaver_dir_can_go_parent(const char *path)
{
    char parent[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN];
    return screensaver_dir_parent(parent, sizeof(parent), path);
}

static lv_obj_t *screensaver_create_small_button(lv_obj_t *parent,
                                                 const char *text,
                                                 lv_color_t bg,
                                                 lv_event_cb_t cb,
                                                 void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 42);
    lv_obj_set_width(btn, 150);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = create_label(btn, TRS(text), THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *screensaver_create_raw_small_button(lv_obj_t *parent,
                                                     const char *text,
                                                     lv_color_t bg,
                                                     lv_event_cb_t cb,
                                                     void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 42);
    lv_obj_set_width(btn, 150);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = screensaver_create_raw_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *screensaver_create_symbol_text_button(lv_obj_t *parent,
                                                       const char *symbol,
                                                       const char *text,
                                                       lv_color_t bg,
                                                       lv_event_cb_t cb,
                                                       void *user_data)
{
    char label_text[64];

    snprintf(label_text,
             sizeof(label_text),
             "%s%s",
             symbol ? symbol : "",
             TRS(text ? text : ""));
    return screensaver_create_raw_small_button(parent, label_text, bg, cb, user_data);
}

static void close_screensaver_dir_picker(void)
{
    if (s_screensaver_dir_picker_overlay) {
        lv_obj_del(s_screensaver_dir_picker_overlay);
    }
    s_screensaver_dir_picker_overlay = NULL;
    s_screensaver_dir_picker_path_label = NULL;
    s_screensaver_dir_picker_list = NULL;
    s_screensaver_dir_picker_status_label = NULL;
    s_screensaver_dir_picker_up_button = NULL;
}

static void screensaver_dir_picker_render(const char *path);

static void on_screensaver_dir_storage_click(lv_event_t *e)
{
    const char *path = (const char *)lv_event_get_user_data(e);
    screensaver_dir_picker_render(path);
}

static void on_screensaver_dir_entry_click(lv_event_t *e)
{
    const char *path = (const char *)lv_event_get_user_data(e);
    screensaver_dir_picker_render(path);
}

static void on_screensaver_dir_parent_click(lv_event_t *e)
{
    char parent[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN];
    (void)e;

    if (screensaver_dir_parent(parent, sizeof(parent), s_screensaver_dir_picker_path)) {
        screensaver_dir_picker_render(parent);
    }
}

static void on_screensaver_dir_choose_click(lv_event_t *e)
{
    (void)e;
    screensaver_dir_set_pending(s_screensaver_dir_picker_path);
    close_screensaver_dir_picker();
    set_screensaver_status("已选择壁纸目录。", THEME_SUCCESS_LIGHT);
}

static void on_screensaver_dir_cancel_click(lv_event_t *e)
{
    (void)e;
    close_screensaver_dir_picker();
}

static void screensaver_dir_add_storage_button(lv_obj_t *parent, const char *path)
{
    char text[64];

    if (!parent || !path) {
        return;
    }
    snprintf(text, sizeof(text), LV_SYMBOL_DIRECTORY " %s", path);
    screensaver_create_small_button(parent,
                                    text,
                                    THEME_BG_HOVER,
                                    on_screensaver_dir_storage_click,
                                    (void *)path);
}

static void screensaver_dir_add_entry(lv_obj_t *parent, const char *path, const char *name)
{
    lv_obj_t *btn;
    lv_obj_t *label;

    btn = lv_button_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 42);
    lv_obj_set_style_bg_color(btn, THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 12, 0);
    lv_obj_add_event_cb(btn, on_screensaver_dir_entry_click, LV_EVENT_CLICKED, (void *)path);

    label = screensaver_create_raw_label(btn, name, THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
}

static int screensaver_dir_collect_entries(const char *path)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if (!settings_ensure_screensaver_dir_entries()) {
        return -1;
    }
    dir = opendir(path);
    if (!dir) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL &&
           count < SETTINGS_SCREENSAVER_DIR_PICKER_MAX) {
        char full_path[APP_SETTINGS_SCREENSAVER_DIR_MAX_LEN];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!screensaver_dir_join(full_path, sizeof(full_path), path, entry->d_name)) {
            continue;
        }
        if (stat(full_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        snprintf(s_screensaver_dir_picker_entries[count],
                 sizeof(s_screensaver_dir_picker_entries[count]),
                 "%s",
                 full_path);
        count++;
    }
    closedir(dir);
    return count;
}

static void screensaver_dir_picker_render(const char *path)
{
    int count;

    if (!path || path[0] != '/' || !s_screensaver_dir_picker_list) {
        return;
    }
    snprintf(s_screensaver_dir_picker_path,
             sizeof(s_screensaver_dir_picker_path),
             "%s",
             path);
    if (s_screensaver_dir_picker_path_label) {
        lv_label_set_text(s_screensaver_dir_picker_path_label, s_screensaver_dir_picker_path);
    }
    if (s_screensaver_dir_picker_up_button) {
        if (screensaver_dir_can_go_parent(s_screensaver_dir_picker_path)) {
            lv_obj_remove_state(s_screensaver_dir_picker_up_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_screensaver_dir_picker_up_button, LV_STATE_DISABLED);
        }
    }
    lv_obj_clean(s_screensaver_dir_picker_list);
    if (s_screensaver_dir_picker_status_label) {
        lv_label_set_text(s_screensaver_dir_picker_status_label, "");
    }

    count = screensaver_dir_collect_entries(s_screensaver_dir_picker_path);
    if (count < 0) {
        if (s_screensaver_dir_picker_status_label) {
            lv_label_set_text(s_screensaver_dir_picker_status_label, TRS("目录读取失败"));
            lv_obj_set_style_text_color(s_screensaver_dir_picker_status_label,
                                        THEME_WARNING_LIGHT,
                                        0);
        }
        return;
    }
    if (count == 0) {
        create_wrap_label(s_screensaver_dir_picker_list,
                          "未找到子目录",
                          THEME_TEXT_TERTIARY);
        return;
    }
    for (int i = 0; i < count; i++) {
        const char *full_path = s_screensaver_dir_picker_entries[i];
        const char *name = strrchr(full_path, '/');
        screensaver_dir_add_entry(s_screensaver_dir_picker_list,
                                  full_path,
                                  name && name[1] ? name + 1 : full_path);
    }
}

static const char *screensaver_dir_default_picker_path(void)
{
    if (s_screensaver_dir_pending[0] == '/') {
        return s_screensaver_dir_pending;
    }
    if (screensaver_dir_is_tf_ready()) {
        return tf_card_get_mount_point();
    }
    if (usb_storage_is_mounted()) {
        return usb_storage_get_mount_point();
    }
    return APP_SETTINGS_DEFAULT_SCREENSAVER_DIR;
}

static void on_screensaver_dir_open_click(lv_event_t *e)
{
    lv_obj_t *panel;
    lv_obj_t *header;
    lv_obj_t *root_row;
    lv_obj_t *actions;
    lv_obj_t *up_btn;
    (void)e;

    if (s_screensaver_dir_picker_overlay) {
        lv_obj_move_foreground(s_screensaver_dir_picker_overlay);
        return;
    }

    s_screensaver_dir_picker_overlay = lv_obj_create(s_overlay ? s_overlay : lv_layer_top());
    lv_obj_null_on_delete(&s_screensaver_dir_picker_overlay);
    lv_obj_set_size(s_screensaver_dir_picker_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screensaver_dir_picker_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screensaver_dir_picker_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_screensaver_dir_picker_overlay, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_screensaver_dir_picker_overlay, LV_OBJ_FLAG_SCROLLABLE);

    panel = lv_obj_create(s_screensaver_dir_picker_overlay);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_PRIMARY, 14);
    lv_obj_set_size(panel, 760, 540);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    header = lv_obj_create(panel);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 44);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    create_label(header, "选择壁纸目录", THEME_TEXT_PRIMARY);
    screensaver_create_small_button(header, "取消", THEME_BG_HOVER, on_screensaver_dir_cancel_click, NULL);

    root_row = lv_obj_create(panel);
    lv_obj_set_width(root_row, LV_PCT(100));
    lv_obj_set_height(root_row, 46);
    lv_obj_set_style_bg_opa(root_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(root_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root_row, 0, 0);
    lv_obj_set_style_pad_column(root_row, 10, 0);
    lv_obj_set_flex_flow(root_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(root_row, LV_OBJ_FLAG_SCROLLABLE);
    if (screensaver_dir_is_tf_ready()) {
        screensaver_dir_add_storage_button(root_row, tf_card_get_mount_point());
    }
    if (usb_storage_is_mounted()) {
        screensaver_dir_add_storage_button(root_row, usb_storage_get_mount_point());
    }
    if (!screensaver_dir_is_tf_ready() && !usb_storage_is_mounted()) {
        create_wrap_label(root_row, "请选择存储器，或点击挂载U盘", THEME_WARNING_LIGHT);
    }

    s_screensaver_dir_picker_path_label =
        screensaver_create_raw_label(panel, "", THEME_PRIMARY_LIGHT);
    lv_obj_null_on_delete(&s_screensaver_dir_picker_path_label);
    lv_obj_set_width(s_screensaver_dir_picker_path_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_screensaver_dir_picker_path_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_screensaver_dir_picker_path_label, LV_LABEL_LONG_DOT);

    s_screensaver_dir_picker_list = lv_obj_create(panel);
    lv_obj_null_on_delete(&s_screensaver_dir_picker_list);
    lv_obj_set_width(s_screensaver_dir_picker_list, LV_PCT(100));
    lv_obj_set_height(s_screensaver_dir_picker_list, 260);
    lv_obj_set_style_bg_color(s_screensaver_dir_picker_list, THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_screensaver_dir_picker_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_screensaver_dir_picker_list, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(s_screensaver_dir_picker_list, 1, 0);
    lv_obj_set_style_radius(s_screensaver_dir_picker_list, 10, 0);
    lv_obj_set_style_pad_all(s_screensaver_dir_picker_list, 10, 0);
    lv_obj_set_style_pad_row(s_screensaver_dir_picker_list, 8, 0);
    lv_obj_set_flex_flow(s_screensaver_dir_picker_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_screensaver_dir_picker_list, LV_DIR_VER);

    s_screensaver_dir_picker_status_label =
        create_wrap_label(panel, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_screensaver_dir_picker_status_label);

    actions = lv_obj_create(panel);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 46);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 10, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    up_btn = screensaver_create_symbol_text_button(actions,
                                                   LV_SYMBOL_UP,
                                                   " 上一级",
                                                   THEME_BG_HOVER,
                                                   on_screensaver_dir_parent_click,
                                                   NULL);
    s_screensaver_dir_picker_up_button = up_btn;
    lv_obj_null_on_delete(&s_screensaver_dir_picker_up_button);
    screensaver_create_small_button(actions,
                                    "选择当前目录",
                                    THEME_PRIMARY,
                                    on_screensaver_dir_choose_click,
                                    NULL);

    screensaver_dir_picker_render(screensaver_dir_default_picker_path());
}

static void update_screensaver_mode_styles(void)
{
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = s_screensaver_mode_buttons[i];
        if (!btn) {
            continue;
        }
        bool selected = (app_screensaver_mode_t)i == s_screensaver_mode_pending;
        lv_obj_set_style_bg_color(btn, selected ? THEME_PRIMARY : THEME_BG_CARD, 0);
        lv_obj_set_style_border_color(btn,
                                      selected ? THEME_PRIMARY_LIGHT : THEME_BORDER_SECONDARY,
                                      0);
    }
}

static void update_screensaver_order_styles(void)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = s_screensaver_order_buttons[i];
        if (!btn) {
            continue;
        }
        bool selected = (app_screensaver_order_t)i == s_screensaver_order_pending;
        lv_obj_set_style_bg_color(btn, selected ? THEME_PRIMARY : THEME_BG_CARD, 0);
        lv_obj_set_style_border_color(btn,
                                      selected ? THEME_PRIMARY_LIGHT : THEME_BORDER_SECONDARY,
                                      0);
    }
}

static void on_screensaver_mode_click(lv_event_t *e)
{
    s_screensaver_mode_pending =
        (app_screensaver_mode_t)(uintptr_t)lv_event_get_user_data(e);
    update_screensaver_mode_styles();
}

static void on_screensaver_order_click(lv_event_t *e)
{
    s_screensaver_order_pending =
        (app_screensaver_order_t)(uintptr_t)lv_event_get_user_data(e);
    update_screensaver_order_styles();
}

static lv_obj_t *create_screensaver_segment_button(lv_obj_t *parent,
                                                   const char *text,
                                                   bool selected,
                                                   lv_event_cb_t cb,
                                                   void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, 148);
    lv_obj_set_height(btn, 46);
    lv_obj_set_style_bg_color(btn, selected ? THEME_PRIMARY : THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, selected ? THEME_PRIMARY_LIGHT : THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = create_label(btn, TRS(text), THEME_TEXT_PRIMARY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    return btn;
}

static void create_screensaver_dir_picker_row(lv_obj_t *parent,
                                              const char *name,
                                              const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 58);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = create_label(row, TRS(name), THEME_TEXT_SECONDARY);
    lv_obj_set_width(label, 210);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_width(btn, 360);
    lv_obj_set_height(btn, 48);
    lv_obj_set_style_bg_color(btn, THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, on_screensaver_dir_open_click, LV_EVENT_CLICKED, NULL);

    s_screensaver_dir_value_label =
        screensaver_create_raw_label(btn, value ? value : "", THEME_TEXT_PRIMARY);
    lv_obj_null_on_delete(&s_screensaver_dir_value_label);
    lv_obj_set_width(s_screensaver_dir_value_label, 322);
    lv_obj_set_style_text_align(s_screensaver_dir_value_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_screensaver_dir_value_label, LV_LABEL_LONG_DOT);
    lv_obj_center(s_screensaver_dir_value_label);
}

static lv_obj_t *create_screensaver_segment_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_pad_row(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static const char *screensaver_component_label(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return "EXIF标题";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
        return "时间";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return "秒钟";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return "年月日星期";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return "农历日期";
    default:
        return "";
    }
}

static bool screensaver_component_visible_in_settings(app_screensaver_component_t component)
{
    return component != APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR ||
           app_i18n_get_language() != APP_LANGUAGE_EN;
}

static lv_obj_t *create_screensaver_coord_input(lv_obj_t *parent,
                                                int value,
                                                lv_obj_t **out_ta)
{
    char value_text[16];
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, 84);
    lv_obj_set_height(ta, 42);
    lv_obj_set_style_text_font(ta, &APP_FONT, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 5);
    snprintf(value_text, sizeof(value_text), "%d", value);
    lv_textarea_set_text(ta, value_text);
    ui_number_input_attach(ta, s_overlay);
    if (out_ta) {
        *out_ta = ta;
        lv_obj_null_on_delete(out_ta);
    }
    return ta;
}

static void screensaver_refresh_shadow_controls(void)
{
    char text[8];

    if (s_screensaver_shadow_checkbox) {
        if (app_settings_get_screensaver_text_shadow_enabled()) {
            lv_obj_add_state(s_screensaver_shadow_checkbox, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_screensaver_shadow_checkbox, LV_STATE_CHECKED);
        }
    }
    if (s_screensaver_shadow_length_ta) {
        snprintf(text,
                 sizeof(text),
                 "%d",
                 app_settings_get_screensaver_text_shadow_length());
        lv_textarea_set_text(s_screensaver_shadow_length_ta, text);
    }
}

static void create_screensaver_disable_ssh_row(lv_obj_t *parent)
{
    s_screensaver_disable_ssh_checkbox = lv_checkbox_create(parent);
    lv_obj_null_on_delete(&s_screensaver_disable_ssh_checkbox);
    lv_checkbox_set_text(s_screensaver_disable_ssh_checkbox,
                         TRS("SSH 会话中禁用屏保"));
    lv_obj_set_width(s_screensaver_disable_ssh_checkbox, LV_PCT(100));
    lv_obj_set_height(s_screensaver_disable_ssh_checkbox, 42);
    lv_obj_set_style_text_font(s_screensaver_disable_ssh_checkbox, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_screensaver_disable_ssh_checkbox,
                                THEME_TEXT_PRIMARY,
                                0);
    lv_obj_set_style_bg_opa(s_screensaver_disable_ssh_checkbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_screensaver_disable_ssh_checkbox,
                                  THEME_PRIMARY,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_screensaver_disable_ssh_checkbox,
                              THEME_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_screensaver_disable_ssh_checkbox,
                            LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (app_settings_get_screensaver_disable_during_ssh()) {
        lv_obj_add_state(s_screensaver_disable_ssh_checkbox, LV_STATE_CHECKED);
    }
}

static void create_screensaver_shadow_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);

    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 50);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_screensaver_shadow_checkbox = lv_checkbox_create(row);
    lv_obj_null_on_delete(&s_screensaver_shadow_checkbox);
    lv_checkbox_set_text(s_screensaver_shadow_checkbox, TRS("启用文字阴影"));
    lv_obj_set_width(s_screensaver_shadow_checkbox, 190);
    lv_obj_set_style_text_font(s_screensaver_shadow_checkbox, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_screensaver_shadow_checkbox, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(s_screensaver_shadow_checkbox,
                                  THEME_BORDER_SECONDARY,
                                  LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_screensaver_shadow_checkbox, 1, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_screensaver_shadow_checkbox,
                              THEME_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(s_screensaver_shadow_checkbox,
                                  THEME_PRIMARY_LIGHT,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);

    create_label(row, "阴影长度", THEME_TEXT_TERTIARY);
    create_screensaver_coord_input(row,
                                   app_settings_get_screensaver_text_shadow_length(),
                                   &s_screensaver_shadow_length_ta);
    screensaver_refresh_shadow_controls();
}

#include "ui_settings_screensaver_color.inc"

static int screensaver_font_index_to_size(uint32_t index)
{
    static const int sizes[] = {
        APP_SETTINGS_SCREENSAVER_FONT_20,
        APP_SETTINGS_SCREENSAVER_FONT_40,
        APP_SETTINGS_SCREENSAVER_FONT_60,
        APP_SETTINGS_SCREENSAVER_FONT_80,
        APP_SETTINGS_SCREENSAVER_FONT_120,
        APP_SETTINGS_SCREENSAVER_FONT_160,
        APP_SETTINGS_SCREENSAVER_FONT_200,
    };

    if (index >= sizeof(sizes) / sizeof(sizes[0])) {
        return APP_SETTINGS_SCREENSAVER_FONT_80;
    }
    return sizes[index];
}

static uint32_t screensaver_font_size_to_index(int font_size)
{
    switch (font_size) {
    case APP_SETTINGS_SCREENSAVER_FONT_20:
        return 0;
    case APP_SETTINGS_SCREENSAVER_FONT_40:
        return 1;
    case APP_SETTINGS_SCREENSAVER_FONT_60:
        return 2;
    case APP_SETTINGS_SCREENSAVER_FONT_80:
        return 3;
    case APP_SETTINGS_SCREENSAVER_FONT_120:
        return 4;
    case APP_SETTINGS_SCREENSAVER_FONT_160:
        return 5;
    case APP_SETTINGS_SCREENSAVER_FONT_200:
        return 6;
    default:
        return 0;
    }
}

static void screensaver_style_font_dropdown_list(lv_obj_t *dd)
{
    lv_obj_t *list = lv_dropdown_get_list(dd);

    if (!list) {
        return;
    }
    lv_obj_set_style_bg_color(list, THEME_BG_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(list, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(list, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_text_font(list, &APP_FONT, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, THEME_BORDER_SECONDARY, LV_PART_MAIN);
}

static void on_screensaver_font_dropdown_event(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CLICKED || code == LV_EVENT_VALUE_CHANGED) {
        screensaver_style_font_dropdown_list(dd);
    }
}

static lv_obj_t *create_screensaver_font_dropdown(lv_obj_t *parent,
                                                  app_screensaver_component_t component)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, 104);
    lv_obj_set_height(dd, 42);
    lv_dropdown_set_options(dd, "20\n40\n60\n80\n120\n160\n200");
    lv_dropdown_set_selected(
        dd,
        screensaver_font_size_to_index(
            app_settings_get_screensaver_component_font_size(component)));
    lv_obj_set_style_text_font(dd, &APP_FONT, 0);
    lv_obj_set_style_text_color(dd, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd, THEME_BG_CARD, 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dd, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 10, 0);
    lv_obj_add_event_cb(dd, on_screensaver_font_dropdown_event, LV_EVENT_ALL, NULL);
    screensaver_style_font_dropdown_list(dd);
    s_screensaver_font_dropdowns[component] = dd;
    lv_obj_null_on_delete(&s_screensaver_font_dropdowns[component]);
    return dd;
}

static void create_screensaver_position_row(lv_obj_t *parent,
                                            app_screensaver_component_t component)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row,
                      component == APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE ||
                              component == APP_SETTINGS_SCREENSAVER_COMPONENT_DATE
                          ? 56
                          : 50);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    screensaver_create_visible_checkbox(row, component);
    screensaver_create_component_name_button(row, component);

    create_label(row, "X", THEME_TEXT_TERTIARY);
    create_screensaver_coord_input(row,
                                   app_settings_get_screensaver_component_x(component),
                                   &s_screensaver_pos_tas[component][0]);
    create_label(row, "Y", THEME_TEXT_TERTIARY);
    create_screensaver_coord_input(row,
                                   app_settings_get_screensaver_component_y(component),
                                   &s_screensaver_pos_tas[component][1]);
    if (component == APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE) {
        lv_obj_t *spacer_label = create_label(row, "", THEME_TEXT_TERTIARY);
        lv_obj_set_width(spacer_label, 36);
        lv_obj_t *spacer_value = lv_obj_create(row);
        lv_obj_set_size(spacer_value, 104, 42);
        lv_obj_set_style_bg_opa(spacer_value, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(spacer_value, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(spacer_value, 0, 0);
        lv_obj_clear_flag(spacer_value, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        create_label(row, "字号", THEME_TEXT_TERTIARY);
        create_screensaver_font_dropdown(row, component);
    }
}

static bool screensaver_parse_position_input(app_screensaver_component_t component,
                                             int *out_x,
                                             int *out_y)
{
    bool x_ok = false;
    bool y_ok = false;
    int x;
    int y;

    if ((int)component < 0 || component >= APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT ||
        !out_x || !out_y) {
        return false;
    }
    x = settings_parse_int_text(s_screensaver_pos_tas[component][0]
                                    ? lv_textarea_get_text(s_screensaver_pos_tas[component][0])
                                    : NULL,
                                &x_ok);
    y = settings_parse_int_text(s_screensaver_pos_tas[component][1]
                                    ? lv_textarea_get_text(s_screensaver_pos_tas[component][1])
                                    : NULL,
                                &y_ok);
    if (!x_ok || !y_ok ||
        x < APP_SETTINGS_SCREENSAVER_COORD_MIN ||
        x > APP_SETTINGS_SCREENSAVER_COORD_MAX ||
        y < APP_SETTINGS_SCREENSAVER_COORD_MIN ||
        y > APP_SETTINGS_SCREENSAVER_COORD_MAX) {
        return false;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static void screensaver_refresh_position_inputs(void)
{
    char text[16];

    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        if (!screensaver_component_visible_in_settings((app_screensaver_component_t)i)) {
            continue;
        }
        snprintf(text, sizeof(text), "%d", app_settings_get_screensaver_component_x(i));
        if (s_screensaver_pos_tas[i][0]) {
            lv_textarea_set_text(s_screensaver_pos_tas[i][0], text);
        }
        snprintf(text, sizeof(text), "%d", app_settings_get_screensaver_component_y(i));
        if (s_screensaver_pos_tas[i][1]) {
            lv_textarea_set_text(s_screensaver_pos_tas[i][1], text);
        }
        if (s_screensaver_font_dropdowns[i]) {
            lv_dropdown_set_selected(
                s_screensaver_font_dropdowns[i],
                screensaver_font_size_to_index(
                    app_settings_get_screensaver_component_font_size(
                        (app_screensaver_component_t)i)));
        }
    }
    screensaver_refresh_component_controls();
    screensaver_refresh_shadow_controls();
}

static esp_err_t screensaver_save_position_inputs(void)
{
    esp_err_t err = ESP_OK;

    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        int x;
        int y;
        esp_err_t step;

        if (!screensaver_component_visible_in_settings((app_screensaver_component_t)i)) {
            continue;
        }
        if (!screensaver_parse_position_input((app_screensaver_component_t)i, &x, &y)) {
            return ESP_ERR_INVALID_ARG;
        }
        step = app_settings_set_screensaver_component_position((app_screensaver_component_t)i,
                                                               x,
                                                               y);
        if (step != ESP_OK) {
            err = step;
        }
        if (i != APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE) {
            step = app_settings_set_screensaver_component_font_size(
                (app_screensaver_component_t)i,
                screensaver_font_index_to_size(s_screensaver_font_dropdowns[i]
                                                   ? lv_dropdown_get_selected(
                                                         s_screensaver_font_dropdowns[i])
                                                   : 0));
            if (step != ESP_OK) {
                err = step;
            }
        }
    }
    if (screensaver_save_component_controls() != ESP_OK) {
        err = ESP_FAIL;
    }
    return err;
}

static void on_screensaver_position_reset_click(lv_event_t *e)
{
    (void)e;
    if (app_settings_reset_screensaver_component_positions() != ESP_OK) {
        set_screensaver_status("屏保元素布局复位失败。", THEME_DANGER_LIGHT);
        return;
    }
    screensaver_refresh_position_inputs();
    screensaver_service_reload_settings();
    set_screensaver_status("屏保元素布局已复位。", THEME_SUCCESS_LIGHT);
}

static void on_screensaver_save_click(lv_event_t *e)
{
    bool idle_ok = false;
    bool switch_ok = false;
    int idle_minutes =
        settings_parse_int_text(s_screensaver_idle_ta ? lv_textarea_get_text(s_screensaver_idle_ta) : NULL,
                                &idle_ok);
    int switch_minutes =
        settings_parse_int_text(s_screensaver_switch_ta ? lv_textarea_get_text(s_screensaver_switch_ta) : NULL,
                                &switch_ok);
    const char *dir = s_screensaver_dir_pending;
    (void)e;

    close_screensaver_dir_picker();
    if (!idle_ok ||
        idle_minutes < APP_SETTINGS_MIN_SCREENSAVER_IDLE_MINUTES ||
        idle_minutes > APP_SETTINGS_MAX_SCREENSAVER_IDLE_MINUTES) {
        set_screensaver_status_range("待机时间有效范围：%d-%d 分钟。",
                                     APP_SETTINGS_MIN_SCREENSAVER_IDLE_MINUTES,
                                     APP_SETTINGS_MAX_SCREENSAVER_IDLE_MINUTES,
                                     THEME_DANGER_LIGHT);
        return;
    }
    if (!switch_ok ||
        switch_minutes < APP_SETTINGS_MIN_SCREENSAVER_SWITCH_MINUTES ||
        switch_minutes > APP_SETTINGS_MAX_SCREENSAVER_SWITCH_MINUTES) {
        set_screensaver_status_range("图片切换间隔有效范围：%d-%d 分钟。",
                                     APP_SETTINGS_MIN_SCREENSAVER_SWITCH_MINUTES,
                                     APP_SETTINGS_MAX_SCREENSAVER_SWITCH_MINUTES,
                                     THEME_DANGER_LIGHT);
        return;
    }
    if (!dir || dir[0] != '/') {
        set_screensaver_status("请选择壁纸目录。", THEME_DANGER_LIGHT);
        return;
    }
    bool shadow_ok = false;
    int shadow_length =
        settings_parse_int_text(s_screensaver_shadow_length_ta
                                    ? lv_textarea_get_text(s_screensaver_shadow_length_ta)
                                    : NULL,
                                &shadow_ok);
    bool shadow_enabled = s_screensaver_shadow_checkbox &&
                          lv_obj_has_state(s_screensaver_shadow_checkbox, LV_STATE_CHECKED);
    bool disable_during_ssh = s_screensaver_disable_ssh_checkbox &&
                              lv_obj_has_state(s_screensaver_disable_ssh_checkbox,
                                               LV_STATE_CHECKED);
    if (!shadow_ok ||
        shadow_length < APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MIN ||
        shadow_length > APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MAX) {
        set_screensaver_status_range("文字阴影长度有效范围：%d-%d。",
                                     APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MIN,
                                     APP_SETTINGS_SCREENSAVER_SHADOW_LENGTH_MAX,
                                     THEME_DANGER_LIGHT);
        return;
    }
    if (screensaver_save_position_inputs() != ESP_OK) {
        set_screensaver_status("屏保元素坐标有效范围：X/Y -9999 到 9999。", THEME_DANGER_LIGHT);
        return;
    }
    if (app_settings_set_screensaver_mode(s_screensaver_mode_pending) != ESP_OK ||
        app_settings_set_screensaver_idle_minutes(idle_minutes) != ESP_OK ||
        app_settings_set_screensaver_wallpaper_dir(dir) != ESP_OK ||
        app_settings_set_screensaver_order(s_screensaver_order_pending) != ESP_OK ||
        app_settings_set_screensaver_switch_minutes(switch_minutes) != ESP_OK ||
        app_settings_set_screensaver_disable_during_ssh(disable_during_ssh) != ESP_OK ||
        app_settings_set_screensaver_text_shadow_enabled(shadow_enabled) != ESP_OK ||
        app_settings_set_screensaver_text_shadow_length(shadow_length) != ESP_OK) {
        set_screensaver_status("屏保设置保存失败。", THEME_DANGER_LIGHT);
        return;
    }
    screensaver_service_reload_settings();
    set_screensaver_status("屏保设置已保存。长按标题、时间、秒钟、日期或农历可拖动位置，松手自动保存。",
                           THEME_SUCCESS_LIGHT);
}

static void render_screensaver_settings_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    apply_surface(card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_screensaver_mode_pending = app_settings_get_screensaver_mode();
    s_screensaver_order_pending = app_settings_get_screensaver_order();
    screensaver_dir_set_pending(app_settings_get_screensaver_wallpaper_dir());

    create_label(card, TRS("待机屏保"), THEME_TEXT_SECONDARY);
    create_wrap_label(card,
                      TRS("达到待机时间后自动熄屏或显示壁纸；主界面左上角标题可手动进入屏保。"),
                      THEME_TEXT_TERTIARY);

    lv_obj_t *mode_row = create_screensaver_segment_row(card);
    s_screensaver_mode_buttons[APP_SETTINGS_SCREENSAVER_MODE_OFF] =
        create_screensaver_segment_button(mode_row,
                                          "关闭",
                                          s_screensaver_mode_pending == APP_SETTINGS_SCREENSAVER_MODE_OFF,
                                          on_screensaver_mode_click,
                                          (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_MODE_OFF);
    s_screensaver_mode_buttons[APP_SETTINGS_SCREENSAVER_MODE_BLANK] =
        create_screensaver_segment_button(mode_row,
                                          "熄屏",
                                          s_screensaver_mode_pending == APP_SETTINGS_SCREENSAVER_MODE_BLANK,
                                          on_screensaver_mode_click,
                                          (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_MODE_BLANK);
    s_screensaver_mode_buttons[APP_SETTINGS_SCREENSAVER_MODE_WALLPAPER] =
        create_screensaver_segment_button(mode_row,
                                          "显示壁纸",
                                          s_screensaver_mode_pending == APP_SETTINGS_SCREENSAVER_MODE_WALLPAPER,
                                          on_screensaver_mode_click,
                                          (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_MODE_WALLPAPER);

    create_number_input_row(card,
                            "待机时间（分钟）",
                            app_settings_get_screensaver_idle_minutes(),
                            &s_screensaver_idle_ta);
    create_screensaver_disable_ssh_row(card);
    create_screensaver_dir_picker_row(card,
                                      "壁纸目录",
                                      s_screensaver_dir_pending);

    lv_obj_t *order_row = create_screensaver_segment_row(card);
    s_screensaver_order_buttons[APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL] =
        create_screensaver_segment_button(order_row,
                                          "顺序切换",
                                          s_screensaver_order_pending ==
                                              APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL,
                                          on_screensaver_order_click,
                                          (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_ORDER_SEQUENTIAL);
    s_screensaver_order_buttons[APP_SETTINGS_SCREENSAVER_ORDER_RANDOM] =
        create_screensaver_segment_button(order_row,
                                          "随机切换",
                                          s_screensaver_order_pending ==
                                              APP_SETTINGS_SCREENSAVER_ORDER_RANDOM,
                                          on_screensaver_order_click,
                                          (void *)(uintptr_t)APP_SETTINGS_SCREENSAVER_ORDER_RANDOM);

    create_number_input_row(card,
                            "切换间隔（分钟）",
                            app_settings_get_screensaver_switch_minutes(),
                            &s_screensaver_switch_ta);

    create_wrap_label(card,
                      "元素坐标 X/Y 表示元素左上角距离屏幕左上角的像素位置，可按实际布局手工调整。点击名称可修改颜色。",
                      THEME_TEXT_TERTIARY);
    create_screensaver_position_row(card, APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE);
    create_screensaver_position_row(card, APP_SETTINGS_SCREENSAVER_COMPONENT_TIME);
    create_screensaver_position_row(card, APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND);
    create_screensaver_position_row(card, APP_SETTINGS_SCREENSAVER_COMPONENT_DATE);
    if (screensaver_component_visible_in_settings(APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR)) {
        create_screensaver_position_row(card, APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR);
    }
    create_screensaver_shadow_row(card);

    lv_obj_t *reset_btn = lv_button_create(card);
    lv_obj_set_width(reset_btn, LV_PCT(100));
    lv_obj_set_height(reset_btn, 46);
    lv_obj_set_style_bg_color(reset_btn, THEME_BG_HOVER, 0);
    lv_obj_set_style_bg_opa(reset_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(reset_btn, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(reset_btn, 1, 0);
    lv_obj_set_style_radius(reset_btn, 12, 0);
    lv_obj_add_event_cb(reset_btn, on_screensaver_position_reset_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = create_label(reset_btn, "复位屏保元素布局", THEME_TEXT_PRIMARY);
    lv_obj_center(reset_label);

    lv_obj_t *save_btn = lv_button_create(card);
    lv_obj_set_width(save_btn, LV_PCT(100));
    lv_obj_set_height(save_btn, 50);
    lv_obj_set_style_bg_color(save_btn, THEME_PRIMARY, 0);
    lv_obj_set_style_bg_opa(save_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    lv_obj_set_style_radius(save_btn, 12, 0);
    lv_obj_add_event_cb(save_btn, on_screensaver_save_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_label = create_label(save_btn, TRS(LV_SYMBOL_SAVE " 保存屏保设置"), THEME_TEXT_PRIMARY);
    lv_obj_center(save_label);

    s_screensaver_status_label = create_wrap_label(card, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_screensaver_status_label);
}

static void render_screensaver_settings_page(lv_obj_t *parent)
{
    create_label(parent, "待机屏保", THEME_TEXT_PRIMARY);
    render_screensaver_settings_card(parent);
}
