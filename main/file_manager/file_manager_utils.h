#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool file_manager_snprintf_ok(int n, size_t out_len);
bool file_manager_join_path(char *out, size_t out_len, const char *base, const char *name);
bool file_manager_path_in_root(const char *path, const char *root);
bool file_manager_is_safe_name(const char *name);
bool file_manager_is_safe_rel_path(const char *rel);
bool file_manager_build_tf_path(const char *rel, char *out, size_t out_len);
void file_manager_format_size(uint64_t bytes, char *out, size_t out_len);
void file_manager_format_size_fixed2(uint64_t bytes, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
