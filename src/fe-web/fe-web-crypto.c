/*
 fe-web-crypto.c : Application-level encryption for fe-web

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "fe-web-crypto.h"

#include <irssi/src/core/settings.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/core/levels.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <string.h>

/* Global encryption key (derived from password) */
static unsigned char global_key[FE_WEB_CRYPTO_KEY_SIZE];
static int key_initialized = 0;

/* Fixed salt for PBKDF2 (same for all instances)
 * This is OK because we're using password for authentication too
 * If password is compromised, encryption doesn't help anyway
 */
static const unsigned char PBKDF2_SALT[] = "irssi-fe-web-v1";
static const int PBKDF2_SALT_LEN = 15;

/* Initialize crypto subsystem */
void fe_web_crypto_init(void)
{
	const char *password;

	/* Initialize OpenSSL */
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	/* Derive key from password setting */
	password = settings_get_str("fe_web_password");
	if (password == NULL || *password == '\0') {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: ERROR: No password set - encryption disabled");
		key_initialized = 0;
		return;
	}

	if (!fe_web_crypto_derive_key(password, global_key)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: ERROR: Failed to derive encryption key");
		key_initialized = 0;
		return;
	}

	key_initialized = 1;
}

/* Cleanup crypto subsystem */
void fe_web_crypto_deinit(void)
{
	/* Clear key from memory */
	memset(global_key, 0, sizeof(global_key));
	key_initialized = 0;

	/* Cleanup OpenSSL */
	EVP_cleanup();
	ERR_free_strings();
}

/* Derive encryption key from password using PBKDF2 */
int fe_web_crypto_derive_key(const char *password, unsigned char *key_out)
{
	int ret;

	if (password == NULL || key_out == NULL) {
		return 0;
	}

	/* PBKDF2-HMAC-SHA256 */
	ret = PKCS5_PBKDF2_HMAC(
		password, strlen(password),
		PBKDF2_SALT, PBKDF2_SALT_LEN,
		FE_WEB_CRYPTO_ITERATIONS,
		EVP_sha256(),
		FE_WEB_CRYPTO_KEY_SIZE,
		key_out
	);

	if (ret != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: PBKDF2 failed");
		return 0;
	}

	return 1;
}

/* Encrypt plaintext using AES-256-GCM */
int fe_web_crypto_encrypt(const unsigned char *plaintext, int plaintext_len,
                          const unsigned char *key,
                          unsigned char *ciphertext_out, int *ciphertext_len_out)
{
	EVP_CIPHER_CTX *ctx;
	unsigned char iv[FE_WEB_CRYPTO_IV_SIZE];
	unsigned char tag[FE_WEB_CRYPTO_TAG_SIZE];
	unsigned char *ciphertext_ptr;
	int len;
	int ciphertext_len;

	if (plaintext == NULL || key == NULL || ciphertext_out == NULL || ciphertext_len_out == NULL) {
		return 0;
	}

	/* Generate random IV */
	if (RAND_bytes(iv, sizeof(iv)) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Failed to generate IV");
		return 0;
	}

	/* Create cipher context */
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Failed to create cipher context");
		return 0;
	}

	/* Initialize encryption */
	if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Encryption init failed");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}

	/* Encrypt plaintext */
	ciphertext_ptr = ciphertext_out + FE_WEB_CRYPTO_IV_SIZE;
	if (EVP_EncryptUpdate(ctx, ciphertext_ptr, &len, plaintext, plaintext_len) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Encryption update failed");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}
	ciphertext_len = len;

	/* Finalize encryption */
	if (EVP_EncryptFinal_ex(ctx, ciphertext_ptr + len, &len) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Encryption final failed");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}
	ciphertext_len += len;

	/* Get authentication tag */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, FE_WEB_CRYPTO_TAG_SIZE, tag) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Failed to get authentication tag");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}

	/* Build output: IV + ciphertext + tag */
	memcpy(ciphertext_out, iv, FE_WEB_CRYPTO_IV_SIZE);
	memcpy(ciphertext_out + FE_WEB_CRYPTO_IV_SIZE + ciphertext_len, tag, FE_WEB_CRYPTO_TAG_SIZE);

	*ciphertext_len_out = FE_WEB_CRYPTO_IV_SIZE + ciphertext_len + FE_WEB_CRYPTO_TAG_SIZE;

	EVP_CIPHER_CTX_free(ctx);
	return 1;
}

/* Decrypt ciphertext using AES-256-GCM */
int fe_web_crypto_decrypt(const unsigned char *ciphertext, int ciphertext_len,
                          const unsigned char *key,
                          unsigned char *plaintext_out, int *plaintext_len_out)
{
	EVP_CIPHER_CTX *ctx;
	const unsigned char *iv;
	const unsigned char *encrypted_data;
	const unsigned char *tag;
	int encrypted_data_len;
	int len;
	int plaintext_len;
	int ret;

	if (ciphertext == NULL || key == NULL || plaintext_out == NULL || plaintext_len_out == NULL) {
		return 0;
	}

	/* Verify minimum length: IV + tag */
	if (ciphertext_len < FE_WEB_CRYPTO_IV_SIZE + FE_WEB_CRYPTO_TAG_SIZE) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Ciphertext too short");
		return 0;
	}

	/* Extract components: IV + encrypted_data + tag */
	iv = ciphertext;
	encrypted_data = ciphertext + FE_WEB_CRYPTO_IV_SIZE;
	encrypted_data_len = ciphertext_len - FE_WEB_CRYPTO_IV_SIZE - FE_WEB_CRYPTO_TAG_SIZE;
	tag = ciphertext + ciphertext_len - FE_WEB_CRYPTO_TAG_SIZE;

	/* Create cipher context */
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Failed to create cipher context");
		return 0;
	}

	/* Initialize decryption */
	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Decryption init failed");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}

	/* Decrypt ciphertext */
	if (EVP_DecryptUpdate(ctx, plaintext_out, &len, encrypted_data, encrypted_data_len) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Decryption update failed");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}
	plaintext_len = len;

	/* Set expected tag */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, FE_WEB_CRYPTO_TAG_SIZE, (void *)tag) != 1) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Failed to set authentication tag");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}

	/* Finalize decryption and verify tag */
	ret = EVP_DecryptFinal_ex(ctx, plaintext_out + len, &len);
	if (ret <= 0) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-crypto: Authentication failed - message tampered or wrong password");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}
	plaintext_len += len;

	*plaintext_len_out = plaintext_len;

	EVP_CIPHER_CTX_free(ctx);
	return 1;
}

/* Check if encryption is enabled */
int fe_web_crypto_is_enabled(void)
{
	return key_initialized;
}

/* Get current encryption key */
const unsigned char *fe_web_crypto_get_key(void)
{
	if (!key_initialized) {
		return NULL;
	}
	return global_key;
}

