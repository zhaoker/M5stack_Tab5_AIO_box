static const char *fm_volume_root(fm_volume_t volume)
{
    switch (volume) {
    case FM_VOLUME_TF:
        return tf_card_get_mount_point();
    case FM_VOLUME_USB:
        return usb_storage_get_mount_point();
    case FM_VOLUME_NONE:
    default:
        return "";
    }
}

static const char *fm_volume_title(fm_volume_t volume)
{
    switch (volume) {
    case FM_VOLUME_TF:
        return "TF 卡";
    case FM_VOLUME_USB:
        return "U盘";
    case FM_VOLUME_NONE:
    default:
        return "存储器";
    }
}

static bool fm_volume_is_mounted(fm_volume_t volume)
{
    switch (volume) {
    case FM_VOLUME_TF:
        return tf_card_is_mounted() && !tf_usb_disk_mode_blocks_tf_access();
    case FM_VOLUME_USB:
        return usb_storage_is_mounted();
    case FM_VOLUME_NONE:
    default:
        return false;
    }
}

static bool fm_path_in_known_volume(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    if (tf_card_is_mounted() &&
        !tf_usb_disk_mode_blocks_tf_access() &&
        file_manager_path_in_root(path, tf_card_get_mount_point())) {
        return true;
    }
    if (usb_storage_is_mounted() &&
        file_manager_path_in_root(path, usb_storage_get_mount_point())) {
        return true;
    }
    return false;
}

static bool fm_path_is_volume_root(const char *path)
{
    return path && path[0] &&
           (strcmp(path, tf_card_get_mount_point()) == 0 ||
            strcmp(path, usb_storage_get_mount_point()) == 0);
}

static fm_volume_t __attribute__((unused)) fm_volume_for_path(const char *path)
{
    if (!path || !path[0]) {
        return FM_VOLUME_NONE;
    }
    if (file_manager_path_in_root(path, tf_card_get_mount_point())) {
        return FM_VOLUME_TF;
    }
    if (file_manager_path_in_root(path, usb_storage_get_mount_point())) {
        return FM_VOLUME_USB;
    }
    return FM_VOLUME_NONE;
}

static void fm_set_storage_picker(bool active)
{
    s_storage_picker_active = active;
    if (active) {
        s_current_volume = FM_VOLUME_NONE;
        s_root_path[0] = '\0';
        s_current_path[0] = '\0';
        s_page = 0;
        s_selected_index = 0;
        s_all_active = false;
        s_selection_status_active = false;
    }
}

static void fm_prepare_default_storage_view(void)
{
    if (usb_storage_is_mounted()) {
        fm_set_storage_picker(true);
        return;
    }

    s_storage_picker_active = false;
    s_current_volume = FM_VOLUME_TF;
    strlcpy(s_root_path, tf_card_get_mount_point(), sizeof(s_root_path));
    strlcpy(s_current_path, tf_card_get_mount_point(), sizeof(s_current_path));
    s_page = 0;
    s_selected_index = 0;
    s_all_active = false;
    s_selection_status_active = false;
}

static void fm_enter_volume(fm_volume_t volume)
{
    const char *root = fm_volume_root(volume);

    if (!root || !root[0]) {
        fm_set_status(TRS("存储器不可用"), THEME_DANGER_LIGHT);
        return;
    }
    if (!fm_volume_is_mounted(volume)) {
        fm_set_status(volume == FM_VOLUME_USB ? TRS("U盘未挂载") : TRS("TF卡未挂载"),
                      THEME_WARNING_LIGHT);
        return;
    }

    s_storage_picker_active = false;
    s_current_volume = volume;
    strlcpy(s_root_path, root, sizeof(s_root_path));
    strlcpy(s_current_path, root, sizeof(s_current_path));
    s_page = 0;
    s_selected_index = 0;
    s_all_active = false;
    s_selection_status_active = false;
    fm_render();
}

static void fm_leave_to_storage_picker(void)
{
    fm_set_storage_picker(true);
    fm_render();
}

static void fm_clear_clipboard_if_volume_path(fm_volume_t volume)
{
    const char *root = fm_volume_root(volume);

    if (!root || !root[0] || s_clipboard_mode == FM_CLIPBOARD_NONE) {
        return;
    }
    if ((s_clipboard_path[0] && file_manager_path_in_root(s_clipboard_path, root)) ||
        (s_clipboard_dir[0] && file_manager_path_in_root(s_clipboard_dir, root))) {
        fm_clear_clipboard();
    }
}

static const char *fm_usb_state_text(usb_storage_state_t state)
{
    switch (state) {
    case USB_STORAGE_STATE_OFF:
        return "未挂载";
    case USB_STORAGE_STATE_STARTING:
        return "启动中";
    case USB_STORAGE_STATE_WAITING_DEVICE:
        return "等待U盘";
    case USB_STORAGE_STATE_MOUNTED:
        return "已挂载";
    case USB_STORAGE_STATE_UNMOUNTING:
        return "卸载中";
    case USB_STORAGE_STATE_ERROR:
        return "挂载失败";
    default:
        return "未知";
    }
}

static void fm_format_volume_space(fm_volume_t volume, char *out, size_t out_len)
{
    uint64_t total = 0;
    uint64_t free = 0;
    char total_text[32];
    char free_text[32];

    if (!out || out_len == 0) {
        return;
    }
    if (volume == FM_VOLUME_TF) {
        if (esp_vfs_fat_info(tf_card_get_mount_point(), &total, &free) != ESP_OK) {
            snprintf(out, out_len, "%s", TRS("未挂载"));
            return;
        }
    } else if (volume == FM_VOLUME_USB) {
        usb_storage_status_t status;
        usb_storage_get_status(&status);
        if (!status.mounted || status.total_bytes == 0) {
            snprintf(out, out_len, "%s", TRS("未挂载"));
            return;
        }
        total = status.total_bytes;
        free = status.free_bytes;
    } else {
        snprintf(out, out_len, "%s", TRS("未选择"));
        return;
    }

    file_manager_format_size_fixed2(total, total_text, sizeof(total_text));
    file_manager_format_size_fixed2(free, free_text, sizeof(free_text));
    snprintf(out, out_len, TRS("总计 %s，可用 %s"), total_text, free_text);
}

static void fm_on_storage_card_click(lv_event_t *e)
{
    fm_volume_t volume = (fm_volume_t)(uintptr_t)lv_event_get_user_data(e);
    fm_enter_volume(volume);
}

static void fm_create_storage_card(fm_volume_t volume,
                                   const char *symbol,
                                   const char *title,
                                   const char *status,
                                   lv_color_t accent,
                                   bool enabled,
                                   int32_t card_w)
{
    lv_obj_t *card = lv_obj_create(s_list);
    lv_obj_t *icon_box;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *meta;
    lv_obj_t *hint;
    int32_t text_w = card_w - 96;

    if (text_w < 120) {
        text_w = 120;
    }
    lv_obj_set_size(card, card_w, 118);
    fm_style_surface(card, enabled ? THEME_BG_CARD : THEME_BG_PRIMARY,
                     enabled ? accent : THEME_BORDER_SECONDARY, 8);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (enabled) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, fm_on_storage_card_click, LV_EVENT_CLICKED, (void *)(uintptr_t)volume);
    } else {
        lv_obj_set_style_opa(card, LV_OPA_70, 0);
    }

    icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, 58, 58);
    fm_style_surface(icon_box, THEME_BG_HOVER, enabled ? accent : THEME_BORDER_SECONDARY, 8);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);

    icon = fm_label(icon_box, symbol, enabled ? accent : THEME_TEXT_TERTIARY);
    lv_obj_center(icon);

    name = fm_label(card, title, THEME_TEXT_PRIMARY);
    lv_obj_set_width(name, text_w);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 78, 2);

    meta = fm_label(card, status, enabled ? THEME_TEXT_SECONDARY : THEME_WARNING_LIGHT);
    lv_obj_set_width(meta, text_w);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_align(meta, LV_ALIGN_TOP_LEFT, 78, 40);

    hint = fm_label(card, enabled ? TRS("点击打开") : TRS("不可用"), THEME_TEXT_TERTIARY);
    lv_obj_set_width(hint, text_w);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 78, 74);
}

static void fm_render_storage_picker(void)
{
    int32_t screen_w;
    int32_t card_w;
    char tf_status[96];
    char usb_status[96];
    usb_storage_status_t usb_status_info;
    bool tf_ready = tf_card_is_mounted() && !tf_usb_disk_mode_blocks_tf_access();
    bool usb_ready;

    lv_obj_clean(s_list);
    if (s_title_label) {
        lv_label_set_text_fmt(s_title_label, LV_SYMBOL_DRIVE " %s", TRS("文件管理器"));
    }
    if (s_path_label) {
        lv_label_set_text(s_path_label, TRS("选择存储器"));
    }
    if (s_count_label) {
        lv_label_set_text(s_count_label, TRS("2 个存储器"));
    }
    fm_update_navigation_controls();

    lv_obj_update_layout(s_list);
    screen_w = lv_obj_get_content_width(s_list);
    if (screen_w <= 0) {
        screen_w = lv_display_get_horizontal_resolution(NULL) - FM_PAD * 2;
    }
    card_w = (screen_w - FM_GAP - 2) / 2;
    if (card_w < 220) {
        card_w = 220;
    }

    if (tf_ready) {
        fm_format_volume_space(FM_VOLUME_TF, tf_status, sizeof(tf_status));
    } else if (tf_usb_disk_mode_blocks_tf_access()) {
        snprintf(tf_status, sizeof(tf_status), "%s", TRS("TF卡正在USB磁盘模式中"));
    } else {
        snprintf(tf_status, sizeof(tf_status), "%s", TRS("TF卡未挂载"));
    }

    memset(&usb_status_info, 0, sizeof(usb_status_info));
    usb_storage_get_status(&usb_status_info);
    usb_ready = usb_status_info.mounted;
    if (usb_ready) {
        fm_format_volume_space(FM_VOLUME_USB, usb_status, sizeof(usb_status));
    } else if (usb_status_info.message[0]) {
        snprintf(usb_status, sizeof(usb_status), "%s", TRS(usb_status_info.message));
    } else {
        snprintf(usb_status, sizeof(usb_status), "%s", TRS(fm_usb_state_text(usb_status_info.state)));
    }

    fm_create_storage_card(FM_VOLUME_TF,
                           LV_SYMBOL_SD_CARD,
                           TRS("TF 卡"),
                           tf_status,
                           THEME_ACCENT_CYAN,
                           tf_ready,
                           card_w);
    fm_create_storage_card(FM_VOLUME_USB,
                           LV_SYMBOL_USB,
                           TRS("U盘"),
                           usb_status,
                           THEME_SUCCESS_LIGHT,
                           usb_ready,
                           card_w);
    fm_update_folder_status();
}

static void fm_stop_usb_dependents(void)
{
    audio_player_status_t audio_status;

    if (s_current_volume == FM_VOLUME_USB) {
        fm_close_viewer();
        fm_close_text_viewer();
        fm_close_audio_player();
        fm_leave_to_storage_picker();
    }
    fm_clear_clipboard_if_volume_path(FM_VOLUME_USB);

    if (audio_player_get_status(&audio_status) == ESP_OK &&
        audio_status.path[0] &&
        file_manager_path_in_root(audio_status.path, usb_storage_get_mount_point())) {
        (void)audio_player_stop();
    }
}

void ui_file_manager_prepare_usb_unmount(void)
{
    if (!s_overlay) {
        audio_player_status_t audio_status;

        if (audio_player_get_status(&audio_status) == ESP_OK &&
            audio_status.path[0] &&
            file_manager_path_in_root(audio_status.path, usb_storage_get_mount_point())) {
            (void)audio_player_stop();
        }
        return;
    }
    fm_stop_usb_dependents();
}

static void fm_poll_usb_status(void)
{
    usb_storage_status_t status;

    memset(&status, 0, sizeof(status));
    usb_storage_get_status(&status);
    if (status.generation == s_usb_status_generation) {
        return;
    }
    s_usb_status_generation = status.generation;

    if (s_current_volume == FM_VOLUME_USB && !status.mounted) {
        fm_close_viewer();
        fm_close_text_viewer();
        fm_close_audio_player();
        fm_prepare_default_storage_view();
        fm_render();
        fm_set_status(status.message[0] ? TRS(status.message) : TRS("U盘已移除"),
                      THEME_WARNING_LIGHT);
        return;
    }
    if (s_storage_picker_active && !status.mounted) {
        fm_prepare_default_storage_view();
        fm_render();
        return;
    }
    if (s_storage_picker_active || s_current_volume == FM_VOLUME_USB) {
        fm_render();
    }
}
