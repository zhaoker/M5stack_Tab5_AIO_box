#include "storage/nvs_config_backup.h"
#include "storage/server_config.h"
#include "security/password_crypto.h"
#include "wifi_manager.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

static const char *TAG = "nvs_backup";
static const char *const BACKUP_NAMESPACES[] = {
    "pwd_crypto",
    "srv_cfg",
    "app_settings",
    "wifi_creds",
};

#define BACKUP_MIGRATION_SCHEMA 1
#define BACKUP_KDF_ITERATIONS   12000u
#define BACKUP_SALT_LEN         16
#define BACKUP_NONCE_LEN        12
#define BACKUP_TAG_LEN          16
#define BACKUP_AES_KEY_LEN      32

static const uint8_t BACKUP_GCM_AAD[] = "tab5:nvs-config-migration:v1";

static void set_message(char *message, size_t message_len, const char *fmt, ...)
{
    if (!message || message_len == 0) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, message_len, TRS(fmt), args);
    va_end(args);
}

static bool ensure_backup_dir(void)
{
    const char *dir = "/sdcard/backup";
    if (mkdir(dir, 0775) == 0 || errno == EEXIST) {
        return true;
    }
    ESP_LOGW(TAG, "mkdir %s failed: errno=%d", dir, errno);
    return false;
}

static char *base64_encode_alloc(const uint8_t *data, size_t len)
{
    size_t out_len = 0;
    if (mbedtls_base64_encode(NULL, 0, &out_len, data, len) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return NULL;
    }
    char *out = calloc(1, out_len + 1);
    if (!out) {
        return NULL;
    }
    if (mbedtls_base64_encode((uint8_t *)out, out_len, &out_len, data, len) != 0) {
        free(out);
        return NULL;
    }
    out[out_len] = '\0';
    return out;
}

static uint8_t *base64_decode_alloc(const char *text, size_t *out_len)
{
    size_t needed = 0;
    if (!text || !out_len) {
        return NULL;
    }
    int ret = mbedtls_base64_decode(NULL, 0, &needed, (const uint8_t *)text, strlen(text));
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && ret != 0) {
        return NULL;
    }
    uint8_t *out = calloc(1, needed ? needed : 1);
    if (!out) {
        return NULL;
    }
    ret = mbedtls_base64_decode(out, needed, out_len, (const uint8_t *)text, strlen(text));
    if (ret != 0) {
        free(out);
        return NULL;
    }
    return out;
}

static bool backup_password_is_valid(const char *password)
{
    size_t len;

    if (!password) {
        return false;
    }
    len = strlen(password);
    if (len < NVS_CONFIG_BACKUP_PASSWORD_MIN_LEN ||
        len > NVS_CONFIG_BACKUP_PASSWORD_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isalnum((unsigned char)password[i])) {
            return false;
        }
    }
    return true;
}

static esp_err_t derive_backup_key(const char *password,
                                   const uint8_t salt[BACKUP_SALT_LEN],
                                   uint32_t iterations,
                                   uint8_t key[BACKUP_AES_KEY_LEN])
{
    if (!backup_password_is_valid(password) || !salt || !key || iterations == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                            (const uint8_t *)password,
                                            strlen(password),
                                            salt,
                                            BACKUP_SALT_LEN,
                                            iterations,
                                            BACKUP_AES_KEY_LEN,
                                            key);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t backup_gcm_encrypt(const char *password,
                                    const uint8_t *plaintext,
                                    size_t plaintext_len,
                                    uint8_t salt[BACKUP_SALT_LEN],
                                    uint8_t nonce[BACKUP_NONCE_LEN],
                                    uint8_t tag[BACKUP_TAG_LEN],
                                    uint8_t *ciphertext)
{
    if (!plaintext || !ciphertext) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t key[BACKUP_AES_KEY_LEN] = {0};
    esp_fill_random(salt, BACKUP_SALT_LEN);
    esp_fill_random(nonce, BACKUP_NONCE_LEN);

    esp_err_t err = derive_backup_key(password, salt, BACKUP_KDF_ITERATIONS, key);
    if (err != ESP_OK) {
        return err;
    }

    mbedtls_gcm_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        mbedtls_platform_zeroize(key, sizeof(key));
        return ESP_ERR_NO_MEM;
    }

    mbedtls_gcm_init(ctx);
    int ret = mbedtls_gcm_setkey(ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret == 0) {
        ret = mbedtls_gcm_crypt_and_tag(ctx,
                                        MBEDTLS_GCM_ENCRYPT,
                                        plaintext_len,
                                        nonce,
                                        BACKUP_NONCE_LEN,
                                        BACKUP_GCM_AAD,
                                        sizeof(BACKUP_GCM_AAD) - 1,
                                        plaintext,
                                        ciphertext,
                                        BACKUP_TAG_LEN,
                                        tag);
    }
    mbedtls_gcm_free(ctx);
    mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    free(ctx);
    mbedtls_platform_zeroize(key, sizeof(key));
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t backup_gcm_decrypt(const char *password,
                                    const uint8_t salt[BACKUP_SALT_LEN],
                                    uint32_t iterations,
                                    const uint8_t nonce[BACKUP_NONCE_LEN],
                                    const uint8_t tag[BACKUP_TAG_LEN],
                                    const uint8_t *ciphertext,
                                    size_t ciphertext_len,
                                    uint8_t *plaintext)
{
    if (!ciphertext || !plaintext) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t key[BACKUP_AES_KEY_LEN] = {0};
    esp_err_t err = derive_backup_key(password, salt, iterations, key);
    if (err != ESP_OK) {
        return err;
    }

    mbedtls_gcm_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        mbedtls_platform_zeroize(key, sizeof(key));
        return ESP_ERR_NO_MEM;
    }

    mbedtls_gcm_init(ctx);
    int ret = mbedtls_gcm_setkey(ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret == 0) {
        ret = mbedtls_gcm_auth_decrypt(ctx,
                                       ciphertext_len,
                                       nonce,
                                       BACKUP_NONCE_LEN,
                                       BACKUP_GCM_AAD,
                                       sizeof(BACKUP_GCM_AAD) - 1,
                                       tag,
                                       BACKUP_TAG_LEN,
                                       ciphertext,
                                       plaintext);
    }
    mbedtls_gcm_free(ctx);
    mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    free(ctx);
    mbedtls_platform_zeroize(key, sizeof(key));
    return ret == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static bool json_add_b64(cJSON *obj, const char *name, const uint8_t *data, size_t len)
{
    char *b64 = base64_encode_alloc(data, len);
    if (!b64) {
        return false;
    }
    cJSON_AddStringToObject(obj, name, b64);
    free(b64);
    return true;
}

static const char *json_string_or_empty(const cJSON *obj, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

static int json_int_or_default(const cJSON *obj, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static void zeroize_json_strings(cJSON *item)
{
    if (!item) {
        return;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        mbedtls_platform_zeroize(item->valuestring, strlen(item->valuestring));
    }
    for (cJSON *child = item->child; child; child = child->next) {
        zeroize_json_strings(child);
    }
}

static void delete_sensitive_json(cJSON *item)
{
    zeroize_json_strings(item);
    cJSON_Delete(item);
}

static esp_err_t read_b64_fixed(const cJSON *obj, const char *name, uint8_t *out, size_t expected_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(item) || !item->valuestring || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = 0;
    uint8_t *decoded = base64_decode_alloc(item->valuestring, &len);
    if (!decoded) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (len != expected_len) {
        free(decoded);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, decoded, expected_len);
    free(decoded);
    return ESP_OK;
}

static esp_err_t decrypt_migration_payload(const cJSON *root,
                                           const char *backup_password,
                                           cJSON **out_payload)
{
    if (!root || !out_payload || !backup_password_is_valid(backup_password)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_payload = NULL;

    const cJSON *migration = cJSON_GetObjectItemCaseSensitive(root, "migration");
    if (!cJSON_IsObject(migration)) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t salt[BACKUP_SALT_LEN] = {0};
    uint8_t nonce[BACKUP_NONCE_LEN] = {0};
    uint8_t tag[BACKUP_TAG_LEN] = {0};
    esp_err_t err = read_b64_fixed(migration, "salt", salt, sizeof(salt));
    if (err == ESP_OK) {
        err = read_b64_fixed(migration, "nonce", nonce, sizeof(nonce));
    }
    if (err == ESP_OK) {
        err = read_b64_fixed(migration, "tag", tag, sizeof(tag));
    }
    if (err != ESP_OK) {
        return err;
    }

    const cJSON *cipher_item = cJSON_GetObjectItemCaseSensitive(migration, "ciphertext");
    if (!cJSON_IsString(cipher_item) || !cipher_item->valuestring) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ciphertext_len = 0;
    uint8_t *ciphertext = base64_decode_alloc(cipher_item->valuestring, &ciphertext_len);
    if (!ciphertext) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (ciphertext_len == 0) {
        free(ciphertext);
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t iterations = (uint32_t)json_int_or_default(migration, "iterations", BACKUP_KDF_ITERATIONS);
    uint8_t *plaintext = calloc(1, ciphertext_len + 1);
    if (!plaintext) {
        free(ciphertext);
        return ESP_ERR_NO_MEM;
    }

    err = backup_gcm_decrypt(backup_password,
                             salt,
                             iterations,
                             nonce,
                             tag,
                             ciphertext,
                             ciphertext_len,
                             plaintext);
    mbedtls_platform_zeroize(ciphertext, ciphertext_len);
    free(ciphertext);
    if (err != ESP_OK) {
        mbedtls_platform_zeroize(plaintext, ciphertext_len);
        free(plaintext);
        return err;
    }

    cJSON *payload = cJSON_Parse((const char *)plaintext);
    mbedtls_platform_zeroize(plaintext, ciphertext_len);
    free(plaintext);
    if (!payload) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_payload = payload;
    return ESP_OK;
}

static esp_err_t apply_migration_servers(const cJSON *payload, int *out_count)
{
    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(payload, "servers");
    if (out_count) {
        *out_count = 0;
    }
    if (!cJSON_IsArray(servers)) {
        return ESP_OK;
    }

    esp_err_t err = server_config_clear_all();
    if (err != ESP_OK) {
        return err;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, servers) {
        if (!cJSON_IsObject(item)) {
            continue;
        }

        server_entry_t entry = {0};
        strlcpy(entry.name, json_string_or_empty(item, "name"), sizeof(entry.name));
        strlcpy(entry.host, json_string_or_empty(item, "host"), sizeof(entry.host));
        entry.port = (uint16_t)json_int_or_default(item, "port", 22);
        strlcpy(entry.username, json_string_or_empty(item, "username"), sizeof(entry.username));
        strlcpy(entry.password, json_string_or_empty(item, "password"), sizeof(entry.password));
        strlcpy(entry.auto_cmd, json_string_or_empty(item, "auto_cmd"), sizeof(entry.auto_cmd));
        entry.icon_id = (uint8_t)json_int_or_default(item, "icon_id", 0);

        if (!entry.host[0] || !entry.username[0]) {
            mbedtls_platform_zeroize(&entry, sizeof(entry));
            continue;
        }

        err = server_config_append(&entry);
        mbedtls_platform_zeroize(&entry, sizeof(entry));
        if (err != ESP_OK) {
            return err;
        }
        if (out_count) {
            (*out_count)++;
        }
    }

    return ESP_OK;
}

static esp_err_t apply_migration_wifi(const cJSON *payload, int *out_count)
{
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(payload, "wifi");
    if (out_count) {
        *out_count = 0;
    }
    if (!cJSON_IsArray(wifi)) {
        return ESP_OK;
    }

    esp_err_t err = wifi_manager_clear_saved_networks();
    if (err != ESP_OK) {
        return err;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, wifi) {
        if (!cJSON_IsObject(item)) {
            continue;
        }
        const char *ssid = json_string_or_empty(item, "ssid");
        const char *password = json_string_or_empty(item, "password");
        if (!ssid[0]) {
            continue;
        }
        err = wifi_manager_save_network(ssid, password);
        if (err != ESP_OK) {
            return err;
        }
        if (out_count) {
            (*out_count)++;
        }
    }

    return ESP_OK;
}

static esp_err_t apply_decoded_migration_payload(cJSON *payload, int *out_servers, int *out_wifi)
{
    if (!payload) {
        return ESP_ERR_INVALID_ARG;
    }

    int servers = 0;
    int wifi = 0;
    esp_err_t err = apply_migration_servers(payload, &servers);
    if (err == ESP_OK) {
        err = apply_migration_wifi(payload, &wifi);
    }
    if (err == ESP_OK) {
        if (out_servers) {
            *out_servers = servers;
        }
        if (out_wifi) {
            *out_wifi = wifi;
        }
    }
    return err;
}

static esp_err_t add_migration_payload(cJSON *root, const char *backup_password)
{
    if (!root || !backup_password_is_valid(backup_password)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *payload = cJSON_CreateObject();
    cJSON *servers = cJSON_CreateArray();
    cJSON *wifi = cJSON_CreateArray();
    if (!payload || !servers || !wifi) {
        cJSON_Delete(payload);
        cJSON_Delete(servers);
        cJSON_Delete(wifi);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(payload, "schema", BACKUP_MIGRATION_SCHEMA);
    cJSON_AddItemToObject(payload, "servers", servers);
    cJSON_AddItemToObject(payload, "wifi", wifi);

    int server_count = server_config_count();
    if (server_count > MAX_SERVERS) {
        server_count = MAX_SERVERS;
    }
    for (int i = 0; i < server_count; ++i) {
        server_entry_t entry = {0};
        esp_err_t err = server_config_load(i, &entry);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "skip migration server %d: %s", i, esp_err_to_name(err));
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            mbedtls_platform_zeroize(&entry, sizeof(entry));
            cJSON_Delete(payload);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(item, "name", entry.name);
        cJSON_AddStringToObject(item, "host", entry.host);
        cJSON_AddNumberToObject(item, "port", entry.port);
        cJSON_AddStringToObject(item, "username", entry.username);
        cJSON_AddStringToObject(item, "password", entry.password);
        cJSON_AddStringToObject(item, "auto_cmd", entry.auto_cmd);
        cJSON_AddNumberToObject(item, "icon_id", entry.icon_id);
        cJSON_AddItemToArray(servers, item);
        mbedtls_platform_zeroize(&entry, sizeof(entry));
    }

    wifi_saved_network_t networks[WIFI_MAX_SAVED_NETWORKS] = {0};
    size_t wifi_count = wifi_manager_get_saved_networks(networks, WIFI_MAX_SAVED_NETWORKS);
    for (size_t i = 0; i < wifi_count; ++i) {
        if (!networks[i].ssid[0]) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            mbedtls_platform_zeroize(networks, sizeof(networks));
            cJSON_Delete(payload);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
        cJSON_AddStringToObject(item, "password", networks[i].password);
        cJSON_AddItemToArray(wifi, item);
    }
    mbedtls_platform_zeroize(networks, sizeof(networks));

    int migration_server_count = cJSON_GetArraySize(servers);
    int migration_wifi_count = cJSON_GetArraySize(wifi);
    char *plain_text = cJSON_PrintUnformatted(payload);
    delete_sensitive_json(payload);
    if (!plain_text) {
        return ESP_ERR_NO_MEM;
    }

    size_t plain_len = strlen(plain_text);
    uint8_t *ciphertext = malloc(plain_len ? plain_len : 1);
    if (!ciphertext) {
        mbedtls_platform_zeroize(plain_text, plain_len);
        free(plain_text);
        return ESP_ERR_NO_MEM;
    }

    uint8_t salt[BACKUP_SALT_LEN] = {0};
    uint8_t nonce[BACKUP_NONCE_LEN] = {0};
    uint8_t tag[BACKUP_TAG_LEN] = {0};
    esp_err_t err = backup_gcm_encrypt(backup_password,
                                       (const uint8_t *)plain_text,
                                       plain_len,
                                       salt,
                                       nonce,
                                       tag,
                                       ciphertext);
    mbedtls_platform_zeroize(plain_text, plain_len);
    free(plain_text);
    if (err != ESP_OK) {
        mbedtls_platform_zeroize(ciphertext, plain_len);
        free(ciphertext);
        return err;
    }

    cJSON *migration = cJSON_CreateObject();
    if (!migration) {
        mbedtls_platform_zeroize(ciphertext, plain_len);
        free(ciphertext);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(migration, "schema", BACKUP_MIGRATION_SCHEMA);
    cJSON_AddStringToObject(migration, "kdf", "pbkdf2-sha256");
    cJSON_AddNumberToObject(migration, "iterations", BACKUP_KDF_ITERATIONS);
    cJSON_AddNumberToObject(migration, "ciphertext_size", (double)plain_len);
    bool ok = json_add_b64(migration, "salt", salt, sizeof(salt)) &&
              json_add_b64(migration, "nonce", nonce, sizeof(nonce)) &&
              json_add_b64(migration, "tag", tag, sizeof(tag)) &&
              json_add_b64(migration, "ciphertext", ciphertext, plain_len);
    mbedtls_platform_zeroize(ciphertext, plain_len);
    free(ciphertext);
    if (!ok) {
        cJSON_Delete(migration);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddItemToObject(root, "migration", migration);
    ESP_LOGI(TAG, "migration payload added: servers=%d wifi=%d", migration_server_count, migration_wifi_count);
    return ESP_OK;
}

static esp_err_t add_blob_value(cJSON *entry, nvs_handle_t h, const char *key)
{
    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, key, NULL, &len);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t *buf = malloc(len ? len : 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(h, key, buf, &len);
    if (err == ESP_OK) {
        char *b64 = base64_encode_alloc(buf, len);
        if (!b64) {
            err = ESP_ERR_NO_MEM;
        } else {
            cJSON_AddStringToObject(entry, "value", b64);
            cJSON_AddNumberToObject(entry, "size", (double)len);
            free(b64);
        }
    }
    free(buf);
    return err;
}

static esp_err_t add_string_value(cJSON *entry, nvs_handle_t h, const char *key)
{
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, NULL, &len);
    if (err != ESP_OK) {
        return err;
    }
    char *buf = calloc(1, len ? len : 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_str(h, key, buf, &len);
    if (err == ESP_OK) {
        cJSON_AddStringToObject(entry, "value", buf);
    }
    free(buf);
    return err;
}

static esp_err_t add_integer_value(cJSON *entry, nvs_handle_t h, const char *key, nvs_type_t type)
{
    esp_err_t err = ESP_OK;
    switch (type) {
    case NVS_TYPE_U8: {
        uint8_t value = 0;
        err = nvs_get_u8(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", value);
        break;
    }
    case NVS_TYPE_I8: {
        int8_t value = 0;
        err = nvs_get_i8(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", value);
        break;
    }
    case NVS_TYPE_U16: {
        uint16_t value = 0;
        err = nvs_get_u16(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", value);
        break;
    }
    case NVS_TYPE_I16: {
        int16_t value = 0;
        err = nvs_get_i16(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", value);
        break;
    }
    case NVS_TYPE_U32: {
        uint32_t value = 0;
        err = nvs_get_u32(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", value);
        break;
    }
    case NVS_TYPE_I32: {
        int32_t value = 0;
        err = nvs_get_i32(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", value);
        break;
    }
    case NVS_TYPE_U64: {
        uint64_t value = 0;
        err = nvs_get_u64(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", (double)value);
        break;
    }
    case NVS_TYPE_I64: {
        int64_t value = 0;
        err = nvs_get_i64(h, key, &value);
        cJSON_AddNumberToObject(entry, "value", (double)value);
        break;
    }
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    return err;
}

static const char *nvs_type_name(nvs_type_t type)
{
    switch (type) {
    case NVS_TYPE_U8: return "u8";
    case NVS_TYPE_I8: return "i8";
    case NVS_TYPE_U16: return "u16";
    case NVS_TYPE_I16: return "i16";
    case NVS_TYPE_U32: return "u32";
    case NVS_TYPE_I32: return "i32";
    case NVS_TYPE_U64: return "u64";
    case NVS_TYPE_I64: return "i64";
    case NVS_TYPE_STR: return "str";
    case NVS_TYPE_BLOB: return "blob";
    default: return "unknown";
    }
}

static bool namespace_allowed(const char *name)
{
    if (!name) {
        return false;
    }
    for (size_t i = 0; i < sizeof(BACKUP_NAMESPACES) / sizeof(BACKUP_NAMESPACES[0]); ++i) {
        if (strcmp(name, BACKUP_NAMESPACES[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool namespace_export_allowed(const char *name, bool migration_mode)
{
    if (!migration_mode) {
        return true;
    }
    return name && strcmp(name, "app_settings") == 0;
}

static nvs_type_t nvs_type_from_name(const char *name)
{
    if (!name) return NVS_TYPE_ANY;
    if (strcmp(name, "u8") == 0) return NVS_TYPE_U8;
    if (strcmp(name, "i8") == 0) return NVS_TYPE_I8;
    if (strcmp(name, "u16") == 0) return NVS_TYPE_U16;
    if (strcmp(name, "i16") == 0) return NVS_TYPE_I16;
    if (strcmp(name, "u32") == 0) return NVS_TYPE_U32;
    if (strcmp(name, "i32") == 0) return NVS_TYPE_I32;
    if (strcmp(name, "u64") == 0) return NVS_TYPE_U64;
    if (strcmp(name, "i64") == 0) return NVS_TYPE_I64;
    if (strcmp(name, "str") == 0) return NVS_TYPE_STR;
    if (strcmp(name, "blob") == 0) return NVS_TYPE_BLOB;
    return NVS_TYPE_ANY;
}

static esp_err_t export_namespace(cJSON *namespaces, const char *ns, int *entry_count)
{
    ESP_LOGI(TAG, "export namespace begin: %s", ns);

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "namespace not found, skip: %s", ns);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open namespace failed: %s err=%s", ns, esp_err_to_name(err));
        return err;
    }

    cJSON *ns_obj = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();
    if (!ns_obj || !entries) {
        nvs_close(h);
        cJSON_Delete(ns_obj);
        cJSON_Delete(entries);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(ns_obj, "name", ns);
    cJSON_AddItemToObject(ns_obj, "entries", entries);

    nvs_iterator_t it = NULL;
    err = nvs_entry_find_in_handle(h, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "namespace has no entries: %s", ns);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "find entries failed: %s err=%s", ns, esp_err_to_name(err));
    }

    int ns_entry_count = 0;
    while (err == ESP_OK) {
        nvs_entry_info_t info = {0};
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read entry info failed: %s err=%s", ns, esp_err_to_name(err));
            break;
        }

        ESP_LOGI(TAG, "export entry: %s/%s type=%s", ns, info.key, nvs_type_name(info.type));
        cJSON *entry = cJSON_CreateObject();
        if (!entry) {
            nvs_release_iterator(it);
            nvs_close(h);
            cJSON_Delete(ns_obj);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(entry, "key", info.key);
        cJSON_AddStringToObject(entry, "type", nvs_type_name(info.type));
        esp_err_t value_err = info.type == NVS_TYPE_BLOB ? add_blob_value(entry, h, info.key)
                                  : info.type == NVS_TYPE_STR ? add_string_value(entry, h, info.key)
                                                              : add_integer_value(entry, h, info.key, info.type);
        if (value_err == ESP_OK) {
            cJSON_AddItemToArray(entries, entry);
            ns_entry_count++;
            if (entry_count) {
                (*entry_count)++;
            }
        } else {
            ESP_LOGW(TAG, "skip %s/%s: %s", ns, info.key, esp_err_to_name(value_err));
            cJSON_Delete(entry);
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(h);

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        cJSON_Delete(ns_obj);
        ESP_LOGE(TAG, "export namespace failed: %s err=%s", ns, esp_err_to_name(err));
        return err;
    }
    cJSON_AddItemToArray(namespaces, ns_obj);
    ESP_LOGI(TAG, "export namespace done: %s entries=%d", ns, ns_entry_count);
    return ESP_OK;
}

static esp_err_t nvs_config_backup_export_impl(const char *backup_password,
                                               bool migration_mode,
                                               char *message,
                                               size_t message_len)
{
    ESP_LOGI(TAG, "export config backup begin: %s", NVS_CONFIG_BACKUP_PATH);

    if (migration_mode && !backup_password_is_valid(backup_password)) {
        set_message(message, message_len, "备份密码需为 4-64 位英文或数字");
        return ESP_ERR_INVALID_ARG;
    }

    if (!ensure_backup_dir()) {
        set_message(message, message_len, "创建备份目录失败");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *namespaces = cJSON_CreateArray();
    if (!root || !namespaces) {
        cJSON_Delete(root);
        cJSON_Delete(namespaces);
        set_message(message, message_len, "内存不足，无法导出");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddStringToObject(root, "project", "M5stack_tab5_ssh_client");
    cJSON_AddStringToObject(root, "mode", migration_mode ? "encrypted-migration" : "plain-device");
    cJSON_AddItemToObject(root, "namespaces", namespaces);

    int entry_count = 0;
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < sizeof(BACKUP_NAMESPACES) / sizeof(BACKUP_NAMESPACES[0]); ++i) {
        const char *ns = BACKUP_NAMESPACES[i];
        if (!namespace_export_allowed(ns, migration_mode)) {
            ESP_LOGI(TAG, "skip namespace in encrypted backup: %s", ns);
            continue;
        }
        err = export_namespace(namespaces, ns, &entry_count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "export stopped at namespace %s: %s", ns, esp_err_to_name(err));
            break;
        }
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        set_message(message, message_len, "导出失败：%s", esp_err_to_name(err));
        return err;
    }

    if (migration_mode) {
        err = add_migration_payload(root, backup_password);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            set_message(message, message_len, "生成加密备份失败：%s", esp_err_to_name(err));
            return err;
        }
    }

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        set_message(message, message_len, "生成备份 JSON 失败");
        return ESP_ERR_NO_MEM;
    }

    FILE *fp = fopen(NVS_CONFIG_BACKUP_PATH, "wb");
    if (!fp) {
        free(text);
        set_message(message, message_len, "打开备份文件失败");
        return ESP_FAIL;
    }
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, fp) == len;
    fclose(fp);
    free(text);
    if (!ok) {
        set_message(message, message_len, "写入备份文件失败");
        return ESP_FAIL;
    }
    if (migration_mode) {
        set_message(message, message_len, "已导出加密备份到 %s（支持跨设备迁移）", NVS_CONFIG_BACKUP_PATH);
    } else {
        set_message(message, message_len, "已导出非加密备份 %d 项到 %s", entry_count, NVS_CONFIG_BACKUP_PATH);
    }
    ESP_LOGI(TAG, "export config backup done: entries=%d path=%s", entry_count, NVS_CONFIG_BACKUP_PATH);
    return ESP_OK;
}

esp_err_t nvs_config_backup_export(char *message, size_t message_len)
{
    return nvs_config_backup_export_impl(NULL, false, message, message_len);
}

esp_err_t nvs_config_backup_export_with_password(const char *backup_password,
                                                 char *message,
                                                 size_t message_len)
{
    return nvs_config_backup_export_impl(backup_password, true, message, message_len);
}

static char *read_file_text(const char *path, size_t max_bytes)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0 || (size_t)len > max_bytes) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *text = calloc(1, (size_t)len + 1);
    if (!text) {
        fclose(fp);
        return NULL;
    }
    if (fread(text, 1, (size_t)len, fp) != (size_t)len) {
        free(text);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return text;
}

esp_err_t nvs_config_backup_has_migration_payload(bool *out_has_migration,
                                                  char *message,
                                                  size_t message_len)
{
    if (!out_has_migration) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_has_migration = false;

    char *text = read_file_text(NVS_CONFIG_BACKUP_PATH, 64 * 1024);
    if (!text) {
        set_message(message, message_len, "读取备份文件失败：%s", NVS_CONFIG_BACKUP_PATH);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        set_message(message, message_len, "备份 JSON 格式错误");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *migration = cJSON_GetObjectItemCaseSensitive(root, "migration");
    *out_has_migration = cJSON_IsObject(migration);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t import_entry(nvs_handle_t h, const cJSON *entry)
{
    const cJSON *key_item = cJSON_GetObjectItemCaseSensitive(entry, "key");
    const cJSON *type_item = cJSON_GetObjectItemCaseSensitive(entry, "type");
    const cJSON *value_item = cJSON_GetObjectItemCaseSensitive(entry, "value");
    if (!cJSON_IsString(key_item) || !key_item->valuestring ||
        !cJSON_IsString(type_item) || !type_item->valuestring || !value_item) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *key = key_item->valuestring;
    nvs_type_t type = nvs_type_from_name(type_item->valuestring);
    switch (type) {
    case NVS_TYPE_U8:
        return nvs_set_u8(h, key, (uint8_t)value_item->valueint);
    case NVS_TYPE_I8:
        return nvs_set_i8(h, key, (int8_t)value_item->valueint);
    case NVS_TYPE_U16:
        return nvs_set_u16(h, key, (uint16_t)value_item->valueint);
    case NVS_TYPE_I16:
        return nvs_set_i16(h, key, (int16_t)value_item->valueint);
    case NVS_TYPE_U32:
        return nvs_set_u32(h, key, (uint32_t)value_item->valuedouble);
    case NVS_TYPE_I32:
        return nvs_set_i32(h, key, (int32_t)value_item->valuedouble);
    case NVS_TYPE_U64:
        return nvs_set_u64(h, key, (uint64_t)value_item->valuedouble);
    case NVS_TYPE_I64:
        return nvs_set_i64(h, key, (int64_t)value_item->valuedouble);
    case NVS_TYPE_STR:
        if (!cJSON_IsString(value_item) || !value_item->valuestring) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_str(h, key, value_item->valuestring);
    case NVS_TYPE_BLOB: {
        if (!cJSON_IsString(value_item) || !value_item->valuestring) {
            return ESP_ERR_INVALID_ARG;
        }
        size_t blob_len = 0;
        uint8_t *blob = base64_decode_alloc(value_item->valuestring, &blob_len);
        if (!blob) {
            return ESP_ERR_NO_MEM;
        }
        esp_err_t err = nvs_set_blob(h, key, blob, blob_len);
        free(blob);
        return err;
    }
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t nvs_config_backup_import_impl(const char *backup_password,
                                               bool migration_mode,
                                               char *message,
                                               size_t message_len)
{
    if (migration_mode && !backup_password_is_valid(backup_password)) {
        set_message(message, message_len, "备份密码需为 4-64 位英文或数字");
        return ESP_ERR_INVALID_ARG;
    }

    char *text = read_file_text(NVS_CONFIG_BACKUP_PATH, 64 * 1024);
    if (!text) {
        set_message(message, message_len, "读取备份文件失败：%s", NVS_CONFIG_BACKUP_PATH);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        set_message(message, message_len, "备份 JSON 格式错误");
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON *namespaces = cJSON_GetObjectItemCaseSensitive(root, "namespaces");
    if (!cJSON_IsArray(namespaces)) {
        cJSON_Delete(root);
        set_message(message, message_len, "备份内容缺少 namespaces");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!migration_mode) {
        const cJSON *migration = cJSON_GetObjectItemCaseSensitive(root, "migration");
        if (cJSON_IsObject(migration)) {
            cJSON_Delete(root);
            set_message(message, message_len, "此备份是加密备份，请输入备份密码导入");
            return ESP_ERR_INVALID_STATE;
        }
    }

    cJSON *migration_payload = NULL;
    if (migration_mode) {
        esp_err_t err = decrypt_migration_payload(root, backup_password, &migration_payload);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            if (err == ESP_ERR_NOT_FOUND) {
                set_message(message, message_len, "备份文件不含加密数据");
            } else if (err == ESP_ERR_INVALID_STATE) {
                set_message(message, message_len, "备份密码错误或加密数据已损坏");
            } else {
                set_message(message, message_len, "加密数据验证失败：%s", esp_err_to_name(err));
            }
            return err;
        }
    }

    int imported = 0;
    const cJSON *ns_obj = NULL;
    cJSON_ArrayForEach(ns_obj, namespaces) {
        const cJSON *name_item = cJSON_GetObjectItemCaseSensitive(ns_obj, "name");
        const cJSON *entries = cJSON_GetObjectItemCaseSensitive(ns_obj, "entries");
        if (!cJSON_IsString(name_item) || !name_item->valuestring || !cJSON_IsArray(entries)) {
            continue;
        }
        if (!namespace_allowed(name_item->valuestring)) {
            ESP_LOGW(TAG, "skip unsupported namespace from backup: %s", name_item->valuestring);
            continue;
        }
        if (migration_mode && strcmp(name_item->valuestring, "pwd_crypto") == 0) {
            ESP_LOGI(TAG, "skip device-local password key during migration import");
            continue;
        }
        nvs_handle_t h = 0;
        esp_err_t err = nvs_open(name_item->valuestring, NVS_READWRITE, &h);
        if (err != ESP_OK) {
            delete_sensitive_json(migration_payload);
            cJSON_Delete(root);
            set_message(message, message_len, "打开 %s 失败：%s", name_item->valuestring, esp_err_to_name(err));
            return err;
        }
        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, entries) {
            err = import_entry(h, entry);
            if (err == ESP_OK) {
                imported++;
            } else {
                ESP_LOGW(TAG, "import entry failed in %s: %s", name_item->valuestring, esp_err_to_name(err));
            }
        }
        err = nvs_commit(h);
        nvs_close(h);
        if (err != ESP_OK) {
            delete_sensitive_json(migration_payload);
            cJSON_Delete(root);
            set_message(message, message_len, "提交 %s 失败：%s", name_item->valuestring, esp_err_to_name(err));
            return err;
        }
    }

    password_crypto_reset();
    (void)server_config_init();

    if (migration_mode) {
        int migrated_servers = 0;
        int migrated_wifi = 0;
        esp_err_t err = apply_decoded_migration_payload(migration_payload, &migrated_servers, &migrated_wifi);
        delete_sensitive_json(migration_payload);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            set_message(message, message_len, "加密备份导入失败：%s", esp_err_to_name(err));
            return err;
        }
        (void)server_config_init();
        cJSON_Delete(root);
        set_message(message, message_len,
                    "已导入加密备份：%d 个服务器、%d 个 WiFi",
                    migrated_servers,
                    migrated_wifi);
        return ESP_OK;
    }

    cJSON_Delete(root);
    set_message(message, message_len, "已导入非加密备份 %d 项", imported);
    return ESP_OK;
}

esp_err_t nvs_config_backup_import(char *message, size_t message_len)
{
    return nvs_config_backup_import_impl(NULL, false, message, message_len);
}

esp_err_t nvs_config_backup_import_with_password(const char *backup_password,
                                                 char *message,
                                                 size_t message_len)
{
    return nvs_config_backup_import_impl(backup_password, true, message, message_len);
}
