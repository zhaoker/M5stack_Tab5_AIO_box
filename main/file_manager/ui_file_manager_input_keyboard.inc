static lv_obj_t *fm_create_modal_panel(lv_coord_t w, lv_coord_t h)
{
    fm_close_modal();
    s_modal = lv_obj_create(s_overlay ? s_overlay : lv_scr_act());
    lv_obj_null_on_delete(&s_modal);
    lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(s_modal, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_modal, 0, 0);
    lv_obj_set_style_pad_all(s_modal, 0, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_modal);
    lv_obj_set_size(panel, w, h);
    lv_obj_center(panel);
    fm_style_surface(panel, THEME_BG_PANEL, THEME_BORDER_SECONDARY, 12);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void fm_input_sync_hw_pinyin_target(void)
{
    if (!s_input_hw_pinyin) {
        return;
    }
    if (app_i18n_default_chinese_input()) {
        ui_hw_pinyin_input_set_target(s_input_hw_pinyin, s_input_textarea, s_input_textarea);
    } else {
        ui_hw_pinyin_input_set_target(s_input_hw_pinyin, NULL, NULL);
    }
    ui_hw_pinyin_input_reposition(s_input_hw_pinyin);
}

static void fm_input_hide_keyboard(void)
{
    if (s_input_keyboard) {
        lv_keyboard_set_textarea(s_input_keyboard, NULL);
        lv_obj_add_flag(s_input_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
#if LV_USE_IME_PINYIN != 0
    if (s_input_pinyin_cand_panel) {
        lv_obj_add_flag(s_input_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    ui_hw_pinyin_input_reset(s_input_hw_pinyin);
    ui_pinyin_ime_ext_reset(s_input_pinyin_ext);
    fm_input_sync_hw_pinyin_target();
}

static void fm_input_keyboard_ready(lv_event_t *e)
{
    (void)e;
    fm_input_hide_keyboard();
}

static void fm_input_textarea_focus(lv_event_t *e)
{
    (void)e;
    fm_input_sync_hw_pinyin_target();
    if (s_input_hw_keyboard_present) {
        fm_input_hide_keyboard();
        return;
    }
    if (!s_input_keyboard || !s_input_textarea) {
        return;
    }
    lv_keyboard_set_textarea(s_input_keyboard, s_input_textarea);
    lv_keyboard_set_mode(s_input_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_flag(s_input_keyboard, LV_OBJ_FLAG_HIDDEN);
#if LV_USE_IME_PINYIN != 0
    if (app_i18n_default_chinese_input() && s_input_pinyin_cand_panel) {
        lv_obj_align_to(s_input_pinyin_cand_panel, s_input_keyboard, LV_ALIGN_OUT_TOP_MID, 0, 0);
    } else if (s_input_pinyin_cand_panel) {
        lv_obj_add_flag(s_input_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

static void fm_input_finish_textarea(void)
{
    ui_hw_pinyin_input_reset(s_input_hw_pinyin);
    if (s_input_textarea) {
        lv_obj_remove_state(s_input_textarea, LV_STATE_FOCUSED);
    }
    fm_input_sync_hw_pinyin_target();
}

static bool fm_input_handle_hw_key(ui_hw_textarea_key_t key)
{
    if (!s_input_textarea) {
        return false;
    }
    switch (key) {
    case UI_HW_TEXTAREA_KEY_ENTER:
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        fm_input_finish_textarea();
        return true;
    default:
        return ui_hw_textarea_apply_edit_key(s_input_textarea, key);
    }
}

static void fm_input_insert_hw_text(const char *txt, size_t len, uint8_t modifier)
{
    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;

    if (!s_input_textarea || !txt || len == 0) {
        return;
    }
    if (app_i18n_default_chinese_input() &&
        ui_hw_pinyin_input_handle_event(s_input_hw_pinyin, txt, len, modifier)) {
        return;
    }
    if (ui_hw_textarea_parse_key(txt, len, &key) && fm_input_handle_hw_key(key)) {
        return;
    }
    if (ui_hw_textarea_modifier_blocks_text(modifier)) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = txt[i];
        if (ch == '\r' || ch == '\n') {
            fm_input_finish_textarea();
        } else if (ch == '\b' || (uint8_t)ch == 0x7f) {
            lv_textarea_delete_char(s_input_textarea);
        } else if ((uint8_t)ch >= 0x20) {
            lv_textarea_add_char(s_input_textarea, (uint32_t)(uint8_t)ch);
        }
    }
}

static void fm_text_scroll_keyboard(int direction)
{
    int32_t line_h;
    int32_t current_y;
    int32_t next_y;

    if (!s_text_textarea || direction == 0) {
        return;
    }
    lv_obj_update_layout(s_text_textarea);
    line_h = lv_font_get_line_height(&APP_FONT) + lv_obj_get_style_text_line_space(s_text_textarea, 0);
    if (line_h <= 0) {
        line_h = 18;
    }
    current_y = lv_obj_get_scroll_y(s_text_textarea);
    next_y = current_y + direction * line_h * FM_TEXT_KEYBOARD_SCROLL_LINES;
    if (next_y < 0) {
        next_y = 0;
    }
    lv_obj_scroll_to_y(s_text_textarea, next_y, LV_ANIM_ON);
    fm_text_update_status();
}

static bool fm_handle_viewer_hw_key(ui_hw_textarea_key_t key)
{
    if (!s_viewer) {
        return false;
    }
    switch (key) {
    case UI_HW_TEXTAREA_KEY_LEFT:
    case UI_HW_TEXTAREA_KEY_UP:
        fm_on_viewer_prev(NULL);
        return true;
    case UI_HW_TEXTAREA_KEY_RIGHT:
    case UI_HW_TEXTAREA_KEY_DOWN:
        fm_on_viewer_next(NULL);
        return true;
    case UI_HW_TEXTAREA_KEY_ENTER:
        fm_on_viewer_rotate(NULL);
        return true;
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        fm_close_viewer();
        return true;
    default:
        return true;
    }
}

static bool fm_handle_text_viewer_hw_key(ui_hw_textarea_key_t key)
{
    if (!s_text_viewer || !s_text_textarea) {
        return false;
    }
    switch (key) {
    case UI_HW_TEXTAREA_KEY_LEFT:
    case UI_HW_TEXTAREA_KEY_UP:
        fm_text_scroll_keyboard(-1);
        return true;
    case UI_HW_TEXTAREA_KEY_RIGHT:
    case UI_HW_TEXTAREA_KEY_DOWN:
        fm_text_scroll_keyboard(1);
        return true;
    case UI_HW_TEXTAREA_KEY_HOME:
        lv_obj_scroll_to_y(s_text_textarea, 0, LV_ANIM_ON);
        fm_text_update_status();
        return true;
    case UI_HW_TEXTAREA_KEY_END:
        lv_obj_scroll_to_y(s_text_textarea,
                           lv_obj_get_scroll_y(s_text_textarea) + lv_obj_get_scroll_bottom(s_text_textarea),
                           LV_ANIM_ON);
        fm_text_update_status();
        return true;
    case UI_HW_TEXTAREA_KEY_ENTER:
        fm_on_text_theme(NULL);
        return true;
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        fm_close_text_viewer();
        return true;
    default:
        return true;
    }
}

static bool fm_handle_list_hw_key(ui_hw_textarea_key_t key)
{
    switch (key) {
    case UI_HW_TEXTAREA_KEY_LEFT:
        fm_select_relative(-1);
        return true;
    case UI_HW_TEXTAREA_KEY_RIGHT:
        fm_select_relative(1);
        return true;
    case UI_HW_TEXTAREA_KEY_UP:
        fm_select_relative(-FM_LIST_KEYBOARD_COLUMNS);
        return true;
    case UI_HW_TEXTAREA_KEY_DOWN:
        fm_select_relative(FM_LIST_KEYBOARD_COLUMNS);
        return true;
    case UI_HW_TEXTAREA_KEY_HOME:
        fm_select_index(0, true);
        return true;
    case UI_HW_TEXTAREA_KEY_END:
        if (s_entry_count > 0) {
            fm_select_index(s_entry_count - 1, true);
        }
        return true;
    case UI_HW_TEXTAREA_KEY_ENTER:
        fm_open_selected();
        return true;
    case UI_HW_TEXTAREA_KEY_SPACE:
        fm_show_first_active_action_sheet();
        return true;
    case UI_HW_TEXTAREA_KEY_DELETE_FORWARD:
        fm_delete_first_active();
        return true;
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        fm_go_parent();
        return true;
    default:
        return false;
    }
}

static bool fm_handle_modal_hw_key(ui_hw_textarea_key_t key)
{
    if (!s_modal) {
        return false;
    }
    switch (key) {
    case UI_HW_TEXTAREA_KEY_ESCAPE:
        fm_close_modal();
        return true;
    case UI_HW_TEXTAREA_KEY_ENTER:
        if (s_modal_enter_action == FM_MODAL_ENTER_DELETE) {
            fm_confirm_delete_selection();
        }
        return true;
    default:
        return true;
    }
}

static bool fm_handle_list_hw_shortcut(const tab5_keyboard_event_t *evt)
{
    char ch;

    if (!evt || evt->length != 1 || (evt->modifier & UI_HW_MOD_CTRL) == 0U ||
        (evt->modifier & UI_HW_MOD_ALT) != 0U) {
        return false;
    }
    ch = evt->chars[0];
    if (ch >= 'A' && ch <= 'Z') {
        ch = (char)(ch - 'A' + 'a');
    }
    switch (ch) {
    case 'a':
        fm_select_all_active();
        return true;
    case 'c':
        fm_set_clipboard(FM_CLIPBOARD_COPY);
        return true;
    case 'x':
        fm_set_clipboard(FM_CLIPBOARD_MOVE);
        return true;
    case 'v':
        fm_paste_clipboard(false);
        return true;
    case 'r':
        if (fm_set_active_from_first_active()) {
            fm_show_input_dialog(FM_INPUT_RENAME, s_active_name);
        }
        return true;
    default:
        return false;
    }
}

static bool fm_handle_navigation_hw_key(ui_hw_textarea_key_t key)
{
    if (key == UI_HW_TEXTAREA_KEY_NONE) {
        return false;
    }
    if (fm_handle_viewer_hw_key(key)) {
        return true;
    }
    if (fm_handle_text_viewer_hw_key(key)) {
        return true;
    }
    if (s_modal) {
        return fm_handle_modal_hw_key(key);
    }
    return fm_handle_list_hw_key(key);
}

static void fm_handle_hw_keyboard_event(const tab5_keyboard_event_t *evt)
{
    ui_hw_textarea_key_t key = UI_HW_TEXTAREA_KEY_NONE;

    if (!evt || evt->length == 0) {
        return;
    }
    if (s_input_textarea) {
        fm_input_insert_hw_text(evt->chars, evt->length, evt->modifier);
        return;
    }
    if (!s_viewer && !s_text_viewer && !s_modal && fm_handle_list_hw_shortcut(evt)) {
        return;
    }
    if (ui_hw_textarea_parse_key(evt->chars, evt->length, &key)) {
        (void)fm_handle_navigation_hw_key(key);
    }
}

static void fm_hw_keyboard_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    static uint32_t s_last_probe = 0;

    if (!s_input_hw_keyboard_present) {
        uint32_t now = lv_tick_get();
        if (s_last_probe == 0 || now - s_last_probe >= 1000) {
            s_last_probe = now;
            if (tab5_keyboard_init() == ESP_OK) {
                s_input_hw_keyboard_present = true;
                fm_input_hide_keyboard();
                fm_render();
                ESP_LOGI(TAG, "Physical keyboard detected; file manager soft keyboard disabled");
            }
        }
        return;
    }

    for (int i = 0; i < 8; i++) {
        tab5_keyboard_event_t evt;
        esp_err_t err = tab5_keyboard_read_event(&evt);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "hardware keyboard read failed: %s", esp_err_to_name(err));
            s_input_hw_keyboard_present = false;
            tab5_keyboard_deinit();
            if (s_input_textarea) {
                fm_input_textarea_focus(NULL);
            }
            fm_render();
            break;
        }
        fm_handle_hw_keyboard_event(&evt);
    }
}

static void fm_destroy_input_method(void)
{
    ui_hw_pinyin_input_destroy(&s_input_hw_pinyin);
    ui_pinyin_ime_ext_forget_objects(s_input_pinyin_ext);
    s_input_keyboard = NULL;
#if LV_USE_IME_PINYIN != 0
    s_input_pinyin_ime = NULL;
    s_input_pinyin_cand_panel = NULL;
#endif
}

static void fm_close_modal(void)
{
    bool clear_delete_selection = s_modal_enter_action == FM_MODAL_ENTER_DELETE;

    fm_destroy_input_method();
    s_input_textarea = NULL;
    s_input_status = NULL;
    s_modal_enter_action = FM_MODAL_ENTER_NONE;
    if (s_modal) {
        lv_obj_del(s_modal);
        s_modal = NULL;
    }
    ui_pinyin_ime_ext_destroy(&s_input_pinyin_ext);
    if (clear_delete_selection) {
        fm_clear_delete_selection();
    }
}

static void fm_on_modal_cancel(lv_event_t *e)
{
    (void)e;
    fm_close_modal();
}

static void fm_on_input_save(lv_event_t *e)
{
    const char *name;
    char target[FM_PATH_MAX];
    char parent[FM_PATH_MAX];
    esp_err_t result = ESP_OK;

    (void)e;
    if (!s_input_textarea) {
        return;
    }
    name = lv_textarea_get_text(s_input_textarea);
    if (!fm_is_safe_name(name)) {
        if (s_input_status) {
            lv_label_set_text(s_input_status, TRS("文件名不能包含 / 或特殊路径。"));
        }
        return;
    }

    if (s_input_mode == FM_INPUT_MKDIR) {
        if (!fm_join_path(target, sizeof(target), s_current_path, name)) {
            result = ESP_ERR_INVALID_SIZE;
        } else if (mkdir(target, 0775) != 0) {
            result = ESP_FAIL;
        }
    } else if (s_input_mode == FM_INPUT_RENAME) {
        strlcpy(parent, s_active_path, sizeof(parent));
        char *slash = strrchr(parent, '/');
        if (!slash || !fm_path_in_root(s_active_path)) {
            result = ESP_FAIL;
        } else {
            slash[1] = '\0';
            if (!fm_snprintf_ok(snprintf(target, sizeof(target), "%s%s", parent, name), sizeof(target)) ||
                !fm_path_in_root(target)) {
                result = ESP_ERR_INVALID_SIZE;
            } else if (access(target, F_OK) == 0) {
                result = ESP_ERR_INVALID_STATE;
            } else if (rename(s_active_path, target) != 0) {
                result = ESP_FAIL;
            }
        }
    }

    if (result != ESP_OK) {
        if (s_input_status) {
            lv_label_set_text_fmt(s_input_status, TRS("操作失败 (errno=%d)"), errno);
        }
        return;
    }
    fm_close_modal();
    fm_set_status(TRS("完成"), THEME_SUCCESS_LIGHT);
    fm_render();
}

static void fm_show_input_dialog(fm_input_mode_t mode, const char *initial)
{
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *row;
    lv_obj_t *btn;

    s_input_mode = mode;
    panel = fm_create_modal_panel(720, 250);

    title = fm_label(panel,
                     mode == FM_INPUT_MKDIR ? TRS("新建文件夹") : TRS("重命名"),
                     THEME_TEXT_PRIMARY);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    s_input_textarea = lv_textarea_create(panel);
    lv_obj_set_width(s_input_textarea, LV_PCT(100));
    lv_obj_set_height(s_input_textarea, 58);
    lv_obj_set_style_text_font(s_input_textarea, &APP_FONT, 0);
    lv_textarea_set_one_line(s_input_textarea, true);
    lv_textarea_set_max_length(s_input_textarea, FM_NAME_MAX - 1);
    lv_textarea_set_text(s_input_textarea, initial ? initial : "");
    lv_obj_add_event_cb(s_input_textarea, fm_input_textarea_focus, LV_EVENT_FOCUSED, NULL);

    s_input_status = fm_label(panel, "", THEME_WARNING_LIGHT);
    lv_obj_set_width(s_input_status, LV_PCT(100));

    row = lv_obj_create(panel);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 48);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    btn = fm_button(row, TRS("取消"), THEME_BG_HOVER, THEME_BORDER_SECONDARY);
    lv_obj_set_size(btn, 130, 46);
    lv_obj_add_event_cb(btn, fm_on_modal_cancel, LV_EVENT_CLICKED, NULL);

    btn = fm_button(row, TRS("保存"), THEME_PRIMARY, THEME_PRIMARY);
    lv_obj_set_size(btn, 130, 46);
    lv_obj_add_event_cb(btn, fm_on_input_save, LV_EVENT_CLICKED, NULL);

    s_input_keyboard = lv_keyboard_create(s_modal);
    lv_obj_align(s_input_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_popovers(s_input_keyboard, true);
    lv_keyboard_set_textarea(s_input_keyboard, s_input_textarea);
    lv_obj_set_style_bg_color(s_input_keyboard, THEME_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_input_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_input_keyboard, THEME_BORDER_SECONDARY, 0);
    lv_obj_set_style_border_width(s_input_keyboard, 1, 0);
    lv_obj_set_style_bg_color(s_input_keyboard, THEME_BG_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_input_keyboard, THEME_BG_HOVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_input_keyboard, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_input_keyboard, APP_KEYBOARD_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_input_keyboard, APP_KEYBOARD_FONT, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_input_keyboard, fm_input_keyboard_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_input_keyboard, fm_input_keyboard_ready, LV_EVENT_CANCEL, NULL);

    ui_hw_pinyin_input_config_t hw_pinyin_cfg = {
        .parent = s_modal,
        .target_ta = s_input_textarea,
        .anchor = s_input_textarea,
        .width = 0,
        .height = 52,
    };
    s_input_hw_pinyin = ui_hw_pinyin_input_create(&hw_pinyin_cfg);
    fm_input_sync_hw_pinyin_target();

    if (s_input_hw_keyboard_present) {
        lv_obj_add_flag(s_input_keyboard, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Physical keyboard detected; file manager soft keyboard disabled");
    } else {
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, -160);
    }

#if LV_USE_IME_PINYIN != 0
    if (app_i18n_default_chinese_input()) {
        s_input_pinyin_ime = lv_ime_pinyin_create(s_modal);
        lv_obj_set_style_text_font(s_input_pinyin_ime, APP_KEYBOARD_FONT, 0);
        lv_ime_pinyin_set_keyboard(s_input_pinyin_ime, s_input_keyboard);
        lv_ime_pinyin_set_dict(s_input_pinyin_ime, g_app_pinyin_dict);
        lv_ime_pinyin_set_mode(s_input_pinyin_ime, LV_IME_PINYIN_MODE_K26);

        s_input_pinyin_cand_panel = lv_ime_pinyin_get_cand_panel(s_input_pinyin_ime);
        lv_obj_set_size(s_input_pinyin_cand_panel, lv_display_get_horizontal_resolution(NULL), 52);
        lv_obj_align_to(s_input_pinyin_cand_panel, s_input_keyboard, LV_ALIGN_OUT_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(s_input_pinyin_cand_panel, THEME_BG_PANEL, 0);
        lv_obj_set_style_bg_opa(s_input_pinyin_cand_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_input_pinyin_cand_panel, 1, 0);
        lv_obj_set_style_border_color(s_input_pinyin_cand_panel, THEME_BORDER_SECONDARY, 0);
        lv_obj_set_style_radius(s_input_pinyin_cand_panel, 0, 0);
        lv_obj_set_style_pad_all(s_input_pinyin_cand_panel, 2, 0);
        lv_obj_set_style_pad_gap(s_input_pinyin_cand_panel, 0, 0);
        lv_obj_set_style_text_font(s_input_pinyin_cand_panel, &APP_IME_CAND_FONT, 0);
        lv_obj_set_style_text_font(s_input_pinyin_cand_panel, &APP_IME_CAND_FONT, LV_PART_ITEMS);
        lv_obj_set_style_text_color(s_input_pinyin_cand_panel, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(s_input_pinyin_cand_panel, THEME_BG_CARD, LV_PART_ITEMS);
        lv_obj_set_style_radius(s_input_pinyin_cand_panel, 0, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(s_input_pinyin_cand_panel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(s_input_pinyin_cand_panel, THEME_BG_HOVER, LV_PART_ITEMS | LV_STATE_PRESSED);
        s_input_pinyin_ext = ui_pinyin_ime_ext_create(s_input_pinyin_ime, s_input_keyboard, s_input_pinyin_cand_panel);
    }
#endif

    lv_obj_add_state(s_input_textarea, LV_STATE_FOCUSED);
    fm_input_textarea_focus(NULL);
}
