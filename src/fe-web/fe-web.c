/*
 fe-web.c : WebSocket-based web frontend for irssi

    Copyright (C) 2025

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
#include "fe-web.h"
#include "fe-web-ssl.h"
#include "fe-web-crypto.h"

#include <irssi/src/core/modules.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/fe-common/core/printtext.h>

#include <string.h>

/* Global clients list */
GSList *web_clients = NULL;

/* fe-web settings last applied. "setup changed" is emitted on every /SET,
 * so only a real change of these restarts the server - an unrelated /SET
 * must not drop web clients or print notices. */
static gboolean applied_enabled = FALSE;
static int applied_port = -1;
static char *applied_bind = NULL;
static char *applied_socket = NULL;
static char *applied_password = NULL;

static gboolean fe_web_settings_differ(void)
{
	return settings_get_bool("fe_web_enabled") != applied_enabled ||
	       settings_get_int("fe_web_port") != applied_port ||
	       g_strcmp0(settings_get_str("fe_web_bind"), applied_bind) != 0 ||
	       g_strcmp0(settings_get_str("fe_web_socket"), applied_socket) != 0 ||
	       g_strcmp0(settings_get_str("fe_web_password"), applied_password) != 0;
}

static void fe_web_settings_forget(void)
{
	g_free(applied_bind);
	applied_bind = NULL;
	g_free(applied_socket);
	applied_socket = NULL;
	if (applied_password != NULL)
		memset(applied_password, 0, strlen(applied_password));
	g_free(applied_password);
	applied_password = NULL;
}

/* Start, stop or restart the server for the current settings. The server
 * prints its own started/stopped notices and errors (e.g. no password). */
static void fe_web_apply_settings(void)
{
	if (applied_enabled)
		fe_web_server_deinit();

	fe_web_settings_forget();
	applied_enabled = settings_get_bool("fe_web_enabled");
	applied_port = settings_get_int("fe_web_port");
	applied_bind = g_strdup(settings_get_str("fe_web_bind"));
	applied_socket = g_strdup(settings_get_str("fe_web_socket"));
	applied_password = g_strdup(settings_get_str("fe_web_password"));

	if (applied_enabled) {
		/* key derived from the (possibly new) password */
		fe_web_crypto_deinit();
		fe_web_crypto_init();
		fe_web_server_init();
	}
}

static guint apply_tag = 0;

static gboolean fe_web_apply_idle(gpointer data)
{
	apply_tag = 0;
	if (fe_web_settings_differ())
		fe_web_apply_settings();
	return FALSE;
}

/* Restarting the server drops every web client, also the one whose /SET
 * is still running - so the restart waits until that command is done. */
static void fe_web_setup_changed(void)
{
	if (apply_tag == 0 && fe_web_settings_differ())
		apply_tag = g_idle_add(fe_web_apply_idle, NULL);
}

/* SYNTAX: FE_WEB STATUS */
static void cmd_fe_web_status(const char *data, IRC_SERVER_REC *server)
{
	GSList *tmp;
	int count;

	if (!settings_get_bool("fe_web_enabled")) {
		printtext(server, NULL, MSGLEVEL_CLIENTNOTICE,
		          "fe-web is currently disabled");
		return;
	}

	count = g_slist_length(web_clients);
	printtext(server, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: Currently connected clients: %d", count);
	printtext(server, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: Security: SSL/TLS (wss://) + AES-256-GCM encryption (always enabled)");

	for (tmp = web_clients; tmp != NULL; tmp = tmp->next) {
		WEB_CLIENT_REC *client = tmp->data;
		const char *server_tag;

		if (client->server != NULL) {
			server_tag = client->server->tag;
		} else if (client->wants_all_servers) {
			server_tag = "*";
		} else {
			server_tag = "(none)";
		}

		printtext(server, NULL, MSGLEVEL_CLIENTNOTICE,
		          "  %s connect%s from %s (server: %s, sent: %lu, recv: %lu)",
		          client->id,
		          client->authenticated ? "ed" : "ing",
		          client->addr,
		          server_tag,
		          client->messages_sent,
		          client->messages_received);
	}
}

/* SYNTAX: FE_WEB */
static void cmd_fe_web(const char *data, IRC_SERVER_REC *server, void *item)
{
	if (*data == '\0') {
		cmd_fe_web_status(data, server);
		return;
	}

	command_runsub("fe_web", data, server, item);
}

void fe_web_init(void)
{
	/* Register settings */
	settings_add_bool("lookandfeel", "fe_web_enabled", FALSE);
	settings_add_int("lookandfeel", "fe_web_port", 9001);
	settings_add_str("lookandfeel", "fe_web_bind", "127.0.0.1");
	/* a Unix socket path; when set it is used instead of fe_web_bind/port */
	settings_add_str("lookandfeel", "fe_web_socket", "");
	settings_add_str("lookandfeel", "fe_web_password", "");

	/* Register commands */
	command_bind("fe_web", NULL, (SIGNAL_FUNC) cmd_fe_web);
	command_bind("fe_web status", NULL, (SIGNAL_FUNC) cmd_fe_web_status);

	/* Initialize subsystems */
	fe_web_signals_init();

	/* SSL and encryption are ALWAYS enabled - no option to disable; the
	 * TLS key and the encryption key are made when the server starts
	 * (only if fe_web_enabled) */
	fe_web_ssl_init();

	/* Watch for settings changes */
	signal_add_first("setup changed", (SIGNAL_FUNC) fe_web_setup_changed);

	/* Start server if enabled */
	if (settings_get_bool("fe_web_enabled"))
		fe_web_apply_settings();

	/* Register module */
	module_register("web", "fe");
}

void fe_web_deinit(void)
{
	/* Cleanup */
	signal_remove("setup changed", (SIGNAL_FUNC) fe_web_setup_changed);
	if (apply_tag != 0) {
		g_source_remove(apply_tag);
		apply_tag = 0;
	}
	command_unbind("fe_web", (SIGNAL_FUNC) cmd_fe_web);
	command_unbind("fe_web status", (SIGNAL_FUNC) cmd_fe_web_status);

	if (applied_enabled)
		fe_web_server_deinit();
	fe_web_settings_forget();
	fe_web_signals_deinit();
	fe_web_ssl_deinit();
	fe_web_crypto_deinit();
}

MODULE_ABICHECK(fe_web)
