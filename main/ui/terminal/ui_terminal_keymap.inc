static lv_obj_t *s_vta = NULL;

static bool is_ascii_letters_only(const char *txt)
{
    if (!txt || !txt[0]) return false;
    for (const unsigned char *p = (const unsigned char *)txt; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
            return false;
        }
    }
    return true;
}

static bool ascii_streq_ignore_case(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static bool keyboard_text_is_mode_key(const char *txt)
{
    return txt &&
           (strcmp(txt, "abc") == 0 ||
            strcmp(txt, "ABC") == 0 ||
            strcmp(txt, "1#") == 0 ||
            strcmp(txt, "123") == 0 ||
            strcmp(txt, LV_SYMBOL_KEYBOARD) == 0 ||
            strcmp(txt, LV_SYMBOL_OK) == 0);
}

static bool keyboard_text_is_direct_ssh_key(const char *txt)
{
    return txt &&
           (strcmp(txt, LV_SYMBOL_LEFT) == 0 ||
            strcmp(txt, LV_SYMBOL_RIGHT) == 0 ||
            strcmp(txt, LV_SYMBOL_UP) == 0 ||
            strcmp(txt, LV_SYMBOL_DOWN) == 0 ||
            strcmp(txt, LV_SYMBOL_BACKSPACE) == 0 ||
            strcmp(txt, LV_SYMBOL_NEW_LINE) == 0 ||
            strcmp(txt, "Tab") == 0 ||
            strcmp(txt, "TAB") == 0 ||
            strcmp(txt, "Delete") == 0 ||
            strcmp(txt, "Del") == 0 ||
            strcmp(txt, "Enter") == 0);
}

static bool terminal_key_text_to_sequence(const char *txt, size_t len, const char **seq, size_t *seq_len)
{
    if (!txt || !seq || !seq_len) return false;
    if (len == 0) {
        len = strlen(txt);
    }

    if ((len == 1 && txt[0] == '\r') ||
        (len == 1 && txt[0] == '\n') ||
        ascii_streq_ignore_case(txt, "Enter") ||
        strcmp(txt, LV_SYMBOL_NEW_LINE) == 0 ||
        strcmp(txt, LV_SYMBOL_OK) == 0) {
        *seq = "\r";
        *seq_len = 1;
        return true;
    }
    if ((len == 1 && txt[0] == '\t') ||
        ascii_streq_ignore_case(txt, "Tab")) {
        *seq = "\t";
        *seq_len = 1;
        return true;
    }
    if ((len == 1 && txt[0] == '\b') ||
        (len == 1 && (uint8_t)txt[0] == 0x7f) ||
        ascii_streq_ignore_case(txt, "Backspace") ||
        strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        *seq = "\x7f";
        *seq_len = 1;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "Delete") ||
        ascii_streq_ignore_case(txt, "Del")) {
        *seq = "\x1b[3~";
        *seq_len = 4;
        return true;
    }
    if ((len == 1 && txt[0] == 0x1b) ||
        ascii_streq_ignore_case(txt, "ESC") ||
        ascii_streq_ignore_case(txt, "Escape")) {
        *seq = "\x1b";
        *seq_len = 1;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "UP") ||
        ascii_streq_ignore_case(txt, "ArrowUp") ||
        strcmp(txt, LV_SYMBOL_UP) == 0) {
        *seq = "\x1b[A";
        *seq_len = 3;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "DOWN") ||
        ascii_streq_ignore_case(txt, "ArrowDown") ||
        strcmp(txt, LV_SYMBOL_DOWN) == 0) {
        *seq = "\x1b[B";
        *seq_len = 3;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "RIGHT") ||
        ascii_streq_ignore_case(txt, "ArrowRight") ||
        strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        *seq = "\x1b[C";
        *seq_len = 3;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "LEFT") ||
        ascii_streq_ignore_case(txt, "ArrowLeft") ||
        strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        *seq = "\x1b[D";
        *seq_len = 3;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "HOME")) {
        *seq = "\x1b[H";
        *seq_len = 3;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "END")) {
        *seq = "\x1b[F";
        *seq_len = 3;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "PGUP") ||
        ascii_streq_ignore_case(txt, "PageUp")) {
        *seq = "\x1b[5~";
        *seq_len = 4;
        return true;
    }
    if (ascii_streq_ignore_case(txt, "PGDN") ||
        ascii_streq_ignore_case(txt, "PageDown")) {
        *seq = "\x1b[6~";
        *seq_len = 4;
        return true;
    }

    return false;
}

static bool send_terminal_key_text(const char *txt, size_t len)
{
    const char *seq = NULL;
    size_t seq_len = 0;
    if (!terminal_key_text_to_sequence(txt, len, &seq, &seq_len)) {
        return false;
    }
    if (ssh_client_is_connected()) {
        ssh_client_write(seq, seq_len);
    }
    return true;
}

