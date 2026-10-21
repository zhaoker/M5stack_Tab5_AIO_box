#include "security/password_crypto.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "soc/soc_caps.h"
#if SOC_HMAC_SUPPORTED
#include "esp_efuse.h"
#include "esp_efuse_chip.h"
#include "esp_hmac.h"
#include "hal/hmac_types.h"
#endif
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

#define PASSWORD_CRYPTO_NVS_NAMESPACE "pwd_crypto"
#define PASSWORD_CRYPTO_NVS_SECRET_KEY "secret"

static const char *TAG = "pwd_crypto";
static const uint8_t HMAC_ROOT_SEED[] = "tab5:ssh-password-root:v1";
static const uint8_t AES_KEY_LABEL[] = "tab5:ssh-password-aes-256-gcm:v1";

static bool s_ready = false;
static bool s_hmac_key_ready = false;
static bool s_nvs_key_ready = false;
static uint8_t s_hmac_aes_key[PASSWORD_CRYPTO_SECRET_LEN];
static uint8_t s_nvs_aes_key[PASSWORD_CRYPTO_SECRET_LEN];
static password_crypto_key_source_t s_key_source = PASSWORD_CRYPTO_KEY_SOURCE_UNKNOWN;

#if SOC_HMAC_SUPPORTED
static bool hmac_key_id_from_block(esp_efuse_block_t block, hmac_key_id_t *out)
{
    int key_id = (int)block - (int)EFUSE_BLK_KEY0;
    if (key_id < 0 || key_id >= (int)HMAC_KEY_MAX) {
        return false;
    }
    *out = (hmac_key_id_t)key_id;
    return true;
}

static esp_err_t try_hmac_root_key(uint8_t root_key[PASSWORD_CRYPTO_SECRET_LEN])
{
    esp_efuse_block_t block = EFUSE_BLK_KEY_MAX;
    if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &block)) {
        return ESP_ERR_NOT_FOUND;
    }

    hmac_key_id_t key_id = HMAC_KEY0;
    if (!hmac_key_id_from_block(block, &key_id)) {
        ESP_LOGW(TAG, "Found HMAC_UP key block %d but cannot map it to HMAC key id", (int)block);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_hmac_calculate(key_id,
                                       HMAC_ROOT_SEED,
                                       sizeof(HMAC_ROOT_SEED) - 1,
                                       root_key);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Using eFuse HMAC key block %d for SSH password encryption", (int)block);
    } else {
        ESP_LOGW(TAG, "HMAC key derivation failed: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

static esp_err_t load_or_create_nvs_root_key(uint8_t root_key[PASSWORD_CRYPTO_SECRET_LEN], bool create_if_missing)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(PASSWORD_CRYPTO_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = PASSWORD_CRYPTO_SECRET_LEN;
    err = nvs_get_blob(h, PASSWORD_CRYPTO_NVS_SECRET_KEY, root_key, &len);
    if (err == ESP_OK && len == PASSWORD_CRYPTO_SECRET_LEN) {
        nvs_close(h);
        ESP_LOGI(TAG, "Loaded device-local NVS key for SSH password encryption");
        return ESP_OK;
    }

    if (!create_if_missing) {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    esp_fill_random(root_key, PASSWORD_CRYPTO_SECRET_LEN);
    err = nvs_set_blob(h, PASSWORD_CRYPTO_NVS_SECRET_KEY, root_key, PASSWORD_CRYPTO_SECRET_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Created device-local NVS key for SSH password encryption");
    }
    return err;
}

static esp_err_t derive_aes_key(const uint8_t root_key[PASSWORD_CRYPTO_SECRET_LEN],
                                uint8_t out_key[PASSWORD_CRYPTO_SECRET_LEN])
{
    uint8_t material[sizeof(AES_KEY_LABEL) - 1 + PASSWORD_CRYPTO_SECRET_LEN];
    memcpy(material, AES_KEY_LABEL, sizeof(AES_KEY_LABEL) - 1);
    memcpy(material + sizeof(AES_KEY_LABEL) - 1, root_key, PASSWORD_CRYPTO_SECRET_LEN);

    int ret = mbedtls_sha256(material, sizeof(material), out_key, 0);
    mbedtls_platform_zeroize(material, sizeof(material));
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t password_crypto_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    uint8_t root_key[PASSWORD_CRYPTO_SECRET_LEN] = {0};
    esp_err_t err = ESP_ERR_NOT_FOUND;

#if SOC_HMAC_SUPPORTED
    err = try_hmac_root_key(root_key);
    if (err == ESP_OK) {
        err = derive_aes_key(root_key, s_hmac_aes_key);
    }
    if (err == ESP_OK) {
        s_hmac_key_ready = true;
        s_key_source = PASSWORD_CRYPTO_KEY_SOURCE_HMAC_EFUSE;
    }
#endif

    if (err != ESP_OK) {
        err = load_or_create_nvs_root_key(root_key, true);
        if (err == ESP_OK) {
            err = derive_aes_key(root_key, s_nvs_aes_key);
        }
        if (err == ESP_OK) {
            s_nvs_key_ready = true;
            s_key_source = PASSWORD_CRYPTO_KEY_SOURCE_NVS_SECRET;
        }
    } else {
        uint8_t nvs_root_key[PASSWORD_CRYPTO_SECRET_LEN] = {0};
        esp_err_t nvs_err = load_or_create_nvs_root_key(nvs_root_key, false);
        if (nvs_err == ESP_OK) {
            nvs_err = derive_aes_key(nvs_root_key, s_nvs_aes_key);
        }
        if (nvs_err == ESP_OK) {
            s_nvs_key_ready = true;
            ESP_LOGI(TAG, "NVS fallback key is available for older SSH password records");
        }
        mbedtls_platform_zeroize(nvs_root_key, sizeof(nvs_root_key));
    }
    mbedtls_platform_zeroize(root_key, sizeof(root_key));

    if (err == ESP_OK) {
        s_ready = true;
        ESP_LOGI(TAG, "SSH password crypto ready, key source: %s", password_crypto_key_source_name());
    } else {
        s_key_source = PASSWORD_CRYPTO_KEY_SOURCE_UNKNOWN;
        mbedtls_platform_zeroize(s_hmac_aes_key, sizeof(s_hmac_aes_key));
        mbedtls_platform_zeroize(s_nvs_aes_key, sizeof(s_nvs_aes_key));
        ESP_LOGE(TAG, "SSH password crypto init failed: %s", esp_err_to_name(err));
    }
    return err;
}

void password_crypto_reset(void)
{
    s_ready = false;
    s_hmac_key_ready = false;
    s_nvs_key_ready = false;
    s_key_source = PASSWORD_CRYPTO_KEY_SOURCE_UNKNOWN;
    mbedtls_platform_zeroize(s_hmac_aes_key, sizeof(s_hmac_aes_key));
    mbedtls_platform_zeroize(s_nvs_aes_key, sizeof(s_nvs_aes_key));
}

password_crypto_key_source_t password_crypto_key_source(void)
{
    return s_key_source;
}

const char *password_crypto_key_source_name(void)
{
    switch (s_key_source) {
    case PASSWORD_CRYPTO_KEY_SOURCE_HMAC_EFUSE:
        return "hmac_efuse";
    case PASSWORD_CRYPTO_KEY_SOURCE_NVS_SECRET:
        return "nvs_secret";
    default:
        return "unknown";
    }
}

static const uint8_t *key_for_source(uint8_t source)
{
    if (source == PASSWORD_CRYPTO_KEY_SOURCE_HMAC_EFUSE && s_hmac_key_ready) {
        return s_hmac_aes_key;
    }
    if (source == PASSWORD_CRYPTO_KEY_SOURCE_NVS_SECRET && s_nvs_key_ready) {
        return s_nvs_aes_key;
    }
    return NULL;
}

esp_err_t password_crypto_encrypt(const uint8_t *plaintext,
                                  size_t plaintext_len,
                                  const uint8_t *aad,
                                  size_t aad_len,
                                  password_crypto_blob_t *out)
{
    if (!plaintext || !out || plaintext_len > PASSWORD_CRYPTO_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = password_crypto_init();
    if (err != ESP_OK) {
        return err;
    }

    memset(out, 0, sizeof(*out));
    out->key_source = (uint8_t)s_key_source;
    esp_fill_random(out->nonce, sizeof(out->nonce));

    const uint8_t *key = key_for_source(out->key_source);
    if (!key) {
        return ESP_ERR_INVALID_STATE;
    }

    mbedtls_gcm_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    mbedtls_gcm_init(ctx);
    int ret = mbedtls_gcm_setkey(ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret == 0) {
        ret = mbedtls_gcm_crypt_and_tag(ctx,
                                        MBEDTLS_GCM_ENCRYPT,
                                        plaintext_len,
                                        out->nonce,
                                        sizeof(out->nonce),
                                        aad,
                                        aad_len,
                                        plaintext,
                                        out->ciphertext,
                                        PASSWORD_CRYPTO_TAG_LEN,
                                        out->tag);
    }
    mbedtls_gcm_free(ctx);
    mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    free(ctx);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t password_crypto_decrypt(const password_crypto_blob_t *in,
                                  const uint8_t *aad,
                                  size_t aad_len,
                                  uint8_t *plaintext,
                                  size_t plaintext_len)
{
    if (!in || !plaintext || plaintext_len > PASSWORD_CRYPTO_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = password_crypto_init();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t *key = key_for_source(in->key_source);
    if (!key) {
        return ESP_ERR_INVALID_STATE;
    }

    mbedtls_gcm_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    mbedtls_gcm_init(ctx);
    int ret = mbedtls_gcm_setkey(ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret == 0) {
        ret = mbedtls_gcm_auth_decrypt(ctx,
                                       plaintext_len,
                                       in->nonce,
                                       sizeof(in->nonce),
                                       aad,
                                       aad_len,
                                       in->tag,
                                       PASSWORD_CRYPTO_TAG_LEN,
                                       in->ciphertext,
                                       plaintext);
    }
    mbedtls_gcm_free(ctx);
    mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    free(ctx);
    return ret == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}
