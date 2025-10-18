/*
 fe-credential.c : Frontend commands for credential management

    Copyright (C) 2024 Irssi Project

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
#include <irssi/src/fe-common/core/fe-credential.h>
#include <irssi/src/fe-common/core/module-formats.h>
#include <irssi/src/core/credential.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/command-history.h>
#include <irssi/src/fe-common/core/fe-windows.h>

#include <string.h>

/* === Helper functions === */

static void credential_clear_command_from_history(void)
{
	WINDOW_REC *window;
	HISTORY_REC *history;
	GList *last;
	HISTORY_ENTRY_REC *entry;
	const char *text;

	/* Get active window */
	window = active_win;
	if (window == NULL)
		return;

	/* Get command history for this window */
	history = command_history_current(window);
	if (history == NULL)
		return;

	/* Get last entry */
	last = command_history_list_last(history);
	if (last == NULL)
		return;

	entry = (HISTORY_ENTRY_REC *)last->data;
	text = entry->text;

	/* Check if last command was /credential passwd */
	if (text != NULL && (g_str_has_prefix(text, "/credential passwd ") ||
	                     g_str_has_prefix(text, "/CREDENTIAL PASSWD "))) {
		/* Replace password with asterisks */
		/* Free old text and allocate masked version */
		g_free((char *)entry->text);
		entry->text = g_strdup("/credential passwd *****");
	}
}

/* credential_parse_context() removed - was only used by deleted /credential set/get/remove commands */

/* === User commands === */

/* SYNTAX: CREDENTIAL */
void cmd_credential(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	if (data == NULL || *data == '\0') {
		credential_show_help();
		return;
	}

	/* Use irssi's standard subcommand mechanism */
	command_runsub("credential", data, server, item);
}

/* SYNTAX: CREDENTIAL PASSWD <password> */
void cmd_credential_passwd(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	g_return_if_fail(data != NULL);
	
	if (*data == '\0') {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Usage: /CREDENTIAL PASSWD <password>");
		return;
	}
	
	if (credential_set_master_password(data)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		         "Master password set. Unlocking credentials...");

		/* Unlock credentials in memory */
		credential_unlock_config();

		/* Immediate removal from command history */
		credential_clear_command_from_history();
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Failed to set master password");
	}
}

/* Removed - use standard Irssi commands:
 * /network add -sasl_username ... -sasl_password ...
 * /server add -password ...
 */

/* SYNTAX: CREDENTIAL LIST */
void cmd_credential_list(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	CONFIG_REC *config = NULL;
	CONFIG_NODE *root, *node;
	GSList *tmp;
	int count = 0;

	/* Check if password is needed */
	if (credential_config_encrypt && !credential_has_master_password()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Encryption is enabled but master password not set.");
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Use /CREDENTIAL PASSWD <password> to unlock credentials.");
		return;
	}

	/* Open appropriate file */
	if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL) {
		char *path = g_strdup_printf("%s/%s", get_irssi_dir(),
		                             credential_external_file ? credential_external_file : ".credentials");
		config = config_open(path, -1);
		g_free(path);

		if (config == NULL || config_parse(config) != 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
			         "No credentials file found or file is empty");
			if (config) config_close(config);
			return;
		}
	} else {
		/* Config mode - use mainconfig */
		config = mainconfig;
		if (config == NULL) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			         "Main config not loaded");
			return;
		}
	}

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Stored credentials:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Network                Context              Value");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "----------------------------------------------------");

	root = config->mainnode;

	/* Display chatnets (sasl_username, sasl_password, autosendcmd) */
	node = config_node_find(root, "chatnets");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *chatnet = tmp->data;
			char *network_name = chatnet->key;
			char *sasl_username, *sasl_password, *autosendcmd;

			if (network_name == NULL) continue;

			sasl_username = config_node_get_str(chatnet, "sasl_username", NULL);
			sasl_password = config_node_get_str(chatnet, "sasl_password", NULL);
			autosendcmd = config_node_get_str(chatnet, "autosendcmd", NULL);

			/* SASL Username */
			if (sasl_username != NULL && *sasl_username != '\0') {
				char *display_value;
				char *decrypted;
				char *line;

				display_value = sasl_username;

				/* If encrypted and we have master password, decrypt */
				if (strchr(sasl_username, ':') != NULL && credential_has_master_password()) {
					decrypted = credential_get(network_name, CREDENTIAL_CONTEXT_SASL_USERNAME);
					if (decrypted != NULL) {
						display_value = decrypted;
					}
				}

				line = g_strdup_printf("%-22s %-20s %s", network_name, "sasl_username", display_value);
				printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE, "%s", line);
				g_free(line);

				if (display_value != sasl_username) {
					g_free(display_value);
				}
				count++;
			}

			/* SASL Password */
			if (sasl_password != NULL && *sasl_password != '\0') {
				char *line = g_strdup_printf("%-22s %-20s %s", network_name, "sasl_password", "***");
				printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE, "%s", line);
				g_free(line);
				count++;
			}

			/* Autosendcmd */
			if (autosendcmd != NULL && *autosendcmd != '\0') {
				char *line = g_strdup_printf("%-22s %-20s %s", network_name, "autosendcmd", "***");
				printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE, "%s", line);
				g_free(line);
				count++;
			}
		}
	}

	/* Display servers (password) */
	node = config_node_find(root, "servers");
	if (node != NULL && node->value != NULL) {
		for (tmp = config_node_first(node->value); tmp != NULL; tmp = config_node_next(tmp)) {
			CONFIG_NODE *srv = tmp->data;
			char *chatnet, *password;

			if (srv->type != NODE_TYPE_BLOCK) continue;

			chatnet = config_node_get_str(srv, "chatnet", NULL);
			password = config_node_get_str(srv, "password", NULL);

			if (password != NULL && *password != '\0') {
				char *line = g_strdup_printf("%-22s %-20s %s",
				                             chatnet ? chatnet : "(no chatnet)",
				                             "server_password", "***");
				printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE, "%s", line);
				g_free(line);
				count++;
			}
		}
	}

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "----------------------------------------------------");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Total: %d credentials", count);

	/* Close config if it was external */
	if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL && config != mainconfig) {
		config_close(config);
	}
}

/* Removed - use standard Irssi commands:
 * /network remove <network>
 * /server remove <address>
 */

/* === Migration functions === */

/* SYNTAX: CREDENTIAL MIGRATE EXTERNAL|CONFIG */
void cmd_credential_migrate(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	char *target;

	if (data == NULL || *data == '\0') {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Usage: /CREDENTIAL MIGRATE <external|config>");
		return;
	}

	/* Parse target */
	while (*data == ' ') data++;
	target = g_strdup(data);

	/* Remove trailing whitespace */
	g_strchomp(target);

	if (g_ascii_strcasecmp(target, "external") == 0) {
		cmd_credential_migrate_to_external(data, server, item);
	} else if (g_ascii_strcasecmp(target, "config") == 0) {
		cmd_credential_migrate_to_config(data, server, item);
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Unknown migrate target: %s (use 'external' or 'config')", target);
	}

	g_free(target);
}

void cmd_credential_migrate_to_external(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	/* Check current storage mode */
	if (credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		         "Already using external storage mode");
		return;
	}

	/* Change mode - /SET will automatically trigger migration via signal handler */
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Migrating credentials to external file...");
	signal_emit("command set", 3, "credential_storage_mode external", server, item);
}

void cmd_credential_migrate_to_config(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	/* Check current storage mode */
	if (credential_storage_mode == CREDENTIAL_STORAGE_CONFIG) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		         "Already using config storage mode");
		return;
	}

	/* Change mode - /SET will automatically trigger migration via signal handler */
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Migrating credentials to config file...");
	signal_emit("command set", 3, "credential_storage_mode config", server, item);
}

void cmd_credential_encrypt(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	/* 1. Check if master password is set */
	if (!credential_has_master_password()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Master password not set. Use /CREDENTIAL PASSWD <password> first.");
		return;
	}

	/* 2. Check if encryption is already enabled */
	if (credential_config_encrypt) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		         "Encryption is already enabled. Credentials are encrypted.");
		return;
	}

	/* 3. Enable encryption - automatic conversion will happen via signal handler */
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Encrypting credentials in %s...",
	         credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL ? "external file" : "config file");
	signal_emit("command set", 3, "credential_config_encrypt ON", server, item);
}

void cmd_credential_decrypt(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	/* 1. Check if master password is set (needed for verification) */
	if (!credential_has_master_password()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Master password not set. Use /CREDENTIAL PASSWD <password> first.");
		return;
	}

	/* 2. Check if encryption is disabled */
	if (!credential_config_encrypt) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		         "Encryption is already disabled. Credentials are in plaintext.");
		return;
	}

	/* 3. Disable encryption - automatic conversion will happen via signal handler */
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Decrypting credentials in %s...",
	         credential_storage_mode == CREDENTIAL_STORAGE_EXTERNAL ? "external file" : "config file");
	signal_emit("command set", 3, "credential_config_encrypt OFF", server, item);
}

void cmd_credential_reload(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	if (credential_external_reload()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		         "Successfully reloaded external credentials");
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		         "Failed to reload external credentials");
	}
}

/* === Helper functions === */

void credential_show_help(void)
{
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "CREDENTIAL commands:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Basic usage:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL PASSWD <password>           - Set master password");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL LIST                        - List all credentials");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL STATUS                      - Show current status");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "To add credentials, use standard Irssi commands:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /NETWORK ADD -sasl_username <user> -sasl_password <pass> <name>");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /SERVER ADD -password <pass> [-net <network>] <address>");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Migration & management:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL MIGRATE EXTERNAL            - Move credentials to external file");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL MIGRATE CONFIG              - Move credentials to config file");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL ENCRYPT                     - Encrypt credentials in current storage");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL DECRYPT                     - Decrypt credentials in current storage");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /CREDENTIAL RELOAD                      - Reload external file");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Settings:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /SET credential_storage_mode <config|external>");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /SET credential_config_encrypt <on|off>");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  /SET credential_external_file <filename>");
}

void credential_show_status(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	const char *storage_mode = credential_storage_mode_to_string(credential_storage_mode);
	
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "Credential Management Status:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  Storage mode: %s", storage_mode);
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  External file: %s", credential_external_file ? credential_external_file : "(none)");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  Config encryption: %s", credential_config_encrypt ? "ON" : "OFF");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	         "  Master password: %s", credential_has_master_password() ? "SET" : "NOT SET");
}

/* === Initialization and deinitialization === */

void fe_credential_init(void)
{
	/* Register main command */
	command_bind("credential", NULL, (SIGNAL_FUNC) cmd_credential);

	/* Register subcommands */
	command_bind("credential passwd", NULL, (SIGNAL_FUNC) cmd_credential_passwd);
	command_bind("credential list", NULL, (SIGNAL_FUNC) cmd_credential_list);
	command_bind("credential status", NULL, (SIGNAL_FUNC) credential_show_status);
	command_bind("credential migrate", NULL, (SIGNAL_FUNC) cmd_credential_migrate);
	command_bind("credential encrypt", NULL, (SIGNAL_FUNC) cmd_credential_encrypt);
	command_bind("credential decrypt", NULL, (SIGNAL_FUNC) cmd_credential_decrypt);
	command_bind("credential reload", NULL, (SIGNAL_FUNC) cmd_credential_reload);

	/* Command options */
	command_set_options("credential", "");
}

void fe_credential_deinit(void)
{
	/* Remove main command */
	command_unbind("credential", (SIGNAL_FUNC) cmd_credential);

	/* Remove subcommands */
	command_unbind("credential passwd", (SIGNAL_FUNC) cmd_credential_passwd);
	command_unbind("credential list", (SIGNAL_FUNC) cmd_credential_list);
	command_unbind("credential status", (SIGNAL_FUNC) credential_show_status);
	command_unbind("credential migrate", (SIGNAL_FUNC) cmd_credential_migrate);
	command_unbind("credential encrypt", (SIGNAL_FUNC) cmd_credential_encrypt);
	command_unbind("credential decrypt", (SIGNAL_FUNC) cmd_credential_decrypt);
	command_unbind("credential reload", (SIGNAL_FUNC) cmd_credential_reload);
}