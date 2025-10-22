/*
 fe-web-client.c : Client connection handling for fe-web

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "fe-web.h"

#include <irssi/src/core/signals.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-activity.h>
#include <irssi/src/fe-text/sidepanels-activity.h>
#include <irssi/src/fe-text/sidepanels-render.h>
#include <stdio.h>
#include <string.h>

/* Create new client record */
WEB_CLIENT_REC *fe_web_client_create(int fd, const char *addr)
{
	WEB_CLIENT_REC *client;

	client = g_new0(WEB_CLIENT_REC, 1);
	client->fd = fd;
	client->id = fe_web_generate_message_id();
	client->addr = g_strdup(addr);
	client->connected_at = time(NULL);
	client->authenticated = FALSE;
	client->handshake_done = FALSE;
	client->websocket_key = NULL;
	client->server = NULL;
	client->synced_channels = NULL;
	client->wants_all_servers = FALSE;
	client->handle = NULL;
	client->recv_tag = -1;
	client->output_buffer = g_string_new("");
	client->input_buffer = g_byte_array_new();
	client->messages_sent = 0;
	client->messages_received = 0;
	client->pending_requests = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	/* Add to global list */
	web_clients = g_slist_append(web_clients, client);

	return client;
}

/* Destroy client record */
void fe_web_client_destroy(WEB_CLIENT_REC *client)
{
	if (client == NULL) {
		return;
	}

	/* Remove from global list */
	web_clients = g_slist_remove(web_clients, client);

	/* Cleanup */
	g_free(client->id);
	g_free(client->addr);
	g_free(client->websocket_key);

	if (client->synced_channels != NULL) {
		g_slist_free_full(client->synced_channels, g_free);
	}

	if (client->output_buffer != NULL) {
		g_string_free(client->output_buffer, TRUE);
	}

	if (client->input_buffer != NULL) {
		g_byte_array_free(client->input_buffer, TRUE);
	}

	if (client->pending_requests != NULL) {
		g_hash_table_destroy(client->pending_requests);
	}

	/* Note: handle and server are managed elsewhere */

	g_free(client);
}

/* Handle client command (sync_server, command, etc.) */
void fe_web_client_handle_message(WEB_CLIENT_REC *client, const char *json)
{
	char *type;
	char *id;

	if (client == NULL || json == NULL) {
		return;
	}

	client->messages_received++;

	/* Parse message type */
	type = fe_web_json_get_string(json, "type");
	if (type == NULL) {
		return;
	}

	/* Get message ID for responses */
	id = fe_web_json_get_string(json, "id");

	/* Handle different message types */
	if (g_strcmp0(type, "sync_server") == 0) {
		char *server_tag;
		server_tag = fe_web_json_get_string(json, "server");
		if (server_tag != NULL) {
			fe_web_client_sync_server(client, server_tag);
			g_free(server_tag);
		}
	} else if (g_strcmp0(type, "command") == 0) {
		char *command;
		char *server_tag;

		command = fe_web_json_get_string(json, "command");
		server_tag = fe_web_json_get_string(json, "server");

		if (command != NULL) {
			/* If server is specified, use it for this command */
			if (server_tag != NULL) {
				IRC_SERVER_REC *server;
				server = IRC_SERVER(server_find_tag(server_tag));
				if (server != NULL) {
					/* Temporarily assign server for this command */
					client->server = server;
				}
			}

			fe_web_client_execute_command(client, command);
			g_free(command);

			if (server_tag != NULL) {
				g_free(server_tag);
			}
		}
	} else if (g_strcmp0(type, "ping") == 0) {
		WEB_MESSAGE_REC *msg;
		msg = fe_web_message_new(WEB_MSG_PONG);
		msg->id = fe_web_generate_message_id();
		if (id != NULL) {
			msg->response_to = g_strdup(id);
		}
		fe_web_send_message(client, msg);
		fe_web_message_free(msg);
	} else if (g_strcmp0(type, "close_query") == 0) {
		char *nick;
		char *server_tag;
		QUERY_REC *query;

		nick = fe_web_json_get_string(json, "nick");
		server_tag = fe_web_json_get_string(json, "server");

		if (nick != NULL && server_tag != NULL) {
			IRC_SERVER_REC *server;
			server = IRC_SERVER(server_find_tag(server_tag));
			if (server != NULL) {
				query = query_find(SERVER(server), nick);
				if (query != NULL) {
					query_destroy(query);
				}
			}
		}

		g_free(nick);
		g_free(server_tag);
	} else if (g_strcmp0(type, "names") == 0) {
		char *channel;
		char *server_tag;
		IRC_SERVER_REC *server;
		IRC_CHANNEL_REC *chanrec;

		channel = fe_web_json_get_string(json, "channel");
		server_tag = fe_web_json_get_string(json, "server");

		if (channel != NULL && server_tag != NULL) {
			server = IRC_SERVER(server_find_tag(server_tag));
			if (server != NULL) {
				/* Execute physical NAMES command in IRC */
				char *cmd = g_strdup_printf("NAMES %s", channel);
				irc_send_cmd(server, cmd);
				g_free(cmd);

				/* Send nicklist from irssi's current state
				 * (irssi tracks nicklist automatically and updates it when
				 * NAMES response arrives, so we trust irssi's internal state) */
				chanrec = irc_channel_find(server, channel);
				if (chanrec != NULL) {
					/* Use the helper function to send full nicklist */
					fe_web_send_nicklist_for_channel(server, chanrec);
				}
			}
		}

		g_free(channel);
		g_free(server_tag);
	} else if (g_strcmp0(type, "mark_read") == 0) {
		char *target;
		char *server_tag;
		IRC_SERVER_REC *server;
		WINDOW_REC *window;
		WI_ITEM_REC *item;
		WEB_MESSAGE_REC *msg;

		target = fe_web_json_get_string(json, "target");
		server_tag = fe_web_json_get_string(json, "server");

		if (target != NULL && server_tag != NULL) {
			server = IRC_SERVER(server_find_tag(server_tag));
			if (server != NULL) {
				/* Find window item (channel or query) */
				item = window_item_find(SERVER(server), target);
				if (item != NULL) {
					window = window_item_window(item);
					if (window != NULL) {
						/* DON'T switch window - frontend already switched
						 * Switching here causes unnecessary window jumping
						 * in irssi when user clicks on channel in browser.
						 * We only need to clear activity markers.
						 */
						/* window_set_active(window); // REMOVED */

						/* Clear activity using core irssi function
						 * This properly updates statusbar and emits signals
						 */
						window_activity(window, 0, NULL);

						/* Clear sidepanel priority marker and redraw left panel */
						reset_window_priority(window);
						redraw_left_panels_only("mark_read");

						/* IMPORTANT: Manually send activity_update with
						 * level=0 because sig_window_activity() ignores
						 * level decrease (see fe-web-signals.c line 1366)
						 */
						msg = fe_web_message_new(WEB_MSG_ACTIVITY_UPDATE);
						msg->id = fe_web_generate_message_id();
						msg->server_tag = g_strdup(server->tag);
						msg->target = g_strdup(item->visible_name);
						msg->level = 0; /* DATA_LEVEL_NONE = read */
						fe_web_send_to_all_clients(msg);
						fe_web_message_free(msg);
					}
				}
			}
		}

		g_free(target);
		g_free(server_tag);
	} else if (g_strcmp0(type, "network_list") == 0) {
		fe_web_handle_network_list(client, json);
	} else if (g_strcmp0(type, "server_list") == 0) {
		fe_web_handle_server_list(client, json);
	} else if (g_strcmp0(type, "network_add") == 0) {
		fe_web_handle_network_add(client, json);
	} else if (g_strcmp0(type, "network_remove") == 0) {
		fe_web_handle_network_remove(client, json);
	} else if (g_strcmp0(type, "server_add") == 0) {
		fe_web_handle_server_add(client, json);
	} else if (g_strcmp0(type, "server_remove") == 0) {
		fe_web_handle_server_remove(client, json);
	}

	g_free(type);
	g_free(id);
}

/* Sync client to specific server */
void fe_web_client_sync_server(WEB_CLIENT_REC *client, const char *server_tag)
{
	IRC_SERVER_REC *server;

	if (client == NULL || server_tag == NULL) {
		return;
	}

	/* Special case: sync all servers */
	if (g_strcmp0(server_tag, "*") == 0) {
		client->wants_all_servers = TRUE;
		client->server = NULL;
		fe_web_dump_state(client);
		return;
	}

	/* Find specific server */
	server = IRC_SERVER(server_find_tag(server_tag));
	if (server == NULL) {
		/* Send error message */
		WEB_MESSAGE_REC *msg;
		msg = fe_web_message_new(WEB_MSG_ERROR);
		msg->text = g_strdup("Server not found");
		fe_web_send_message(client, msg);
		fe_web_message_free(msg);
		return;
	}

	/* Assign server */
	client->server = server;
	client->wants_all_servers = FALSE;

	/* Dump initial state */
	fe_web_dump_state(client);
}

/* Execute IRC command for client */
void fe_web_client_execute_command(WEB_CLIENT_REC *client, const char *command)
{
	if (client == NULL || command == NULL) {
		return;
	}

	/* Send command via signal system
	 * NOTE: client->server CAN BE NULL for global commands like SERVER CONNECT!
	 * The irssi command system (src/core/commands.c) fully supports NULL server
	 * for protocol-independent commands (protocol == -1).
	 * Commands that require a server will emit CMDERR_NOT_CONNECTED automatically.
	 */
	/* Signal: "send command", cmd, SERVER_REC, active_item */
	signal_emit("send command", 3, command, client->server, NULL);
}
