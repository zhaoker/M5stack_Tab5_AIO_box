#if LV_USE_IME_PINYIN != 0
static bool pinyin_ime_has_composition(void)
{
    if (!s_pinyin_enabled || !s_pinyin_ime) return false;
    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    return ime->ta_count > 0;
}

static bool pinyin_cand_visible(void)
{
    return s_pinyin_enabled &&
           s_pinyin_cand_panel &&
           !lv_obj_has_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
}

static bool pinyin_phrase_mode_enabled(void)
{
    return app_settings_get_ime_input_mode() == APP_SETTINGS_IME_INPUT_MODE_PHRASE;
}

static void pinyin_set_candidate_map_kind(pinyin_cand_map_kind_t kind)
{
    s_pinyin_cand_map_kind = kind;
    s_hw_pinyin_cand_map_active = (kind != PINYIN_CAND_MAP_DEFAULT);
}
#endif

static const char *keyboard_selected_button_text(void)
{
    if (!s_keyboard || !s_kb_visible) return NULL;
    uint32_t btn = lv_buttonmatrix_get_selected_button(s_keyboard);
    if (btn == LV_BUTTONMATRIX_BUTTON_NONE) return NULL;
    return lv_buttonmatrix_get_button_text(s_keyboard, btn);
}

static bool keyboard_value_is_pinyin_comp_key(const char *txt)
{
#if LV_USE_IME_PINYIN != 0
    if (!s_pinyin_enabled) return false;
    if (s_ctrl_active || s_alt_active) return false;
    lv_keyboard_mode_t mode = lv_keyboard_get_mode(s_keyboard);
    if (mode != LV_KEYBOARD_MODE_TEXT_LOWER && mode != LV_KEYBOARD_MODE_TEXT_UPPER) return false;

    if (!txt || txt[1] != '\0') return false;
    return (txt[0] >= 'a' && txt[0] <= 'z') || (txt[0] >= 'A' && txt[0] <= 'Z');
#else
    (void)txt;
    return false;
#endif
}

static void clear_vta_text(void)
{
    if (!s_vta) return;
    s_vta_internal_update = true;
    lv_textarea_set_text(s_vta, "");
    s_vta_internal_update = false;
}

#if LV_USE_IME_PINYIN != 0
static uint16_t pinyin_single_candidate_total(const lv_ime_pinyin_t *ime)
{
    return (ime && ime->cand_str) ? ime->cand_num : 0;
}

static uint16_t pinyin_phrase_candidate_total(const lv_ime_pinyin_t *ime)
{
    uint32_t total = (uint32_t)pinyin_single_candidate_total(ime) + s_pinyin_phrase_count;
    return total > UINT16_MAX ? UINT16_MAX : (uint16_t)total;
}

static uint8_t pinyin_candidate_count_on_page(const lv_ime_pinyin_t *ime)
{
    if (!ime) return 0;
    uint16_t total = 0;
    if (s_pinyin_cand_map_kind == PINYIN_CAND_MAP_PHRASE) {
        total = pinyin_phrase_candidate_total(ime);
    } else if (ime->cand_str) {
        total = ime->cand_num;
    }
    if (total == 0) return 0;
    uint16_t offset = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM;
    if (offset >= total) return 0;
    uint16_t remaining = total - offset;
    return (uint8_t)(remaining > LV_IME_PINYIN_CAND_TEXT_NUM ? LV_IME_PINYIN_CAND_TEXT_NUM : remaining);
}

static uint16_t pinyin_last_page_index(const lv_ime_pinyin_t *ime)
{
    if (!ime) return 0;
    uint16_t total = 0;
    if (s_pinyin_cand_map_kind == PINYIN_CAND_MAP_PHRASE) {
        total = pinyin_phrase_candidate_total(ime);
    } else if (ime->cand_str) {
        total = ime->cand_num;
    }
    if (total == 0) return 0;
    return (uint16_t)((total - 1) / LV_IME_PINYIN_CAND_TEXT_NUM);
}

static uint16_t pinyin_candidate_total(const lv_ime_pinyin_t *ime)
{
    if (!ime) return 0;
    if (s_pinyin_cand_map_kind == PINYIN_CAND_MAP_PHRASE) {
        return pinyin_phrase_candidate_total(ime);
    }
    return ime->cand_str ? ime->cand_num : 0;
}

static void pinyin_hide_hardware_digit_labels(void)
{
    for (uint8_t i = 0; i < IME_HW_CAND_DIGIT_COUNT; i++) {
        if (s_hw_pinyin_digit_labels[i]) {
            lv_obj_add_flag(s_hw_pinyin_digit_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void pinyin_position_hardware_digit_labels(uint8_t count)
{
    if (!s_pinyin_cand_panel || count == 0 || !s_hw_pinyin_cand_map_active) {
        pinyin_hide_hardware_digit_labels();
        return;
    }

    if (count > IME_HW_CAND_DIGIT_COUNT) {
        count = IME_HW_CAND_DIGIT_COUNT;
    }

    lv_obj_update_layout(s_pinyin_cand_panel);
    lv_coord_t panel_w = lv_obj_get_width(s_pinyin_cand_panel);
    lv_coord_t panel_h = lv_obj_get_height(s_pinyin_cand_panel);
    if (panel_w <= 0 || panel_h <= 0) {
        pinyin_hide_hardware_digit_labels();
        return;
    }

    lv_coord_t pad_left = lv_obj_get_style_pad_left(s_pinyin_cand_panel, LV_PART_MAIN);
    lv_coord_t pad_right = lv_obj_get_style_pad_right(s_pinyin_cand_panel, LV_PART_MAIN);
    lv_coord_t pad_top = lv_obj_get_style_pad_top(s_pinyin_cand_panel, LV_PART_MAIN);
    lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(s_pinyin_cand_panel, LV_PART_MAIN);
    lv_coord_t gap = lv_obj_get_style_pad_column(s_pinyin_cand_panel, LV_PART_MAIN);
    lv_coord_t content_w = panel_w - pad_left - pad_right;
    lv_coord_t content_h = panel_h - pad_top - pad_bottom;
    const uint8_t button_count = LV_IME_PINYIN_CAND_TEXT_NUM + 2;
    lv_coord_t cell_w = (content_w - (gap * (button_count - 1))) / button_count;
    if (cell_w < 1 || content_h < 1) {
        pinyin_hide_hardware_digit_labels();
        return;
    }

    for (uint8_t i = 0; i < IME_HW_CAND_DIGIT_COUNT; i++) {
        lv_obj_t *label = s_hw_pinyin_digit_labels[i];
        if (!label) {
            continue;
        }

        if (i >= count) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_coord_t cell_x = pad_left + ((i + 1) * (cell_w + gap));
        lv_obj_set_size(label, 18, content_h);
        lv_obj_set_pos(label, cell_x + 4, pad_top);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    }
}

static void pinyin_create_hardware_digit_labels(void)
{
    if (!s_pinyin_cand_panel) {
        return;
    }

    static const char *digits[IME_HW_CAND_DIGIT_COUNT] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
    };

    for (uint8_t i = 0; i < IME_HW_CAND_DIGIT_COUNT; i++) {
        s_hw_pinyin_digit_labels[i] = lv_label_create(s_pinyin_cand_panel);
        lv_label_set_text(s_hw_pinyin_digit_labels[i], digits[i]);
        lv_obj_set_style_text_font(s_hw_pinyin_digit_labels[i], IME_HW_CAND_DIGIT_FONT, 0);
        lv_obj_set_style_text_color(s_hw_pinyin_digit_labels[i], THEME_WARNING_LIGHT, 0);
        lv_obj_set_style_text_align(s_hw_pinyin_digit_labels[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_clear_flag(s_hw_pinyin_digit_labels[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_hw_pinyin_digit_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void pinyin_clear_hardware_candidates(void)
{
    s_pinyin_phrase_count = 0;
    memset(s_pinyin_phrase_words, 0, sizeof(s_pinyin_phrase_words));
    memset(s_hw_pinyin_cand_buf, 0, sizeof(s_hw_pinyin_cand_buf));
    s_hw_pinyin_cand_map[0] = "<";
    for (uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
        s_hw_pinyin_cand_map[i + 1] = " ";
    }
    s_hw_pinyin_cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 1] = ">";
    s_hw_pinyin_cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 2] = "";
    if (s_pinyin_cand_panel && (s_hw_keyboard_present || s_hw_pinyin_composing)) {
        lv_buttonmatrix_set_map(s_pinyin_cand_panel, s_hw_pinyin_cand_map);
        pinyin_set_candidate_map_kind(PINYIN_CAND_MAP_HARDWARE);
    } else {
        pinyin_restore_default_candidate_map();
    }
    pinyin_hide_hardware_digit_labels();
}

static void pinyin_restore_default_candidate_map(void)
{
    if (s_pinyin_cand_panel && s_pinyin_default_cand_map && s_pinyin_cand_map_kind != PINYIN_CAND_MAP_DEFAULT) {
        lv_buttonmatrix_set_map(s_pinyin_cand_panel, s_pinyin_default_cand_map);
    }
    s_pinyin_phrase_count = 0;
    memset(s_pinyin_phrase_words, 0, sizeof(s_pinyin_phrase_words));
    pinyin_set_candidate_map_kind(PINYIN_CAND_MAP_DEFAULT);
    pinyin_hide_hardware_digit_labels();
}

static bool pinyin_normalize_phrase_query(const char *py, char *normalized, size_t normalized_len)
{
    size_t i = 0;

    if (!py || !py[0] || !normalized || normalized_len == 0) return false;

    for (; py[i] && i < normalized_len - 1; i++) {
        char ch = py[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch < 'a' || ch > 'z') {
            return false;
        }
        normalized[i] = ch;
    }
    if (py[i] != '\0') return false;
    normalized[i] = '\0';

    return true;
}

static bool pinyin_refresh_phrase_candidates(lv_ime_pinyin_t *ime)
{
    char query[sizeof(ime->input_char)];
    uint8_t count;
    uint16_t page_offset;
    uint16_t single_count;

    if (!s_pinyin_cand_panel || !ime || !pinyin_phrase_mode_enabled()) {
        return false;
    }

    if (!pinyin_normalize_phrase_query(ime->input_char, query, sizeof(query))) {
        s_pinyin_phrase_count = 0;
        return false;
    }

    s_pinyin_phrase_count =
        pinyin_phrase_collect(query, s_pinyin_phrase_words, IME_PHRASE_CAND_MAX);
    if (s_pinyin_phrase_count == 0) {
        return false;
    }

    pinyin_set_candidate_map_kind(PINYIN_CAND_MAP_PHRASE);
    memset(s_hw_pinyin_cand_buf, 0, sizeof(s_hw_pinyin_cand_buf));
    s_hw_pinyin_cand_map[0] = "<";
    for (uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
        s_hw_pinyin_cand_map[i + 1] = " ";
    }
    s_hw_pinyin_cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 1] = ">";
    s_hw_pinyin_cand_map[LV_IME_PINYIN_CAND_TEXT_NUM + 2] = "";

    page_offset = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM;
    single_count = pinyin_single_candidate_total(ime);
    count = pinyin_candidate_count_on_page(ime);
    for (uint8_t i = 0; i < count; i++) {
        uint16_t candidate_index = page_offset + i;
        if (candidate_index < single_count && ime->cand_str) {
            uint16_t byte_offset = candidate_index * 3;
            memcpy(s_hw_pinyin_cand_buf[i], &ime->cand_str[byte_offset], 3);
            s_hw_pinyin_cand_buf[i][3] = '\0';
            s_hw_pinyin_cand_map[i + 1] = s_hw_pinyin_cand_buf[i];
        } else {
            uint16_t phrase_index = candidate_index - single_count;
            const char *word = s_pinyin_phrase_words[phrase_index];
            if (word) {
                s_hw_pinyin_cand_map[i + 1] = word;
            }
        }
    }

    lv_buttonmatrix_set_map(s_pinyin_cand_panel, s_hw_pinyin_cand_map);
    lv_obj_remove_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    pinyin_position_hardware_digit_labels(count);
    return true;
}

static bool pinyin_get_candidate_text(lv_ime_pinyin_t *ime, uint8_t idx, const char **text, char *buf, size_t buf_len)
{
    uint16_t candidate_index;

    if (!ime || !text || idx >= LV_IME_PINYIN_CAND_TEXT_NUM) {
        return false;
    }

    candidate_index = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM + idx;
    if (candidate_index >= pinyin_candidate_total(ime)) {
        return false;
    }

    if (s_pinyin_cand_map_kind == PINYIN_CAND_MAP_PHRASE) {
        uint16_t single_count = pinyin_single_candidate_total(ime);
        if (candidate_index < single_count) {
            if (!ime->cand_str || !buf || buf_len < 4) {
                return false;
            }
            uint16_t byte_offset = candidate_index * 3;
            buf[0] = ime->cand_str[byte_offset];
            buf[1] = ime->cand_str[byte_offset + 1];
            buf[2] = ime->cand_str[byte_offset + 2];
            buf[3] = '\0';
            *text = buf;
            return true;
        }

        const char *word = s_pinyin_phrase_words[candidate_index - single_count];
        if (!word) return false;
        *text = word;
        return true;
    }

    if (!ime->cand_str || !buf || buf_len < 4) {
        return false;
    }

    uint16_t byte_offset = candidate_index * 3;
    buf[0] = ime->cand_str[byte_offset];
    buf[1] = ime->cand_str[byte_offset + 1];
    buf[2] = ime->cand_str[byte_offset + 2];
    buf[3] = '\0';
    *text = buf;
    return true;
}

static void pinyin_refresh_hardware_candidates(void)
{
    if (!s_pinyin_ime || !s_pinyin_cand_panel) return;
    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;

    if (pinyin_refresh_phrase_candidates(ime)) {
        return;
    }

    if (!s_hw_keyboard_present && !s_hw_pinyin_composing) {
        pinyin_restore_default_candidate_map();
        return;
    }

    pinyin_clear_hardware_candidates();
    uint8_t count = pinyin_candidate_count_on_page(ime);
    if (count == 0) {
        lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint16_t page_offset = ime->py_page * LV_IME_PINYIN_CAND_TEXT_NUM * 3;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(s_hw_pinyin_cand_buf[i], &ime->cand_str[page_offset + i * 3], 3);
        s_hw_pinyin_cand_buf[i][3] = '\0';
        s_hw_pinyin_cand_map[i + 1] = s_hw_pinyin_cand_buf[i];
    }
    lv_buttonmatrix_set_map(s_pinyin_cand_panel, s_hw_pinyin_cand_map);
    pinyin_set_candidate_map_kind(PINYIN_CAND_MAP_HARDWARE);
    lv_obj_remove_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    pinyin_position_hardware_digit_labels(count);
}

static char *pinyin_find_candidates(const char *py, uint16_t *cand_num)
{
    if (cand_num) *cand_num = 0;
    if (!s_pinyin_ime || !py || py[0] == '\0') return NULL;
    if (py[0] == 'i' || py[0] == 'u' || py[0] == 'v') return NULL;

    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    if (!ime->dict) return NULL;

    char first = py[0];
    if (first >= 'A' && first <= 'Z') first = (char)(first - 'A' + 'a');
    if (first < 'a' || first > 'z') return NULL;

    uint8_t offset = (uint8_t)(first - 'a');
    uint16_t count = ime->py_num[offset];
    if (count == 0) return NULL;

    const lv_pinyin_dict_t *entry = &ime->dict[ime->py_pos[offset]];
    size_t len = strlen(py);
    while (count--) {
        if (!entry->py || !entry->py_mb) return NULL;
        if (strncmp(py, entry->py, len) == 0) {
            if (cand_num) *cand_num = (uint16_t)(strlen(entry->py_mb) / 3);
            return (char *)entry->py_mb;
        }
        entry++;
    }
    return NULL;
}

static void pinyin_rebuild_from_input(void)
{
    if (!s_pinyin_ime) return;
    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    ime->cand_str = pinyin_find_candidates(ime->input_char, &ime->cand_num);
    ime->py_page = 0;
    s_hw_pinyin_composing = ime->ta_count > 0;
    pinyin_refresh_hardware_candidates();
}

static void pinyin_ime_reset_composition(void)
{
    if (s_pinyin_ime) {
        lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
        ime->ta_count = 0;
        ime->cand_num = 0;
        ime->py_page = 0;
        ime->cand_str = NULL;
        memset(ime->input_char, 0, sizeof(ime->input_char));
#if LV_IME_PINYIN_USE_K9_MODE
        ime->k9_py_ll_pos = 0;
        ime->k9_legal_py_count = 0;
        ime->k9_input_str_len = 0;
        memset(ime->k9_input_str, 0, sizeof(ime->k9_input_str));
#endif
    }
    if (s_pinyin_cand_panel) {
        pinyin_restore_default_candidate_map();
        lv_obj_add_flag(s_pinyin_cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_pinyin_input_label) {
        lv_label_set_text(s_pinyin_input_label, "");
    }
    if (s_pinyin_input_bar) {
        lv_obj_add_flag(s_pinyin_input_bar, LV_OBJ_FLAG_HIDDEN);
    }
    s_hw_pinyin_composing = false;
    clear_vta_text();
}

static bool pinyin_commit_raw_input(void)
{
    if (!s_pinyin_ime || !pinyin_ime_has_composition()) return false;

    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    char raw[sizeof(ime->input_char)];
    snprintf(raw, sizeof(raw), "%s", ime->input_char);

    pinyin_ime_reset_composition();
    if (raw[0] != '\0') {
        ssh_send_terminal_text(raw, strlen(raw));
    }
    reposition_elements();
    return true;
}

static bool pinyin_handle_hardware_char(char ch)
{
    if (!s_pinyin_enabled || !s_pinyin_ime || s_ctrl_active || s_alt_active) return false;
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    if (ch < 'a' || ch > 'z') return false;

    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    size_t len = strlen(ime->input_char);
    if (len >= sizeof(ime->input_char) - 1) return true;

    ime->input_char[len] = ch;
    ime->input_char[len + 1] = '\0';
    ime->ta_count++;
    s_hw_pinyin_composing = true;
    pinyin_rebuild_from_input();
    reposition_elements();
    return true;
}

static bool pinyin_backspace_hardware(void)
{
    if (!s_pinyin_ime || !pinyin_ime_has_composition()) return false;
    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    size_t len = strlen(ime->input_char);
    if (len == 0 || ime->ta_count == 0) {
        pinyin_ime_reset_composition();
        reposition_elements();
        return true;
    }

    ime->input_char[len - 1] = '\0';
    ime->ta_count--;
    if (ime->ta_count == 0) {
        pinyin_ime_reset_composition();
    } else {
        pinyin_rebuild_from_input();
    }
    reposition_elements();
    return true;
}

static bool pinyin_page_hardware_candidates(bool next)
{
    if (!s_pinyin_enabled || !s_pinyin_ime || !pinyin_ime_has_composition()) return false;
    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    if (pinyin_candidate_total(ime) == 0) return true;

    if (!s_hw_keyboard_present && !s_hw_pinyin_composing && !s_hw_pinyin_cand_map_active && s_pinyin_cand_panel) {
        uint32_t btn_id = next ? (LV_IME_PINYIN_CAND_TEXT_NUM + 1) : 0;
        lv_buttonmatrix_set_selected_button(s_pinyin_cand_panel, btn_id);
        lv_obj_send_event(s_pinyin_cand_panel, LV_EVENT_VALUE_CHANGED, NULL);
        reposition_elements();
        return true;
    }

    uint16_t last_page = pinyin_last_page_index(ime);
    if (next) {
        if (ime->py_page < last_page) {
            ime->py_page++;
        }
    } else if (ime->py_page > 0) {
        ime->py_page--;
    }
    pinyin_refresh_hardware_candidates();
    reposition_elements();
    return true;
}

static bool pinyin_handle_page_key_text(const char *txt, size_t len)
{
    if (!s_pinyin_enabled || !pinyin_ime_has_composition() || !txt) return false;

    if (len == 0) {
        len = strlen(txt);
    }

    const char *key = txt;
    char key_buf[16];
    if (len > 0 && len < sizeof(key_buf)) {
        memcpy(key_buf, txt, len);
        key_buf[len] = '\0';
        key = key_buf;
    }

    if ((len == 3 && memcmp(txt, "\x1b[A", 3) == 0) ||
        (len == 3 && memcmp(txt, "\x1b[D", 3) == 0) ||
        ascii_streq_ignore_case(key, "UP") ||
        ascii_streq_ignore_case(key, "ArrowUp") ||
        ascii_streq_ignore_case(key, "LEFT") ||
        ascii_streq_ignore_case(key, "ArrowLeft") ||
        strcmp(key, LV_SYMBOL_UP) == 0 ||
        strcmp(key, LV_SYMBOL_LEFT) == 0) {
        return pinyin_page_hardware_candidates(false);
    }

    if ((len == 3 && memcmp(txt, "\x1b[B", 3) == 0) ||
        (len == 3 && memcmp(txt, "\x1b[C", 3) == 0) ||
        ascii_streq_ignore_case(key, "DOWN") ||
        ascii_streq_ignore_case(key, "ArrowDown") ||
        ascii_streq_ignore_case(key, "RIGHT") ||
        ascii_streq_ignore_case(key, "ArrowRight") ||
        strcmp(key, LV_SYMBOL_DOWN) == 0 ||
        strcmp(key, LV_SYMBOL_RIGHT) == 0) {
        return pinyin_page_hardware_candidates(true);
    }

    return false;
}

static bool pinyin_select_candidate_by_digit(char digit)
{
    if (!s_pinyin_enabled || !s_pinyin_ime || !pinyin_ime_has_composition()) return false;
    uint8_t idx = (digit == '0') ? 9 : (uint8_t)(digit - '1');
    if (idx >= LV_IME_PINYIN_CAND_TEXT_NUM) return false;

    return pinyin_commit_candidate_index(idx);
}

static bool pinyin_commit_candidate_index(uint8_t idx)
{
    if (!s_pinyin_enabled || !s_pinyin_ime || !pinyin_ime_has_composition()) return false;
    if (idx >= LV_IME_PINYIN_CAND_TEXT_NUM) return false;

    lv_ime_pinyin_t *ime = (lv_ime_pinyin_t *)s_pinyin_ime;
    uint8_t count = pinyin_candidate_count_on_page(ime);
    const char *selected = NULL;
    char selected_buf[4];
    if (idx >= count || !pinyin_get_candidate_text(ime, idx, &selected, selected_buf, sizeof(selected_buf))) return false;

    pinyin_ime_reset_composition();
    ssh_send_terminal_text(selected, strlen(selected));
    reposition_elements();
    return true;
}

static void on_pinyin_cand_panel_preprocess(lv_event_t *e)
{
    if (!s_pinyin_ime || !s_pinyin_cand_panel || s_pinyin_cand_map_kind == PINYIN_CAND_MAP_DEFAULT) {
        return;
    }

    lv_obj_t *cand_panel = lv_event_get_current_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(cand_panel);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    if (id == 0) {
        (void)pinyin_page_hardware_candidates(false);
        lv_event_stop_processing(e);
        return;
    }

    if (id == (LV_IME_PINYIN_CAND_TEXT_NUM + 1)) {
        (void)pinyin_page_hardware_candidates(true);
        lv_event_stop_processing(e);
        return;
    }

    if (id > 0 && id <= LV_IME_PINYIN_CAND_TEXT_NUM) {
        uint8_t idx = (uint8_t)(id - 1);
        if (pinyin_commit_candidate_index(idx)) {
            lv_event_stop_processing(e);
        }
    }
}

#endif
