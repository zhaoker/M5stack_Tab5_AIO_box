#include "file_manager/file_manager_utils.h"

#include <stdio.h>
#include <string.h>

#include "storage/tf_card.h"

bool file_manager_snprintf_ok(int n, size_t out_len)
{
    return n > 0 && (size_t)n < out_len;
}

bool file_manager_join_path(char *out, size_t out_len, const char *base, const char *name)
{
    int n;

    if (!out || out_len == 0) {
        return false;
    }
    if (!base || !base[0]) {
        n = snprintf(out, out_len, "%s", name ? name : "");
    } else if (!name || !name[0]) {
        n = snprintf(out, out_len, "%s", base);
    } else if (base[strlen(base) - 1] == '/') {
        n = snprintf(out, out_len, "%s%s", base, name);
    } else {
        n = snprintf(out, out_len, "%s/%s", base, name);
    }
    if (!file_manager_snprintf_ok(n, out_len)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool file_manager_path_in_root(const char *path, const char *root)
{
    size_t root_len;

    if (!path || !root || !root[0]) {
        return false;
    }
    root_len = strlen(root);
    return strncmp(path, root, root_len) == 0 &&
           (path[root_len] == '\0' || path[root_len] == '/');
}

bool file_manager_is_safe_name(const char *name)
{
    return name && name[0] &&
           !strchr(name, '/') &&
           !strchr(name, '\\') &&
           strcmp(name, ".") != 0 &&
           strcmp(name, "..") != 0;
}

bool file_manager_is_safe_rel_path(const char *rel)
{
    const char *seg = rel;

    if (!rel) {
        return false;
    }
    if (rel[0] == '/' || strchr(rel, '\\')) {
        return false;
    }
    if (rel[0] == '\0') {
        return true;
    }

    while (*seg) {
        const char *slash = strchr(seg, '/');
        size_t len = slash ? (size_t)(slash - seg) : strlen(seg);
        if (len == 0 || (len == 1 && seg[0] == '.') ||
            (len == 2 && seg[0] == '.' && seg[1] == '.')) {
            return false;
        }
        if (!slash) {
            break;
        }
        seg = slash + 1;
    }
    return true;
}

bool file_manager_build_tf_path(const char *rel, char *out, size_t out_len)
{
    const char *mount = tf_card_get_mount_point();

    if (!out || out_len == 0 || !mount || !tf_card_is_mounted() ||
        !file_manager_is_safe_rel_path(rel ? rel : "")) {
        return false;
    }
    return file_manager_join_path(out, out_len, mount, rel ? rel : "");
}

void file_manager_format_size(uint64_t bytes, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(out, out_len, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, out_len, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(out, out_len, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}

void file_manager_format_size_fixed2(uint64_t bytes, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(out, out_len, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, out_len, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(out, out_len, "%.2f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}
