static bool screensaver_component_is_valid(app_screensaver_component_t component)
{
    return (int)component >= 0 && component < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT;
}

static bool screensaver_component_default_visible(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_VISIBLE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_VISIBLE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_VISIBLE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_VISIBLE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_VISIBLE;
    }
}

static uint32_t screensaver_component_default_color(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_COLOR;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_COLOR;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_COLOR;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_COLOR;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_COLOR;
    }
}

static const char *screensaver_component_visible_key(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return "ss_title_v";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return "ss_sec_v";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return "ss_date_v";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return "ss_lunar_v";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return "ss_time_v";
    }
}

static const char *screensaver_component_color_key(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return "ss_title_c";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return "ss_sec_c";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return "ss_date_c";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return "ss_lunar_c";
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return "ss_time_c";
    }
}

static void app_settings_screensaver_set_default_component_state(void)
{
    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        app_screensaver_component_t component = (app_screensaver_component_t)i;
        s_screensaver_component_visible[i] =
            screensaver_component_default_visible(component);
        s_screensaver_component_color[i] =
            screensaver_component_default_color(component) & 0xFFFFFF;
    }
}

static void app_settings_screensaver_load_component_state(nvs_handle_t h)
{
    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        app_screensaver_component_t component = (app_screensaver_component_t)i;
        uint8_t visible = screensaver_component_default_visible(component) ? 1 : 0;
        int32_t color = (int32_t)(screensaver_component_default_color(component) & 0xFFFFFF);
        esp_err_t visible_err = nvs_get_u8(h, screensaver_component_visible_key(component), &visible);
        esp_err_t color_err = nvs_get_i32(h, screensaver_component_color_key(component), &color);

        if (visible_err == ESP_OK || visible_err == ESP_ERR_NVS_NOT_FOUND) {
            s_screensaver_component_visible[i] = visible != 0;
        } else {
            ESP_LOGW(TAG, "load screensaver component visible failed: %s",
                     esp_err_to_name(visible_err));
            s_screensaver_component_visible[i] =
                screensaver_component_default_visible(component);
        }

        if (color_err == ESP_OK && color >= 0 && color <= 0xFFFFFF) {
            s_screensaver_component_color[i] = (uint32_t)color;
        } else if (color_err == ESP_ERR_NVS_NOT_FOUND) {
            s_screensaver_component_color[i] =
                screensaver_component_default_color(component) & 0xFFFFFF;
        } else {
            ESP_LOGW(TAG, "load screensaver component color failed: %s",
                     esp_err_to_name(color_err));
            s_screensaver_component_color[i] =
                screensaver_component_default_color(component) & 0xFFFFFF;
        }
    }
}

app_screensaver_mode_t app_settings_get_screensaver_mode(void)
{
    return s_screensaver_mode;
}

esp_err_t app_settings_set_screensaver_mode(app_screensaver_mode_t mode)
{
    app_screensaver_mode_t clamped = clamp_screensaver_mode((uint8_t)mode);
    if (clamped != mode) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_mode = clamped;
    return save_u8_setting(SETTINGS_NVS_SCREENSAVER_MODE_KEY,
                           (uint8_t)s_screensaver_mode,
                           "screensaver mode");
}

int app_settings_get_screensaver_idle_minutes(void)
{
    return s_screensaver_idle_minutes;
}

esp_err_t app_settings_set_screensaver_idle_minutes(int minutes)
{
    int clamped = clamp_screensaver_idle_minutes(minutes);
    if (clamped != minutes) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_idle_minutes = clamped;
    return save_i32_setting(SETTINGS_NVS_SCREENSAVER_IDLE_KEY,
                            s_screensaver_idle_minutes,
                            "screensaver idle");
}

bool app_settings_get_screensaver_disable_during_ssh(void)
{
    return s_screensaver_disable_during_ssh;
}

esp_err_t app_settings_set_screensaver_disable_during_ssh(bool enabled)
{
    s_screensaver_disable_during_ssh = enabled;
    return save_u8_setting(SETTINGS_NVS_SCREENSAVER_DISABLE_SSH_KEY,
                           enabled ? 1 : 0,
                           "screensaver ssh disable");
}

const char *app_settings_get_screensaver_wallpaper_dir(void)
{
    return s_screensaver_wallpaper_dir;
}

esp_err_t app_settings_set_screensaver_wallpaper_dir(const char *path)
{
    if (!path || !path[0] ||
        strlen(path) >= sizeof(s_screensaver_wallpaper_dir) ||
        path[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_screensaver_wallpaper_dir, path, sizeof(s_screensaver_wallpaper_dir));
    return save_string_setting(SETTINGS_NVS_SCREENSAVER_DIR_KEY,
                               s_screensaver_wallpaper_dir,
                               "screensaver dir");
}

app_screensaver_order_t app_settings_get_screensaver_order(void)
{
    return s_screensaver_order;
}

esp_err_t app_settings_set_screensaver_order(app_screensaver_order_t order)
{
    app_screensaver_order_t clamped = clamp_screensaver_order((uint8_t)order);
    if (clamped != order) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_order = clamped;
    return save_u8_setting(SETTINGS_NVS_SCREENSAVER_ORDER_KEY,
                           (uint8_t)s_screensaver_order,
                           "screensaver order");
}

int app_settings_get_screensaver_switch_minutes(void)
{
    return s_screensaver_switch_minutes;
}

esp_err_t app_settings_set_screensaver_switch_minutes(int minutes)
{
    int clamped = clamp_screensaver_switch_minutes(minutes);
    if (clamped != minutes) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_switch_minutes = clamped;
    return save_i32_setting(SETTINGS_NVS_SCREENSAVER_SWITCH_KEY,
                            s_screensaver_switch_minutes,
                            "screensaver switch");
}

bool app_settings_get_screensaver_show_exif_title(void)
{
    return s_screensaver_show_exif_title;
}

esp_err_t app_settings_set_screensaver_show_exif_title(bool enabled)
{
    s_screensaver_show_exif_title = enabled;
    return save_u8_setting(SETTINGS_NVS_SCREENSAVER_EXIF_KEY,
                           enabled ? 1 : 0,
                           "screensaver exif");
}

int app_settings_get_screensaver_time_x(void)
{
    return app_settings_get_screensaver_component_x(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME);
}

int app_settings_get_screensaver_time_y(void)
{
    return app_settings_get_screensaver_component_y(APP_SETTINGS_SCREENSAVER_COMPONENT_TIME);
}

esp_err_t app_settings_set_screensaver_time_position(int x, int y)
{
    return app_settings_set_screensaver_component_position(
        APP_SETTINGS_SCREENSAVER_COMPONENT_TIME,
        x,
        y);
}

int app_settings_get_screensaver_component_x(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return s_screensaver_title_x;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return s_screensaver_second_x;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return s_screensaver_date_x;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return s_screensaver_lunar_x;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return s_screensaver_time_x;
    }
}

int app_settings_get_screensaver_component_y(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return s_screensaver_title_y;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return s_screensaver_second_y;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return s_screensaver_date_y;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return s_screensaver_lunar_y;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return s_screensaver_time_y;
    }
}

static const char *screensaver_component_x_key(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return SETTINGS_NVS_SCREENSAVER_TITLE_X_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return SETTINGS_NVS_SCREENSAVER_SECOND_X_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return SETTINGS_NVS_SCREENSAVER_DATE_X_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return SETTINGS_NVS_SCREENSAVER_LUNAR_X_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return SETTINGS_NVS_SCREENSAVER_TIME_X_KEY;
    }
}

static const char *screensaver_component_y_key(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return SETTINGS_NVS_SCREENSAVER_TITLE_Y_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return SETTINGS_NVS_SCREENSAVER_SECOND_Y_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return SETTINGS_NVS_SCREENSAVER_DATE_Y_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return SETTINGS_NVS_SCREENSAVER_LUNAR_Y_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return SETTINGS_NVS_SCREENSAVER_TIME_Y_KEY;
    }
}

static void screensaver_component_set_cached(app_screensaver_component_t component, int x, int y)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        s_screensaver_title_x = x;
        s_screensaver_title_y = y;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        s_screensaver_second_x = x;
        s_screensaver_second_y = y;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        s_screensaver_date_x = x;
        s_screensaver_date_y = y;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        s_screensaver_lunar_x = x;
        s_screensaver_lunar_y = y;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        s_screensaver_time_x = x;
        s_screensaver_time_y = y;
        break;
    }
}

static const char *screensaver_component_font_key(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return SETTINGS_NVS_SCREENSAVER_TITLE_FONT_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return SETTINGS_NVS_SCREENSAVER_SECOND_FONT_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return SETTINGS_NVS_SCREENSAVER_DATE_FONT_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return SETTINGS_NVS_SCREENSAVER_LUNAR_FONT_KEY;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return SETTINGS_NVS_SCREENSAVER_TIME_FONT_KEY;
    }
}

static int screensaver_component_default_font_size(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_FONT_SIZE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_FONT_SIZE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_FONT_SIZE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_FONT_SIZE;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_FONT_SIZE;
    }
}

static void screensaver_component_set_cached_font_size(app_screensaver_component_t component,
                                                       int font_size)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        s_screensaver_title_font_size = font_size;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        s_screensaver_second_font_size = font_size;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        s_screensaver_date_font_size = font_size;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        s_screensaver_lunar_font_size = font_size;
        break;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        s_screensaver_time_font_size = font_size;
        break;
    }
}

esp_err_t app_settings_set_screensaver_component_position(app_screensaver_component_t component,
                                                          int x,
                                                          int y)
{
    int clamped_x = clamp_screensaver_time_x(x);
    int clamped_y = clamp_screensaver_time_y(y);
    nvs_handle_t h;
    esp_err_t err;

    if ((int)component < 0 || component >= APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    screensaver_component_set_cached(component, clamped_x, clamped_y);

    err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for screensaver time position save failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = nvs_set_i32(h, screensaver_component_x_key(component), clamped_x);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, screensaver_component_y_key(component), clamped_y);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save screensaver time position failed: %s", esp_err_to_name(err));
    }
    return err;
}

int app_settings_get_screensaver_component_font_size(app_screensaver_component_t component)
{
    switch (component) {
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE:
        return s_screensaver_title_font_size;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND:
        return s_screensaver_second_font_size;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_DATE:
        return s_screensaver_date_font_size;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR:
        return s_screensaver_lunar_font_size;
    case APP_SETTINGS_SCREENSAVER_COMPONENT_TIME:
    default:
        return s_screensaver_time_font_size;
    }
}

esp_err_t app_settings_set_screensaver_component_font_size(app_screensaver_component_t component,
                                                           int font_size)
{
    int clamped = clamp_screensaver_font_size(font_size);

    if ((int)component < 0 || component >= APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT ||
        clamped != font_size) {
        return ESP_ERR_INVALID_ARG;
    }
    screensaver_component_set_cached_font_size(component, clamped);
    return save_i32_setting(screensaver_component_font_key(component),
                            clamped,
                            "screensaver font size");
}

bool app_settings_get_screensaver_component_visible(app_screensaver_component_t component)
{
    if (!screensaver_component_is_valid(component)) {
        return true;
    }
    return s_screensaver_component_visible[component];
}

esp_err_t app_settings_set_screensaver_component_visible(app_screensaver_component_t component,
                                                         bool visible)
{
    if (!screensaver_component_is_valid(component)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_component_visible[component] = visible;
    return save_u8_setting(screensaver_component_visible_key(component),
                           visible ? 1 : 0,
                           "screensaver component visible");
}

uint32_t app_settings_get_screensaver_component_color(app_screensaver_component_t component)
{
    if (!screensaver_component_is_valid(component)) {
        return 0xFFFFFF;
    }
    return s_screensaver_component_color[component] & 0xFFFFFF;
}

esp_err_t app_settings_set_screensaver_component_color(app_screensaver_component_t component,
                                                       uint32_t rgb)
{
    if (!screensaver_component_is_valid(component) || rgb > 0xFFFFFF) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_component_color[component] = rgb;
    return save_i32_setting(screensaver_component_color_key(component),
                            (int32_t)rgb,
                            "screensaver component color");
}

bool app_settings_get_screensaver_text_shadow_enabled(void)
{
    return s_screensaver_text_shadow_enabled;
}

esp_err_t app_settings_set_screensaver_text_shadow_enabled(bool enabled)
{
    s_screensaver_text_shadow_enabled = enabled;
    return save_u8_setting(SETTINGS_NVS_SCREENSAVER_SHADOW_ENABLED_KEY,
                           enabled ? 1 : 0,
                           "screensaver text shadow enabled");
}

int app_settings_get_screensaver_text_shadow_length(void)
{
    return s_screensaver_text_shadow_length;
}

esp_err_t app_settings_set_screensaver_text_shadow_length(int length)
{
    int clamped = clamp_screensaver_shadow_length(length);

    if (clamped != length) {
        return ESP_ERR_INVALID_ARG;
    }
    s_screensaver_text_shadow_length = clamped;
    return save_i32_setting(SETTINGS_NVS_SCREENSAVER_SHADOW_LENGTH_KEY,
                            clamped,
                            "screensaver text shadow length");
}

esp_err_t app_settings_reset_screensaver_component_positions(void)
{
    esp_err_t err = ESP_OK;
    esp_err_t step;

    step = app_settings_set_screensaver_component_position(
        APP_SETTINGS_SCREENSAVER_COMPONENT_TITLE,
        APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_X,
        APP_SETTINGS_DEFAULT_SCREENSAVER_TITLE_Y);
    if (step != ESP_OK) {
        err = step;
    }
    step = app_settings_set_screensaver_component_position(
        APP_SETTINGS_SCREENSAVER_COMPONENT_TIME,
        APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_X,
        APP_SETTINGS_DEFAULT_SCREENSAVER_TIME_Y);
    if (step != ESP_OK) {
        err = step;
    }
    step = app_settings_set_screensaver_component_position(
        APP_SETTINGS_SCREENSAVER_COMPONENT_SECOND,
        APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_X,
        APP_SETTINGS_DEFAULT_SCREENSAVER_SECOND_Y);
    if (step != ESP_OK) {
        err = step;
    }
    step = app_settings_set_screensaver_component_position(
        APP_SETTINGS_SCREENSAVER_COMPONENT_DATE,
        APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_X,
        APP_SETTINGS_DEFAULT_SCREENSAVER_DATE_Y);
    if (step != ESP_OK) {
        err = step;
    }
    step = app_settings_set_screensaver_component_position(
        APP_SETTINGS_SCREENSAVER_COMPONENT_LUNAR,
        APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_X,
        APP_SETTINGS_DEFAULT_SCREENSAVER_LUNAR_Y);
    if (step != ESP_OK) {
        err = step;
    }
    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        step = app_settings_set_screensaver_component_font_size(
            (app_screensaver_component_t)i,
            screensaver_component_default_font_size((app_screensaver_component_t)i));
        if (step != ESP_OK) {
            err = step;
        }
    }
    for (int i = 0; i < APP_SETTINGS_SCREENSAVER_COMPONENT_COUNT; i++) {
        app_screensaver_component_t component = (app_screensaver_component_t)i;

        step = app_settings_set_screensaver_component_visible(
            component,
            screensaver_component_default_visible(component));
        if (step != ESP_OK) {
            err = step;
        }
        step = app_settings_set_screensaver_component_color(
            component,
            screensaver_component_default_color(component) & 0xFFFFFF);
        if (step != ESP_OK) {
            err = step;
        }
    }
    step = app_settings_set_screensaver_text_shadow_enabled(
        APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_ENABLED);
    if (step != ESP_OK) {
        err = step;
    }
    step = app_settings_set_screensaver_text_shadow_length(
        APP_SETTINGS_DEFAULT_SCREENSAVER_TEXT_SHADOW_LENGTH);
    if (step != ESP_OK) {
        err = step;
    }
    return err;
}
