#include "file_manager/web_file_manager.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "file_manager/file_manager_text.h"
#include "file_manager/file_manager_utils.h"
#include "sdkconfig.h"
#include "storage/tf_card.h"
#include "storage/tf_usb_disk_mode.h"
#include "storage/usb_storage.h"
#include "wifi_manager.h"

static const char *TAG = "web_file_mgr";

#define WEBFM_MAX_PATH_LEN 512
#define WEBFM_IO_BUF_SIZE 4096
#define WEBFM_USB_IO_BUF_SIZE 512
#define WEBFM_USB_IO_LOCK_TIMEOUT_MS 30000
#define WEBFM_COPY_IO_DELAY_MS 2
#define WEBFM_USB_READ_DELAY_MS 2
#define WEBFM_UPLOAD_CHUNK_LIMIT_BYTES (96U * 1024U)
#define WEBFM_UPLOAD_RECV_DELAY_MS 5
#define WEBFM_UPLOAD_LOW_DMA_DELAY_MS 40
#define WEBFM_UPLOAD_CRITICAL_DMA_DELAY_MS 120
#define WEBFM_UPLOAD_LOW_DMA_BYTES (24U * 1024U)
#define WEBFM_UPLOAD_CRITICAL_DMA_BYTES (16U * 1024U)
#define WEBFM_IMAGE_CACHE_MAX_BYTES (8U * 1024U * 1024U)
#define WEBFM_IMAGE_CACHE_PSRAM_RESERVE_BYTES (512U * 1024U)
#define WEBFM_JSON_NAME_LEN 320
#define WEBFM_EDIT_LIMIT_BYTES (512U * 1024U)
#define WEBFM_FORM_LIMIT_BYTES 4096U
#define WEBFM_TF_LABEL "TF 卡"
#define WEBFM_USB_LABEL "U盘"

static httpd_handle_t s_httpd;
static volatile uint32_t s_webfm_upload_active;

bool web_file_manager_is_upload_active(void)
{
    return s_webfm_upload_active > 0;
}

static const char WEBFM_FAVICON_SVG[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
    "<rect width='64' height='64' rx='14' fill='#0f172a'/>"
    "<g fill='none' stroke='#38bdf8' stroke-width='4' stroke-linecap='round'>"
    "<path d='M10 20h8M10 32h8M10 44h8M46 20h8M46 32h8M46 44h8M20 10v8M32 10v8M44 10v8M20 46v8M32 46v8M44 46v8'/>"
    "</g><rect x='18' y='18' width='28' height='28' rx='5' fill='#1e293b' stroke='#93c5fd' stroke-width='3'/>"
    "<path d='M25 32h14M32 25v14' stroke='#facc15' stroke-width='4' stroke-linecap='round'/>"
    "</svg>";

static bool webfm_snprintf_ok(int n, size_t out_len)
{
    return file_manager_snprintf_ok(n, out_len);
}

static void webfm_json_escape(const char *src, char *out, size_t out_len)
{
    size_t w = 0;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!src) {
        return;
    }

    for (size_t r = 0; src[r] != '\0' && w + 1 < out_len; r++) {
        unsigned char ch = (unsigned char)src[r];
        if ((ch == '"' || ch == '\\') && w + 2 < out_len) {
            out[w++] = '\\';
            out[w++] = (char)ch;
        } else if (ch == '\n' && w + 2 < out_len) {
            out[w++] = '\\';
            out[w++] = 'n';
        } else if (ch == '\r' && w + 2 < out_len) {
            out[w++] = '\\';
            out[w++] = 'r';
        } else if (ch == '\t' && w + 2 < out_len) {
            out[w++] = '\\';
            out[w++] = 't';
        } else if (ch < 0x20) {
            if (w + 6 >= out_len) {
                break;
            }
            snprintf(out + w, out_len - w, "\\u%04x", ch);
            w += 6;
        } else {
            out[w++] = (char)ch;
        }
    }
    out[w] = '\0';
}

static bool webfm_has_ext(const char *path, const char *ext)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && strcasecmp(dot + 1, ext) == 0;
}

static bool webfm_is_image_path(const char *path)
{
    return webfm_has_ext(path, "jpg") || webfm_has_ext(path, "jpeg") ||
           webfm_has_ext(path, "png") || webfm_has_ext(path, "gif") ||
           webfm_has_ext(path, "bmp");
}

static bool webfm_is_text_path(const char *path)
{
    static const char *const exts[] = {
        "txt", "log", "md", "json", "ini", "cfg", "conf", "csv",
        "c", "h", "cpp", "hpp", "py", "js", "html", "css", "xml", "yaml", "yml"
    };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (webfm_has_ext(path, exts[i])) {
            return true;
        }
    }
    return false;
}

static const char *webfm_content_type_for_path(const char *path)
{
    if (webfm_has_ext(path, "html") || webfm_has_ext(path, "htm")) {
        return "text/html; charset=utf-8";
    }
    if (webfm_has_ext(path, "css")) {
        return "text/css; charset=utf-8";
    }
    if (webfm_has_ext(path, "js")) {
        return "application/javascript; charset=utf-8";
    }
    if (webfm_is_text_path(path)) {
        return "text/plain; charset=utf-8";
    }
    if (webfm_has_ext(path, "jpg") || webfm_has_ext(path, "jpeg")) {
        return "image/jpeg";
    }
    if (webfm_has_ext(path, "png")) {
        return "image/png";
    }
    if (webfm_has_ext(path, "gif")) {
        return "image/gif";
    }
    if (webfm_has_ext(path, "bmp")) {
        return "image/bmp";
    }
    if (webfm_has_ext(path, "wav")) {
        return "audio/wav";
    }
    if (webfm_has_ext(path, "mp3")) {
        return "audio/mpeg";
    }
    return "application/octet-stream";
}

static bool webfm_is_safe_rel_path(const char *rel)
{
    return file_manager_is_safe_rel_path(rel);
}

static bool webfm_is_safe_name(const char *name)
{
    return file_manager_is_safe_name(name);
}

static bool webfm_virtual_root_enabled(void)
{
    return tf_card_is_mounted() && usb_storage_is_mounted();
}

static const char *webfm_tf_label(void)
{
    return TRS(WEBFM_TF_LABEL);
}

static const char *webfm_usb_label(void)
{
    return TRS(WEBFM_USB_LABEL);
}

static bool webfm_rel_has_volume_prefix(const char *rel, const char *label, const char **child)
{
    size_t label_len;

    if (!rel || !label) {
        return false;
    }
    label_len = strlen(label);
    if (strcmp(rel, label) == 0) {
        if (child) {
            *child = "";
        }
        return true;
    }
    if (strncmp(rel, label, label_len) == 0 && rel[label_len] == '/') {
        if (child) {
            *child = rel + label_len + 1;
        }
        return true;
    }
    return false;
}

static bool webfm_rel_has_tf_prefix(const char *rel, const char **child)
{
    return webfm_rel_has_volume_prefix(rel, WEBFM_TF_LABEL, child) ||
           webfm_rel_has_volume_prefix(rel, webfm_tf_label(), child);
}

static bool webfm_rel_has_usb_prefix(const char *rel, const char **child)
{
    return webfm_rel_has_volume_prefix(rel, WEBFM_USB_LABEL, child) ||
           webfm_rel_has_volume_prefix(rel, webfm_usb_label(), child);
}

static bool webfm_rel_is_virtual_root(const char *rel)
{
    return webfm_virtual_root_enabled() && (!rel || rel[0] == '\0');
}

static bool webfm_build_path(const char *rel, char *out, size_t out_len)
{
    const char *child = rel ? rel : "";

    if (!out || out_len == 0 || webfm_rel_is_virtual_root(child)) {
        return false;
    }
    if (webfm_virtual_root_enabled()) {
        const char *volume_child = NULL;
        if (webfm_rel_has_tf_prefix(child, &volume_child)) {
            if (!webfm_is_safe_rel_path(volume_child)) {
                return false;
            }
            return file_manager_join_path(out, out_len, tf_card_get_mount_point(), volume_child);
        }
        if (webfm_rel_has_usb_prefix(child, &volume_child)) {
            if (!usb_storage_is_mounted()) {
                return false;
            }
            if (!webfm_is_safe_rel_path(volume_child)) {
                return false;
            }
            return file_manager_join_path(out, out_len, usb_storage_get_mount_point(), volume_child);
        }
        return false;
    }
    if (!webfm_is_safe_rel_path(child)) {
        return false;
    }
    return file_manager_build_tf_path(child, out, out_len);
}

#include "web_file_manager_i18n.inc"

static bool webfm_query_value(httpd_req_t *req, const char *name, char *out, size_t out_len)
{
    char *query = NULL;
    size_t query_len;
    esp_err_t err;

    if (!req || !name || !out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0) {
        return false;
    }

    query = calloc(1, query_len + 1);
    if (!query) {
        return false;
    }
    err = httpd_req_get_url_query_str(req, query, query_len + 1);
    if (err == ESP_OK) {
        err = httpd_query_key_value(query, name, out, out_len);
    }
    free(query);
    return err == ESP_OK;
}

static int webfm_hex_to_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static void webfm_url_decode_inplace(char *text)
{
    char *src = text;
    char *dst = text;

    if (!text) {
        return;
    }
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            int hi = webfm_hex_to_nibble(src[1]);
            int lo = webfm_hex_to_nibble(src[2]);
            if (hi >= 0 && lo >= 0) {
                *dst++ = (char)((hi << 4) | lo);
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool webfm_query_decoded(httpd_req_t *req, const char *name, char *out, size_t out_len)
{
    if (!webfm_query_value(req, name, out, out_len)) {
        return false;
    }
    webfm_url_decode_inplace(out);
    return true;
}

static int webfm_recv(httpd_req_t *req, char *buf, size_t len)
{
    while (true) {
        int got = httpd_req_recv(req, buf, len);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        return got;
    }
}

static char *webfm_receive_body_alloc(httpd_req_t *req, size_t limit)
{
    int total = 0;
    int remaining;
    char *body;

    if (!req) {
        return NULL;
    }
    remaining = req->content_len;
    if (remaining <= 0 || (size_t)remaining > limit) {
        return NULL;
    }
    body = calloc(1, (size_t)remaining + 1);
    if (!body) {
        return NULL;
    }

    while (remaining > 0) {
        int got = webfm_recv(req, body + total, (size_t)remaining);
        if (got <= 0) {
            free(body);
            return NULL;
        }
        total += got;
        remaining -= got;
    }
    body[total] = '\0';
    return body;
}

static bool webfm_body_value(const char *body, const char *name, char *out, size_t out_len)
{
    const char *p = body;
    size_t name_len;

    if (!body || !name || !out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    name_len = strlen(name);

    while (p && *p) {
        const char *next = strchr(p, '&');
        size_t field_len = next ? (size_t)(next - p) : strlen(p);
        if (field_len > name_len && strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            size_t value_len = field_len - name_len - 1;
            char *decoded = malloc(value_len + 1);
            if (!decoded) {
                return false;
            }
            memcpy(decoded, p + name_len + 1, value_len);
            decoded[value_len] = '\0';
            webfm_url_decode_inplace(decoded);
            if (strlen(decoded) >= out_len) {
                free(decoded);
                return false;
            }
            strlcpy(out, decoded, out_len);
            free(decoded);
            return true;
        }
        p = next ? next + 1 : NULL;
    }
    return false;
}

static const char *webfm_basename(const char *path)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : (path ? path : "download.bin");
}

static bool webfm_path_is_same_or_child(const char *path, const char *base)
{
    size_t base_len;

    if (!path || !base || !path[0] || !base[0]) {
        return false;
    }
    base_len = strlen(base);
    return strncmp(path, base, base_len) == 0 &&
           (path[base_len] == '\0' || path[base_len] == '/');
}

static bool webfm_path_is_storage_root(const char *path)
{
    return path && (strcmp(path, tf_card_get_mount_point()) == 0 ||
                    strcmp(path, usb_storage_get_mount_point()) == 0);
}

static bool webfm_path_uses_usb(const char *path)
{
    return file_manager_path_in_root(path, usb_storage_get_mount_point());
}

static bool webfm_begin_usb_io_for_paths(const char *path_a, const char *path_b, bool *locked)
{
    bool needs_lock = webfm_path_uses_usb(path_a) || webfm_path_uses_usb(path_b);

    if (locked) {
        *locked = false;
    }
    if (!needs_lock) {
        return true;
    }

    esp_err_t err = usb_storage_begin_io(WEBFM_USB_IO_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "USB storage busy for web file operation: %s", esp_err_to_name(err));
        return false;
    }
    if (locked) {
        *locked = true;
    }
    return true;
}

static void webfm_end_usb_io(bool locked)
{
    if (locked) {
        usb_storage_end_io();
    }
}

static void webfm_parent_rel(const char *rel, char *out, size_t out_len)
{
    const char *slash;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!rel) {
        return;
    }
    slash = strrchr(rel, '/');
    if (!slash) {
        return;
    }
    snprintf(out, out_len, "%.*s", (int)(slash - rel), rel);
}

static esp_err_t webfm_send_error(httpd_req_t *req, int status, const char *text)
{
    char status_text[32];
    const char *phrase = "Error";
    switch (status) {
    case 400:
        phrase = "Bad Request";
        break;
    case 404:
        phrase = "Not Found";
        break;
    case 409:
        phrase = "Conflict";
        break;
    case 413:
        phrase = "Payload Too Large";
        break;
    case 503:
        phrase = "Service Unavailable";
        break;
    case 500:
        phrase = "Internal Server Error";
        break;
    default:
        break;
    }
    snprintf(status_text, sizeof(status_text), "%d %s", status, phrase);
    httpd_resp_set_status(req, status_text);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, text ? TRS(text) : TRS("错误"));
}

static void webfm_sanitize_header_filename(const char *src, char *out, size_t out_len)
{
    size_t w = 0;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!src) {
        return;
    }
    for (size_t r = 0; src[r] && w + 1 < out_len; r++) {
        unsigned char ch = (unsigned char)src[r];
        if (ch < 0x20 || ch == 0x7F || ch == '"' || ch == '\\' || ch == ';') {
            out[w++] = '_';
        } else {
            out[w++] = (char)ch;
        }
    }
    out[w] = '\0';
}

static esp_err_t webfm_root_get_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' href='/favicon.ico' type='image/svg+xml'>"
        "<title>ESP32 在线文件管理</title>"
        "<style>"
        ":root{--ink:#20324a;--muted:#6d7f96;--line:#d9e2ec;--accent:#2f6fed;--danger:#d4380d;--soft:#eef4ff}"
        "*{box-sizing:border-box}body{margin:0;padding:18px;background:radial-gradient(circle at top left,#f8fbff,#e8eef6 42%,#dde7f2 100%);font-family:'Segoe UI','Microsoft YaHei',system-ui,sans-serif;color:var(--ink);font-size:15px}"
        ".shell{max-width:1220px;margin:0 auto;background:rgba(255,255,255,.84);border:1px solid rgba(217,226,236,.92);border-radius:22px;box-shadow:0 24px 60px rgba(40,72,112,.14);overflow:hidden}"
        ".hero{padding:24px 24px 12px;background:linear-gradient(135deg,#f7fbff 0%,#edf5ff 55%,#f7fffd 100%);border-bottom:1px solid var(--line)}.hero h1{margin:0 0 8px;font-size:28px;letter-spacing:0}.hero p{margin:0;color:var(--muted);line-height:1.6;word-break:break-all}"
        ".toolbar,.uploadbar{display:flex;gap:10px;flex-wrap:wrap;align-items:center;padding:16px 24px 0}.stats{padding:0 24px 10px;color:var(--muted);font-size:14px}.grow{flex:1}.table-wrap{padding:0 24px 18px;overflow-x:auto;overflow-y:hidden}.selectionbar{display:none;gap:8px;flex-wrap:wrap;align-items:center;margin:10px 24px 0;padding:10px 12px;border:1px solid var(--line);border-radius:14px;background:#f8fbff}.selectionbar.active{display:flex}.sel-count{font-weight:800;color:var(--ink)}"
        "table{width:100%;min-width:940px;table-layout:auto;border-collapse:separate;border-spacing:0 10px}th{font-size:13px;color:var(--muted);font-weight:600;text-align:left;padding:0 12px 6px}th:nth-child(2),td:nth-child(2){width:96px}th:nth-child(3),td:nth-child(3){width:112px}th:nth-child(4),td:nth-child(4){min-width:500px;width:44%}td{height:auto;min-height:78px;background:#fff;padding:10px 12px;border-top:1px solid #edf2f7;border-bottom:1px solid #edf2f7;vertical-align:middle;overflow:visible}td:first-child{min-width:280px;border-left:1px solid #edf2f7;border-radius:14px 0 0 14px}td:last-child{border-right:1px solid #edf2f7;border-radius:0 14px 14px 0}"
        ".name-cell{display:flex;align-items:center;gap:12px;min-width:0;min-height:56px;overflow:hidden}.pick{display:none;width:18px;height:18px;flex:none;accent-color:var(--accent)}tbody.multi .pick{display:inline-block}.file-icon{width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:none}.file-icon svg{width:24px;height:24px;display:block}.name-meta{display:flex;flex-direction:column;justify-content:center;min-width:0;min-height:56px;overflow:hidden}.name-title{font-weight:700;line-height:1.35;max-height:2.8em;color:var(--ink);text-decoration:none;white-space:normal;word-break:break-all;overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;background:transparent;border:0;padding:1px 0;text-align:left;cursor:pointer}.name-sub{font-size:12px;color:var(--muted);margin-top:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        ".chip{display:inline-flex;align-items:center;justify-content:center;min-width:68px;padding:4px 10px;border-radius:999px;background:#eef4ff;color:#2451a6;font-size:12px;font-weight:700}.chip.dir{background:#fff5d9;color:#9a6700}.chip.image{background:#eef7ff;color:#1d5fd0}.chip.audio{background:#e8fcfa;color:#0e7a7a}.chip.video{background:#fff1f0;color:#b42318}.chip.archive{background:#fff7e6;color:#ad6800}.chip.code{background:#f0f5ff;color:#1d39c4}"
        ".btn,button{font:inherit;border:none;border-radius:12px;padding:9px 12px;min-height:38px;background:#2f6fed;color:#fff;font-weight:700;cursor:pointer;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;white-space:nowrap;background-image:linear-gradient(135deg,#3a7cff,#245bda)}.btn:hover,button:hover{filter:brightness(1.03)}button:disabled{opacity:.55;cursor:not-allowed}.btn-secondary{background-image:none;background:#eef4ff;color:#2451a6}.btn-danger{background-image:none;background:#fff1f0;color:#b42318}.actions{display:flex;gap:7px;flex-wrap:wrap;align-items:center;justify-content:flex-start;overflow:visible;white-space:normal;min-width:0}.actions>*{flex:0 0 auto}.hint{color:var(--muted);font-size:13px}.err{color:var(--danger)}"
        "tbody tr.kb-focus td{box-shadow:inset 0 2px 0 var(--accent),inset 0 -2px 0 var(--accent);background:#f8fbff}tbody tr.kb-focus td:first-child{box-shadow:inset 2px 0 0 var(--accent),inset 0 2px 0 var(--accent),inset 0 -2px 0 var(--accent)}tbody tr.kb-focus td:last-child{box-shadow:inset -2px 0 0 var(--accent),inset 0 2px 0 var(--accent),inset 0 -2px 0 var(--accent)}"
        ".pagination,.bottom-bar{display:flex;gap:8px;justify-content:center;align-items:center;flex-wrap:wrap;padding:10px 24px 18px}.pagination button{padding:7px 11px;background-image:none;background:#fff;color:var(--ink);border:1px solid var(--line)}.pagination button.active{background:var(--accent);color:#fff;border-color:var(--accent)}.pagination button:disabled{color:var(--muted);background:#f7f9fc;cursor:not-allowed}.page-info{color:var(--muted);font-size:14px}"
        "input[type=text],input[type=file]{padding:10px 12px;border:1px solid var(--line);border-radius:12px;background:#fff;color:var(--ink);font:inherit}input[type=text]{min-width:180px}.editor{position:fixed;inset:0;background:rgba(0,0,0,.45);display:none;z-index:1001;align-items:center;justify-content:center;padding:18px}.editor.active{display:flex}.editor-panel{width:min(1100px,96vw);height:min(760px,88vh);background:#fff;border:1px solid #edf2f7;border-radius:18px;box-shadow:0 16px 48px rgba(0,0,0,.2);display:flex;flex-direction:column;padding:14px}.editor textarea{flex:1;width:100%;min-height:0;resize:none;border:1px solid var(--line);border-radius:12px;padding:12px;font:14px ui-monospace,SFMono-Regular,Consolas,monospace;color:#1f2f46;background:#fbfdff}"
        ".modal-overlay{position:fixed;inset:0;background:rgba(0,0,0,.45);display:none;z-index:1000;justify-content:center;align-items:center}.modal-overlay.active{display:flex}.modal-box{background:#fff;border-radius:18px;width:92%;max-width:480px;max-height:80vh;overflow:hidden;display:flex;flex-direction:column;box-shadow:0 16px 48px rgba(0,0,0,.2)}.modal-header{padding:16px 20px;border-bottom:1px solid var(--line);font-weight:800;display:flex;justify-content:space-between;align-items:center}.modal-close{cursor:pointer;font-size:20px;color:var(--muted);padding:4px}.modal-body{padding:16px 20px;overflow-y:auto;flex:1}.modal-footer{padding:12px 20px;border-top:1px solid var(--line);display:flex;gap:8px;justify-content:flex-end}.tree-label{cursor:pointer;display:flex;align-items:center;gap:6px;padding:6px 10px;border-radius:8px;font-size:14px;user-select:none}.tree-label:hover{background:#eef4ff}.tree-label.selected{background:var(--accent);color:#fff}.tree-children{padding-left:18px}.tree-arrow{width:14px;text-align:center;font-size:11px;flex:none}"
        "@media(max-width:900px){body{padding:10px}.shell{border-radius:16px}.hero{padding:18px}.toolbar,.table-wrap{padding-left:14px;padding-right:14px}.selectionbar{margin-left:14px;margin-right:14px}.table-wrap{overflow:visible}table,thead,tbody,tr,th,td{display:block}table{min-width:0;border-spacing:0}thead{display:none}tr{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;margin-bottom:12px;padding:12px;background:#fff;border:1px solid #edf2f7;border-radius:14px}tbody tr.kb-focus{outline:3px solid var(--accent);outline-offset:2px}tbody tr.kb-focus td,tbody tr.kb-focus td:first-child,tbody tr.kb-focus td:last-child{box-shadow:none;background:transparent}td{width:100%!important;height:auto;min-height:0;border:0!important;border-radius:0!important;background:transparent;padding:0;overflow:visible}td:first-child{grid-column:1;min-width:0}td:nth-child(2){grid-column:2;align-self:center}td:nth-child(3){grid-column:1/-1;color:var(--muted);font-size:13px}td:nth-child(3):before{content:'大小: '}td:nth-child(4){grid-column:1/-1}.name-cell,.name-meta{min-height:0;overflow:visible}.name-title{display:block;max-height:none;-webkit-line-clamp:unset}.name-sub{white-space:normal;word-break:break-all}.actions{justify-content:flex-start;flex-wrap:wrap;overflow:visible;padding-bottom:0}.modal-box{width:96%}}"
        "</style></head><body>"
        "<div class='shell'><div class='hero'><h1>ESP32 在线文件管理</h1><p>当前目录: <span id='path'>/</span></p></div>"
        "<div class='toolbar'><button class='btn-secondary' onclick='up()'>返回上级</button><button class='btn-secondary' onclick='load()'>刷新</button><button class='btn-secondary' id='multiBtn' onclick='toggleMulti()'>多选</button><div class='grow'></div><button onclick='openMkdirModal()'>新建目录</button><button onclick='openFileModal()'>新建文件</button><button onclick='openUploadModal()'>上传文件</button></div><div class='selectionbar' id='selbar'><span class='sel-count' id='selCount'>0 项已选</span><button class='btn-secondary' onclick='selectAllItems()'>全选</button><button class='btn-secondary' onclick='copySelected(false)'>复制所选</button><button class='btn-secondary' onclick='copySelected(true)'>剪切</button><button class='btn-secondary' id='pasteBtn' onclick='pasteClip()'>粘贴</button><button class='btn-danger' onclick='deleteSelected()'>删除所选</button><button class='btn-secondary' onclick='clearSelection()'>取消选择</button></div>"
        "<div class='stats'><span id='status'>正在加载...</span></div>"
        "<div class='pagination' id='topPager'></div>"
        "<div class='table-wrap'><table><thead><tr><th>名称</th><th>类型</th><th>大小</th><th>操作</th></tr></thead><tbody id='rows'></tbody></table></div>"
        "<div class='bottom-bar'><span class='page-info' id='pageInfo'></span><div class='pagination' id='bottomPager'></div></div>"
        "<div class='editor' id='editor'><div class='editor-panel'><div class='toolbar' style='padding:0 0 12px'><strong id='editName'></strong><span id='editEncoding' class='hint'></span><div class='grow'></div><button onclick='saveEdit()'>保存</button><button class='btn-secondary' onclick='closeEdit()'>关闭</button></div><textarea id='editText'></textarea></div></div>"
        "<div id='mkdir_modal' class='modal-overlay'><div class='modal-box'><div class='modal-header'><span>新建目录</span><span class='modal-close' onclick='closeMkdirModal()'>&times;</span></div><div class='modal-body'><p class='hint'>请输入新目录名称：</p><input id='dirname' type='text' placeholder='目录名称' style='width:100%' onkeydown=\"if(event.key==='Enter')mkdir()\"></div><div class='modal-footer'><button class='btn-secondary' onclick='closeMkdirModal()'>取消</button><button onclick='mkdir()'>确认创建</button></div></div></div>"
        "<div id='file_modal' class='modal-overlay'><div class='modal-box'><div class='modal-header'><span>新建文件</span><span class='modal-close' onclick='closeFileModal()'>&times;</span></div><div class='modal-body'><p class='hint'>请输入新文件名称：</p><input id='filename' type='text' placeholder='new.txt' style='width:100%' onkeydown=\"if(event.key==='Enter')newFile()\"></div><div class='modal-footer'><button class='btn-secondary' onclick='closeFileModal()'>取消</button><button onclick='newFile()'>确认创建</button></div></div></div>"
        "<div id='upload_modal' class='modal-overlay'><div class='modal-box'><div class='modal-header'><span>上传文件</span><span class='modal-close' onclick='closeUploadModal()'>&times;</span></div><div class='modal-body'><p class='hint'>选择一个或多个文件上传到当前目录：</p><input id='file' type='file' multiple style='width:100%'><div id='uploadStatus' class='hint' style='margin-top:12px'></div></div><div class='modal-footer'><button class='btn-secondary' onclick='closeUploadModal()'>取消</button><button onclick='upload()'>开始上传</button></div></div></div>"
        "<div id='move_modal' class='modal-overlay'><div class='modal-box'><div class='modal-header'><span><span id='moveTitle'>移动</span>: <span id='moveName'></span></span><span class='modal-close' onclick='closeMoveModal()'>&times;</span></div><div class='modal-body'><div class='hint' style='margin-bottom:10px'>目标目录: <b id='movePath'>/</b></div><div id='treeRootLabel' class='tree-label selected' onclick='selectMoveDir(\"\")'><span class='tree-arrow'>&#9660;</span><span style='font-weight:800'>/ (根目录)</span></div><div id='treeRoot' class='tree-children'></div></div><div class='modal-footer'><button class='btn-secondary' onclick='closeMoveModal()'>取消</button><button id='moveConfirm' onclick='confirmMove()'>确认移动</button></div></div></div>"
        "<script>"
        "let dir=new URLSearchParams(location.search).get('dir')||'',items=[],editPath='',page=1,pageSize=20,sel=-1,kbActive=false,moveTarget='',moveSelectedDir='',moveMode='move',multi=false,selected=new Set(),clip=[],clipMode='copy';const el=id=>document.getElementById(id);"
        "function status(t,err=false){el('status').textContent=t;el('status').className=err?'err':''}"
        "const enc=encodeURIComponent;const esc=s=>String(s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
        "function full(it){return (dir?dir+'/':'')+it.name}function fmt(n){if(!n)return'';let u=['B','KB','MB','GB'],i=0,v=n;while(v>=1024&&i<u.length-1){v/=1024;i++}return(i?v.toFixed(1):v)+' '+u[i]}"
        "function ext(n){let i=n.lastIndexOf('.');return i<0?'':n.slice(i+1).toLowerCase()}function isImg(n){return ['jpg','jpeg','png','gif','bmp'].includes(ext(n))}function isText(n){return ['txt','log','md','json','ini','cfg','conf','csv','c','h','cpp','hpp','py','js','html','css','xml','yaml','yml'].includes(ext(n))}function kind(it){if(it.dir)return['文件夹','dir',icoDir()];let e=ext(it.name);if(isImg(it.name))return['图片','image',icoImg()];if(['mp3','wav','flac','aac','ogg'].includes(e))return['音频','audio',icoAud()];if(['mp4','avi','mov','mkv'].includes(e))return['视频','video',icoVid()];if(['zip','gz','tar','rar','7z'].includes(e))return['压缩包','archive',icoArc()];if(isText(it.name))return[e||'文本','code',icoCode()];return[e||'文件','',icoFile()]}"
        "function icoDir(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><path d=\"M3 7.5A2.5 2.5 0 0 1 5.5 5H10l2 2h6.5A2.5 2.5 0 0 1 21 9.5v7A2.5 2.5 0 0 1 18.5 19h-13A2.5 2.5 0 0 1 3 16.5v-9Z\" fill=\"#f4b400\"/></svg>'}function icoFile(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><path d=\"M6 3h8l4 4v14H6V3Z\" fill=\"#dbe7f5\"/><path d=\"M14 3v5h5\" fill=\"#b8c9dc\"/></svg>'}function icoImg(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"3\" fill=\"#8cc8ff\"/><path d=\"m6 17 4-5 3 3 2-2 3 4H6Z\" fill=\"#1d5fd0\"/></svg>'}function icoCode(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><path d=\"M6 3h12v18H6V3Z\" fill=\"#e8edff\"/><path d=\"m10 9-3 3 3 3m4-6 3 3-3 3\" stroke=\"#1d39c4\" stroke-width=\"1.8\"/></svg>'}function icoAud(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><path d=\"M9 18V7l8-2v11\" stroke=\"#0e7a7a\" stroke-width=\"2\"/><circle cx=\"7\" cy=\"18\" r=\"3\" fill=\"#79e1d9\"/><circle cx=\"17\" cy=\"16\" r=\"3\" fill=\"#79e1d9\"/></svg>'}function icoVid(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><rect x=\"4\" y=\"6\" width=\"12\" height=\"12\" rx=\"2\" fill=\"#ffb4ad\"/><path d=\"m16 10 4-2v8l-4-2v-4Z\" fill=\"#b42318\"/></svg>'}function icoArc(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\"><path d=\"M6 3h12v18H6V3Z\" fill=\"#ffe4a8\"/><path d=\"M10 3h4v10h-4V3Z\" fill=\"#ad6800\" opacity=\".45\"/></svg>'}"
        "async function api(url,opt){let r=await fetch(url,opt);let t=await r.text();if(!r.ok)throw new Error(t||r.status);return t}"
        "function syncSel(scroll=false){document.querySelectorAll('#rows tr.kb-focus').forEach(r=>r.classList.remove('kb-focus'));if(!kbActive)return;let r=document.querySelector(`#rows tr[data-i=\"${sel}\"]`);if(r){r.classList.add('kb-focus');if(scroll)r.scrollIntoView({block:'nearest',inline:'nearest'})}}"
        "function selectIdx(i,scroll=true){if(!items.length){sel=-1;kbActive=false;syncSel(false);return}kbActive=true;sel=(i+items.length)%items.length;let np=Math.floor(sel/pageSize)+1;if(page!==np){page=np;render();setTimeout(()=>syncSel(scroll),0)}else syncSel(scroll)}"
        "function moveSel(d){if(!items.length){sel=-1;kbActive=false;syncSel(false);return}if(!kbActive||sel<0||sel>=items.length){kbActive=true;if(sel<0||sel>=items.length)sel=d<0?items.length-1:0;syncSel(true);return}selectIdx(sel+d,true)}"
        "function selectedPaths(){return [...selected].filter(p=>p&&items.some(it=>full(it)===p&&!it.root))}"
        "function updateSelbar(){let n=selectedPaths().length,b=el('selbar');if(b){b.className='selectionbar '+(multi||n||clip.length?'active':'');el('selCount').textContent=n+' 项已选'}let mb=el('multiBtn');if(mb)mb.textContent=multi?'取消多选':'多选';let pb=el('pasteBtn');if(pb)pb.disabled=!clip.length}"
        "function toggleMulti(force){multi=force===undefined?!multi:!!force;if(!multi)selected.clear();render()}function clearSelection(){selected.clear();multi=false;render()}"
        "function togglePick(p,on){if(on)selected.add(p);else selected.delete(p);multi=true;updateSelbar();syncSel(false)}"
        "function selectAllItems(){multi=true;selected.clear();items.forEach(it=>{if(!it.root)selected.add(full(it))});render()}"
        "function focusedPath(){return items.length&&sel>=0&&sel<items.length&&!items[sel].root?full(items[sel]):''}function selectedOrFocused(){let ps=selectedPaths(),fp=focusedPath();return ps.length?ps:(fp?[fp]:[])}"
        "function render(){let total=items.length,tp=Math.max(1,Math.ceil(total/pageSize));if(total&&kbActive&&sel<0)sel=0;if(sel>=total)sel=total-1;if(!total)sel=-1;if(page>tp)page=tp;let start=(page-1)*pageSize,show=items.slice(start,start+pageSize),h='';el('rows').className=multi?'multi':'';show.forEach((it,local)=>{let i=start+local,p=full(it),k=kind(it),picked=selected.has(p),disabled=it.root?'disabled':'';h+=`<tr data-i='${i}' class='${kbActive&&i===sel?'kb-focus':''}'><td><div class='name-cell'><input class='pick' type='checkbox' data-act='toggle' data-i='${i}' ${picked?'checked':''} ${disabled}><span class='file-icon'>${k[2]}</span><div class='name-meta'><button class='name-title' data-act='open' data-i='${i}'>${esc(it.name)}</button><span class='name-sub'>/${esc(p)}</span></div></div></td><td><span class='chip ${k[1]}'>${k[0]}</span></td><td>${it.dir?'-':fmt(it.size)}</td><td><div class='actions'><button class='btn-secondary' data-act='open' data-i='${i}'>打开</button>`;if(!it.root){if(!it.dir){h+=`<a class='btn' href='/webfm/download?path=${enc(p)}'>下载</a>`}h+=`<button class='btn-secondary' data-act='rename' data-i='${i}'>改名</button><button class='btn-secondary' data-act='copy' data-i='${i}'>复制</button><button class='btn-secondary' data-act='move' data-i='${i}'>移动</button><button class='btn-danger' data-act='delete' data-i='${i}'>删除</button>`}h+=`</div></td></tr>`});el('rows').innerHTML=h||\"<tr><td colspan='4' class='hint'>此文件夹为空。</td></tr>\";el('pageInfo').textContent=`共 ${total} 项 / 每页 ${pageSize} 项 / 第 ${page}/${tp}`;pager(tp);status(total+' 项');updateSelbar()}"
        "function pager(tp){let html=`<button ${page<=1?'disabled':''} onclick='goPage(1)'>首页</button><button ${page<=1?'disabled':''} onclick='goPage(page-1)'>上一页</button>`;let b=Math.max(1,page-2),e=Math.min(tp,page+2);for(let p=b;p<=e;p++)html+=`<button class='${p===page?'active':''}' onclick='goPage(${p})'>${p}</button>`;html+=`<button ${page>=tp?'disabled':''} onclick='goPage(page+1)'>下一页</button><button ${page>=tp?'disabled':''} onclick='goPage(${tp})'>末页</button>`;el('topPager').innerHTML=tp>1?html:'';el('bottomPager').innerHTML=tp>1?html:''}function goPage(p){page=p;if(items.length)sel=Math.min((page-1)*pageSize,items.length-1);render();syncSel(true)}"
        "async function load(){let ac=new AbortController(),to=setTimeout(()=>ac.abort(),12000);try{status('正在加载...');let r=await fetch('/webfm/list?dir='+enc(dir),{signal:ac.signal});if(!r.ok)throw new Error(await r.text()||r.status);let j=await r.json();items=j.items||[];items.sort((a,b)=>b.dir-a.dir||a.name.localeCompare(b.name));if(items.length&&kbActive&&sel<0)sel=0;if(sel>=items.length)sel=items.length-1;el('path').textContent='/' + dir;history.replaceState(null,'','/webfm'+(dir?'?dir='+enc(dir):''));render()}catch(e){status(e&&e.name==='AbortError'?'加载超时':e.message,true)}finally{clearTimeout(to)}}"
        "function up(){if(!dir){location='/';return}let i=dir.lastIndexOf('/');dir=i>0?dir.slice(0,i):'';page=1;sel=-1;kbActive=false;selected.clear();multi=false;closeEdit();load()}function openMkdirModal(){el('dirname').value='';el('mkdir_modal').classList.add('active');setTimeout(()=>el('dirname').focus(),80)}function closeMkdirModal(){el('mkdir_modal').classList.remove('active')}function openFileModal(){el('filename').value='';el('file_modal').classList.add('active');setTimeout(()=>el('filename').focus(),80)}function closeFileModal(){el('file_modal').classList.remove('active')}function openUploadModal(){el('file').value='';el('uploadStatus').textContent='';el('upload_modal').classList.add('active')}function closeUploadModal(){el('upload_modal').classList.remove('active')}"
        "async function mkdir(){let n=el('dirname').value.trim();if(!n)return;try{await api('/webfm/mkdir?dir='+enc(dir)+'&name='+enc(n));closeMkdirModal();load()}catch(e){status(e.message,true)}}"
        "async function newFile(){let n=el('filename').value.trim();if(!n)return;try{await api('/webfm/newfile?dir='+enc(dir)+'&name='+enc(n));closeFileModal();load()}catch(e){status(e.message,true)}}"
        "const UP_CHUNK=65536,upSleep=ms=>new Promise(r=>setTimeout(r,ms));"
        "async function uploadFetch(url,body,name){let last='';for(let t=0;t<4;t++){try{let r=await fetch(url,{method:'POST',body});if(r.ok)return;last=await r.text()||r.status}catch(e){last=e.message||String(e)}await upSleep(350*(t+1))}throw new Error(name+': '+last)}"
        "async function uploadOne(f,i,n){if(f.size===0){await uploadFetch('/webfm/upload?dir='+enc(dir)+'&name='+enc(f.name)+'&offset=0&total=0',new Blob([]),f.name);return}for(let off=0;off<f.size;off+=UP_CHUNK){let end=Math.min(f.size,off+UP_CHUNK),part=f.slice(off,end),pct=Math.floor(end*100/f.size);el('uploadStatus').textContent=`正在上传 (${i+1}/${n}) ${pct}% ${f.name}`;await uploadFetch('/webfm/upload?dir='+enc(dir)+'&name='+enc(f.name)+'&offset='+off+'&total='+f.size,part,f.name);await upSleep(30)}}"
        "async function upload(){let fs=[...el('file').files];if(!fs.length)return;try{let ok=0;for(let i=0;i<fs.length;i++){await uploadOne(fs[i],i,fs.length);ok++;await upSleep(180)}el('uploadStatus').textContent=`上传完成：${ok}/${fs.length}`;setTimeout(()=>{closeUploadModal();load()},500)}catch(e){status(e.message,true);el('uploadStatus').textContent=e.message}}"
        "async function del(p){try{if(!confirm('确定删除 '+p+' 吗？'))return;await api('/webfm/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+enc(p)});load()}catch(e){status(e.message,true)}}"
        "async function ren(p){try{let n=prompt('新名称',p.split('/').pop());if(!n)return;await api('/webfm/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+enc(p)+'&name='+enc(n)});load()}catch(e){status(e.message,true)}}"
        "function cp(p){openMoveModal(p,p.split('/').pop(),'copy')}"
        "async function deletePaths(ps){let ok=0,fail=0,last='';for(let p of ps){try{await api('/webfm/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+enc(p)});ok++}catch(e){fail++;last=e.message||String(e)}}return{ok,fail,last}}"
        "async function deleteSelected(){let ps=selectedOrFocused();if(!ps.length){status('请选择项目');return}if(!confirm('确定删除所选 '+ps.length+' 项?'))return;try{let r=await deletePaths(ps);selected.clear();multi=false;await load();status(r.fail?`已删除 ${r.ok} 项，失败 ${r.fail} 项`:`已删除 ${r.ok} 项`)}catch(e){status(e.message,true)}}"
        "function copySelected(cut=false){let ps=selectedOrFocused();if(!ps.length){status('请选择项目');return}clip=ps.slice();clipMode=cut?'move':'copy';status((cut?'已剪切 ':'已复制 ')+clip.length+' 项');updateSelbar()}"
        "async function pasteClip(){if(!clip.length){status('剪贴板为空');updateSelbar();return}try{let op=clipMode==='move'?'/webfm/move':'/webfm/copy',ok=0,fail=0,last='';for(let p of clip){try{await api(op,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+enc(p)+'&dest='+enc(dir)});ok++}catch(e){fail++;last=e.message||String(e)}}if(clipMode==='move'&&!fail)clip=[];selected.clear();multi=false;await load();status(fail?`已完成 ${ok} 项，失败 ${fail} 项`:'粘贴完成')}catch(e){status(e.message,true)}finally{updateSelbar()}}"
        "function selectMoveDir(d){moveSelectedDir=d;document.querySelectorAll('#move_modal .tree-label.selected').forEach(x=>x.classList.remove('selected'));if(d==='')el('treeRootLabel').classList.add('selected');el('movePath').textContent='/' + d}async function openMoveModal(p,name,mode='move'){moveTarget=p;moveSelectedDir='';moveMode=mode==='copy'?'copy':'move';el('moveName').textContent=name;el('moveTitle').textContent=moveMode==='copy'?'复制':'移动';el('moveConfirm').textContent=moveMode==='copy'?'确认复制':'确认移动';selectMoveDir('');el('move_modal').classList.add('active');await loadTree(el('treeRoot'),'')}function closeMoveModal(){el('move_modal').classList.remove('active');moveTarget='';moveMode='move'}async function loadTree(c,d){c.innerHTML='<span class=\"hint\">加载中...</span>';try{let r=await fetch('/webfm/dirtree?dir='+enc(d));let j=await r.json(),dirs=j.dirs||[];c.innerHTML='';if(!dirs.length){c.innerHTML='<span class=\"hint\">(空)</span>';return}dirs.forEach(name=>{let fp=d?d+'/'+name:name,n=document.createElement('div'),l=document.createElement('div'),ch=document.createElement('div');l.className='tree-label';l.innerHTML='<span class=\"tree-arrow\">&#9654;</span><span>'+esc(name)+'</span>';ch.className='tree-children';ch.style.display='none';let loaded=false;l.onclick=async e=>{e.stopPropagation();selectMoveDir(fp);l.classList.add('selected');if(!loaded){loaded=true;await loadTree(ch,fp)}let vis=ch.style.display!=='none';ch.style.display=vis?'none':'block';l.querySelector('.tree-arrow').innerHTML=vis?'&#9654;':'&#9660;'};n.appendChild(l);n.appendChild(ch);c.appendChild(n)})}catch(e){c.innerHTML='<span class=\"err\">加载失败</span>'}}async function confirmMove(){if(!moveTarget)return;try{let op=moveMode==='copy'?'/webfm/copy':'/webfm/move';await api(op,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+enc(moveTarget)+'&dest='+enc(moveSelectedDir)});closeMoveModal();load()}catch(e){status(e.message,true)}}"
        "function openItem(it){if(!it)return;let p=full(it);if(it.dir){dir=p;page=1;sel=-1;kbActive=false;selected.clear();multi=false;closeEdit();load()}else if(isImg(it.name))location='/webfm/imgview?path='+enc(p)+'&dir='+enc(dir);else if(isText(it.name))edit(p);else location='/webfm/download?path='+enc(p)}function openSelected(){if(!items.length)return;if(!kbActive||sel<0){kbActive=true;sel=sel>=0?sel:0;syncSel(true);return}openItem(items[sel])}"
        "async function edit(p){try{editPath=p;el('editName').textContent=p;let r=await fetch('/webfm/read?path='+enc(p));let t=await r.text();if(!r.ok)throw new Error(t||r.status);let code=r.headers.get('X-Text-Encoding')||'UTF-8';el('editEncoding').textContent='编码: '+code+(code.startsWith('UTF-8')?'':'，保存后转为 UTF-8');el('editText').value=t;el('editor').classList.add('active');el('editText').focus()}catch(e){status(e.message,true)}}"
        "async function saveEdit(){try{let r=await fetch('/webfm/save?path='+enc(editPath),{method:'POST',body:el('editText').value});if(!r.ok)throw new Error(await r.text()||r.status);status('已保存 '+editPath);load()}catch(e){status(e.message,true)}}"
        "function closeEdit(){editPath='';el('editor').classList.remove('active');el('editText').value='';el('editEncoding').textContent=''}"
        "document.getElementById('rows').onclick=e=>{let b=e.target.closest('[data-act]');if(!b)return;let i=+b.dataset.i,it=items[i];if(!it)return;let p=full(it),a=b.dataset.act;sel=i;syncSel(false);if(a==='toggle'){togglePick(p,b.checked);return}if(a==='open')openItem(it);else if(a==='delete')del(p);else if(a==='rename')ren(p);else if(a==='copy')cp(p);else if(a==='move')openMoveModal(p,it.name)};"
        "document.addEventListener('keydown',e=>{let k=e.key,tag=(e.target&&e.target.tagName)||'',ctrl=e.ctrlKey||e.metaKey,lk=k.toLowerCase();if(el('editor').classList.contains('active')){if(k==='Escape'){e.preventDefault();closeEdit()}else if(ctrl&&lk==='s'){e.preventDefault();saveEdit()}return}if(document.querySelector('.modal-overlay.active')){if(k==='Escape'){e.preventDefault();closeMkdirModal();closeFileModal();closeUploadModal();closeMoveModal()}return}if(tag==='INPUT'||tag==='TEXTAREA')return;if(ctrl&&lk==='a'){e.preventDefault();selectAllItems()}else if(ctrl&&lk==='c'){e.preventDefault();copySelected(false)}else if(ctrl&&lk==='x'){e.preventDefault();copySelected(true)}else if(ctrl&&lk==='v'){e.preventDefault();pasteClip()}else if(k==='Delete'){e.preventDefault();deleteSelected()}else if(k===' '&&multi&&items[sel]&&!items[sel].root){e.preventDefault();let p=full(items[sel]);togglePick(p,!selected.has(p));render()}else if(k==='ArrowUp'||k==='ArrowLeft'){e.preventDefault();moveSel(-1)}else if(k==='ArrowDown'||k==='ArrowRight'){e.preventDefault();moveSel(1)}else if(k==='Enter'){e.preventDefault();openSelected()}else if(k==='Escape'){e.preventDefault();if(multi||selected.size)clearSelection();else up()}});"
        "load();</script></div></body></html>";

    ESP_LOGI(TAG, "Web UI opened");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return webfm_send_i18n_page(req, page);
}

static esp_err_t webfm_favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, WEBFM_FAVICON_SVG, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t webfm_portal_get_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' href='/favicon.ico' type='image/svg+xml'>"
        "<title>M5stack Tab5在线控制面板</title><style>"
        ":root{--ink:#20324a;--muted:#6d7f96;--line:#d9e2ec;--accent:#2f6fed}"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:radial-gradient(circle at top left,#f8fbff,#e8eef6 42%,#dde7f2 100%);font-family:'Segoe UI','Microsoft YaHei',system-ui,sans-serif;color:var(--ink)}"
        ".wrap{max-width:760px;width:100%;padding:24px}h1{text-align:center;font-size:32px;margin:0 0 8px;letter-spacing:0}.sub{text-align:center;color:var(--muted);margin:0 0 38px;font-size:16px}.cards{display:grid;grid-template-columns:1fr 1fr;gap:24px}"
        ".card{background:rgba(255,255,255,.88);border:1px solid rgba(217,226,236,.9);border-radius:22px;padding:34px 28px;text-align:center;text-decoration:none;color:var(--ink);box-shadow:0 16px 48px rgba(40,72,112,.10);transition:transform .18s,box-shadow .18s}.card:hover{transform:translateY(-5px);box-shadow:0 24px 64px rgba(40,72,112,.18)}"
        ".ci{width:64px;height:64px;margin:0 auto 20px;display:flex;align-items:center;justify-content:center;border-radius:18px}.ci svg{width:42px;height:42px}.ci.f{background:#eef4ff}.ci.s{background:#e8fcfa}.card h2{margin:0 0 8px;font-size:22px}.card p{margin:0;color:var(--muted);font-size:14px;line-height:1.5}@media(max-width:560px){.cards{grid-template-columns:1fr}}"
        "</style></head><body><div class='wrap'><h1>M5stack Tab5在线控制面板</h1><p class='sub'>选择一个功能开始使用</p><div class='cards'>"
        "<a class='card' href='/webfm'><div class='ci f'><svg viewBox='0 0 48 48' fill='none'><path d='M32 6H22V42H32V6Z' fill='#FFE08A' stroke='#36527C' stroke-width='3' stroke-linejoin='round'/><path d='M42 6H32V42H42V6Z' fill='#FFE08A' stroke='#36527C' stroke-width='3' stroke-linejoin='round'/><path d='M10 6L18 7L14.5 42L6 41L10 6Z' fill='#FFE08A' stroke='#36527C' stroke-width='3' stroke-linejoin='round'/></svg></div><h2>文件管理</h2><p>浏览、上传、下载和管理存储器中的文件</p></a>"
        "<a class='card' href='/sysinfo'><div class='ci s'><svg viewBox='0 0 48 48' fill='none'><rect x='8' y='8' width='32' height='32' rx='5' fill='#B2F5EA' stroke='#36527C' stroke-width='3'/><path d='M24 16v8' stroke='#36527C' stroke-width='3' stroke-linecap='round'/><circle cx='24' cy='32' r='2.5' fill='#36527C'/></svg></div><h2>设备信息</h2><p>查看系统状态、网络、内存和 TF 卡信息</p></a>"
        "</div></div></body></html>";

    ESP_LOGI(TAG, "Web portal opened");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return webfm_send_i18n_page(req, page);
}

#include "web_file_manager_sysinfo_helpers.inc"

static esp_err_t webfm_sysinfo_page_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' href='/favicon.ico' type='image/svg+xml'>"
        "<title>设备信息 - ESP32-P4</title><style>"
        ":root{--ink:#20324a;--muted:#6d7f96;--line:#d9e2ec;--accent:#2f6fed}*{box-sizing:border-box}body{margin:0;padding:18px;background:radial-gradient(circle at top left,#f8fbff,#e8eef6 42%,#dde7f2 100%);font-family:'Segoe UI','Microsoft YaHei',system-ui,sans-serif;color:var(--ink)}.shell{max-width:960px;margin:0 auto}.top{display:flex;align-items:center;gap:14px;margin-bottom:20px}.top a{text-decoration:none;color:var(--accent);font-size:14px;padding:8px 16px;border-radius:12px;background:#eef4ff}.top h1{margin:0;font-size:26px;flex:1;letter-spacing:0}.badge{font-size:12px;color:var(--muted);background:#f0f4f8;padding:4px 10px;border-radius:8px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.card{background:rgba(255,255,255,.85);border:1px solid rgba(217,226,236,.9);border-radius:18px;padding:20px 22px;box-shadow:0 8px 32px rgba(40,72,112,.08)}.card h3{margin:0 0 14px;font-size:16px;color:var(--accent);display:flex;align-items:center;gap:8px}.row{display:flex;justify-content:space-between;gap:18px;padding:7px 0;border-bottom:1px solid #f0f4f8;font-size:14px}.row:last-child{border-bottom:none}.rl{color:var(--muted)}.rv{font-weight:600;text-align:right;word-break:break-all}@media(max-width:760px){.grid{grid-template-columns:1fr}.top{flex-wrap:wrap}}"
        "</style></head><body><div class='shell'><div class='top'><a href='/'>&#8592; 返回</a><h1>设备信息</h1><span class='badge' id='ts'>加载中...</span></div><div class='grid'>"
        "<div class='card'><h3>系统概览</h3><div id='c_sys'></div></div><div class='card'><h3>网络状态</h3><div id='c_net'></div></div><div class='card'><h3>内存信息</h3><div id='c_mem'></div></div><div class='card'><h3>TF 卡</h3><div id='c_tf'></div></div><div class='card'><h3>板载设备</h3><div id='c_board'></div></div>"
        "</div></div><script>"
        "function esc(s){return String(s==null?'-':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}function R(l,v){return '<div class=\"row\"><span class=\"rl\">'+esc(l)+'</span><span class=\"rv\">'+esc(v)+'</span></div>'}function lines(t){return String(t||'').split('\\n').filter(x=>x.trim()).map(x=>{let i=x.indexOf(': ');return i>0?R(x.slice(0,i),x.slice(i+2)):R(x,'')}).join('')}"
        "function refresh(){fetch('/api/sysinfo').then(r=>r.json()).then(d=>{c_sys.innerHTML=R('芯片型号',d.chip)+R('IDF 版本',d.idf_ver)+R('CPU 频率',d.cpu_mhz+' MHz')+R('Flash',d.flash_mb+' MB')+R('运行时间',d.uptime);c_net.innerHTML=R('WiFi 状态',d.wifi_status)+R('SSID',d.wifi_ssid)+R('IPv4',d.wifi_ipv4)+R('IPv6',d.wifi_ipv6)+R('RSSI',d.wifi_rssi)+R('信道',d.wifi_channel);c_mem.innerHTML=R('DRAM 可用',d.heap_dram_free_kb+' KB')+R('DRAM 总计',d.heap_dram_total_kb+' KB')+R('DMA 可用',d.dma_free_kb+' KB')+R('DMA 总计',d.dma_total_kb+' KB')+R('PSRAM 可用',d.psram_free_kb+' KB')+R('PSRAM 总计',d.psram_total_kb+' KB');c_tf.innerHTML=lines(d.tf_text);c_board.innerHTML=lines(d.board_text);ts.textContent=new Date().toLocaleTimeString()+' 已更新'}).catch(()=>{ts.textContent='连接失败'})}refresh();setInterval(refresh,2000);"
        "</script></body></html>";

    ESP_LOGI(TAG, "Web sysinfo page opened");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return webfm_send_i18n_page(req, page);
}

static esp_err_t webfm_sysinfo_api_handler(httpd_req_t *req)
{
    esp_chip_info_t chip;
    uint32_t flash_bytes = 0;
    char uptime[32];
    char ipv4[32] = {0};
    char ipv6[WIFI_IPV6_TEXT_LEN] = {0};
    char ssid[WIFI_MAX_SSID_LEN] = {0};
    char wifi_rssi[24] = "-";
    char wifi_channel[16] = "-";
    char tf_text[256];
    char board_text[512];
    char esc_ssid[WIFI_MAX_SSID_LEN * 2];
    char esc_ipv4[64];
    char esc_ipv6[WIFI_IPV6_TEXT_LEN * 2];
    char esc_tf[sizeof(tf_text) * 2];
    char esc_board[sizeof(board_text) * 2];
    char json[4096];
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    wifi_ap_record_t ap_info;
    wifi_state_t wifi_state = wifi_manager_get_state();

    esp_chip_info(&chip);
    (void)esp_flash_get_size(NULL, &flash_bytes);
    webfm_format_uptime(uptime, sizeof(uptime));
    (void)wifi_manager_get_current_ip_text(ipv4, sizeof(ipv4), ipv6, sizeof(ipv6));
    (void)wifi_manager_get_current_ssid(ssid, sizeof(ssid));
    if (wifi_state == WIFI_STATE_CONNECTED && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        snprintf(wifi_rssi, sizeof(wifi_rssi), "%d dBm", (int)ap_info.rssi);
        snprintf(wifi_channel, sizeof(wifi_channel), "%u", (unsigned)ap_info.primary);
    }

    if (tf_card_is_mounted() &&
        esp_vfs_fat_info(tf_card_get_mount_point(), &total_bytes, &free_bytes) == ESP_OK) {
        snprintf(tf_text, sizeof(tf_text),
                 TRS("挂载点: %s\n状态: 已挂载\n总容量: %llu MB\n可用空间: %llu MB"),
                 tf_card_get_mount_point(),
                 (unsigned long long)(total_bytes / (1024ULL * 1024ULL)),
                 (unsigned long long)(free_bytes / (1024ULL * 1024ULL)));
    } else {
        snprintf(tf_text, sizeof(tf_text), TRS("挂载点: %s\n状态: 未挂载"), tf_card_get_mount_point());
    }

    snprintf(board_text, sizeof(board_text),
             TRS("显示屏: M5Stack Tab5\n触摸: GT911 / ST7123\n电源监测: INA226\n音频: ES8388 / ES7210\nTF 卡: SDMMC\nWiFi: ESP32-C6 SDIO"));

    webfm_json_escape(ssid[0] ? ssid : "-", esc_ssid, sizeof(esc_ssid));
    webfm_json_escape(ipv4[0] ? ipv4 : "-", esc_ipv4, sizeof(esc_ipv4));
    webfm_json_escape(ipv6[0] ? ipv6 : "-", esc_ipv6, sizeof(esc_ipv6));
    webfm_json_escape(tf_text, esc_tf, sizeof(esc_tf));
    webfm_json_escape(board_text, esc_board, sizeof(esc_board));

    snprintf(json, sizeof(json),
             "{\"chip\":\"%s (%u 核)\",\"idf_ver\":\"%s\",\"cpu_mhz\":%u,\"flash_mb\":\"%.2f\",\"uptime\":\"%s\","
             "\"wifi_status\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_ipv4\":\"%s\",\"wifi_ipv6\":\"%s\",\"wifi_rssi\":\"%s\",\"wifi_channel\":\"%s\","
             "\"heap_dram_free_kb\":%u,\"heap_dram_total_kb\":%u,\"dma_free_kb\":%u,\"dma_total_kb\":%u,\"psram_free_kb\":%u,\"psram_total_kb\":%u,"
             "\"tf_text\":\"%s\",\"board_text\":\"%s\"}",
             webfm_chip_model_name(chip.model),
             (unsigned)chip.cores,
             esp_get_idf_version(),
             400U,
             (double)flash_bytes / (1024.0 * 1024.0),
             uptime,
             webfm_wifi_state_text(wifi_state),
             esc_ssid,
             esc_ipv4,
             esc_ipv6,
             wifi_rssi,
             wifi_channel,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_DMA) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             32U * 1024U,
             esc_tf,
             esc_board);

    ESP_LOGI(TAG, "Web sysinfo API requested");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

#include "web_file_manager_ops.inc"

static esp_err_t webfm_read_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    uint8_t *buf;
    char *text;
    const char *encoding = "unknown";
    FILE *fp;
    size_t got;
    size_t text_len = 0;
    struct stat st;

    if (!webfm_query_decoded(req, "path", rel, sizeof(rel)) ||
        !webfm_build_path(rel, path, sizeof(path))) {
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        return webfm_send_error(req, 404, TRS("文件打开失败"));
    }
    if ((uint64_t)st.st_size > WEBFM_EDIT_LIMIT_BYTES) {
        return webfm_send_error(req, 413, TRS("文件过大，无法编辑"));
    }
    fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Web read failed: rel='%s' path='%s' errno=%d", rel, path, errno);
        return webfm_send_error(req, 404, TRS("文件打开失败"));
    }
    buf = malloc((size_t)st.st_size + 1);
    if (!buf) {
        fclose(fp);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }
    got = fread(buf, 1, (size_t)st.st_size, fp);
    bool read_error = ferror(fp) != 0;
    fclose(fp);
    if (got != (size_t)st.st_size && read_error) {
        free(buf);
        return webfm_send_error(req, 500, TRS("读取失败"));
    }

    text = file_manager_text_decode_to_utf8(buf, got, &text_len, &encoding);
    free(buf);
    if (!text) {
        return webfm_send_error(req, 500, TRS("文本解码失败"));
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "X-Text-Encoding", encoding);
    ESP_LOGI(TAG, "Web read: rel='%s' path='%s' size=%llu encoding=%s",
             rel,
             path,
             (unsigned long long)st.st_size,
             encoding);
    esp_err_t ret = httpd_resp_send(req, text, text_len);
    free(text);
    return ret;
}

static esp_err_t webfm_save_post_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char tmp[WEBFM_MAX_PATH_LEN + 8];
    char *buf;
    FILE *fp;
    int remaining;

    if (!webfm_query_decoded(req, "path", rel, sizeof(rel)) ||
        !webfm_build_path(rel, path, sizeof(path))) {
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    if ((uint32_t)req->content_len > WEBFM_EDIT_LIMIT_BYTES) {
        return webfm_send_error(req, 413, TRS("文件过大，无法编辑"));
    }
    if (rel[0] == '\0' || !webfm_snprintf_ok(snprintf(tmp, sizeof(tmp), "%s.tmp", path), sizeof(tmp))) {
        return webfm_send_error(req, 400, TRS("路径无效"));
    }

    fp = fopen(tmp, "wb");
    if (!fp) {
        ESP_LOGW(TAG, "Web save failed: fopen tmp rel='%s' tmp='%s' errno=%d", rel, tmp, errno);
        return webfm_send_error(req, 500, TRS("临时文件打开失败"));
    }
    buf = malloc(WEBFM_IO_BUF_SIZE);
    if (!buf) {
        fclose(fp);
        unlink(tmp);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int want = remaining > WEBFM_IO_BUF_SIZE ? WEBFM_IO_BUF_SIZE : remaining;
        int got = webfm_recv(req, buf, (size_t)want);
        if (got <= 0 || fwrite(buf, 1, got, fp) != (size_t)got) {
            ESP_LOGW(TAG, "Web save failed: write rel='%s' tmp='%s' remaining=%d errno=%d",
                     rel, tmp, remaining, errno);
            free(buf);
            fclose(fp);
            unlink(tmp);
            return webfm_send_error(req, 500, TRS("保存失败"));
        }
        remaining -= got;
    }
    free(buf);
    fclose(fp);
    unlink(path);
    if (rename(tmp, path) != 0) {
        ESP_LOGW(TAG, "Web save failed: replace rel='%s' tmp='%s' path='%s' errno=%d", rel, tmp, path, errno);
        unlink(tmp);
        return webfm_send_error(req, 500, TRS("替换失败"));
    }
    ESP_LOGI(TAG, "Web save: rel='%s' path='%s' size=%d", rel, path, req->content_len);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t webfm_dirtree_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    DIR *dir;
    bool first = true;

    (void)webfm_query_decoded(req, "dir", rel, sizeof(rel));
    if (webfm_rel_is_virtual_root(rel)) {
        char escaped[WEBFM_JSON_NAME_LEN];
        char item[256];

        httpd_resp_set_type(req, "application/json; charset=utf-8");
        httpd_resp_sendstr_chunk(req, "{\"dirs\":[");
        webfm_json_escape(webfm_tf_label(), escaped, sizeof(escaped));
        snprintf(item, sizeof(item), "\"%s\"", escaped);
        httpd_resp_sendstr_chunk(req, item);
        webfm_json_escape(webfm_usb_label(), escaped, sizeof(escaped));
        snprintf(item, sizeof(item), ",\"%s\"", escaped);
        httpd_resp_sendstr_chunk(req, item);
        httpd_resp_sendstr_chunk(req, "]}");
        return httpd_resp_sendstr_chunk(req, NULL);
    }
    if (!webfm_build_path(rel, path, sizeof(path))) {
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    dir = opendir(path);
    if (!dir) {
        return webfm_send_error(req, 404, TRS("目录打开失败"));
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr_chunk(req, "{\"dirs\":[");
    while (true) {
        struct dirent *ent = readdir(dir);
        struct stat st;
        char full[WEBFM_MAX_PATH_LEN];
        char escaped[WEBFM_JSON_NAME_LEN];
        char item[384];
        if (!ent) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!webfm_snprintf_ok(snprintf(full, sizeof(full), "%s/%s", path, ent->d_name), sizeof(full)) ||
            stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        webfm_json_escape(ent->d_name, escaped, sizeof(escaped));
        snprintf(item, sizeof(item), "%s\"%s\"", first ? "" : ",", escaped);
        httpd_resp_sendstr_chunk(req, item);
        first = false;
    }
    closedir(dir);
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

#include "web_file_manager_images.inc"

esp_err_t web_file_manager_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (s_httpd) {
        return ESP_OK;
    }
    if (tf_usb_disk_mode_blocks_tf_access()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!tf_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (wifi_manager_get_state() != WIFI_STATE_CONNECTED || !wifi_manager_has_ipv4()) {
        return ESP_ERR_INVALID_STATE;
    }

    config.stack_size = 16384;
    config.max_uri_handlers = 22;
    config.max_uri_len = 2048;
    config.max_req_hdr_len = 2048;
    config.max_open_sockets = 5;
    config.backlog_conn = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        s_httpd = NULL;
        return err;
    }

    const httpd_uri_t portal = {.uri = "/", .method = HTTP_GET, .handler = webfm_portal_get_handler, .user_ctx = NULL};
    const httpd_uri_t favicon = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = webfm_favicon_get_handler, .user_ctx = NULL};
    const httpd_uri_t sysinfo = {.uri = "/sysinfo", .method = HTTP_GET, .handler = webfm_sysinfo_page_handler, .user_ctx = NULL};
    const httpd_uri_t sysapi = {.uri = "/api/sysinfo", .method = HTTP_GET, .handler = webfm_sysinfo_api_handler, .user_ctx = NULL};
    const httpd_uri_t root = {.uri = "/webfm", .method = HTTP_GET, .handler = webfm_root_get_handler, .user_ctx = NULL};
    const httpd_uri_t list = {.uri = "/webfm/list", .method = HTTP_GET, .handler = webfm_list_get_handler, .user_ctx = NULL};
    const httpd_uri_t download = {.uri = "/webfm/download", .method = HTTP_GET, .handler = webfm_download_get_handler, .user_ctx = NULL};
    const httpd_uri_t mkdir_uri = {.uri = "/webfm/mkdir", .method = HTTP_GET, .handler = webfm_mkdir_get_handler, .user_ctx = NULL};
    const httpd_uri_t newfile_uri = {.uri = "/webfm/newfile", .method = HTTP_GET, .handler = webfm_newfile_get_handler, .user_ctx = NULL};
    const httpd_uri_t del = {.uri = "/webfm/delete", .method = HTTP_POST, .handler = webfm_delete_post_handler, .user_ctx = NULL};
    const httpd_uri_t rmdir_uri = {.uri = "/webfm/rmdir", .method = HTTP_POST, .handler = webfm_rmdir_post_handler, .user_ctx = NULL};
    const httpd_uri_t rename_uri = {.uri = "/webfm/rename", .method = HTTP_POST, .handler = webfm_rename_post_handler, .user_ctx = NULL};
    const httpd_uri_t move_uri = {.uri = "/webfm/move", .method = HTTP_POST, .handler = webfm_move_post_handler, .user_ctx = NULL};
    const httpd_uri_t copy_uri = {.uri = "/webfm/copy", .method = HTTP_POST, .handler = webfm_copy_post_handler, .user_ctx = NULL};
    const httpd_uri_t upload = {.uri = "/webfm/upload", .method = HTTP_POST, .handler = webfm_upload_post_handler, .user_ctx = NULL};
    const httpd_uri_t read_uri = {.uri = "/webfm/read", .method = HTTP_GET, .handler = webfm_read_get_handler, .user_ctx = NULL};
    const httpd_uri_t save_uri = {.uri = "/webfm/save", .method = HTTP_POST, .handler = webfm_save_post_handler, .user_ctx = NULL};
    const httpd_uri_t dirtree = {.uri = "/webfm/dirtree", .method = HTTP_GET, .handler = webfm_dirtree_get_handler, .user_ctx = NULL};
    const httpd_uri_t image = {.uri = "/webfm/image", .method = HTTP_GET, .handler = webfm_image_cached_get_handler, .user_ctx = NULL};
    const httpd_uri_t imglist = {.uri = "/webfm/imglist", .method = HTTP_GET, .handler = webfm_imglist_get_handler, .user_ctx = NULL};
    const httpd_uri_t imgview = {.uri = "/webfm/imgview", .method = HTTP_GET, .handler = webfm_imgview_get_handler, .user_ctx = NULL};

    if (httpd_register_uri_handler(s_httpd, &portal) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &favicon) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &sysinfo) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &sysapi) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &root) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &list) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &download) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &mkdir_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &newfile_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &del) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &rmdir_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &rename_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &move_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &copy_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &upload) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &read_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &save_uri) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &dirtree) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &image) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &imglist) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &imgview) != ESP_OK) {
        ESP_LOGE(TAG, "register handlers failed");
        web_file_manager_stop();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "web file manager started");
    return ESP_OK;
}

void web_file_manager_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

bool web_file_manager_is_running(void)
{
    return s_httpd != NULL;
}

bool web_file_manager_can_start(char *reason, size_t reason_len)
{
    if (reason && reason_len > 0) {
        reason[0] = '\0';
    }
    if (tf_usb_disk_mode_blocks_tf_access()) {
        if (reason && reason_len > 0) {
            snprintf(reason, reason_len, TRS("TF卡正在USB磁盘模式中"));
        }
        return false;
    }
    if (!tf_card_is_mounted()) {
        if (reason && reason_len > 0) {
            snprintf(reason, reason_len, TRS("TF 卡未挂载"));
        }
        return false;
    }
    if (wifi_manager_get_state() != WIFI_STATE_CONNECTED || !wifi_manager_has_ipv4()) {
        if (reason && reason_len > 0) {
            snprintf(reason, reason_len, TRS("WiFi IPv4 未连接"));
        }
        return false;
    }
    return true;
}

bool web_file_manager_get_url(char *buffer, size_t buffer_len)
{
    char ipv4[32] = {0};
    char ipv6_unused[WIFI_IPV6_TEXT_LEN] = {0};
    int n;

    if (!buffer || buffer_len == 0) {
        return false;
    }
    buffer[0] = '\0';
    if (wifi_manager_get_current_ip_text(ipv4, sizeof(ipv4), ipv6_unused, sizeof(ipv6_unused)) != ESP_OK ||
        ipv4[0] == '\0') {
        return false;
    }
    n = snprintf(buffer, buffer_len, "http://%s", ipv4);
    return n > 0 && (size_t)n < buffer_len;
}
