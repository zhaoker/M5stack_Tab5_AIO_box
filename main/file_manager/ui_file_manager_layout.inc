static void fm_render(void)
{
    size_t total_pages;
    size_t start;
    size_t end;
    int32_t screen_w;
    int32_t card_w;
    char count_text[80];

    if (!s_overlay || !s_list) {
        return;
    }

    if (s_storage_picker_active) {
        fm_render_storage_picker();
        return;
    }

    lv_obj_clean(s_list);

    if (s_title_label) {
        lv_label_set_text_fmt(s_title_label,
                              s_current_volume == FM_VOLUME_USB ? LV_SYMBOL_USB " %s" : LV_SYMBOL_SD_CARD " %s",
                              TRS(fm_volume_title(s_current_volume)));
    }
    if (s_path_label) {
        lv_label_set_text(s_path_label, fm_display_path());
    }
    fm_update_navigation_controls();

    if (!fm_volume_is_mounted(s_current_volume)) {
        s_entry_count = 0;
        s_selected_index = 0;
        s_all_active = false;
        s_selection_status_active = false;
        fm_label(s_list,
                 s_current_volume == FM_VOLUME_USB ? TRS("U盘未挂载") : TRS("TF卡未挂载"),
                 THEME_WARNING_LIGHT);
        fm_set_status(s_current_volume == FM_VOLUME_USB ? TRS("U盘未挂载") : TRS("TF卡未挂载"),
                      THEME_WARNING_LIGHT);
        fm_update_folder_status();
        return;
    }

    if (!fm_load_entries()) {
        s_entry_count = 0;
        s_selected_index = 0;
        s_all_active = false;
        s_selection_status_active = false;
        fm_label(s_list, TRS("无法读取文件夹"), THEME_DANGER_LIGHT);
        fm_set_status(TRS("无法读取文件夹"), THEME_DANGER_LIGHT);
        fm_update_folder_status();
        return;
    }

    total_pages = s_entry_count == 0 ? 1 : ((s_entry_count - 1) / FM_PAGE_SIZE + 1);
    fm_clamp_selection_to_entries();
    start = s_page * FM_PAGE_SIZE;
    if (s_entry_count > 0 && start >= s_entry_count) {
        s_page = (s_entry_count - 1) / FM_PAGE_SIZE;
        start = s_page * FM_PAGE_SIZE;
    }
    if (s_entry_count > 0 && !s_all_active &&
        (s_selected_index < start || s_selected_index >= start + FM_PAGE_SIZE)) {
        s_selected_index = start;
    }
    end = start + FM_PAGE_SIZE;
    if (end > s_entry_count) {
        end = s_entry_count;
    }

    snprintf(count_text, sizeof(count_text), TRS("%u 项  第 %u/%u 页"),
             (unsigned)s_entry_count,
             (unsigned)(s_page + 1),
             (unsigned)total_pages);
    if (s_count_label) {
        lv_label_set_text(s_count_label, count_text);
    }

    if (s_entry_count == 0) {
        s_all_active = false;
        s_selection_status_active = false;
        lv_obj_t *empty = fm_label(s_list, TRS("当前文件夹为空"), THEME_TEXT_TERTIARY);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        fm_update_folder_status();
        return;
    }

    lv_obj_update_layout(s_list);
    screen_w = lv_obj_get_content_width(s_list);
    if (screen_w <= 0) {
        screen_w = lv_display_get_horizontal_resolution(NULL) - FM_PAD * 2;
    }
    card_w = (screen_w - FM_GAP - 2) / 2;
    if (card_w < 120) {
        card_w = 120;
    }

    for (size_t i = start; i < end; i++) {
        fm_create_entry_card(i, card_w);
    }
    fm_update_folder_status();
}

void ui_file_manager_close(void)
{
    s_ui_generation++;
    fm_unregister_audio_status_cb();
    fm_close_audio_player();
    fm_close_viewer();
    fm_close_text_viewer();
    fm_close_modal();
    fm_hide_busy_toast();
    if (s_status_timer) {
        lv_timer_del(s_status_timer);
        s_status_timer = NULL;
    }
    if (s_hw_keyboard_timer) {
        lv_timer_del(s_hw_keyboard_timer);
        s_hw_keyboard_timer = NULL;
    }
    fm_audio_cancel_list_status_timer();
    if (s_input_hw_keyboard_present) {
        tab5_keyboard_deinit();
        s_input_hw_keyboard_present = false;
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
    }
    s_overlay = NULL;
    s_title_label = NULL;
    s_path_label = NULL;
    s_count_label = NULL;
    s_web_status_label = NULL;
    s_status_label = NULL;
    s_list = NULL;
    s_wifi_chip = NULL;
    s_wifi_label = NULL;
    s_power_chip = NULL;
    s_power_body = NULL;
    s_power_cap = NULL;
    s_power_percent = NULL;
    s_back_button = NULL;
    s_web_button = NULL;
    s_web_button_label = NULL;
    s_paste_button = NULL;
    s_audio_viewer = NULL;
    s_audio_title = NULL;
    s_audio_artist = NULL;
    s_audio_status = NULL;
    s_audio_index = NULL;
    s_audio_play_label = NULL;
    s_audio_mode_icon = NULL;
    s_audio_mode_label = NULL;
    s_audio_progress = NULL;
    s_audio_elapsed = NULL;
    s_audio_duration = NULL;
    s_audio_volume = NULL;
    s_audio_volume_value = NULL;
    s_audio_playlist = NULL;
    s_audio_list_status = NULL;
    s_audio_playlist_count_label = NULL;
    s_audio_tab_now = NULL;
    s_audio_tab_favorites = NULL;
    s_audio_cover_img = NULL;
    s_audio_cover_band = NULL;
    s_audio_cover_icon = NULL;
    fm_audio_cancel_cover_timer();
    s_audio_cover_track_path[0] = '\0';
    s_audio_cover_pending_path[0] = '\0';
    s_audio_showing_favorites = false;
    fm_audio_release_favorites_cache();
    s_status_override_active = false;
    s_selection_status_active = false;
    s_scroll_active_into_view = false;
    s_paste_busy = false;
    s_paste_task = NULL;
    s_active_paste_job = NULL;
    s_audio_status_cb_registered = false;
}

void ui_file_manager_create(lv_obj_t *parent)
{
    lv_obj_t *topbar;
    lv_obj_t *left;
    lv_obj_t *right;
    lv_obj_t *btn;
    lv_obj_t *toolbar;
    lv_obj_t *pager;
    lv_obj_t *body;
    lv_obj_t *footer;
    int32_t screen_h;
    int32_t screen_w;
    int32_t topbar_right_w;
    int32_t topbar_left_w;
    int32_t path_w;

    ui_file_manager_close();

    if (!fm_ensure_large_buffers()) {
        ESP_LOGE(TAG, "file manager open ignored: large buffer allocation failed");
        return;
    }
    if (tf_usb_disk_mode_blocks_tf_access()) {
        ESP_LOGW(TAG, "file manager open ignored: TF card is in USB disk mode");
        return;
    }
    if (!tf_card_is_mounted()) {
        ESP_LOGW(TAG, "file manager open ignored: TF card is not mounted");
        return;
    }

    fm_prepare_default_storage_view();
    s_page = 0;
    s_selected_index = 0;
    s_all_active = false;
    s_selection_status_active = false;
    s_ui_generation++;
    s_status_override_active = false;
    s_status_override_until = 0;
    s_scroll_active_into_view = false;
    s_paste_busy = false;
    s_paste_task = NULL;
    s_active_paste_job = NULL;
    s_input_hw_keyboard_present = (tab5_keyboard_init() == ESP_OK);
    if (s_input_hw_keyboard_present) {
        ESP_LOGI(TAG, "Physical keyboard detected; file manager navigation enabled");
    }
    s_hw_keyboard_timer = lv_timer_create(fm_hw_keyboard_timer_cb, FM_KEYBOARD_POLL_MS, NULL);
    screen_w = lv_display_get_horizontal_resolution(NULL);
    if (screen_w <= 0) {
        screen_w = 720;
    }
    topbar_right_w = 272;
    topbar_left_w = screen_w - (FM_PAD * 2) - topbar_right_w - 12;
    if (topbar_left_w < 160) {
        topbar_left_w = 160;
    }
    path_w = topbar_left_w - 86;
    if (path_w < 80) {
        path_w = 80;
    }

    s_overlay = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_null_on_delete(&s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, THEME_BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    topbar = lv_obj_create(s_overlay);
    lv_obj_set_size(topbar, LV_PCT(100), FM_TOPBAR_H);
    lv_obj_set_pos(topbar, 0, 0);
    fm_style_surface(topbar, THEME_BG_PANEL, THEME_BG_PANEL, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

    left = lv_obj_create(topbar);
    lv_obj_set_size(left, topbar_left_w, FM_TOPBAR_H);
    lv_obj_set_pos(left, FM_PAD, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_column(left, 14, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = fm_label(left, LV_SYMBOL_DRIVE " 文件管理器", THEME_TEXT_PRIMARY);
    s_path_label = fm_label(left,
                            usb_storage_is_mounted() ? TRS("选择存储器") : "/",
                            THEME_TEXT_TERTIARY);
    lv_obj_set_width(s_path_label, path_w);
    lv_label_set_long_mode(s_path_label, LV_LABEL_LONG_DOT);

    right = lv_obj_create(topbar);
    lv_obj_set_size(right, topbar_right_w, FM_TOPBAR_H);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -FM_PAD, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_column(right, 6, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    s_power_chip = fm_create_power_chip(right);
    lv_obj_null_on_delete(&s_power_chip);
    lv_obj_add_flag(s_power_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_power_chip, fm_on_power_chip_click, LV_EVENT_CLICKED, NULL);
    s_wifi_chip = fm_status_chip(right, LV_SYMBOL_WIFI, &s_wifi_label);
    lv_obj_null_on_delete(&s_wifi_chip);
    lv_obj_add_flag(s_wifi_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifi_chip, fm_on_wifi_chip_click, LV_EVENT_CLICKED, NULL);

    s_web_button = fm_button(right, LV_SYMBOL_UPLOAD, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_null_on_delete(&s_web_button);
    lv_obj_set_size(s_web_button, 52, 48);
    lv_obj_set_style_pad_all(s_web_button, 0, 0);
    lv_obj_add_event_cb(s_web_button, fm_on_web_button, LV_EVENT_CLICKED, NULL);

    btn = fm_button(right, LV_SYMBOL_CLOSE, THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, 56, 52);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, fm_on_close, LV_EVENT_CLICKED, NULL);

    lv_obj_update_layout(topbar);
    ESP_LOGI(TAG,
             "topbar layout: screen_w=%ld left=%ld,%ld %ldx%ld right=%ld,%ld %ldx%ld power=%ld,%ld %ldx%ld wifi=%ld,%ld %ldx%ld web=%ld,%ld %ldx%ld close=%ld,%ld %ldx%ld",
             (long)screen_w,
             (long)lv_obj_get_x(left),
             (long)lv_obj_get_y(left),
             (long)lv_obj_get_width(left),
             (long)lv_obj_get_height(left),
             (long)lv_obj_get_x(right),
             (long)lv_obj_get_y(right),
             (long)lv_obj_get_width(right),
             (long)lv_obj_get_height(right),
             (long)lv_obj_get_x(s_power_chip),
             (long)lv_obj_get_y(s_power_chip),
             (long)lv_obj_get_width(s_power_chip),
             (long)lv_obj_get_height(s_power_chip),
             (long)lv_obj_get_x(s_wifi_chip),
             (long)lv_obj_get_y(s_wifi_chip),
             (long)lv_obj_get_width(s_wifi_chip),
             (long)lv_obj_get_height(s_wifi_chip),
             (long)lv_obj_get_x(s_web_button),
             (long)lv_obj_get_y(s_web_button),
             (long)lv_obj_get_width(s_web_button),
             (long)lv_obj_get_height(s_web_button),
             (long)lv_obj_get_x(btn),
             (long)lv_obj_get_y(btn),
             (long)lv_obj_get_width(btn),
             (long)lv_obj_get_height(btn));

    toolbar = lv_obj_create(s_overlay);
    lv_obj_set_size(toolbar, LV_PCT(100), FM_TOOLBAR_H);
    lv_obj_set_pos(toolbar, 0, FM_TOPBAR_H);
    fm_style_surface(toolbar, THEME_BG_PRIMARY, THEME_BORDER_PRIMARY, 0);
    lv_obj_set_style_pad_left(toolbar, FM_PAD, 0);
    lv_obj_set_style_pad_right(toolbar, FM_PAD, 0);
    lv_obj_set_style_pad_column(toolbar, 10, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    s_back_button = fm_button(toolbar, LV_SYMBOL_LEFT " 上一级", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_null_on_delete(&s_back_button);
    lv_obj_set_size(s_back_button, 116, 44);
    lv_obj_add_event_cb(s_back_button, fm_on_back, LV_EVENT_CLICKED, NULL);

    btn = fm_button(toolbar, LV_SYMBOL_REFRESH " 刷新", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 132 : 104, 44);
    lv_obj_add_event_cb(btn, fm_on_refresh, LV_EVENT_CLICKED, NULL);

    btn = fm_button(toolbar, LV_SYMBOL_PLUS " 文件夹", THEME_PRIMARY, THEME_PRIMARY);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_add_event_cb(btn, fm_on_mkdir, LV_EVENT_CLICKED, NULL);

    s_paste_button = fm_button(toolbar, LV_SYMBOL_PASTE " 粘贴", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_null_on_delete(&s_paste_button);
    lv_obj_set_size(s_paste_button, 112, 44);
    lv_obj_add_event_cb(s_paste_button, fm_on_paste, LV_EVENT_CLICKED, NULL);

    btn = fm_button(toolbar, "全选", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 74 : 82, 44);
    lv_obj_add_event_cb(btn, fm_on_select_all, LV_EVENT_CLICKED, NULL);

    btn = fm_button(toolbar, LV_SYMBOL_COPY " 复制", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 92 : 96, 44);
    lv_obj_add_event_cb(btn, fm_on_copy_selection, LV_EVENT_CLICKED, NULL);

    btn = fm_button(toolbar, LV_SYMBOL_CUT " 剪切", THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 82 : 96, 44);
    lv_obj_add_event_cb(btn, fm_on_cut_selection, LV_EVENT_CLICKED, NULL);

    btn = fm_button(toolbar, LV_SYMBOL_TRASH " 删除", THEME_DANGER, THEME_DANGER);
    lv_obj_set_size(btn, app_i18n_get_language() == APP_LANGUAGE_EN ? 82 : 96, 44);
    lv_obj_add_event_cb(btn, fm_on_delete_selection, LV_EVENT_CLICKED, NULL);

    s_web_button_label = fm_label(toolbar, TRS("在线"), THEME_TEXT_TERTIARY);
    lv_obj_set_width(s_web_button_label, 0);
    lv_obj_add_flag(s_web_button_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(s_web_button_label, LV_LABEL_LONG_DOT);

    pager = lv_obj_create(toolbar);
    lv_obj_set_flex_grow(pager, 1);
    lv_obj_set_height(pager, LV_PCT(100));
    lv_obj_set_style_bg_opa(pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(pager, 0, 0);
    lv_obj_set_style_pad_column(pager, 8, 0);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pager, LV_OBJ_FLAG_SCROLLABLE);

    s_count_label = fm_label(pager, TRS("0 项"), THEME_TEXT_TERTIARY);
    lv_obj_set_width(s_count_label, 180);
    lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_RIGHT, 0);

    btn = fm_button(pager, LV_SYMBOL_LEFT, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 48, 42);
    lv_obj_add_event_cb(btn, fm_on_prev, LV_EVENT_CLICKED, NULL);

    btn = fm_button(pager, LV_SYMBOL_RIGHT, THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 48, 42);
    lv_obj_add_event_cb(btn, fm_on_next, LV_EVENT_CLICKED, NULL);

    body = lv_obj_create(s_overlay);
    screen_h = lv_display_get_vertical_resolution(NULL);
    lv_obj_set_size(body, LV_PCT(100), screen_h - FM_TOPBAR_H - FM_TOOLBAR_H - 38);
    lv_obj_set_pos(body, 0, FM_TOPBAR_H + FM_TOOLBAR_H);
    lv_obj_set_style_bg_color(body, THEME_BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, FM_PAD, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    s_list = lv_obj_create(body);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100));
    fm_style_surface(s_list, THEME_BG_PRIMARY, THEME_BORDER_PRIMARY, 8);
    lv_obj_set_style_pad_all(s_list, 14, 0);
    lv_obj_set_style_pad_row(s_list, FM_GAP, 0);
    lv_obj_set_style_pad_column(s_list, FM_GAP, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);

    footer = lv_obj_create(s_overlay);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, 32);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(footer, FM_PAD, 0);
    lv_obj_set_style_pad_right(footer, FM_PAD, 0);
    lv_obj_set_style_pad_top(footer, 0, 0);
    lv_obj_set_style_pad_bottom(footer, 0, 0);
    lv_obj_set_style_pad_column(footer, 10, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_web_status_label = fm_label(footer, TRS("在线文件管理器：未就绪"), THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_web_status_label);
    lv_obj_set_flex_grow(s_web_status_label, 4);
    lv_obj_set_height(s_web_status_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_web_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_web_status_label, LV_LABEL_LONG_DOT);

    s_status_label = fm_label(footer, "", THEME_TEXT_TERTIARY);
    lv_obj_null_on_delete(&s_status_label);
    lv_obj_set_flex_grow(s_status_label, 6);
    lv_obj_set_height(s_status_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);

    fm_render();
    fm_update_status_bar();
    fm_update_paste_button();
    fm_update_web_controls();
    fm_update_folder_status();
    s_status_timer = lv_timer_create(fm_status_timer_cb, 1500, NULL);
}
