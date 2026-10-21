#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define PASSWORD_CRYPTO_NONCE_LEN 12
#define PASSWORD_CRYPTO_TAG_LEN 16
#define PASSWORD_CRYPTO_SECRET_LEN 32
#define PASSWORD_CRYPTO_MAX_DATA_LEN 64

typedef enum {
    PASSWORD_CRYPTO_KEY_SOURCE_UNKNOWN = 0,
    PASSWORD_CRYPTO_KEY_SOURCE_HMAC_EFUSE = 1,
    PASSWORD_CRYPTO_KEY_SOURCE_NVS_SECRET = 2,
} password_crypto_key_source_t;

typedef struct {
    uint8_t key_source;
    uint8_t reserved[3];
    uint8_t nonce[PASSWORD_CRYPTO_NONCE_LEN];
    uint8_t tag[PASSWORD_CRYPTO_TAG_LEN];
    uint8_t ciphertext[PASSWORD_CRYPTO_MAX_DATA_LEN];
} password_crypto_blob_t;

esp_err_t password_crypto_init(void);
void password_crypto_reset(void);
password_crypto_key_source_t password_crypto_key_source(void);
const char *password_crypto_key_source_name(void);

esp_err_t password_crypto_encrypt(const uint8_t *plaintext,
                                  size_t plaintext_len,
                                  const uint8_t *aad,
                                  size_t aad_len,
                                  password_crypto_blob_t *out);

esp_err_t password_crypto_decrypt(const password_crypto_blob_t *in,
                                  const uint8_t *aad,
                                  size_t aad_len,
                                  uint8_t *plaintext,
                                  size_t plaintext_len);

#ifdef __cplusplus
}
#endif
