/* WiFi credential persistence. Kept in a separate include so wifi_manager.c stays readable. */
#include "security/password_crypto.h"

#include "mbedtls/platform_util.h"

#define WIFI_NVS_KEY_NETWORKS_V2 "networks_v2"
#define WIFI_STORE_ENC_MAGIC     0x57463232u /* WF22 */
#define WIFI_STORE_ENC_VERSION   2u

typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];
    password_crypto_blob_t password;
} encrypted_wifi_network_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint8_t reserved[2];
    encrypted_wifi_network_t items[WIFI_MAX_SAVED_NETWORKS];
} encrypted_wifi_store_t;

static bool validate_wifi_store(const wifi_store_t *store)
{
    return store &&
           store->version == WIFI_STORE_VERSION &&
           store->count <= WIFI_MAX_SAVED_NETWORKS;
}

static void make_wifi_password_aad(const encrypted_wifi_store_t *store,
                                   size_t index,
                                   uint8_t aad[sizeof(store->magic) + sizeof(store->version) +
                                               sizeof(store->count) + sizeof(uint8_t) +
                                               WIFI_MAX_SSID_LEN])
{
    size_t off = 0;
    uint8_t item_index = (uint8_t)index;

    memcpy(aad + off, &store->magic, sizeof(store->magic));
    off += sizeof(store->magic);
    memcpy(aad + off, &store->version, sizeof(store->version));
    off += sizeof(store->version);
    memcpy(aad + off, &store->count, sizeof(store->count));
    off += sizeof(store->count);
    memcpy(aad + off, &item_index, sizeof(item_index));
    off += sizeof(item_index);
    memcpy(aad + off, store->items[index].ssid, WIFI_MAX_SSID_LEN);
}

static esp_err_t encrypt_wifi_store(const wifi_store_t *plain, encrypted_wifi_store_t *out)
{
    if (!validate_wifi_store(plain) || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->magic = WIFI_STORE_ENC_MAGIC;
    out->version = WIFI_STORE_ENC_VERSION;
    out->count = (uint8_t)plain->count;

    for (uint32_t i = 0; i < plain->count; ++i) {
        strlcpy(out->items[i].ssid, plain->items[i].ssid, sizeof(out->items[i].ssid));

        uint8_t aad[sizeof(out->magic) + sizeof(out->version) + sizeof(out->count) +
                    sizeof(uint8_t) + WIFI_MAX_SSID_LEN] = {0};
        make_wifi_password_aad(out, i, aad);
        esp_err_t err = password_crypto_encrypt((const uint8_t *)plain->items[i].password,
                                                sizeof(plain->items[i].password),
                                                aad,
                                                sizeof(aad),
                                                &out->items[i].password);
        if (err != ESP_OK) {
            mbedtls_platform_zeroize(out, sizeof(*out));
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t decrypt_wifi_store(const encrypted_wifi_store_t *enc, wifi_store_t *out)
{
    if (!enc || !out ||
        enc->magic != WIFI_STORE_ENC_MAGIC ||
        enc->version != WIFI_STORE_ENC_VERSION ||
        enc->count > WIFI_MAX_SAVED_NETWORKS) {
        return ESP_ERR_INVALID_ARG;
    }

    init_store(out);
    out->count = enc->count;

    for (uint32_t i = 0; i < enc->count; ++i) {
        strlcpy(out->items[i].ssid, enc->items[i].ssid, sizeof(out->items[i].ssid));

        uint8_t aad[sizeof(enc->magic) + sizeof(enc->version) + sizeof(enc->count) +
                    sizeof(uint8_t) + WIFI_MAX_SSID_LEN] = {0};
        make_wifi_password_aad(enc, i, aad);
        esp_err_t err = password_crypto_decrypt(&enc->items[i].password,
                                                aad,
                                                sizeof(aad),
                                                (uint8_t *)out->items[i].password,
                                                sizeof(out->items[i].password));
        if (err != ESP_OK) {
            mbedtls_platform_zeroize(out, sizeof(*out));
            return err;
        }
    }

    return ESP_OK;
}

static bool wifi_store_needs_rekey(const encrypted_wifi_store_t *enc)
{
    if (!enc) {
        return false;
    }
    for (uint32_t i = 0; i < enc->count && i < WIFI_MAX_SAVED_NETWORKS; ++i) {
        if (enc->items[i].password.key_source != (uint8_t)password_crypto_key_source()) {
            return true;
        }
    }
    return false;
}

static esp_err_t erase_nvs_key_if_present(nvs_handle_t h, const char *key)
{
    esp_err_t err = nvs_erase_key(h, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static esp_err_t save_last_ssid(const char *ssid)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);

    if (err != ESP_OK) {
        return err;
    }

    if (ssid && ssid[0]) {
        err = nvs_set_str(h, WIFI_NVS_KEY_LAST_SSID, ssid);
    } else {
        err = erase_nvs_key_if_present(h, WIFI_NVS_KEY_LAST_SSID);
    }

    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t load_last_ssid(char *ssid_buf, size_t buf_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(h, WIFI_NVS_KEY_LAST_SSID, ssid_buf, &buf_len);
    nvs_close(h);
    return err;
}

static esp_err_t persist_store(const wifi_store_t *store)
{
    nvs_handle_t h;
    esp_err_t err;

    if (!validate_wifi_store(store)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    if (store->count > 0) {
        encrypted_wifi_store_t *enc = calloc(1, sizeof(*enc));
        if (!enc) {
            nvs_close(h);
            return ESP_ERR_NO_MEM;
        }
        err = encrypt_wifi_store(store, enc);
        if (err == ESP_OK) {
            err = nvs_set_blob(h, WIFI_NVS_KEY_NETWORKS_V2, enc, sizeof(*enc));
        }
        mbedtls_platform_zeroize(enc, sizeof(*enc));
        free(enc);
        if (err == ESP_OK) {
            err = erase_nvs_key_if_present(h, WIFI_NVS_KEY_NETWORKS);
        }
    } else {
        err = erase_nvs_key_if_present(h, WIFI_NVS_KEY_NETWORKS_V2);
        if (err == ESP_OK) {
            err = erase_nvs_key_if_present(h, WIFI_NVS_KEY_NETWORKS);
        }
        if (err == ESP_OK) {
            err = erase_nvs_key_if_present(h, WIFI_NVS_KEY_LEGACY_SSID);
        }
        if (err == ESP_OK) {
            err = erase_nvs_key_if_present(h, WIFI_NVS_KEY_LEGACY_PASS);
        }
    }

    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t migrate_legacy_credentials(nvs_handle_t h, wifi_store_t *store)
{
    char ssid[WIFI_MAX_SSID_LEN] = {0};
    char pass[WIFI_MAX_PASS_LEN] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    esp_err_t err;

    err = nvs_get_str(h, WIFI_NVS_KEY_LEGACY_SSID, ssid, &ssid_len);
    if (err != ESP_OK || !ssid[0]) {
        return ESP_ERR_NOT_FOUND;
    }

    err = nvs_get_str(h, WIFI_NVS_KEY_LEGACY_PASS, pass, &pass_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    init_store(store);
    store->count = 1;
    strlcpy(store->items[0].ssid, ssid, sizeof(store->items[0].ssid));
    strlcpy(store->items[0].password, pass, sizeof(store->items[0].password));

    err = persist_store(store);
    if (err == ESP_OK) {
        save_last_ssid(ssid);
        ESP_LOGI(TAG, "Migrated legacy WiFi credential to encrypted storage");
    }
    mbedtls_platform_zeroize(pass, sizeof(pass));
    return err;
}

static esp_err_t load_plain_store(nvs_handle_t h, wifi_store_t *store)
{
    size_t len = sizeof(*store);
    esp_err_t err = nvs_get_blob(h, WIFI_NVS_KEY_NETWORKS, store, &len);
    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(*store) || !validate_wifi_store(store)) {
        init_store(store);
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = persist_store(store);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Migrated WiFi credentials to encrypted storage");
    }
    return err;
}

static esp_err_t load_encrypted_store(nvs_handle_t h, wifi_store_t *store)
{
    encrypted_wifi_store_t *enc = calloc(1, sizeof(*enc));
    if (!enc) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = sizeof(*enc);
    esp_err_t err = nvs_get_blob(h, WIFI_NVS_KEY_NETWORKS_V2, enc, &len);
    if (err == ESP_OK) {
        if (len != sizeof(*enc)) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            err = decrypt_wifi_store(enc, store);
            if (err == ESP_OK && wifi_store_needs_rekey(enc)) {
                esp_err_t rekey_err = persist_store(store);
                if (rekey_err == ESP_OK) {
                    ESP_LOGI(TAG, "Rekeyed WiFi credentials with current device key");
                } else {
                    ESP_LOGW(TAG, "WiFi credential rekey failed: %s", esp_err_to_name(rekey_err));
                }
            }
        }
    }

    mbedtls_platform_zeroize(enc, sizeof(*enc));
    free(enc);
    return err;
}

static esp_err_t load_store(wifi_store_t *store)
{
    nvs_handle_t h;
    esp_err_t err;

    init_store(store);

    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = load_encrypted_store(h, store);
    if (err == ESP_OK) {
        nvs_close(h);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load encrypted WiFi store failed: %s", esp_err_to_name(err));
    }

    err = load_plain_store(h, store);
    if (err == ESP_OK) {
        nvs_close(h);
        return ESP_OK;
    }

    err = migrate_legacy_credentials(h, store);
    nvs_close(h);
    return err;
}
