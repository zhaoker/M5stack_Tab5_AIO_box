static void format_tf_usage_text(char *out, size_t out_len)
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    char used_text[32];
    char total_text[32];

    if (!out || out_len == 0) {
        return;
    }
    if (tf_usb_disk_mode_blocks_tf_access()) {
        snprintf(out, out_len, "%s", TRS("TF卡正在USB磁盘模式中"));
        return;
    }
    if (!tf_card_is_mounted() ||
        esp_vfs_fat_info(tf_card_get_mount_point(), &total_bytes, &free_bytes) != ESP_OK ||
        total_bytes == 0) {
        snprintf(out, out_len, TRS("未挂载"));
        return;
    }

    format_storage_size(total_bytes > free_bytes ? total_bytes - free_bytes : 0, used_text, sizeof(used_text));
    format_storage_size(total_bytes, total_text, sizeof(total_text));
    snprintf(out, out_len, TRS("已用 %s / 总量 %s"), used_text, total_text);
}

static const char *settings_usb_state_text(usb_storage_state_t state)
{
    switch (state) {
    case USB_STORAGE_STATE_STARTING:
        return "启动中";
    case USB_STORAGE_STATE_WAITING_DEVICE:
        return "等待U盘";
    case USB_STORAGE_STATE_MOUNTED:
        return "已挂载";
    case USB_STORAGE_STATE_UNMOUNTING:
        return "卸载中";
    case USB_STORAGE_STATE_FORMATTING:
        return "格式化中";
    case USB_STORAGE_STATE_ERROR:
        return "挂载失败";
    case USB_STORAGE_STATE_OFF:
    default:
        return "未挂载";
    }
}

static void format_usb_fs_text(const usb_storage_status_t *status, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (status && status->mounted) {
        const char *fs = status->fs_type[0] && strcmp(status->fs_type, "unknown") != 0
                             ? status->fs_type
                             : TRS("未知");
        snprintf(out, out_len, TRS("文件系统：%s"), fs);
    } else if (status && status->format_available) {
        snprintf(out,
                 out_len,
                 "%s",
                 TRS("U盘未格式化或文件系统不支持，可选择格式化为 FAT32 或 exFAT。"));
    } else if (status && status->message[0]) {
        snprintf(out, out_len, "%s", TRS(status->message));
    } else {
        snprintf(out, out_len, "%s", TRS(settings_usb_state_text(status ? status->state : USB_STORAGE_STATE_OFF)));
    }
}

static void format_usb_usage_text(const usb_storage_status_t *status, char *out, size_t out_len)
{
    char used_text[32];
    char total_text[32];

    if (!out || out_len == 0) {
        return;
    }
    if (status && status->mounted && status->total_bytes > 0) {
        uint64_t used = status->total_bytes > status->free_bytes ? status->total_bytes - status->free_bytes : 0;
        format_storage_size(used, used_text, sizeof(used_text));
        format_storage_size(status->total_bytes, total_text, sizeof(total_text));
        snprintf(out, out_len, TRS("已用 %s / 总量 %s"), used_text, total_text);
    } else if (status && status->message[0]) {
        snprintf(out, out_len, "%s", TRS(status->message));
    } else {
        snprintf(out, out_len, "%s", TRS(settings_usb_state_text(status ? status->state : USB_STORAGE_STATE_OFF)));
    }
}

static void update_usb_storage_status(void)
{
    usb_storage_status_t status;
    char value[96];
    bool busy;
    bool can_unmount;
    bool can_format;

    memset(&status, 0, sizeof(status));
    usb_storage_get_status(&status);
    busy = status.state == USB_STORAGE_STATE_STARTING ||
           status.state == USB_STORAGE_STATE_UNMOUNTING ||
           status.state == USB_STORAGE_STATE_FORMATTING;
    can_unmount = status.mounted ||
                  status.state == USB_STORAGE_STATE_WAITING_DEVICE ||
                  status.state == USB_STORAGE_STATE_ERROR;
    can_format = (status.mounted || status.format_available) && !busy;
    format_usb_usage_text(&status, value, sizeof(value));

    if (s_usb_storage_value_label) {
        lv_label_set_text(s_usb_storage_value_label, value);
        lv_obj_set_style_text_color(s_usb_storage_value_label,
                                    status.mounted ? THEME_TEXT_PRIMARY : THEME_WARNING_LIGHT,
                                    0);
    }
    if (s_usb_storage_status_label) {
        format_usb_fs_text(&status, value, sizeof(value));
        lv_label_set_text(s_usb_storage_status_label, value);
        lv_obj_set_style_text_color(s_usb_storage_status_label,
                                    status.mounted ? THEME_SUCCESS_LIGHT :
                                    busy ? THEME_WARNING_LIGHT : THEME_TEXT_TERTIARY,
                                    0);
    }
    if (s_usb_format_button) {
        if (can_format) {
            lv_obj_remove_state(s_usb_format_button, LV_STATE_DISABLED);
            lv_obj_remove_flag(s_usb_format_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_state(s_usb_format_button, LV_STATE_DISABLED);
            lv_obj_add_flag(s_usb_format_button, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_usb_format_button_label) {
            lv_label_set_text_fmt(s_usb_format_button_label,
                                  "%s %s",
                                  LV_SYMBOL_WARNING,
                                  TRS(status.mounted ? "重新格式化" : "格式化U盘"));
        }
    }
    if (!s_usb_storage_button) {
        return;
    }
    if (busy) {
        lv_obj_add_state(s_usb_storage_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_usb_storage_button, THEME_WARNING_BG, 0);
        if (s_usb_storage_button_label) {
            lv_label_set_text(s_usb_storage_button_label, LV_SYMBOL_USB " ...");
        }
    } else {
        lv_obj_remove_state(s_usb_storage_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_usb_storage_button,
                                  can_unmount ? (status.mounted ? THEME_SUCCESS_BG : THEME_WARNING_BG)
                                              : THEME_BG_HOVER,
                                  0);
        if (s_usb_storage_button_label) {
            lv_label_set_text_fmt(s_usb_storage_button_label,
                                  "%s %s",
                                  LV_SYMBOL_USB,
                                  TRS(can_unmount ? "卸载" : "挂载"));
        }
    }
}

static void on_usb_storage_button(lv_event_t *e)
{
    usb_storage_status_t status;
    esp_err_t err;

    (void)e;
    memset(&status, 0, sizeof(status));
    usb_storage_get_status(&status);
    if (status.mounted || status.state == USB_STORAGE_STATE_WAITING_DEVICE ||
        status.state == USB_STORAGE_STATE_ERROR) {
        ui_file_manager_prepare_usb_unmount();
        err = usb_storage_unmount();
        if (s_usb_storage_status_label) {
            lv_label_set_text(s_usb_storage_status_label,
                              err == ESP_OK ? TRS("正在卸载U盘") : TRS("U盘卸载失败"));
        }
    } else {
        err = usb_storage_mount();
        if (s_usb_storage_status_label) {
            lv_label_set_text(s_usb_storage_status_label,
                              err == ESP_OK ? TRS("正在挂载U盘") : TRS("U盘挂载失败"));
        }
    }
    update_usb_storage_status();
}

static void close_usb_format_dialog(void)
{
    if (s_usb_format_overlay) {
        lv_obj_del(s_usb_format_overlay);
        s_usb_format_overlay = NULL;
    }
}

static void on_usb_format_cancel(lv_event_t *e)
{
    (void)e;
    close_usb_format_dialog();
}

static lv_obj_t *create_usb_format_dialog_button(lv_obj_t *parent,
                                                 const char *text,
                                                 lv_color_t bg,
                                                 lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, 150);
    lv_obj_set_height(btn, 50);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *label = create_label(btn, text, THEME_TEXT_PRIMARY);
    lv_obj_center(label);
    return btn;
}

static void on_usb_format_choice(lv_event_t *e)
{
    usb_storage_format_t format = (usb_storage_format_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err;

    close_usb_format_dialog();
    ui_file_manager_prepare_usb_unmount();
    if (web_file_manager_is_running()) {
        web_file_manager_stop();
    }
    err = usb_storage_format(format);
    if (s_usb_storage_status_label) {
        lv_label_set_text(s_usb_storage_status_label,
                          err == ESP_OK ? TRS("正在格式化U盘") : TRS("U盘格式化失败"));
        lv_obj_set_style_text_color(s_usb_storage_status_label,
                                    err == ESP_OK ? THEME_WARNING_LIGHT : THEME_DANGER_LIGHT,
                                    0);
    }
    update_usb_storage_status();
}

static void show_usb_format_dialog(void)
{
    usb_storage_status_t status;
    char fs_text[64];

    if (s_usb_format_overlay) {
        lv_obj_move_foreground(s_usb_format_overlay);
        return;
    }

    memset(&status, 0, sizeof(status));
    usb_storage_get_status(&status);
    if (!status.mounted && !status.format_available) {
        if (s_usb_storage_status_label) {
            lv_label_set_text(s_usb_storage_status_label, TRS("当前U盘无法格式化"));
            lv_obj_set_style_text_color(s_usb_storage_status_label, THEME_DANGER_LIGHT, 0);
        }
        return;
    }

    lv_obj_t *parent = s_overlay ? s_overlay : lv_scr_act();
    s_usb_format_overlay = lv_obj_create(parent);
    lv_obj_null_on_delete(&s_usb_format_overlay);
    lv_obj_set_size(s_usb_format_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_usb_format_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_usb_format_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_usb_format_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_usb_format_overlay, 0, 0);
    lv_obj_clear_flag(s_usb_format_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_usb_format_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_usb_format_overlay);
    lv_obj_set_size(panel, 520, LV_SIZE_CONTENT);
    apply_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 14);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    create_label(panel, "选择U盘格式", THEME_WARNING_LIGHT);
    create_wrap_label(panel, "格式化当前U盘会清空所有文件。", THEME_TEXT_SECONDARY);
    if (status.mounted && status.fs_type[0]) {
        snprintf(fs_text, sizeof(fs_text), TRS("当前文件系统：%s"), status.fs_type);
        create_wrap_label(panel, fs_text, THEME_TEXT_TERTIARY);
    } else if (status.format_available) {
        create_wrap_label(panel, "U盘未格式化或文件系统不支持，可选择格式化为 FAT32 或 exFAT。", THEME_TEXT_TERTIARY);
    }

    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, 50);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_style_pad_row(buttons, 10, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *exfat_btn = create_usb_format_dialog_button(buttons, "exFAT", THEME_WARNING, NULL);
    lv_obj_add_event_cb(exfat_btn,
                        on_usb_format_choice,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)USB_STORAGE_FORMAT_EXFAT);
    lv_obj_t *fat_btn = create_usb_format_dialog_button(buttons, "FAT32", THEME_WARNING, NULL);
    lv_obj_add_event_cb(fat_btn,
                        on_usb_format_choice,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)USB_STORAGE_FORMAT_FAT32);
    create_usb_format_dialog_button(buttons, "取消", THEME_BG_HOVER, on_usb_format_cancel);
}

static void on_usb_format_button(lv_event_t *e)
{
    (void)e;
    show_usb_format_dialog();
}

static void on_usb_storage_status_timer(lv_timer_t *timer)
{
    (void)timer;
    update_usb_storage_status();
}

static void update_file_manager_web_status(void)
{
    char url[96];
    char reason[96];
    bool running = web_file_manager_is_running();
    bool can_start = web_file_manager_can_start(reason, sizeof(reason));

    if (s_file_web_button) {
        if (running) {
            lv_obj_set_style_bg_color(s_file_web_button, THEME_DANGER, 0);
            lv_obj_remove_state(s_file_web_button, LV_STATE_DISABLED);
            if (s_file_web_button_label) {
                lv_label_set_text(s_file_web_button_label, TRS(LV_SYMBOL_STOP " 关闭"));
            }
        } else {
            lv_obj_set_style_bg_color(s_file_web_button, can_start ? THEME_PRIMARY : THEME_BG_HOVER, 0);
            if (can_start) {
                lv_obj_remove_state(s_file_web_button, LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(s_file_web_button, LV_STATE_DISABLED);
            }
            if (s_file_web_button_label) {
                lv_label_set_text(s_file_web_button_label, TRS(LV_SYMBOL_PLAY " 开启"));
            }
        }
    }

    if (!s_file_web_status) {
        return;
    }
    if (running && web_file_manager_get_url(url, sizeof(url))) {
        lv_label_set_text_fmt(s_file_web_status, TRS("在线文件管理器运行中：%s"), url);
        lv_obj_set_style_text_color(s_file_web_status, THEME_SUCCESS_LIGHT, 0);
    } else if (can_start) {
        lv_label_set_text(s_file_web_status, TRS("WIFI已连接，可开启在线文件管理器"));
        lv_obj_set_style_text_color(s_file_web_status, THEME_TEXT_SECONDARY, 0);
    } else {
        lv_label_set_text_fmt(s_file_web_status,
                              TRS("在线文件管理器不可用：%s"),
                              reason[0] ? reason : TRS("未就绪"));
        lv_obj_set_style_text_color(s_file_web_status, THEME_WARNING_LIGHT, 0);
    }
}

static void update_tf_usb_disk_status(void)
{
    tf_usb_disk_mode_status_t status;
    bool busy;
    bool can_start;
    bool can_stop;
    const char *button_text;
    lv_color_t button_color;

    memset(&status, 0, sizeof(status));
    tf_usb_disk_mode_get_status(&status);
    busy = status.state == TF_USB_DISK_STATE_STARTING ||
           status.state == TF_USB_DISK_STATE_STOPPING;
    can_stop = status.state == TF_USB_DISK_STATE_ACTIVE ||
               status.state == TF_USB_DISK_STATE_WAITING_HOST;
    can_start = !busy &&
                status.state != TF_USB_DISK_STATE_ACTIVE &&
                status.state != TF_USB_DISK_STATE_WAITING_HOST &&
                tf_card_is_mounted();

    if (s_tf_usb_disk_status_label) {
        const char *message = status.message[0] ? status.message
                                                : tf_usb_disk_mode_state_text(status.state);
        lv_label_set_text(s_tf_usb_disk_status_label, TRS(message));
        lv_obj_set_style_text_color(s_tf_usb_disk_status_label,
                                    status.state == TF_USB_DISK_STATE_ACTIVE ? THEME_SUCCESS_LIGHT :
                                    status.state == TF_USB_DISK_STATE_WAITING_HOST ? THEME_WARNING_LIGHT :
                                    status.state == TF_USB_DISK_STATE_ERROR ? THEME_DANGER_LIGHT :
                                    busy ? THEME_WARNING_LIGHT : THEME_TEXT_SECONDARY,
                                    0);
    }

    if (!s_tf_usb_disk_button) {
        return;
    }

    if (busy) {
        lv_obj_add_state(s_tf_usb_disk_button, LV_STATE_DISABLED);
        button_color = THEME_WARNING_BG;
        button_text = LV_SYMBOL_USB " ...";
    } else if (can_stop) {
        lv_obj_remove_state(s_tf_usb_disk_button, LV_STATE_DISABLED);
        button_color = THEME_DANGER;
        button_text = LV_SYMBOL_CLOSE " 关闭USB磁盘模式";
    } else {
        button_color = can_start ? THEME_PRIMARY : THEME_BG_HOVER;
        button_text = LV_SYMBOL_USB " 开启USB磁盘模式";
        if (can_start) {
            lv_obj_remove_state(s_tf_usb_disk_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_tf_usb_disk_button, LV_STATE_DISABLED);
        }
    }

    lv_obj_set_style_bg_color(s_tf_usb_disk_button, button_color, 0);
    if (s_tf_usb_disk_button_label) {
        lv_label_set_text(s_tf_usb_disk_button_label, TRS(button_text));
    }
}

static void on_tf_usb_disk_status_timer(lv_timer_t *timer)
{
    (void)timer;
    update_tf_usb_disk_status();
    update_file_manager_web_status();
}

static void on_tf_usb_disk_button(lv_event_t *e)
{
    tf_usb_disk_mode_status_t status;
    esp_err_t err;

    (void)e;
    memset(&status, 0, sizeof(status));
    tf_usb_disk_mode_get_status(&status);
    if (status.state == TF_USB_DISK_STATE_ACTIVE ||
        status.state == TF_USB_DISK_STATE_WAITING_HOST) {
        err = tf_usb_disk_mode_stop();
        if (s_tf_usb_disk_status_label) {
            lv_label_set_text(s_tf_usb_disk_status_label,
                              err == ESP_OK ? TRS("正在关闭USB磁盘模式")
                                            : TRS("USB磁盘模式关闭失败"));
        }
    } else {
        ui_file_manager_close();
        err = tf_usb_disk_mode_start();
        if (s_tf_usb_disk_status_label) {
            lv_label_set_text(s_tf_usb_disk_status_label,
                              err == ESP_OK ? TRS("正在开启USB磁盘模式")
                                            : TRS("USB磁盘模式启动失败"));
        }
    }
    update_tf_usb_disk_status();
}

static void on_file_manager_web_status_timer(lv_timer_t *timer)
{
    (void)timer;
    update_file_manager_web_status();
}

static void on_file_manager_open_click(lv_event_t *e)
{
    (void)e;
    if (tf_usb_disk_mode_blocks_tf_access()) {
        ESP_LOGW(TAG, "file manager open ignored: TF card is in USB disk mode");
        return;
    }
    if (!tf_card_is_mounted()) {
        ESP_LOGW(TAG, "file manager open ignored: TF card is not mounted");
        return;
    }
    ui_file_manager_create(lv_scr_act());
}

static void on_file_manager_web_button(lv_event_t *e)
{
    bool running = web_file_manager_is_running();

    (void)e;
    if (!running) {
        esp_err_t err = web_file_manager_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "web file manager start failed: %s", esp_err_to_name(err));
        }
    } else {
        web_file_manager_stop();
    }
    update_file_manager_web_status();
}

static void render_file_manager_page(lv_obj_t *parent)
{
    char row_value[96];
    bool tf_mounted;
    lv_obj_t *row;
    lv_obj_t *buttons;

    create_label(parent, "TF 卡文件管理器", THEME_TEXT_PRIMARY);

    lv_obj_t *card = lv_obj_create(parent);
    apply_surface(card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    usb_storage_status_t usb_status;
    memset(&usb_status, 0, sizeof(usb_status));
    usb_storage_get_status(&usb_status);
    format_usb_usage_text(&usb_status, row_value, sizeof(row_value));
    row = create_row(card, "U盘", row_value);
    s_usb_storage_value_label = lv_obj_get_child(row, 1);
    lv_obj_null_on_delete(&s_usb_storage_value_label);

    buttons = lv_obj_create(card);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);
    s_usb_storage_button =
        create_settings_action_button(buttons, LV_SYMBOL_USB " 挂载", THEME_BG_HOVER, on_usb_storage_button);
    lv_obj_set_size(s_usb_storage_button, 216, 58);
    lv_obj_null_on_delete(&s_usb_storage_button);
    s_usb_storage_button_label = lv_obj_get_child(s_usb_storage_button, 0);
    lv_obj_null_on_delete(&s_usb_storage_button_label);
    s_usb_format_button =
        create_settings_action_button(buttons, LV_SYMBOL_WARNING " 格式化U盘", THEME_WARNING, on_usb_format_button);
    lv_obj_set_size(s_usb_format_button, 216, 58);
    lv_obj_null_on_delete(&s_usb_format_button);
    s_usb_format_button_label = lv_obj_get_child(s_usb_format_button, 0);
    lv_obj_null_on_delete(&s_usb_format_button_label);
    lv_obj_add_flag(s_usb_format_button, LV_OBJ_FLAG_HIDDEN);
    s_usb_storage_status_label = create_wrap_label(card, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_usb_storage_status_label);
    update_usb_storage_status();
    if (!s_usb_storage_status_timer) {
        s_usb_storage_status_timer = lv_timer_create(on_usb_storage_status_timer, 600, NULL);
    }

    format_tf_usage_text(row_value, sizeof(row_value));
    create_row(card, "TF 卡", row_value);

    tf_mounted = tf_card_is_mounted() && !tf_usb_disk_mode_blocks_tf_access();
    lv_obj_t *exif_cb = lv_checkbox_create(card);
    lv_checkbox_set_text(exif_cb, TRS("图片浏览器显示 EXIF 信息"));
    lv_obj_set_width(exif_cb, LV_PCT(100));
    lv_obj_set_height(exif_cb, 42);
    lv_obj_set_style_text_font(exif_cb, &APP_FONT, 0);
    lv_obj_set_style_text_color(exif_cb, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_opa(exif_cb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(exif_cb, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(exif_cb, THEME_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(exif_cb, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (app_settings_get_image_exif_enabled()) {
        lv_obj_add_state(exif_cb, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(exif_cb, on_image_exif_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *open_btn = lv_button_create(card);
    lv_obj_set_width(open_btn, LV_PCT(100));
    lv_obj_set_height(open_btn, 54);
    lv_obj_set_style_bg_color(open_btn, tf_mounted ? THEME_PRIMARY : THEME_BG_HOVER, 0);
    lv_obj_set_style_bg_opa(open_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(open_btn, 0, 0);
    lv_obj_set_style_radius(open_btn, 12, 0);
    lv_obj_add_event_cb(open_btn, on_file_manager_open_click, LV_EVENT_CLICKED, NULL);
    if (!tf_mounted) {
        lv_obj_add_state(open_btn, LV_STATE_DISABLED);
    }
    lv_obj_t *open_label =
        create_label(open_btn, LV_SYMBOL_DIRECTORY " 打开文件管理器", tf_mounted ? THEME_TEXT_PRIMARY : THEME_TEXT_SECONDARY);
    lv_obj_center(open_label);

    lv_obj_t *web_card = lv_obj_create(parent);
    apply_surface(web_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(web_card, LV_PCT(100));
    lv_obj_set_height(web_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(web_card, 18, 0);
    lv_obj_set_style_pad_row(web_card, 12, 0);
    lv_obj_set_flex_flow(web_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(web_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *web_row = lv_obj_create(web_card);
    lv_obj_set_width(web_row, LV_PCT(100));
    lv_obj_set_height(web_row, 48);
    lv_obj_set_style_bg_opa(web_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(web_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(web_row, 0, 0);
    lv_obj_set_flex_flow(web_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(web_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(web_row, LV_OBJ_FLAG_SCROLLABLE);

    create_label(web_row, "WiFi 在线文件管理器", THEME_TEXT_PRIMARY);
    s_file_web_button = lv_button_create(web_row);
    lv_obj_null_on_delete(&s_file_web_button);
    lv_obj_set_size(s_file_web_button, 110, 42);
    lv_obj_set_style_bg_color(s_file_web_button, THEME_BG_HOVER, 0);
    lv_obj_set_style_bg_opa(s_file_web_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_file_web_button, 0, 0);
    lv_obj_set_style_radius(s_file_web_button, 12, 0);
    lv_obj_add_event_cb(s_file_web_button, on_file_manager_web_button, LV_EVENT_CLICKED, NULL);

    s_file_web_button_label = create_label(s_file_web_button, LV_SYMBOL_PLAY " 开启", THEME_TEXT_PRIMARY);
    lv_obj_null_on_delete(&s_file_web_button_label);
    lv_obj_center(s_file_web_button_label);

    s_file_web_status = create_wrap_label(web_card, "", THEME_TEXT_SECONDARY);
    lv_obj_null_on_delete(&s_file_web_status);
    update_file_manager_web_status();
    if (!s_file_web_status_timer) {
        s_file_web_status_timer = lv_timer_create(on_file_manager_web_status_timer, 1500, NULL);
    }

    lv_obj_t *disk_card = lv_obj_create(parent);
    apply_surface(disk_card, THEME_BG_CARD, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_width(disk_card, LV_PCT(100));
    lv_obj_set_height(disk_card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(disk_card, 14, 0);
    lv_obj_set_style_pad_row(disk_card, 8, 0);
    lv_obj_set_flex_flow(disk_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(disk_card, LV_OBJ_FLAG_SCROLLABLE);

    create_label(disk_card, "USB 磁盘模式", THEME_TEXT_PRIMARY);
    create_wrap_label(disk_card,
                      "把 Tab5 的 TF 卡通过 USB-C 暴露给电脑，连接成功后电脑会把它识别为普通移动磁盘。",
                      THEME_TEXT_SECONDARY);

    s_tf_usb_disk_status_label = create_wrap_label(disk_card, "", THEME_TEXT_SECONDARY);
    lv_obj_set_style_text_align(s_tf_usb_disk_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_null_on_delete(&s_tf_usb_disk_status_label);

    s_tf_usb_disk_button =
        create_settings_action_button(disk_card, LV_SYMBOL_USB " 开启USB磁盘模式", THEME_PRIMARY, on_tf_usb_disk_button);
    lv_obj_set_size(s_tf_usb_disk_button, LV_PCT(100), 54);
    lv_obj_null_on_delete(&s_tf_usb_disk_button);
    s_tf_usb_disk_button_label = lv_obj_get_child(s_tf_usb_disk_button, 0);
    lv_obj_null_on_delete(&s_tf_usb_disk_button_label);

    create_wrap_label(disk_card,
                      "请使用支持数据传输的 USB-C 线连接电脑；仅充电线不会触发电脑识别。",
                      THEME_WARNING_LIGHT);
    update_tf_usb_disk_status();
    if (!s_tf_usb_disk_status_timer) {
        s_tf_usb_disk_status_timer = lv_timer_create(on_tf_usb_disk_status_timer, 700, NULL);
    }
}
