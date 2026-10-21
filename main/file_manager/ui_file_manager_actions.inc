static void fm_show_info_dialog(const char *title_text, const char *body_text)
{
    lv_obj_t *panel = fm_create_modal_panel(560, 240);
    lv_obj_t *title = fm_label(panel, title_text, THEME_TEXT_PRIMARY);
    lv_obj_t *body = fm_label(panel, body_text, THEME_TEXT_SECONDARY);
    lv_obj_t *btn;

    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(body, 1);

    btn = fm_button(panel, "确定", THEME_PRIMARY, THEME_PRIMARY);
    lv_obj_set_size(btn, 160, 48);
    lv_obj_set_style_align(btn, LV_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(btn, fm_on_modal_cancel, LV_EVENT_CLICKED, NULL);
}

static void fm_on_confirm_delete(lv_event_t *e)
{
    (void)e;
    fm_confirm_delete_selection();
}

static void fm_confirm_delete_selection(void)
{
    fm_delete_selection_t *selection = s_delete_selection;
    size_t ok_count = 0;
    size_t fail_count = 0;
    char text[96];

    if (!selection || selection->count == 0) {
        fm_close_modal();
        fm_set_status(TRS("没有可删除的项目"), THEME_TEXT_SECONDARY);
        return;
    }

    s_delete_selection = NULL;
    s_modal_enter_action = FM_MODAL_ENTER_NONE;
    for (size_t i = 0; i < selection->count; i++) {
        char path[FM_PATH_MAX];

        if (!fm_join_path(path, sizeof(path), selection->dir, selection->names[i]) ||
            !fm_remove_recursive(path)) {
            fail_count++;
            continue;
        }
        ok_count++;
    }
    free(selection);

    fm_close_modal();
    s_all_active = false;
    s_selection_status_active = false;

    if (fail_count > 0) {
        snprintf(text, sizeof(text), TRS("已删除 %u 项，失败 %u 项"), (unsigned)ok_count, (unsigned)fail_count);
        fm_set_status(text, THEME_WARNING_LIGHT);
    } else {
        snprintf(text, sizeof(text), TRS("已删除 %u 项"), (unsigned)ok_count);
        fm_set_status(text, THEME_SUCCESS_LIGHT);
    }
    fm_render();
}

static void fm_show_delete_confirm(void)
{
    char name_text[FM_NAME_MAX + 32];
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *name;
    lv_obj_t *row;
    lv_obj_t *btn;

    if (!s_delete_selection || s_delete_selection->count == 0) {
        fm_set_status(TRS("没有可删除的项目"), THEME_TEXT_SECONDARY);
        return;
    }

    panel = fm_create_modal_panel(580, 260);
    title = fm_label(panel,
                     s_delete_selection->count > 1 ? TRS("删除选中项目？") : TRS("删除此项目？"),
                     THEME_DANGER_LIGHT);
    if (s_delete_selection->count > 1) {
        snprintf(name_text, sizeof(name_text), TRS("%u 项"), (unsigned)s_delete_selection->count);
    } else {
        strlcpy(name_text, s_delete_selection->names[0], sizeof(name_text));
    }
    name = fm_label(panel, name_text, THEME_TEXT_PRIMARY);

    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    row = lv_obj_create(panel);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 54);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    btn = fm_button(row, TRS("取消"), THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 140, 48);
    lv_obj_add_event_cb(btn, fm_on_modal_cancel, LV_EVENT_CLICKED, NULL);
    btn = fm_button(row, TRS("删除"), THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, 140, 48);
    lv_obj_add_event_cb(btn, fm_on_confirm_delete, LV_EVENT_CLICKED, NULL);
    s_modal_enter_action = FM_MODAL_ENTER_DELETE;
}
static void fm_set_clipboard_from_active(fm_clipboard_mode_t mode)
{
    size_t written = 0;

    if (fm_active_count() == 0 || mode == FM_CLIPBOARD_NONE) {
        return;
    }
    memset(s_clipboard_names, 0, fm_clipboard_names_bytes());
    strlcpy(s_clipboard_dir, s_current_path, sizeof(s_clipboard_dir));

    if (s_all_active) {
        for (size_t i = 0; i < s_entry_count && written < FM_CLIPBOARD_MAX_ITEMS; i++) {
            strlcpy(s_clipboard_names[written++], s_entries[i].name, sizeof(s_clipboard_names[0]));
        }
    } else {
        if (!fm_set_active_from_index(fm_first_active_index())) {
            return;
        }
        strlcpy(s_clipboard_names[written++], s_active_name, sizeof(s_clipboard_names[0]));
    }
    if (written == 0) {
        return;
    }

    s_clipboard_count = written;
    s_selection_status_active = false;
    if (written == 1) {
        if (!fm_join_path(s_clipboard_path, sizeof(s_clipboard_path), s_clipboard_dir, s_clipboard_names[0])) {
            fm_clear_clipboard();
            fm_set_status(TRS("剪贴板路径过长"), THEME_DANGER_LIGHT);
            return;
        }
        strlcpy(s_clipboard_name, s_clipboard_names[0], sizeof(s_clipboard_name));
    } else {
        s_clipboard_path[0] = '\0';
        snprintf(s_clipboard_name, sizeof(s_clipboard_name), TRS("%u 项"), (unsigned)written);
    }
    s_clipboard_mode = mode;
    fm_update_paste_button();

    if (written == 1) {
        char text[220];
        snprintf(text,
                 sizeof(text),
                 mode == FM_CLIPBOARD_COPY ? TRS("已复制：%s") : TRS("已剪切：%s"),
                 s_clipboard_name);
        fm_set_status(text, THEME_SUCCESS_LIGHT);
    } else {
        char text[96];
        snprintf(text,
                 sizeof(text),
                 mode == FM_CLIPBOARD_COPY ? TRS("已复制 %u 项") : TRS("已剪切 %u 项"),
                 (unsigned)written);
        fm_set_status(text, THEME_SUCCESS_LIGHT);
    }
}

static void fm_set_clipboard(fm_clipboard_mode_t mode)
{
    fm_set_clipboard_from_active(mode);
}

static void fm_on_action_copy(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
    fm_set_clipboard(FM_CLIPBOARD_COPY);
}

static void fm_on_action_move(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
    fm_set_clipboard(FM_CLIPBOARD_MOVE);
}

static void fm_enter_path(const char *path)
{
    if (!path || !path[0] || !fm_path_in_root(path)) {
        fm_set_status(TRS("无效路径"), THEME_DANGER_LIGHT);
        return;
    }
    strlcpy(s_current_path, path, sizeof(s_current_path));
    s_page = 0;
    s_selected_index = 0;
    s_all_active = false;
    s_selection_status_active = false;
    fm_render();
}

static void fm_on_confirm_paste_overwrite(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
    fm_paste_clipboard(true);
}

static void fm_show_paste_overwrite_confirm(void)
{
    lv_obj_t *panel = fm_create_modal_panel(600, 260);
    lv_obj_t *title = fm_label(panel, TRS("目标已存在，是否覆盖？"), THEME_WARNING_LIGHT);
    lv_obj_t *body = fm_label(panel, TRS("覆盖会删除当前文件夹中的同名项目。"), THEME_TEXT_SECONDARY);
    lv_obj_t *row;
    lv_obj_t *btn;

    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);

    row = lv_obj_create(panel);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 54);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    btn = fm_button(row, TRS("取消"), THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 140, 48);
    lv_obj_add_event_cb(btn, fm_on_modal_cancel, LV_EVENT_CLICKED, NULL);
    btn = fm_button(row, TRS("覆盖"), THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, 140, 48);
    lv_obj_add_event_cb(btn, fm_on_confirm_paste_overwrite, LV_EVENT_CLICKED, NULL);
}

static void fm_on_paste_job_done(void *arg)
{
    fm_paste_job_t *job = (fm_paste_job_t *)arg;
    char text[96];

    if (!job) {
        return;
    }
    if (job == s_active_paste_job) {
        s_active_paste_job = NULL;
        s_paste_task = NULL;
        s_paste_busy = false;
        fm_hide_busy_toast();
        fm_update_paste_button();
    }
    if (job->generation != s_ui_generation || !s_overlay) {
        free(job);
        return;
    }

    if (job->ok_count == job->count) {
        snprintf(text,
                 sizeof(text),
                 job->mode == FM_CLIPBOARD_COPY ? TRS("已复制 %u 项") : TRS("已移动 %u 项"),
                 (unsigned)job->ok_count);
        if (job->mode == FM_CLIPBOARD_MOVE) {
            fm_clear_clipboard();
        } else {
            fm_update_paste_button();
        }
        fm_set_status(text, THEME_SUCCESS_LIGHT);
    } else {
        snprintf(text,
                 sizeof(text),
                 job->mode == FM_CLIPBOARD_COPY ? TRS("已复制 %u/%u 项") : TRS("已移动 %u/%u 项"),
                 (unsigned)job->ok_count,
                 (unsigned)job->count);
        if (job->mode == FM_CLIPBOARD_MOVE && job->ok_count > 0) {
            fm_clear_clipboard();
        } else {
            fm_update_paste_button();
        }
        fm_set_status(text, job->ok_count > 0 ? THEME_WARNING_LIGHT : THEME_DANGER_LIGHT);
    }
    fm_render();
    free(job);
}

static void fm_paste_task(void *arg)
{
    fm_paste_job_t *job = (fm_paste_job_t *)arg;
    char src[FM_PATH_MAX];
    char dst[FM_PATH_MAX];

    if (!job) {
        vTaskDelete(NULL);
        return;
    }

    for (size_t i = 0; i < job->count; i++) {
        struct stat st;
        bool ok = false;

        if (!fm_paste_job_source_path(job, src, sizeof(src), i) ||
            !fm_join_path(dst, sizeof(dst), job->dest_dir, job->names[i]) ||
            !fm_path_in_known_volume(src) ||
            !fm_path_in_root(dst) ||
            stat(src, &st) != 0) {
            continue;
        }
        if (job->mode == FM_CLIPBOARD_COPY) {
            ok = fm_copy_recursive_overwrite(src, dst, job->overwrite);
        } else {
            ok = fm_move_clipboard_item(src, dst, &st, job->overwrite);
        }
        if (ok) {
            job->ok_count++;
        }
        vTaskDelay(1);
    }

    if (lv_async_call(fm_on_paste_job_done, job) != LV_RESULT_OK) {
        free(job);
    }
    vTaskDelete(NULL);
}

static fm_paste_job_t *fm_create_paste_job(bool overwrite)
{
    fm_paste_job_t *job = calloc(1, sizeof(*job));

    if (!job) {
        return NULL;
    }
    job->mode = s_clipboard_mode;
    job->overwrite = overwrite;
    job->generation = s_ui_generation;
    job->count = s_clipboard_count;
    strlcpy(job->dest_dir, s_current_path, sizeof(job->dest_dir));
    strlcpy(job->source_dir, s_clipboard_dir, sizeof(job->source_dir));
    strlcpy(job->single_source_path, s_clipboard_path, sizeof(job->single_source_path));
    if (job->count > FM_CLIPBOARD_MAX_ITEMS) {
        job->count = FM_CLIPBOARD_MAX_ITEMS;
    }
    for (size_t i = 0; i < job->count; i++) {
        strlcpy(job->names[i], s_clipboard_names[i], sizeof(job->names[i]));
    }
    return job;
}

static void fm_paste_clipboard(bool overwrite)
{
    fm_paste_job_t *job;
    char busy_text[96];

    if (s_paste_busy) {
        fm_set_status(TRS("文件操作正在进行"), THEME_WARNING_LIGHT);
        return;
    }
    if (s_storage_picker_active) {
        fm_set_status(TRS("请先选择存储器"), THEME_WARNING_LIGHT);
        return;
    }
    if (s_clipboard_mode == FM_CLIPBOARD_NONE || s_clipboard_count == 0 || !s_clipboard_dir[0]) {
        fm_set_status(TRS("剪贴板为空"), THEME_WARNING_LIGHT);
        return;
    }
    if (!fm_path_in_root(s_current_path)) {
        fm_set_status(TRS("无效目标路径"), THEME_DANGER_LIGHT);
        return;
    }
    if (!overwrite && fm_clipboard_has_conflict()) {
        fm_show_paste_overwrite_confirm();
        return;
    }

    job = fm_create_paste_job(overwrite);
    if (!job) {
        fm_set_status(TRS("文件操作内存不足"), THEME_DANGER_LIGHT);
        return;
    }

    s_paste_busy = true;
    s_active_paste_job = job;
    fm_update_paste_button();
    snprintf(busy_text,
             sizeof(busy_text),
             job->mode == FM_CLIPBOARD_COPY ? TRS("正在复制 %u 项...") : TRS("正在移动 %u 项..."),
             (unsigned)job->count);
    fm_show_busy_toast(busy_text);

    if (xTaskCreate(fm_paste_task,
                    "fm_paste",
                    FM_PASTE_TASK_STACK,
                    job,
                    FM_PASTE_TASK_PRIORITY,
                    &s_paste_task) != pdPASS) {
        s_paste_busy = false;
        s_paste_task = NULL;
        s_active_paste_job = NULL;
        fm_hide_busy_toast();
        free(job);
        fm_update_paste_button();
        fm_set_status(TRS("文件操作任务创建失败"), THEME_DANGER_LIGHT);
    }
}

static void fm_on_paste(lv_event_t *e)
{
    (void)e;
    fm_paste_clipboard(false);
}

static void fm_on_select_all(lv_event_t *e)
{
    (void)e;
    fm_select_all_active();
}

static void fm_on_copy_selection(lv_event_t *e)
{
    (void)e;
    fm_set_clipboard(FM_CLIPBOARD_COPY);
}

static void fm_on_cut_selection(lv_event_t *e)
{
    (void)e;
    fm_set_clipboard(FM_CLIPBOARD_MOVE);
}

static void fm_on_delete_selection(lv_event_t *e)
{
    (void)e;
    fm_delete_first_active();
}

static void fm_open_active(void)
{
    struct stat st;
    if (!fm_path_in_root(s_active_path) || stat(s_active_path, &st) != 0) {
        fm_set_status(TRS("项目不存在"), THEME_DANGER_LIGHT);
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        fm_enter_path(s_active_path);
        return;
    }
    fm_file_type_t type = fm_file_type(s_active_name, false);
    if (type == FM_TYPE_IMAGE) {
        fm_show_image_viewer(s_active_path);
    } else if (type == FM_TYPE_AUDIO) {
        ui_file_manager_audio_play_reserved(s_active_path);
    } else if (type == FM_TYPE_TEXT) {
        fm_show_text_viewer(s_active_path, s_active_name);
    } else {
        fm_show_info_dialog(TRS("无法预览"), TRS("此文件可在TF卡中管理，但本机暂不支持预览。"));
    }
}

static void fm_on_action_open(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
    fm_open_active();
}

static void fm_on_action_rename(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
    fm_show_input_dialog(FM_INPUT_RENAME, s_active_name);
}

static void fm_on_action_delete(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
    if (fm_prepare_delete_single_active()) {
        fm_show_delete_confirm();
    }
}

static void fm_show_action_sheet(size_t index)
{
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *btn;

    if (index >= s_entry_count) {
        return;
    }
    s_selected_index = index;
    s_all_active = false;
    s_selection_status_active = false;
    strlcpy(s_active_name, s_entries[index].name, sizeof(s_active_name));
    if (!fm_join_path(s_active_path, sizeof(s_active_path), s_current_path, s_active_name)) {
        fm_set_status(TRS("路径过长"), THEME_DANGER_LIGHT);
        return;
    }

    panel = fm_create_modal_panel(560, 460);
    title = fm_label(panel, s_active_name, THEME_TEXT_PRIMARY);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    btn = fm_button(panel,
                    s_entries[index].is_dir ? LV_SYMBOL_DIRECTORY " 打开" : TRS("打开/预览"),
                    THEME_PRIMARY,
                    THEME_PRIMARY);
    lv_obj_set_size(btn, LV_PCT(100), 52);
    lv_obj_add_event_cb(btn, fm_on_action_open, LV_EVENT_CLICKED, NULL);

    btn = fm_button(panel, LV_SYMBOL_EDIT " 重命名", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, LV_PCT(100), 52);
    lv_obj_add_event_cb(btn, fm_on_action_rename, LV_EVENT_CLICKED, NULL);

    btn = fm_button(panel, LV_SYMBOL_COPY " 复制", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, LV_PCT(100), 52);
    lv_obj_add_event_cb(btn, fm_on_action_copy, LV_EVENT_CLICKED, NULL);

    btn = fm_button(panel, LV_SYMBOL_CUT " 移动", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, LV_PCT(100), 52);
    lv_obj_add_event_cb(btn, fm_on_action_move, LV_EVENT_CLICKED, NULL);

    btn = fm_button(panel, LV_SYMBOL_TRASH " 删除", THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, LV_PCT(100), 52);
    lv_obj_add_event_cb(btn, fm_on_action_delete, LV_EVENT_CLICKED, NULL);

    btn = fm_button(panel, TRS("取消"), THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, LV_PCT(100), 52);
    lv_obj_add_event_cb(btn, fm_on_modal_cancel, LV_EVENT_CLICKED, NULL);
}

static void fm_on_entry_click(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= s_entry_count) {
        return;
    }
    s_selected_index = index;
    s_all_active = false;
    s_selection_status_active = false;
    strlcpy(s_active_name, s_entries[index].name, sizeof(s_active_name));
    if (!fm_join_path(s_active_path, sizeof(s_active_path), s_current_path, s_active_name)) {
        fm_set_status(TRS("路径过长"), THEME_DANGER_LIGHT);
        return;
    }
    fm_open_active();
}

static void fm_on_entry_action(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    fm_show_action_sheet(index);
}

static void fm_create_entry_card(size_t index, int32_t card_w)
{
    fm_entry_t *entry = &s_entries[index];
    lv_obj_t *card = lv_obj_create(s_list);
    lv_obj_t *icon_box;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *meta;
    lv_obj_t *action;
    char meta_text[64];
    int32_t text_w = card_w - 132;
    lv_point_t name_size;
    int32_t name_line_space = 0;
    bool name_wraps;
    const lv_coord_t card_h = 96;
    const lv_coord_t icon_sz = 48;
    const lv_coord_t content_x = 62;
    lv_coord_t name_y;
    lv_coord_t name_h;
    lv_coord_t meta_y;

    if (text_w < 80) {
        text_w = 80;
    }

    lv_text_get_size(&name_size, entry->name, &APP_FONT, 0, name_line_space, text_w, LV_TEXT_FLAG_NONE);
    name_wraps = name_size.y > (&APP_FONT)->line_height;
    if (name_wraps) {
        name_line_space = -6;
        name_y = -8;
        name_h = (&APP_FONT)->line_height * 2 + name_line_space;
        meta_y = 50;
    } else {
        name_y = 8;
        name_h = (&APP_FONT)->line_height;
        meta_y = 44;
    }

    lv_obj_set_size(card, card_w, card_h);
    fm_style_surface(card, entry->is_dir ? lv_color_hex(0x1E2A3A) : THEME_BG_CARD,
                     entry->is_dir ? THEME_PRIMARY : THEME_BORDER_SECONDARY, 8);
    if (s_input_hw_keyboard_present && fm_entry_active(index)) {
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, s_all_active ? THEME_WARNING_LIGHT : THEME_ACCENT_CYAN, 0);
        lv_obj_set_style_outline_width(card, 1, 0);
        lv_obj_set_style_outline_color(card, s_all_active ? THEME_WARNING_LIGHT : THEME_ACCENT_CYAN, 0);
        lv_obj_set_style_outline_opa(card, LV_OPA_60, 0);
        lv_obj_set_style_outline_pad(card, 1, 0);
    }
    if (s_input_hw_keyboard_present && index == fm_first_active_index()) {
        lv_obj_set_style_border_width(card, 3, 0);
        lv_obj_set_style_border_color(card, THEME_ACCENT_CYAN, 0);
        lv_obj_set_style_outline_width(card, 2, 0);
        lv_obj_set_style_outline_color(card, THEME_ACCENT_CYAN, 0);
        lv_obj_set_style_outline_opa(card, LV_OPA_80, 0);
        lv_obj_set_style_outline_pad(card, 2, 0);
    }
    if (s_scroll_active_into_view && index == fm_first_active_index()) {
        lv_obj_update_layout(s_list);
        lv_obj_scroll_to_view(card, LV_ANIM_ON);
        s_scroll_active_into_view = false;
    }
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, fm_on_entry_click, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, icon_sz, icon_sz);
    fm_style_surface(icon_box, THEME_BG_HOVER, fm_type_color(entry->type), 8);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);

    icon = fm_label(icon_box, fm_type_symbol(entry->type), fm_type_color(entry->type));
    lv_obj_center(icon);

    name = fm_label(card, entry->name, THEME_TEXT_PRIMARY);
    lv_obj_set_width(name, text_w);
    lv_obj_set_height(name, name_h);
    lv_label_set_long_mode(name, name_wraps ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_line_space(name, name_line_space, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, content_x, name_y);

    if (entry->is_dir) {
        snprintf(meta_text, sizeof(meta_text), TRS("文件夹"));
    } else {
        fm_format_size(entry->size, meta_text, sizeof(meta_text));
    }
    meta = fm_label(card, meta_text, THEME_TEXT_TERTIARY);
    lv_obj_set_width(meta, text_w);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_align(meta, LV_ALIGN_TOP_LEFT, content_x, meta_y);

    action = fm_button(card, LV_SYMBOL_SETTINGS, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(action, 44, 40);
    lv_obj_align(action, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(action, fm_on_entry_action, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
}

static void fm_on_close(lv_event_t *e)
{
    (void)e;
    ui_file_manager_close();
}

static void fm_on_back(lv_event_t *e)
{
    (void)e;
    fm_go_parent();
}

static void fm_on_refresh(lv_event_t *e)
{
    (void)e;
    fm_render();
}

static void fm_on_mkdir(lv_event_t *e)
{
    (void)e;
    if (s_storage_picker_active) {
        fm_set_status(TRS("请先选择存储器"), THEME_WARNING_LIGHT);
        return;
    }
    fm_show_input_dialog(FM_INPUT_MKDIR, "");
}

static void fm_on_prev(lv_event_t *e)
{
    (void)e;
    if (s_page > 0) {
        s_page--;
        fm_select_page_start();
        fm_render();
    }
}

static void fm_on_next(lv_event_t *e)
{
    (void)e;
    size_t total_pages = s_entry_count == 0 ? 1 : ((s_entry_count - 1) / FM_PAGE_SIZE + 1);
    if (s_page + 1 < total_pages) {
        s_page++;
        fm_select_page_start();
        fm_render();
    }
}

static void fm_update_paste_button(void)
{
    if (!s_paste_button) {
        return;
    }
    if (s_storage_picker_active || s_paste_busy ||
        s_clipboard_mode == FM_CLIPBOARD_NONE || s_clipboard_count == 0) {
        lv_obj_add_state(s_paste_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_paste_button, LV_STATE_DISABLED);
    }
}

static void fm_set_button_text(lv_obj_t *btn, const char *text)
{
    lv_obj_t *child;

    if (!btn) {
        return;
    }
    child = lv_obj_get_child(btn, 0);
    if (child) {
        lv_label_set_text(child, TRS(text ? text : ""));
        lv_obj_center(child);
    }
}

static void fm_update_navigation_controls(void)
{
    bool disabled = s_storage_picker_active;

    if (!s_back_button) {
        return;
    }
    fm_set_button_text(s_back_button, fm_is_root() ? LV_SYMBOL_LEFT " 存储器" : LV_SYMBOL_LEFT " 上一级");
    if (disabled) {
        lv_obj_add_state(s_back_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_back_button, THEME_BG_CARD, 0);
        lv_obj_set_style_border_color(s_back_button, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_opa(s_back_button, LV_OPA_50, 0);
    } else {
        lv_obj_remove_state(s_back_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_back_button, THEME_BG_HOVER, 0);
        lv_obj_set_style_border_color(s_back_button, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_opa(s_back_button, LV_OPA_COVER, 0);
    }
}

static void fm_update_web_controls(void)
{
    char reason[96];
    bool running = web_file_manager_is_running();
    bool can_start = web_file_manager_can_start(reason, sizeof(reason));

    if (s_web_button) {
        if (running) {
            lv_obj_remove_state(s_web_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_web_button, THEME_SUCCESS_BG, 0);
            lv_obj_set_style_border_color(s_web_button, THEME_SUCCESS, 0);
            fm_set_button_text(s_web_button, LV_SYMBOL_STOP);
        } else if (can_start) {
            lv_obj_remove_state(s_web_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_web_button, THEME_BG_HOVER, 0);
            lv_obj_set_style_border_color(s_web_button, THEME_BORDER_SECONDARY, 0);
            fm_set_button_text(s_web_button, LV_SYMBOL_UPLOAD);
        } else {
            lv_obj_add_state(s_web_button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_web_button, THEME_BG_CARD, 0);
            lv_obj_set_style_border_color(s_web_button, THEME_BORDER_SECONDARY, 0);
            fm_set_button_text(s_web_button, LV_SYMBOL_UPLOAD);
        }
    }

    fm_show_web_status_in_footer();
}

static void fm_on_web_button(lv_event_t *e)
{
    (void)e;
    if (web_file_manager_is_running()) {
        web_file_manager_stop();
        fm_set_web_status(TRS("在线文件管理器：已停止"), THEME_TEXT_SECONDARY);
    } else {
        esp_err_t err = web_file_manager_start();
        if (err != ESP_OK) {
            fm_set_web_status(TRS("在线文件管理器：启动失败"), THEME_DANGER_LIGHT);
        } else {
            fm_set_web_status(TRS("在线文件管理器：已启动"), THEME_SUCCESS_LIGHT);
        }
    }
    fm_update_web_controls();
}

static void fm_on_power_chip_click(lv_event_t *e)
{
    (void)e;
    ui_server_list_show_power_detail();
}

static void fm_on_wifi_chip_click(lv_event_t *e)
{
    (void)e;
    ui_server_list_show_wifi_panel();
}

static void fm_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    fm_poll_usb_status();
    fm_update_status_bar();
    fm_update_web_controls();
    fm_update_folder_status();
}

static void fm_update_status_bar(void)
{
    power_monitor_status_t power;
    esp_err_t err;
    lv_color_t power_color;
    char text[8];

    if (s_wifi_chip && s_wifi_label) {
        if (wifi_manager_get_state() == WIFI_STATE_CONNECTED) {
            lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(s_wifi_label, THEME_SUCCESS_LIGHT, 0);
            lv_obj_set_style_bg_color(s_wifi_chip, THEME_SUCCESS_BG, 0);
            lv_obj_set_style_border_color(s_wifi_chip, THEME_SUCCESS, 0);
        } else if (wifi_manager_get_state() == WIFI_STATE_CONNECTING) {
            lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(s_wifi_label, THEME_WARNING_LIGHT, 0);
            lv_obj_set_style_bg_color(s_wifi_chip, THEME_WARNING_BG, 0);
            lv_obj_set_style_border_color(s_wifi_chip, THEME_WARNING, 0);
        } else {
            lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(s_wifi_label, THEME_TEXT_SECONDARY, 0);
            lv_obj_set_style_bg_color(s_wifi_chip, THEME_BG_PANEL, 0);
            lv_obj_set_style_border_color(s_wifi_chip, THEME_BORDER_SECONDARY, 0);
        }
    }

    if (!s_power_chip || !s_power_body || !s_power_cap || !s_power_percent) {
        return;
    }

    memset(&power, 0, sizeof(power));
    err = power_monitor_get_cached_status(&power) ? power.last_error : ESP_ERR_INVALID_STATE;
    power_color = fm_power_color_for_status(&power);

    if (err != ESP_OK && !power.usb_c_present) {
        lv_label_set_text(s_power_percent, "--");
        lv_obj_set_style_border_color(s_power_body, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_bg_opa(s_power_body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_power_body, THEME_BG_PANEL, 0);
    } else if (!power.battery_present) {
        lv_label_set_text(s_power_percent, power.usb_c_present ? "USB" : "--");
        lv_obj_set_style_border_color(s_power_body, power.usb_c_present ? THEME_ACCENT_CYAN : THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_bg_opa(s_power_body, power.usb_c_present ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_power_body, THEME_ACCENT_CYAN, 0);
    } else {
        snprintf(text, sizeof(text), "%.0f", power.battery_percent);
        lv_label_set_text(s_power_percent, text);
        lv_obj_set_style_border_color(s_power_body, power_color, 0);
        lv_obj_set_style_bg_opa(s_power_body, LV_OPA_30, 0);
        lv_obj_set_style_bg_color(s_power_body, power_color, 0);
    }

    lv_obj_set_style_border_color(s_power_cap, power_color, 0);
    lv_obj_set_style_bg_color(s_power_cap, power_color, 0);
    lv_obj_set_style_text_color(s_power_percent, power_color, 0);
    lv_obj_center(s_power_percent);
}
