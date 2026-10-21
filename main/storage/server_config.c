/**
 * @file server_config.c
 * @brief 服务器配置 NVS 存储实现
 *
 * NVS 布局：
 *   namespace: "srv_cfg"
 *   key "count"       : uint8_t  服务器数量
 *   key "srv_0" ~ "srv_9" : blob  encrypted_server_entry_t
 */
#include "server_config.h"
#include "security/password_crypto.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "mbedtls/platform_util.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "server_cfg";
static int s_count = 0;
static const uint8_t SERVER_ICON_DEFAULT = 0;
#define SERVER_STORE_MAGIC 0x53325632u /* S2V2 */
#define SERVER_STORE_VERSION 2u

typedef struct {
    char     name[32];
    char     host[64];
    uint16_t port;
    char     username[32];
    char     password[64];
    char     auto_cmd[128];
} legacy_server_entry_t;

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  icon_id;
    uint16_t port;
    char     name[32];
    char     host[64];
    char     username[32];
    char     auto_cmd[128];
    password_crypto_blob_t password;
} encrypted_server_entry_t;

/* ---- 内部工具 ---- */

static void make_key(int index, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "srv_%d", index);
}

static void make_password_aad(const encrypted_server_entry_t *entry,
                              uint8_t aad[sizeof(entry->magic) + sizeof(entry->version) +
                                          sizeof(entry->icon_id) + sizeof(entry->port) +
                                          sizeof(entry->name) + sizeof(entry->host) +
                                          sizeof(entry->username) + sizeof(entry->auto_cmd)])
{
    size_t off = 0;
    memcpy(aad + off, &entry->magic, sizeof(entry->magic));
    off += sizeof(entry->magic);
    memcpy(aad + off, &entry->version, sizeof(entry->version));
    off += sizeof(entry->version);
    memcpy(aad + off, &entry->icon_id, sizeof(entry->icon_id));
    off += sizeof(entry->icon_id);
    memcpy(aad + off, &entry->port, sizeof(entry->port));
    off += sizeof(entry->port);
    memcpy(aad + off, entry->name, sizeof(entry->name));
    off += sizeof(entry->name);
    memcpy(aad + off, entry->host, sizeof(entry->host));
    off += sizeof(entry->host);
    memcpy(aad + off, entry->username, sizeof(entry->username));
    off += sizeof(entry->username);
    memcpy(aad + off, entry->auto_cmd, sizeof(entry->auto_cmd));
}

static void fill_store_metadata(encrypted_server_entry_t *store, const server_entry_t *entry)
{
    memset(store, 0, sizeof(*store));
    store->magic = SERVER_STORE_MAGIC;
    store->version = SERVER_STORE_VERSION;
    store->icon_id = entry->icon_id;
    store->port = entry->port;
    strlcpy(store->name, entry->name, sizeof(store->name));
    strlcpy(store->host, entry->host, sizeof(store->host));
    strlcpy(store->username, entry->username, sizeof(store->username));
    strlcpy(store->auto_cmd, entry->auto_cmd, sizeof(store->auto_cmd));
}

static esp_err_t encrypt_entry(const server_entry_t *entry, encrypted_server_entry_t *store)
{
    if (!entry || !store) {
        return ESP_ERR_INVALID_ARG;
    }
    fill_store_metadata(store, entry);
    uint8_t aad[sizeof(store->magic) + sizeof(store->version) + sizeof(store->icon_id) +
                sizeof(store->port) + sizeof(store->name) + sizeof(store->host) +
                sizeof(store->username) + sizeof(store->auto_cmd)] = {0};
    make_password_aad(store, aad);
    return password_crypto_encrypt((const uint8_t *)entry->password,
                                   sizeof(entry->password),
                                   aad,
                                   sizeof(aad),
                                   &store->password);
}

static esp_err_t decrypt_entry(const encrypted_server_entry_t *store, server_entry_t *out)
{
    if (!store || !out || store->magic != SERVER_STORE_MAGIC || store->version != SERVER_STORE_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    strlcpy(out->name, store->name, sizeof(out->name));
    strlcpy(out->host, store->host, sizeof(out->host));
    out->port = store->port;
    strlcpy(out->username, store->username, sizeof(out->username));
    strlcpy(out->auto_cmd, store->auto_cmd, sizeof(out->auto_cmd));
    out->icon_id = store->icon_id;

    uint8_t aad[sizeof(store->magic) + sizeof(store->version) + sizeof(store->icon_id) +
                sizeof(store->port) + sizeof(store->name) + sizeof(store->host) +
                sizeof(store->username) + sizeof(store->auto_cmd)] = {0};
    make_password_aad(store, aad);
    esp_err_t err = password_crypto_decrypt(&store->password,
                                            aad,
                                            sizeof(aad),
                                            (uint8_t *)out->password,
                                            sizeof(out->password));
    if (err != ESP_OK) {
        mbedtls_platform_zeroize(out, sizeof(*out));
    }
    return err;
}

static esp_err_t server_config_save_blob(int index, const server_entry_t *entry)
{
    encrypted_server_entry_t *store = calloc(1, sizeof(*store));
    if (!store) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = encrypt_entry(entry, store);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "encrypt server %d failed: %s", index, esp_err_to_name(err));
        mbedtls_platform_zeroize(store, sizeof(*store));
        free(store);
        return err;
    }

    nvs_handle_t h;
    err = nvs_open(SERVER_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        mbedtls_platform_zeroize(store, sizeof(*store));
        free(store);
        return err;
    }

    char key[16];
    make_key(index, key, sizeof(key));
    err = nvs_set_blob(h, key, store, sizeof(*store));
    mbedtls_platform_zeroize(store, sizeof(*store));
    free(store);
    if (err == ESP_OK) {
        if (index >= s_count) {
            s_count = index + 1;
        }
        nvs_set_u8(h, "count", (uint8_t)s_count);
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void migrate_plaintext_entry_if_needed(int index, const server_entry_t *entry, const char *source)
{
    esp_err_t err = server_config_save_blob(index, entry);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Migrated %s server entry %d to encrypted storage", source, index);
    } else {
        ESP_LOGW(TAG, "Failed to migrate %s server entry %d: %s", source, index, esp_err_to_name(err));
    }
}

/* ---- 公共接口 ---- */

esp_err_t server_config_init(void)
{
    esp_err_t crypto_err = password_crypto_init();
    if (crypto_err != ESP_OK) {
        ESP_LOGE(TAG, "password crypto init failed: %s", esp_err_to_name(crypto_err));
        return crypto_err;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(SERVER_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_count = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t cnt = 0;
    nvs_get_u8(h, "count", &cnt);
    s_count = cnt;
    nvs_close(h);
    ESP_LOGI(TAG, "Loaded %d server entries", s_count);
    return ESP_OK;
}

int server_config_count(void)
{
    return s_count;
}

esp_err_t server_config_load_public(int index, server_entry_t *out)
{
    if (index < 0 || index >= s_count || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(SERVER_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    char key[16];
    make_key(index, key, sizeof(key));
    encrypted_server_entry_t *store = calloc(1, sizeof(*store));
    if (!store) {
        nvs_close(h);
        return ESP_ERR_NO_MEM;
    }
    size_t sz = sizeof(*store);
    err = nvs_get_blob(h, key, store, &sz);
    nvs_close(h);
    if (err == ESP_OK && sz == sizeof(*store) &&
        store->magic == SERVER_STORE_MAGIC && store->version == SERVER_STORE_VERSION) {
        memset(out, 0, sizeof(*out));
        strlcpy(out->name, store->name, sizeof(out->name));
        strlcpy(out->host, store->host, sizeof(out->host));
        out->port = store->port;
        strlcpy(out->username, store->username, sizeof(out->username));
        strlcpy(out->auto_cmd, store->auto_cmd, sizeof(out->auto_cmd));
        out->icon_id = store->icon_id;
        mbedtls_platform_zeroize(store, sizeof(*store));
        free(store);
        return ESP_OK;
    }
    mbedtls_platform_zeroize(store, sizeof(*store));
    free(store);
    return err;
}

esp_err_t server_config_load(int index, server_entry_t *out)
{
    if (index < 0 || index >= s_count || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(SERVER_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    char key[16];
    make_key(index, key, sizeof(key));
    encrypted_server_entry_t *store = calloc(1, sizeof(*store));
    if (!store) {
        nvs_close(h);
        return ESP_ERR_NO_MEM;
    }
    size_t sz = sizeof(*store);
    err = nvs_get_blob(h, key, store, &sz);
    if (err == ESP_OK && sz == sizeof(*store) &&
        store->magic == SERVER_STORE_MAGIC && store->version == SERVER_STORE_VERSION) {
        nvs_close(h);
        err = decrypt_entry(store, out);
        if (err == ESP_OK && store->password.key_source != (uint8_t)password_crypto_key_source()) {
            migrate_plaintext_entry_if_needed(index, out, "rekeyed");
        }
        mbedtls_platform_zeroize(store, sizeof(*store));
        free(store);
        return err;
    }
    mbedtls_platform_zeroize(store, sizeof(*store));
    free(store);

    if (err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_OK) {
        server_entry_t plain = {0};
        sz = sizeof(plain);
        err = nvs_get_blob(h, key, &plain, &sz);
        if (err == ESP_OK && sz == sizeof(plain)) {
            nvs_close(h);
            memcpy(out, &plain, sizeof(*out));
            migrate_plaintext_entry_if_needed(index, &plain, "plain");
            mbedtls_platform_zeroize(&plain, sizeof(plain));
            return ESP_OK;
        }

        legacy_server_entry_t legacy = {0};
        size_t legacy_sz = sizeof(legacy);
        err = nvs_get_blob(h, key, &legacy, &legacy_sz);
        if (err == ESP_OK) {
            memset(out, 0, sizeof(*out));
            memcpy(out, &legacy, sizeof(legacy));
            out->icon_id = SERVER_ICON_DEFAULT;
            nvs_close(h);
            migrate_plaintext_entry_if_needed(index, out, "legacy");
            mbedtls_platform_zeroize(&legacy, sizeof(legacy));
            return ESP_OK;
        }
    }
    nvs_close(h);
    return err;
}

esp_err_t server_config_save(int index, const server_entry_t *entry)
{
    if (index < 0 || index >= MAX_SERVERS) {
        return ESP_ERR_INVALID_ARG;
    }
    return server_config_save_blob(index, entry);
}

esp_err_t server_config_append(const server_entry_t *entry)
{
    if (s_count >= MAX_SERVERS) {
        return ESP_ERR_NO_MEM;
    }
    return server_config_save(s_count, entry);
}

esp_err_t server_config_clear_all(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SERVER_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }

    for (int i = 0; i < MAX_SERVERS; ++i) {
        char key[16];
        make_key(i, key, sizeof(key));
        esp_err_t erase_err = nvs_erase_key(h, key);
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(h);
            return erase_err;
        }
    }

    nvs_set_u8(h, "count", 0);
    err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_count = 0;
    }
    return err;
}

esp_err_t server_config_delete(int index)
{
    if (index < 0 || index >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 将后续条目前移一位 */
    for (int i = index; i < s_count - 1; i++) {
        server_entry_t tmp;
        esp_err_t err = server_config_load(i + 1, &tmp);
        if (err != ESP_OK) return err;
        err = server_config_save(i, &tmp);
        mbedtls_platform_zeroize(&tmp, sizeof(tmp));
        if (err != ESP_OK) return err;
    }
    /* 删除最后一个槽位 */
    nvs_handle_t h;
    esp_err_t err = nvs_open(SERVER_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    char key[16];
    make_key(s_count - 1, key, sizeof(key));
    nvs_erase_key(h, key);  /* 忽略不存在时的错误 */
    s_count--;
    nvs_set_u8(h, "count", (uint8_t)s_count);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}
