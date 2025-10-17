/*
 credential.h : Secure credential management for Irssi/erssi

    Copyright (C) 2024 Irssi Project  
    Copyright (C) 2024-2025 erssi-org team
    Lead Developer: Jerzy (kofany) Dąbrowski <https://github.com/kofany>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef IRSSI_CORE_CREDENTIAL_H
#define IRSSI_CORE_CREDENTIAL_H

#include <irssi/src/lib-config/iconfig.h>

/* Credential storage modes */
typedef enum {
	CREDENTIAL_STORAGE_CONFIG,           /* In main configuration file */
	CREDENTIAL_STORAGE_EXTERNAL          /* In external file */
} CredentialStorageMode;

/* Credential contexts */
typedef enum {
	CREDENTIAL_CONTEXT_SERVER_PASSWORD,
	CREDENTIAL_CONTEXT_SASL_USERNAME,
	CREDENTIAL_CONTEXT_SASL_PASSWORD,
	CREDENTIAL_CONTEXT_PROXY_PASSWORD,
	CREDENTIAL_CONTEXT_OPER_PASSWORD,
	CREDENTIAL_CONTEXT_TLS_PASS,
	CREDENTIAL_CONTEXT_AUTOSENDCMD
} CredentialContext;

/* Structure holding credential data */
typedef struct {
	char *network;           /* Network/server name */
	CredentialContext context; /* Context (data type) */
	char *encrypted_value;   /* Encrypted value */
	char *salt;             /* Salt for encryption */
} CREDENTIAL_REC;

/* === Public functions === */

/* Initialization and deinitialization */
void credential_init(void);
void credential_deinit(void);

/* Master password management */
gboolean credential_set_master_password(const char *password);
void credential_clear_master_password(void);
gboolean credential_has_master_password(void);

/* Credential operations */
gboolean credential_set(const char *network, CredentialContext context, const char *value);
char *credential_get(const char *network, CredentialContext context);
gboolean credential_remove(const char *network, CredentialContext context);
GSList *credential_list(void);

/* Sensitive field detection */
gboolean credential_is_sensitive_field(const char *key, const char *value);
gboolean credential_is_autosendcmd_sensitive(const char *cmd);

/* Migration between modes */
gboolean credential_migrate_to_external(void);
gboolean credential_migrate_to_config(void);
gboolean credential_encrypt_config(void);
gboolean credential_decrypt_config(void);

/* File operations */
gboolean credential_external_save(void);
gboolean credential_external_load(void);
gboolean credential_external_reload(void);

/* Hooks for config write/read */
void credential_config_write_hook(CONFIG_REC *config);
void credential_config_read_hook(CONFIG_REC *config);
void credential_unlock_config(void);

/* Helper functions */
const char *credential_context_to_string(CredentialContext context);
CredentialContext credential_string_to_context(const char *str);
const char *credential_storage_mode_to_string(CredentialStorageMode mode);

/* Cryptographic functions */
char *credential_encrypt(const char *plaintext, const char *password);
char *credential_decrypt(const char *encrypted_data, const char *password);
gboolean credential_crypto_init(void);
void credential_crypto_deinit(void);

/* Global variables */
extern CredentialStorageMode credential_storage_mode;
extern char *credential_external_file;
extern gboolean credential_config_encrypt;

#endif