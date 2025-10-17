/*
 fe-web-crypto.h : Application-level encryption for fe-web

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef FE_WEB_CRYPTO_H
#define FE_WEB_CRYPTO_H

#include <glib.h>

/* Encryption constants */
#define FE_WEB_CRYPTO_KEY_SIZE 32      /* AES-256 key size (bytes) */
#define FE_WEB_CRYPTO_IV_SIZE 12       /* GCM IV size (bytes) */
#define FE_WEB_CRYPTO_TAG_SIZE 16      /* GCM authentication tag size (bytes) */
#define FE_WEB_CRYPTO_SALT_SIZE 16     /* PBKDF2 salt size (bytes) */
#define FE_WEB_CRYPTO_ITERATIONS 10000 /* PBKDF2 iterations */

/* Encrypted message structure:
 * [IV (12 bytes)] [Ciphertext (variable)] [Tag (16 bytes)]
 */

/* Initialize crypto subsystem */
void fe_web_crypto_init(void);

/* Cleanup crypto subsystem */
void fe_web_crypto_deinit(void);

/* Derive encryption key from password using PBKDF2
 * 
 * @param password: Password string
 * @param key_out: Output buffer for 32-byte key (must be pre-allocated)
 * @return: 1 on success, 0 on failure
 */
int fe_web_crypto_derive_key(const char *password, unsigned char *key_out);

/* Encrypt plaintext using AES-256-GCM
 * 
 * @param plaintext: Data to encrypt
 * @param plaintext_len: Length of plaintext
 * @param key: 32-byte encryption key (from derive_key)
 * @param ciphertext_out: Output buffer (must be pre-allocated: plaintext_len + IV_SIZE + TAG_SIZE)
 * @param ciphertext_len_out: Output length of ciphertext (including IV and tag)
 * @return: 1 on success, 0 on failure
 */
int fe_web_crypto_encrypt(const unsigned char *plaintext, int plaintext_len,
                          const unsigned char *key,
                          unsigned char *ciphertext_out, int *ciphertext_len_out);

/* Decrypt ciphertext using AES-256-GCM
 * 
 * @param ciphertext: Encrypted data (IV + ciphertext + tag)
 * @param ciphertext_len: Length of ciphertext (including IV and tag)
 * @param key: 32-byte encryption key (from derive_key)
 * @param plaintext_out: Output buffer (must be pre-allocated: ciphertext_len - IV_SIZE - TAG_SIZE)
 * @param plaintext_len_out: Output length of plaintext
 * @return: 1 on success, 0 on failure (includes authentication failure)
 */
int fe_web_crypto_decrypt(const unsigned char *ciphertext, int ciphertext_len,
                          const unsigned char *key,
                          unsigned char *plaintext_out, int *plaintext_len_out);

/* Check if encryption is enabled */
int fe_web_crypto_is_enabled(void);

/* Get current encryption key (derived from password setting) */
const unsigned char *fe_web_crypto_get_key(void);

#endif /* FE_WEB_CRYPTO_H */

