/*
 credential.c : Secure credential management for Irssi/erssi

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

#include "module.h"
#include <irssi/src/core/credential.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/misc.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/lib-config/iconfig.h>
#include <irssi/src/core/chatnets.h>
#include <irssi/src/core/servers-setup.h>
#include <irssi/src/fe-common/core/printtext.h>





#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Global configuration variables */
CredentialStorageMode credential_storage_mode = CREDENTIAL_STORAGE_CONFIG;
char *credential_external_file = NULL;
gboolean credential_config_encrypt = FALSE;

/* Private variables */
static char *master_password = NULL;
static GSList *credentials = NULL;
static CONFIG_REC *external_config = NULL;

/* Forward declaration */
static void credential_decrypt_config_nodes(CONFIG_REC *config);
static void sig_chatnet_read_credential_fill(void *rec, void *node);
static void sig_chatnet_saved_credential_capture(void *rec, void *node);
static void sig_server_setup_saved_credential_capture(void *rec, void *node);


/* Array mapping contexts to strings */
static const struct {
	CredentialContext context;
	const char *name;
} context_names[] = {
	{ CREDENTIAL_CONTEXT_SERVER_PASSWORD, "server_password" },
	{ CREDENTIAL_CONTEXT_SASL_USERNAME, "sasl_username" },
	{ CREDENTIAL_CONTEXT_SASL_PASSWORD, "sasl_password" },
	{ CREDENTIAL_CONTEXT_PROXY_PASSWORD, "proxy_password" },
	{ CREDENTIAL_CONTEXT_OPER_PASSWORD, "oper_password" },
	{ CREDENTIAL_CONTEXT_TLS_PASS, "tls_pass" },
	{ CREDENTIAL_CONTEXT_AUTOSENDCMD, "autosendcmd" }
};

/* Array of sensitive fields */
static const char *sensitive_fields[] = {
	"password",
	"sasl_password", 
	"sasl_username",
	"proxy_password",
	"irssiproxy_password",
	"oper_password",
	"tls_pass",
	"autosendcmd",
	"fe_web_password",
	NULL
};

/* Patterns for autosendcmd containing credentials */
static const char *autosendcmd_patterns[] = {
	"NickServ identify",
	"Q@CServe.quakenet.org AUTH",
	"NS IDENTIFY",
	"MSG NickServ",
	"PRIVMSG NickServ",
	"PRIVMSG Q@CServe.quakenet.org",
	NULL
};

/* === Helper functions === */

static void credential_free(CREDENTIAL_REC *rec)
{
	if (rec == NULL) return;
	
	g_free(rec->network);
	g_free(rec->encrypted_value);
	g_free(rec->salt);
	g_free(rec);
}

static CREDENTIAL_REC *credential_find(const char *network, CredentialContext context)
{
	GSList *tmp;
	
	g_return_val_if_fail(network != NULL, NULL);
	
	for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
		CREDENTIAL_REC *rec = tmp->data;
		
		if (rec->context == context && 
		    g_ascii_strcasecmp(rec->network, network) == 0) {
			return rec;
		}
	}
	
	return NULL;
}

static void credential_storage_mode_changed(void)
{
	const char *mode_str = settings_get_str("credential_storage_mode");
	CredentialStorageMode old_mode = credential_storage_mode;
	CredentialStorageMode new_mode;

	if (g_ascii_strcasecmp(mode_str, "external") == 0) {
		new_mode = CREDENTIAL_STORAGE_EXTERNAL;
	} else {
		new_mode = CREDENTIAL_STORAGE_CONFIG;
	}

	/* No change - exit */
	if (old_mode == new_mode) {
		credential_storage_mode = new_mode;
		return;
	}

	/* Migration Config -> External */
	if (old_mode == CREDENTIAL_STORAGE_CONFIG && new_mode == CREDENTIAL_STORAGE_EXTERNAL) {
		credential_storage_mode = new_mode;

		/* Call migration function */
		credential_migrate_to_external();
	}
	/* Migration External -> Config */
	else if (old_mode == CREDENTIAL_STORAGE_EXTERNAL && new_mode == CREDENTIAL_STORAGE_CONFIG) {
		credential_storage_mode = new_mode;

		/* Call migration function */
		credential_migrate_to_config();
	} else {
		credential_storage_mode = new_mode;
	}
}

static void credential_external_file_changed(void)
{
	g_free(credential_external_file);
	credential_external_file = g_strdup(settings_get_str("credential_external_file"));
}

static void credential_config_encrypt_changed(void)
{
	gboolean new_value = settings_get_bool("credential_config_encrypt");
	gboolean was_enabled = credential_config_encrypt;
	GSList *tmp;

	if (new_value == was_enabled) {
		return; /* No change */
	}

	/* Check if we can perform the operation */
	if (new_value && !credential_has_master_password()) {
		signal_emit("gui dialog", 2, "warning",
			"You are enabling encryption without a master password. "
			"Credentials will NOT be encrypted until you set one with /credential passwd.");
		credential_config_encrypt = new_value;
		return;
	}

	/* Change OFF -> ON: Encrypt all data */
	if (new_value && !was_enabled && credential_has_master_password()) {
		g_warning("Encryption enabled - converting all credentials to encrypted format");

		credential_config_encrypt = new_value; /* Set before conversion */

		/* Process all credentials */
		for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
			CREDENTIAL_REC *rec = tmp->data;
			char *encrypted_value;

			/* Check if already encrypted */
			if (rec->encrypted_value && strchr(rec->encrypted_value, ':') != NULL) {
				continue; /* Already encrypted */
			}

			/* Encrypt */
			encrypted_value = credential_encrypt(rec->encrypted_value, master_password);
			if (encrypted_value != NULL) {
				g_free(rec->encrypted_value);
				rec->encrypted_value = encrypted_value;
				g_warning("Encrypted credential for %s (%s)",
				         rec->network, credential_context_to_string(rec->context));
			} else {
				g_warning("Failed to encrypt credential for %s (%s)",
				         rec->network, credential_context_to_string(rec->context));
			}
		}

		/* Save to appropriate storage */
		if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL) {
			credential_external_save();
			signal_emit("gui dialog", 2, "info",
				"All credentials encrypted and saved to external file.");
		} else {
			/* In config mode - will be saved by config_write_hook */
			signal_emit("gui dialog", 2, "info",
				"All credentials encrypted. Use /SAVE to write to config.");
		}
	}
	/* Change ON -> OFF: Decrypt all data */
	else if (!new_value && was_enabled && credential_has_master_password()) {
		g_warning("Encryption disabled - converting all credentials to plaintext");

		credential_config_encrypt = new_value; /* Set before conversion */

		/* Process all credentials */
		for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
			CREDENTIAL_REC *rec = tmp->data;
			char *decrypted_value;

			/* Check if encrypted */
			if (!rec->encrypted_value || strchr(rec->encrypted_value, ':') == NULL) {
				continue; /* Already plaintext */
			}

			/* Decrypt */
			decrypted_value = credential_decrypt(rec->encrypted_value, master_password);
			if (decrypted_value != NULL) {
				g_free(rec->encrypted_value);
				rec->encrypted_value = decrypted_value;
				g_warning("Decrypted credential for %s (%s)",
				         rec->network, credential_context_to_string(rec->context));
			} else {
				g_warning("Failed to decrypt credential for %s (%s)",
				         rec->network, credential_context_to_string(rec->context));
			}
		}

		/* Save to appropriate storage */
		if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL) {
			credential_external_save();
			signal_emit("gui dialog", 2, "info",
				"All credentials decrypted and saved to external file.");
		} else {
			signal_emit("gui dialog", 2, "info",
				"All credentials decrypted. Use /SAVE to write to config.");
		}
	} else {
		credential_config_encrypt = new_value;
	}
}

/* === Public functions === */

const char *credential_context_to_string(CredentialContext context)
{
	int i;
	
	for (i = 0; i < G_N_ELEMENTS(context_names); i++) {
		if (context_names[i].context == context) {
			return context_names[i].name;
		}
	}
	
	return "unknown";
}

CredentialContext credential_string_to_context(const char *str)
{
	int i;
	
	g_return_val_if_fail(str != NULL, CREDENTIAL_CONTEXT_SERVER_PASSWORD);
	
	for (i = 0; i < G_N_ELEMENTS(context_names); i++) {
		if (g_ascii_strcasecmp(context_names[i].name, str) == 0) {
			return context_names[i].context;
		}
	}
	
	return CREDENTIAL_CONTEXT_SERVER_PASSWORD;
}

const char *credential_storage_mode_to_string(CredentialStorageMode mode)
{
	switch (mode) {
	case CREDENTIAL_STORAGE_EXTERNAL:
		return "external";
	default:
		return "config";
	}
}

gboolean credential_set_master_password(const char *password)
{
	g_return_val_if_fail(password != NULL, FALSE);
	
	/* Securely clear previous password */
	if (master_password != NULL) {
		memset(master_password, 0, strlen(master_password));
		g_free(master_password);
	}
	
	master_password = g_strdup(password);
	return TRUE;
}

void credential_clear_master_password(void)
{
	if (master_password != NULL) {
		memset(master_password, 0, strlen(master_password));
		g_free(master_password);
		master_password = NULL;
	}
}

gboolean credential_has_master_password(void)
{
	return master_password != NULL;
}

gboolean credential_is_sensitive_field(const char *key, const char *value)
{
	int i;
	
	g_return_val_if_fail(key != NULL, FALSE);
	
	/* Check direct sensitive fields */
	for (i = 0; sensitive_fields[i] != NULL; i++) {
		if (g_ascii_strcasecmp(key, sensitive_fields[i]) == 0) {
			return TRUE;
		}
	}
	
	/* Special case - autosendcmd */
	if (g_ascii_strcasecmp(key, "autosendcmd") == 0 && value != NULL) {
		return credential_is_autosendcmd_sensitive(value);
	}
	
	return FALSE;
}

gboolean credential_is_autosendcmd_sensitive(const char *cmd)
{
	int i;
	
	g_return_val_if_fail(cmd != NULL, FALSE);
	
	for (i = 0; autosendcmd_patterns[i] != NULL; i++) {
		if (strstr(cmd, autosendcmd_patterns[i]) != NULL) {
			return TRUE;
		}
	}
	
	return FALSE;
}

gboolean credential_set(const char *network, CredentialContext context, const char *value)
{
	CREDENTIAL_REC *rec;
	char *encrypted_value = NULL;
	gboolean should_encrypt = FALSE;

	g_return_val_if_fail(network != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);

	/* Find existing record or create new one */
	rec = credential_find(network, context);
	if (rec == NULL) {
		rec = g_new0(CREDENTIAL_REC, 1);
		rec->network = g_strdup(network);
		rec->context = context;
		credentials = g_slist_append(credentials, rec);
	} else {
		/* Clear old data */
		g_free(rec->encrypted_value);
		g_free(rec->salt);
	}

	/* Encryption logic:
	 * - credential_config_encrypt=ON: encrypt (requires master_password)
	 * - credential_config_encrypt=OFF: plaintext
	 * Independent of storage mode (config or external)
	 */

	if (credential_config_encrypt) {
		should_encrypt = TRUE;
	}

	/* Check if value is already encrypted */
	if (value != NULL && strchr(value, ':') != NULL) {
		should_encrypt = FALSE;
	}

	if (should_encrypt) {
		if (master_password == NULL) {
			/* Cannot encrypt without password - store plaintext with warning */
			g_warning("Encryption is ON but master password not set. Storing in plaintext.");
			rec->encrypted_value = g_strdup(value);
		} else {
			encrypted_value = credential_encrypt(value, master_password);
			if (encrypted_value == NULL) {
				if (rec->network == NULL) {
					credentials = g_slist_remove(credentials, rec);
					credential_free(rec);
				}
				return FALSE;
			}
			rec->encrypted_value = encrypted_value;
		}
	} else {
		/* Store in plaintext */
		rec->encrypted_value = g_strdup(value);
	}

	rec->salt = NULL;

	/* Automatic save to external file if used */
	if (credential_storage_mode != CREDENTIAL_STORAGE_CONFIG) {
		credential_external_save();
	}

	return TRUE;
}

char *credential_get(const char *network, CredentialContext context)
{
	CREDENTIAL_REC *rec;
	char *decrypted_value;
	
	g_return_val_if_fail(network != NULL, NULL);

	rec = credential_find(network, context);
	if (rec == NULL) {
		return NULL;
	}

	/* Check if value is encrypted (contains ':' as separator) */
	if (rec->encrypted_value != NULL && strchr(rec->encrypted_value, ':') != NULL) {
		if (master_password == NULL) {
			g_warning("Credential for %s (%s) is encrypted but no master password set",
			         network, credential_context_to_string(context));
			return NULL;
		}
		/* Decrypt value */
		decrypted_value = credential_decrypt(rec->encrypted_value, master_password);
		if (decrypted_value == NULL) {
			g_warning("Failed to decrypt credential for %s (%s)",
			         network, credential_context_to_string(context));
			return NULL;
		}
		return decrypted_value;
	} else {
		/* Return plaintext */
		return g_strdup(rec->encrypted_value);
	}
}

gboolean credential_remove(const char *network, CredentialContext context)
{
	CREDENTIAL_REC *rec;
	
	g_return_val_if_fail(network != NULL, FALSE);
	
	rec = credential_find(network, context);
	if (rec == NULL) {
		return FALSE;
	}
	
	credentials = g_slist_remove(credentials, rec);
	credential_free(rec);
	
	return TRUE;
}

GSList *credential_list(void)
{
	return g_slist_copy(credentials);
}

/* === Migration functions === */

gboolean credential_migrate_to_external(void)
{
	char *external_path;
	char *config_path;
	CONFIG_REC *external_config;
	CONFIG_REC *source_config;
	CONFIG_NODE *src_root, *dst_root;
	CONFIG_NODE *src_node;
	CONFIG_NODE *dst_servers_node, *dst_chatnets_node;
	GSList *tmp;
	gboolean success = TRUE;

	/* Open main config file directly from disk (without hooks)
	   to get encrypted values if encryption is ON */
	config_path = g_strdup_printf("%s/config", get_irssi_dir());
	source_config = config_open(config_path, -1);
	g_free(config_path);

	if (source_config == NULL) {
		g_warning("Failed to open source config file");
		return FALSE;
	}

	if (config_parse(source_config) != 0) {
		g_warning("Failed to parse source config file");
		config_close(source_config);
		return FALSE;
	}

	/* Open external file for writing */
	if (credential_external_file == NULL) {
		/* Should never happen - defensive check */
		config_close(source_config);
		return FALSE;
	}

	external_path = g_strdup_printf("%s/%s", get_irssi_dir(), credential_external_file);
	external_config = config_open(external_path, 0600);
	g_free(external_path);

	if (external_config == NULL) {
		g_warning("Failed to open external config");
		config_close(source_config);
		return FALSE;
	}

	/* Clear existing content of external file */
	config_nodes_remove_all(external_config);

	src_root = source_config->mainnode;
	dst_root = external_config->mainnode;

	/* Create sections in destination file */
	dst_servers_node = config_node_section(external_config, dst_root, "servers", NODE_TYPE_LIST);
	dst_chatnets_node = config_node_section(external_config, dst_root, "chatnets", NODE_TYPE_BLOCK);

	/* Copy servers section - ONLY password (without decryption) */
	src_node = config_node_find(src_root, "servers");
	if (src_node != NULL && src_node->value != NULL) {
		for (tmp = config_node_first(src_node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *src_server = tmp->data;
			char *address, *chatnet, *password;

			if (src_server->type != NODE_TYPE_BLOCK) continue;

			address = config_node_get_str(src_server, "address", NULL);
			chatnet = config_node_get_str(src_server, "chatnet", NULL);
			password = config_node_get_str(src_server, "password", NULL);

			if (password != NULL) {
				CONFIG_NODE *dst_server = config_node_section(external_config, dst_servers_node, NULL, NODE_TYPE_BLOCK);

				/* Copy server metadata */
				if (address != NULL) {
					config_node_set_str(external_config, dst_server, "address", address);
				}
				if (chatnet != NULL) {
					config_node_set_str(external_config, dst_server, "chatnet", chatnet);
				}

				/* Copy password AS-IS (may be encrypted or plaintext) */
				config_node_set_str(external_config, dst_server, "password", password);

				/* Remove from mainconfig */
				config_node_set_str(mainconfig, src_server, "password", NULL);
			}
		}
	}

	/* Copy chatnets section - sasl_username, sasl_password, autosendcmd */
	src_node = config_node_find(src_root, "chatnets");
	if (src_node != NULL && src_node->value != NULL) {
		for (tmp = config_node_first(src_node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *src_chatnet = tmp->data;
			char *chatnet_name;
			char *sasl_username, *sasl_password, *autosendcmd;
			CONFIG_NODE *dst_chatnet = NULL;

			if (src_chatnet->type != NODE_TYPE_BLOCK || src_chatnet->key == NULL) continue;

			chatnet_name = src_chatnet->key;
			sasl_username = config_node_get_str(src_chatnet, "sasl_username", NULL);
			sasl_password = config_node_get_str(src_chatnet, "sasl_password", NULL);
			autosendcmd = config_node_get_str(src_chatnet, "autosendcmd", NULL);

			/* Copy credentials AS-IS if they exist */
			if (sasl_username != NULL || sasl_password != NULL ||
			    (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd))) {

				/* Create node in external file if it doesn't exist yet */
				dst_chatnet = config_node_section(external_config, dst_chatnets_node, chatnet_name, NODE_TYPE_BLOCK);
			}

			if (sasl_username != NULL) {
				/* Copy AS-IS without decryption */
				config_node_set_str(external_config, dst_chatnet, "sasl_username", sasl_username);
				/* Remove from mainconfig */
				config_node_set_str(mainconfig, src_chatnet, "sasl_username", NULL);
			}

			if (sasl_password != NULL) {
				/* Copy AS-IS without decryption */
				config_node_set_str(external_config, dst_chatnet, "sasl_password", sasl_password);
				/* Remove from mainconfig */
				config_node_set_str(mainconfig, src_chatnet, "sasl_password", NULL);
			}

			if (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd)) {
				/* Copy AS-IS without decryption */
				config_node_set_str(external_config, dst_chatnet, "autosendcmd", autosendcmd);
				/* Remove from mainconfig */
				config_node_set_str(mainconfig, src_chatnet, "autosendcmd", NULL);
			}
		}
	}

	/* Save external file */
	if (config_write(external_config, NULL, 0600) != 0) {
		g_warning("Failed to write external credentials file");
		success = FALSE;
	}

	config_close(external_config);
	config_close(source_config);

	/* Save modified mainconfig (without credentials) */
	if (success) {
		if (settings_save(NULL, FALSE) == 0) {
			g_warning("Failed to save modified config");
			success = FALSE;
		}
	}

	if (!success) {
		g_warning("Migration failed - config may be in inconsistent state");
	}

	return success;
}

gboolean credential_migrate_to_config(void)
{
	char *external_path;
	CONFIG_REC *external_config;
	CONFIG_NODE *src_root, *dst_root;
	CONFIG_NODE *src_node;
	CONFIG_NODE *dst_servers_node, *dst_chatnets_node;
	GSList *tmp;
	gboolean success = TRUE;

	/* Open external file for reading */
	if (credential_external_file == NULL) {
		/* Should never happen - defensive check */
		return FALSE;
	}

	external_path = g_strdup_printf("%s/%s", get_irssi_dir(), credential_external_file);
	external_config = config_open(external_path, -1);
	g_free(external_path);

	if (external_config == NULL) {
		g_warning("Failed to open external config");
		return FALSE;
	}

	if (config_parse(external_config) != 0) {
		g_warning("Failed to parse external config");
		config_close(external_config);
		return FALSE;
	}

	src_root = external_config->mainnode;
	dst_root = mainconfig->mainnode;

	/* Create sections in mainconfig if they don't exist */
	dst_servers_node = config_node_section(mainconfig, dst_root, "servers", NODE_TYPE_LIST);
	dst_chatnets_node = config_node_section(mainconfig, dst_root, "chatnets", NODE_TYPE_BLOCK);

	/* Copy servers section - ONLY password (without decryption) */
	src_node = config_node_find(src_root, "servers");
	if (src_node != NULL && src_node->value != NULL) {
		for (tmp = config_node_first(src_node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *src_server = tmp->data;
			char *address, *chatnet, *password;

			if (src_server->type != NODE_TYPE_BLOCK) continue;

			address = config_node_get_str(src_server, "address", NULL);
			chatnet = config_node_get_str(src_server, "chatnet", NULL);
			password = config_node_get_str(src_server, "password", NULL);

			if (password != NULL && chatnet != NULL) {
				/* Find or create appropriate server in mainconfig */
				GSList *dst_tmp;
				CONFIG_NODE *dst_server = NULL;

				/* Search for existing server */
				for (dst_tmp = config_node_first(dst_servers_node->value);
				     dst_tmp != NULL; dst_tmp = config_node_next(dst_tmp)) {
					CONFIG_NODE *srv = dst_tmp->data;
					char *srv_address, *srv_chatnet;

					if (srv->type != NODE_TYPE_BLOCK) continue;

					srv_address = config_node_get_str(srv, "address", NULL);
					srv_chatnet = config_node_get_str(srv, "chatnet", NULL);

					if ((srv_address && g_ascii_strcasecmp(srv_address, address) == 0) ||
					    (srv_chatnet && g_ascii_strcasecmp(srv_chatnet, chatnet) == 0)) {
						dst_server = srv;
						break;
					}
				}

				/* Create new server if not found */
				if (dst_server == NULL) {
					dst_server = config_node_section(mainconfig, dst_servers_node, NULL, NODE_TYPE_BLOCK);
					if (address != NULL) {
						config_node_set_str(mainconfig, dst_server, "address", address);
					}
					if (chatnet != NULL) {
						config_node_set_str(mainconfig, dst_server, "chatnet", chatnet);
					}
				}

				/* Copy password AS-IS (may be encrypted or plaintext) */
				config_node_set_str(mainconfig, dst_server, "password", password);
			}
		}
	}

	/* Copy chatnets section - sasl_username, sasl_password, autosendcmd */
	src_node = config_node_find(src_root, "chatnets");
	if (src_node != NULL && src_node->value != NULL) {
		for (tmp = config_node_first(src_node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *src_chatnet = tmp->data;
			char *chatnet_name;
			char *sasl_username, *sasl_password, *autosendcmd;
			CONFIG_NODE *dst_chatnet;

			if (src_chatnet->type != NODE_TYPE_BLOCK || src_chatnet->key == NULL) continue;

			chatnet_name = src_chatnet->key;
			sasl_username = config_node_get_str(src_chatnet, "sasl_username", NULL);
			sasl_password = config_node_get_str(src_chatnet, "sasl_password", NULL);
			autosendcmd = config_node_get_str(src_chatnet, "autosendcmd", NULL);

			/* Copy credentials AS-IS if they exist */
			if (sasl_username != NULL || sasl_password != NULL ||
			    (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd))) {

				/* Find or create node in mainconfig */
				dst_chatnet = config_node_section(mainconfig, dst_chatnets_node, chatnet_name, NODE_TYPE_BLOCK);
			} else {
				continue;
			}

			if (sasl_username != NULL) {
				/* Copy AS-IS without decryption */
				config_node_set_str(mainconfig, dst_chatnet, "sasl_username", sasl_username);
			}

			if (sasl_password != NULL) {
				/* Copy AS-IS without decryption */
				config_node_set_str(mainconfig, dst_chatnet, "sasl_password", sasl_password);
			}

			if (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd)) {
				/* Copy AS-IS without decryption */
				config_node_set_str(mainconfig, dst_chatnet, "autosendcmd", autosendcmd);
			}
		}
	}

	config_close(external_config);

	/* Save modified mainconfig (with credentials) */
	if (settings_save(NULL, FALSE) == 0) {
		g_warning("Failed to save modified config");
		success = FALSE;
	}

	/* Remove external file after successful migration */
	if (success) {
		external_path = g_strdup_printf("%s/%s", get_irssi_dir(), credential_external_file);
		if (unlink(external_path) != 0) {
			g_warning("Warning: Failed to remove external credentials file: %s", external_path);
			/* Don't set success = FALSE - this is just a warning */
		}
		g_free(external_path);
	}

	if (!success) {
		g_warning("Migration failed - config may be in inconsistent state");
	}

	return success;
}

gboolean credential_encrypt_config(void)
{
	gboolean success = TRUE;

	if (!credential_has_master_password()) {
		g_warning("Master password not set - cannot encrypt config");
		return FALSE;
	}

	if (credential_config_encrypt) {
		g_warning("Config encryption already enabled");
		return FALSE;
	}

	/* Enable encryption */
	settings_set_bool("credential_config_encrypt", TRUE);

	/* Save config - hooks will automatically encrypt data */
	if (settings_save(NULL, FALSE) == 0) {
		g_warning("Failed to save encrypted config");
		success = FALSE;
	}

	if (!success) {
		settings_set_bool("credential_config_encrypt", FALSE);
		g_warning("Encryption failed - config may be in inconsistent state");
	}

	return success;
}

gboolean credential_decrypt_config(void)
{
	CONFIG_REC *decrypted_config;
	char *decrypted_path;
	gboolean success = TRUE;

	if (!credential_config_encrypt) {
		g_warning("Config encryption not enabled");
		return FALSE;
	}

	if (!credential_has_master_password()) {
		g_warning("Master password not set, cannot decrypt config");
		return FALSE;
	}

	/* Create copy of current config in memory */
	decrypted_config = config_open(mainconfig->fname, -1);
	if (decrypted_config == NULL) {
		g_warning("Failed to create config copy");
		return FALSE;
	}

	if (config_parse(decrypted_config) != 0) {
		g_warning("Failed to parse config copy");
		config_close(decrypted_config);
		return FALSE;
	}

	/* Decrypt data in copy */
	credential_decrypt_config_nodes(decrypted_config);

	/* Save decrypted version to config.decrypted */
	decrypted_path = g_strdup_printf("%s/config.decrypted", get_irssi_dir());
	if (config_write(decrypted_config, decrypted_path, 0600) != 0) {
		g_warning("Failed to write decrypted config to: %s", decrypted_path);
		success = FALSE;
	}

	config_close(decrypted_config);
	g_free(decrypted_path);
	return success;
}

/* === External file operations === */

void credential_unlock_config(void)
{
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG) {
		/* Config mode - decrypt data in mainconfig */
		if (mainconfig != NULL) {
			credential_decrypt_config_nodes(mainconfig);

			/* Notify components to reload data */
			signal_emit("setup reread", 0);
		}
	} else if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL ||
	          credential_config_encrypt) {
		/* External mode - load data from external file */
		if (credential_external_load()) {
			/* Notify components to reload data */
			signal_emit("setup reread", 0);
		} else {
			g_warning("Failed to load external credentials after unlocking");
		}
	}
}


static CONFIG_REC *credential_config_open(void)
{
	char *path;
	CONFIG_REC *config;
	
	if (credential_external_file == NULL) {
		return NULL;
	}
	
	path = g_strdup_printf("%s/%s", get_irssi_dir(), credential_external_file);
	config = config_open(path, 0600);
	g_free(path);
	
	return config;
}

gboolean credential_external_save(void)
{
	CONFIG_REC *config;
	CONFIG_NODE *root, *servers_node, *chatnets_node, *proxies_node;
	GSList *tmp;
	GHashTable *servers_hash, *chatnets_hash, *proxies_hash;
	GHashTableIter iter;
	char *network_name;
	CREDENTIAL_REC *rec;
	int ret;

	if (credential_external_file == NULL) {
		return FALSE;
	}

	config = credential_config_open();
	if (config == NULL) {
		return FALSE;
	}
	
	/* Clear existing content */
	config_nodes_remove_all(config);
	
	root = config->mainnode;
	servers_node = config_node_section(config, root, "servers", NODE_TYPE_LIST);
	chatnets_node = config_node_section(config, root, "chatnets", NODE_TYPE_BLOCK);
	proxies_node = config_node_section(config, root, "proxies", NODE_TYPE_LIST);
	
	/* Hash tables for grouping credentials per network/server */
	servers_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	chatnets_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	proxies_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	
	/* Group credentials by network */
	for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
		CREDENTIAL_REC *rec = tmp->data;
		GHashTable *target_hash = NULL;
		
		switch (rec->context) {
		case CREDENTIAL_CONTEXT_SERVER_PASSWORD:
			target_hash = servers_hash;
			break;
		case CREDENTIAL_CONTEXT_SASL_USERNAME:
		case CREDENTIAL_CONTEXT_SASL_PASSWORD:
		case CREDENTIAL_CONTEXT_AUTOSENDCMD:
			target_hash = chatnets_hash;
			break;
		case CREDENTIAL_CONTEXT_PROXY_PASSWORD:
			target_hash = proxies_hash;
			break;
		default:
			continue;
		}
		
		if (target_hash != NULL) {
			g_hash_table_insert(target_hash, g_strdup(rec->network), rec);
		}
	}
	
	/* Save servers in standard Irssi format */
	g_hash_table_iter_init(&iter, servers_hash);
	while (g_hash_table_iter_next(&iter, (gpointer*)&network_name, (gpointer*)&rec)) {
		CONFIG_NODE *server_node;

		server_node = config_node_section(config, servers_node, NULL, NODE_TYPE_BLOCK);
		config_node_set_str(config, server_node, "address", network_name);
		config_node_set_str(config, server_node, "chatnet", network_name);

		/* Save AS-IS - credential_set() already handled encryption */
		config_node_set_str(config, server_node, "password", rec->encrypted_value);
	}
	
	/* Save chatnets in standard Irssi format */
	g_hash_table_iter_init(&iter, chatnets_hash);
	while (g_hash_table_iter_next(&iter, (gpointer*)&network_name, (gpointer*)&rec)) {
		CONFIG_NODE *chatnet_node = config_node_section(config, chatnets_node, network_name, NODE_TYPE_BLOCK);

		/* Find all credentials for this network */
		for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
			CREDENTIAL_REC *cred = tmp->data;
			const char *field_name = NULL;

			if (strcmp(cred->network, network_name) != 0) continue;

			switch (cred->context) {
			case CREDENTIAL_CONTEXT_SASL_USERNAME:
				field_name = "sasl_username";
				break;
			case CREDENTIAL_CONTEXT_SASL_PASSWORD:
				field_name = "sasl_password";
				break;
			case CREDENTIAL_CONTEXT_AUTOSENDCMD:
				field_name = "autosendcmd";
				break;
			default:
				continue;
			}

			/* Save AS-IS - credential_set() already handled encryption */
			config_node_set_str(config, chatnet_node, field_name, cred->encrypted_value);
		}
	}
	
	/* Save proxies (similar to servers) */
	g_hash_table_iter_init(&iter, proxies_hash);
	while (g_hash_table_iter_next(&iter, (gpointer*)&network_name, (gpointer*)&rec)) {
		CONFIG_NODE *proxy_node;

		proxy_node = config_node_section(config, proxies_node, NULL, NODE_TYPE_BLOCK);
		config_node_set_str(config, proxy_node, "address", network_name);

		/* Save AS-IS - credential_set() already handled encryption */
		config_node_set_str(config, proxy_node, "password", rec->encrypted_value);
	}
	
	g_hash_table_destroy(servers_hash);
	g_hash_table_destroy(chatnets_hash);
	g_hash_table_destroy(proxies_hash);

	/* Save file */
	ret = config_write(config, NULL, 0600);
	config_close(config);

	return ret == 0;
}

gboolean credential_external_load(void)
{
	CONFIG_REC *config;
	CONFIG_NODE *root, *node;
	GSList *tmp;
	
	if (credential_external_file == NULL) {
		return FALSE;
	}
	
	config = credential_config_open();
	if (config == NULL) {
		return FALSE;
	}
	
	if (config_parse(config) != 0) {
		config_close(config);
		return FALSE;
	}
	
	/* Clear existing credentials */
	for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
		credential_free(tmp->data);
	}
	g_slist_free(credentials);
	credentials = NULL;

	root = config->mainnode;

	/* Load from servers section */
	node = config_node_find(root, "servers");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *server = tmp->data;
			char *chatnet, *password;
			CREDENTIAL_REC *rec;
			char *decrypted_value = NULL;
			
			if (server->type != NODE_TYPE_BLOCK) continue;
			
			chatnet = config_node_get_str(server, "chatnet", NULL);
			password = config_node_get_str(server, "password", NULL);
			
			if (chatnet == NULL || password == NULL) continue;
			
			/* Decrypt if needed */
			if (credential_config_encrypt &&
			    master_password != NULL) {
				decrypted_value = credential_decrypt(password, master_password);
				if (decrypted_value == NULL) {
					g_warning("Failed to decrypt server password for %s", chatnet);
					continue;
				}
				password = decrypted_value;
			}
			
			rec = g_new0(CREDENTIAL_REC, 1);
			rec->network = g_strdup(chatnet);
			rec->context = CREDENTIAL_CONTEXT_SERVER_PASSWORD;
			rec->encrypted_value = g_strdup(password);
			rec->salt = NULL;
			
			credentials = g_slist_append(credentials, rec);
			g_free(decrypted_value);
		}
	}
	
	/* Load from chatnets section */
	node = config_node_find(root, "chatnets");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *chatnet = tmp->data;
			char *network_name;
			char *sasl_username, *sasl_password, *autosendcmd;
			
			if (chatnet->type != NODE_TYPE_BLOCK) continue;
			
			network_name = chatnet->key;
			if (network_name == NULL) continue;
			
			sasl_username = config_node_get_str(chatnet, "sasl_username", NULL);
			sasl_password = config_node_get_str(chatnet, "sasl_password", NULL);
			autosendcmd = config_node_get_str(chatnet, "autosendcmd", NULL);
			
			/* Load sasl_username */
			if (sasl_username != NULL) {
				CREDENTIAL_REC *rec;
				char *decrypted_value = NULL;

				if (credential_config_encrypt &&
				    master_password != NULL) {
					decrypted_value = credential_decrypt(sasl_username, master_password);
					if (decrypted_value == NULL) {
						g_warning("Failed to decrypt sasl_username for %s", network_name);
					} else {
						sasl_username = decrypted_value;
					}
				}

				if (decrypted_value != NULL || !credential_config_encrypt) {
					rec = g_new0(CREDENTIAL_REC, 1);
					rec->network = g_strdup(network_name);
					rec->context = CREDENTIAL_CONTEXT_SASL_USERNAME;
					rec->encrypted_value = g_strdup(sasl_username);
					rec->salt = NULL;
					credentials = g_slist_append(credentials, rec);
				}

				g_free(decrypted_value);
			}
			
			/* Load sasl_password */
			if (sasl_password != NULL) {
				CREDENTIAL_REC *rec;
				char *decrypted_value = NULL;

				if (credential_config_encrypt &&
				    master_password != NULL) {
					decrypted_value = credential_decrypt(sasl_password, master_password);
					if (decrypted_value == NULL) {
						g_warning("Failed to decrypt sasl_password for %s", network_name);
					} else {
						sasl_password = decrypted_value;
					}
				}

				if (decrypted_value != NULL || !credential_config_encrypt) {
					rec = g_new0(CREDENTIAL_REC, 1);
					rec->network = g_strdup(network_name);
					rec->context = CREDENTIAL_CONTEXT_SASL_PASSWORD;
					rec->encrypted_value = g_strdup(sasl_password);
					rec->salt = NULL;
					credentials = g_slist_append(credentials, rec);
				}

				g_free(decrypted_value);
			}
			
			/* Load autosendcmd if it contains credentials */
			if (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd)) {
				CREDENTIAL_REC *rec;
				char *decrypted_value = NULL;
				
				if (credential_config_encrypt &&
				    master_password != NULL) {
					decrypted_value = credential_decrypt(autosendcmd, master_password);
					if (decrypted_value == NULL) {
						g_warning("Failed to decrypt autosendcmd for %s", network_name);
					} else {
						autosendcmd = decrypted_value;
					}
				}
				
				if (decrypted_value != NULL || !credential_config_encrypt) {
					rec = g_new0(CREDENTIAL_REC, 1);
					rec->network = g_strdup(network_name);
					rec->context = CREDENTIAL_CONTEXT_AUTOSENDCMD;
					rec->encrypted_value = g_strdup(autosendcmd);
					rec->salt = NULL;
					credentials = g_slist_append(credentials, rec);
				}
				
				g_free(decrypted_value);
			}
		}
	}
	
	/* Load from proxies section */
	node = config_node_find(root, "proxies");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *proxy = tmp->data;
			char *address, *password;
			CREDENTIAL_REC *rec;
			char *decrypted_value = NULL;
			
			if (proxy->type != NODE_TYPE_BLOCK) continue;
			
			address = config_node_get_str(proxy, "address", NULL);
			password = config_node_get_str(proxy, "password", NULL);
			
			if (address == NULL || password == NULL) continue;
			
			/* Decrypt if needed */
			if (credential_config_encrypt &&
			    master_password != NULL) {
				decrypted_value = credential_decrypt(password, master_password);
				if (decrypted_value == NULL) {
					g_warning("Failed to decrypt proxy password for %s", address);
					continue;
				}
				password = decrypted_value;
			}
			
			rec = g_new0(CREDENTIAL_REC, 1);
			rec->network = g_strdup(address);
			rec->context = CREDENTIAL_CONTEXT_PROXY_PASSWORD;
			rec->encrypted_value = g_strdup(password);
			rec->salt = NULL;
			
			credentials = g_slist_append(credentials, rec);
			g_free(decrypted_value);
		}
	}
	
	config_close(config);
	return TRUE;
}

gboolean credential_external_reload(void)
{
	return credential_external_load();
}

/* === Configuration hooks === */

static void credential_encrypt_config_nodes(CONFIG_REC *config)
{
	CONFIG_NODE *root, *node;
	GSList *tmp;
	
	if (!credential_config_encrypt || master_password == NULL) {
		return;
	}
	
	root = config->mainnode;
	
	/* Encrypt fields in servers section */
	node = config_node_find(root, "servers");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *server = tmp->data;
			char *password, *encrypted;
			
			if (server->type != NODE_TYPE_BLOCK) continue;
			
			password = config_node_get_str(server, "password", NULL);
			if (password != NULL && !strchr(password, ':')) {
				encrypted = credential_encrypt(password, master_password);
				if (encrypted != NULL) {
					config_node_set_str(config, server, "password", encrypted);
					g_free(encrypted);
				}
			}
		}
	}
	
	/* Encrypt fields in chatnets section */
	node = config_node_find(root, "chatnets");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *chatnet = tmp->data;
			char *sasl_password, *sasl_username, *autosendcmd;
			char *encrypted;
			
			if (chatnet->type != NODE_TYPE_BLOCK) continue;
			
			/* Encrypt sasl_password */
			sasl_password = config_node_get_str(chatnet, "sasl_password", NULL);
			if (sasl_password != NULL && !strchr(sasl_password, ':')) {
				encrypted = credential_encrypt(sasl_password, master_password);
				if (encrypted != NULL) {
					config_node_set_str(config, chatnet, "sasl_password", encrypted);
					g_free(encrypted);
				}
			}
			
			/* Encrypt sasl_username */
			sasl_username = config_node_get_str(chatnet, "sasl_username", NULL);
			if (sasl_username != NULL && !strchr(sasl_username, ':')) {
				encrypted = credential_encrypt(sasl_username, master_password);
				if (encrypted != NULL) {
					config_node_set_str(config, chatnet, "sasl_username", encrypted);
					g_free(encrypted);
				}
			}
			
			/* Encrypt autosendcmd if it contains credentials */
			autosendcmd = config_node_get_str(chatnet, "autosendcmd", NULL);
			if (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd) &&
			    !strchr(autosendcmd, ':')) {
				encrypted = credential_encrypt(autosendcmd, master_password);
				if (encrypted != NULL) {
					config_node_set_str(config, chatnet, "autosendcmd", encrypted);
					g_free(encrypted);
				}
			}
		}
	}
}

static void credential_decrypt_config_nodes(CONFIG_REC *config)
{
	CONFIG_NODE *root, *node;
	GSList *tmp;

	if (!credential_config_encrypt || master_password == NULL) {
		return;
	}
	
	root = config->mainnode;
	
	/* Decrypt fields in servers section */
	node = config_node_find(root, "servers");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *server = tmp->data;
			char *password, *decrypted;
			
			if (server->type != NODE_TYPE_BLOCK) continue;
			
			password = config_node_get_str(server, "password", NULL);
			if (password != NULL && strchr(password, ':')) {
				decrypted = credential_decrypt(password, master_password);
				if (decrypted != NULL) {
					config_node_set_str(config, server, "password", decrypted);
					g_free(decrypted);
				}
			}
		}
	}
	
	/* Decrypt fields in chatnets section */
	node = config_node_find(root, "chatnets");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *chatnet = tmp->data;
			char *sasl_password, *sasl_username, *autosendcmd;
			char *decrypted;
			
			if (chatnet->type != NODE_TYPE_BLOCK) continue;
			
			/* Decrypt sasl_password */
			sasl_password = config_node_get_str(chatnet, "sasl_password", NULL);
			if (sasl_password != NULL && strchr(sasl_password, ':')) {
				decrypted = credential_decrypt(sasl_password, master_password);
				if (decrypted != NULL) {
					config_node_set_str(config, chatnet, "sasl_password", decrypted);
					g_free(decrypted);
				}
			}
			
			/* Decrypt sasl_username */
			sasl_username = config_node_get_str(chatnet, "sasl_username", NULL);
			if (sasl_username != NULL && strchr(sasl_username, ':')) {
				decrypted = credential_decrypt(sasl_username, master_password);
				if (decrypted != NULL) {
					config_node_set_str(config, chatnet, "sasl_username", decrypted);
					g_free(decrypted);
				}
			}
			
			/* Decrypt autosendcmd */
			autosendcmd = config_node_get_str(chatnet, "autosendcmd", NULL);
			if (autosendcmd != NULL && strchr(autosendcmd, ':')) {
				decrypted = credential_decrypt(autosendcmd, master_password);
				if (decrypted != NULL) {
					config_node_set_str(config, chatnet, "autosendcmd", decrypted);
					g_free(decrypted);
				}
			}
		}
	}
}

static void credential_remove_config_nodes(CONFIG_REC *config)
{
	CONFIG_NODE *root, *node;
	GSList *tmp;

	root = config->mainnode;

	/* Remove credential fields from servers section */
	node = config_node_find(root, "servers");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *server = tmp->data;

			if (server->type != NODE_TYPE_BLOCK) continue;

			/* Remove password */
			if (config_node_get_str(server, "password", NULL) != NULL) {
				config_node_set_str(config, server, "password", NULL);
			}
		}
	}

	/* Remove credential fields from chatnets section */
	node = config_node_find(root, "chatnets");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *chatnet = tmp->data;
			char *autosendcmd;

			if (chatnet->type != NODE_TYPE_BLOCK) continue;

			/* Remove sasl_username and sasl_password */
			if (config_node_get_str(chatnet, "sasl_username", NULL) != NULL) {
				config_node_set_str(config, chatnet, "sasl_username", NULL);
			}
			if (config_node_get_str(chatnet, "sasl_password", NULL) != NULL) {
				config_node_set_str(config, chatnet, "sasl_password", NULL);
			}

			/* Remove autosendcmd if it contains credentials */
			autosendcmd = config_node_get_str(chatnet, "autosendcmd", NULL);
			if (autosendcmd != NULL && credential_is_autosendcmd_sensitive(autosendcmd)) {
				config_node_set_str(config, chatnet, "autosendcmd", NULL);
			}
		}
	}

	/* Remove credential fields from proxies section */
	node = config_node_find(root, "proxies");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *proxy = tmp->data;

			if (proxy->type != NODE_TYPE_BLOCK) continue;

			/* Remove password */
			if (config_node_get_str(proxy, "password", NULL) != NULL) {
				config_node_set_str(config, proxy, "password", NULL);
			}
		}
	}
}

static void sig_chatnet_saved_credential_capture(void *chatnet_rec, void *node)
{
	CHATNET_REC *rec;
	CONFIG_NODE *config_node;
	char *network_name;
	char *sasl_username, *sasl_password, *autosendcmd;

	rec = (CHATNET_REC*)chatnet_rec;
	config_node = (CONFIG_NODE*)node;

	if (rec == NULL || config_node == NULL) {
		return;
	}

	network_name = rec->name;
	if (network_name == NULL) {
		return;
	}

	/* Skip if not using external storage */
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG) {
		return;
	}

	/* Check if there is new data in config node */
	sasl_username = config_node_get_str(config_node, "sasl_username", NULL);
	sasl_password = config_node_get_str(config_node, "sasl_password", NULL);
	autosendcmd = config_node_get_str(config_node, "autosendcmd", NULL);

	/* Move data to credential storage and remove from config */
	if (sasl_username != NULL && *sasl_username != '\0') {
		credential_set(network_name, CREDENTIAL_CONTEXT_SASL_USERNAME, sasl_username);
		iconfig_node_set_str(config_node, "sasl_username", NULL);
	}

	if (sasl_password != NULL && *sasl_password != '\0') {
		credential_set(network_name, CREDENTIAL_CONTEXT_SASL_PASSWORD, sasl_password);
		iconfig_node_set_str(config_node, "sasl_password", NULL);
	}

	if (autosendcmd != NULL && *autosendcmd != '\0' && 
	    credential_is_autosendcmd_sensitive(autosendcmd)) {
		credential_set(network_name, CREDENTIAL_CONTEXT_AUTOSENDCMD, autosendcmd);
		iconfig_node_set_str(config_node, "autosendcmd", NULL);
	}

	/* Save changes to external storage */
	if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL ||
	    credential_config_encrypt) {
		credential_external_save();
	}
}

static void sig_server_setup_saved_credential_capture(void *server_rec, void *node)
{
	SERVER_SETUP_REC *rec;
	CONFIG_NODE *config_node;
	char *address, *password;

	rec = (SERVER_SETUP_REC*)server_rec;
	config_node = (CONFIG_NODE*)node;

	if (rec == NULL || config_node == NULL) {
		return;
	}

	/* Skip if not using external storage */
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG) {
		return;
	}

	/* Check if there is server password in config node */
	address = rec->address;
	password = config_node_get_str(config_node, "password", NULL);

	/* Move server password to credential storage and remove from config */
	if (address != NULL && password != NULL && *password != '\0') {
		credential_set(address, CREDENTIAL_CONTEXT_SERVER_PASSWORD, password);
		iconfig_node_set_str(config_node, "password", NULL);
		
		/* Save changes to external storage */
		if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL ||
		    credential_config_encrypt) {
			credential_external_save();
		}
	}
}




void credential_config_write_hook(CONFIG_REC *config)
{
	/* Encrypt data before save if config encryption is enabled */
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG &&
	    credential_config_encrypt) {
		credential_encrypt_config_nodes(config);
	}

	/* In external mode remove credentials from main config before save */
	if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL) {
		credential_remove_config_nodes(config);
	}
}

void credential_config_read_hook(CONFIG_REC *config)
{
	/* Decrypt data after loading or restore original after save */
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG &&
	    credential_config_encrypt) {
		credential_decrypt_config_nodes(config);
	}
}

/* === Initialization and deinitialization === */

static gboolean show_startup_encryption_warning(gpointer data)
{
	if (credential_config_encrypt && !credential_has_master_password()) {
		signal_emit("gui dialog", 2, "Credentials Locked",
			"Configuration encryption is ON, but your credentials are\n"
			"currently LOCKED. Use /credential passwd <password> to unlock them.");
	}
	return G_SOURCE_REMOVE; /* Execute only once */
}

void credential_init(void)
{
	/* Initialize cryptography */
	if (!credential_crypto_init()) {
		g_warning("Failed to initialize credential cryptography");
		return;
	}

	/* Add configuration settings */
	settings_add_str("misc", "credential_storage_mode", "config");
	settings_add_str("misc", "credential_external_file", ".credentials");
	settings_add_bool("misc", "credential_config_encrypt", FALSE);

	/* Initialize global variables */
	/* IMPORTANT: Initialize credential_external_file BEFORE storage_mode to avoid NULL errors during auto-migration */
	credential_external_file_changed();
	credential_storage_mode_changed();
	credential_config_encrypt_changed();

	/* Settings change handlers */
	signal_add("setup changed", (SIGNAL_FUNC) credential_storage_mode_changed);
	signal_add("setup changed", (SIGNAL_FUNC) credential_external_file_changed);
	signal_add("setup changed", (SIGNAL_FUNC) credential_config_encrypt_changed);

	/* Hook to fill credentials from credential storage */
	signal_add("chatnet read", (SIGNAL_FUNC) sig_chatnet_read_credential_fill);

	/* Hook to capture saves in external mode */
	signal_add("chatnet saved", (SIGNAL_FUNC) sig_chatnet_saved_credential_capture);
	signal_add("server setup saved", (SIGNAL_FUNC) sig_server_setup_saved_credential_capture);

	/* Load data from external storage if external mode is active */
	if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL ||
	    credential_config_encrypt) {
		credential_external_load();
	}

	if (credential_config_encrypt) {
		/* Display message with small delay so it's visible */
		g_timeout_add(500, (GSourceFunc)show_startup_encryption_warning, NULL);
	}
}

void credential_deinit(void)
{
	GSList *tmp;

	/* Remove signals */
	signal_remove("setup changed", (SIGNAL_FUNC) credential_storage_mode_changed);
	signal_remove("setup changed", (SIGNAL_FUNC) credential_external_file_changed);
	signal_remove("setup changed", (SIGNAL_FUNC) credential_config_encrypt_changed);
	signal_remove("chatnet read", (SIGNAL_FUNC) sig_chatnet_read_credential_fill);
	signal_remove("chatnet saved", (SIGNAL_FUNC) sig_chatnet_saved_credential_capture);
	signal_remove("server setup saved", (SIGNAL_FUNC) sig_server_setup_saved_credential_capture);

	/* Clear master password */
	credential_clear_master_password();

	/* Clear credentials list */
	for (tmp = credentials; tmp != NULL; tmp = tmp->next) {
		credential_free(tmp->data);
	}
	g_slist_free(credentials);
	credentials = NULL;

	/* Clear global variables */
	g_free(credential_external_file);
	credential_external_file = NULL;

	/* Close external configuration */
	if (external_config != NULL) {
		config_close(external_config);
		external_config = NULL;
	}

	/* Deinitialize cryptography */
	credential_crypto_deinit();
}

/* === Signal handlers === */

static void sig_chatnet_read_credential_fill(void *chatnet_rec, void *node)
{
	CHATNET_REC *rec;
	CONFIG_NODE *config_node;
	char *network_name;
	char *sasl_username, *sasl_password;
	char *credential_value;
	gboolean changed = FALSE;
	static gboolean in_reemit = FALSE; /* Protection against loop */

	/* Prevent loop during re-emitting */
	if (in_reemit) {
		return;
	}

	rec = (CHATNET_REC*)chatnet_rec;
	config_node = (CONFIG_NODE*)node;

	if (rec == NULL || config_node == NULL) {
		return;
	}

	network_name = rec->name;
	if (network_name == NULL) {
		return;
	}

	/* Skip if not using external storage */
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG) {
		return;
	}

	/* Check current values in config node */
	sasl_username = config_node_get_str(config_node, "sasl_username", NULL);
	sasl_password = config_node_get_str(config_node, "sasl_password", NULL);

	/* Fill or replace sasl_username - check if empty */
	if (sasl_username == NULL || *sasl_username == '\0') {
		credential_value = credential_get(network_name, CREDENTIAL_CONTEXT_SASL_USERNAME);
		if (credential_value != NULL) {
			iconfig_node_set_str(config_node, "sasl_username", credential_value);
			g_free(credential_value);
			changed = TRUE;
		}
	}

	/* Fill or replace sasl_password - check if empty */
	if (sasl_password == NULL || *sasl_password == '\0') {
		credential_value = credential_get(network_name, CREDENTIAL_CONTEXT_SASL_PASSWORD);
		if (credential_value != NULL) {
			iconfig_node_set_str(config_node, "sasl_password", credential_value);
			g_free(credential_value);
			changed = TRUE;
		}
	}

	/* If something changed, re-emit "chatnet read" signal
	 * so the IRC module loads data from config node into its structure */
	if (changed) {
		in_reemit = TRUE;
		signal_emit("chatnet read", 2, rec, config_node);
		in_reemit = FALSE;
	}
}