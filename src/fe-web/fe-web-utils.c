/*
 fe-web-utils.c : Utility functions for fe-web module

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "fe-web.h"
#include "fe-web-ssl.h"
#include "fe-web-crypto.h"

#include <irssi/src/core/net-sendbuffer.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Generate unique message ID (timestamp-counter format) */
char *fe_web_generate_message_id(void)
{
	static int counter = 0;
	time_t now;
	char *id;

	now = time(NULL);
	id = g_strdup_printf("%ld-%04d", (long) now, counter);

	counter++;
	if (counter >= 10000) {
		counter = 0;
	}

	return id;
}

/* Escape JSON string - handles UTF-8 correctly
 * JSON supports UTF-8 natively, so we only escape:
 * - Special JSON characters: " \
 * - Control characters (< 32): \b \f \n \r \t and others as \uXXXX
 * - UTF-8 multi-byte sequences (> 127) are passed through unchanged
 */
char *fe_web_escape_json(const char *str)
{
	GString *result;
	const unsigned char *p;

	if (str == NULL) {
		return g_strdup("");
	}

	result = g_string_new("");

	for (p = (const unsigned char *) str; *p != '\0'; p++) {
		switch (*p) {
		case '"':
			g_string_append(result, "\\\"");
			break;
		case '\\':
			g_string_append(result, "\\\\");
			break;
		case '\b':
			g_string_append(result, "\\b");
			break;
		case '\f':
			g_string_append(result, "\\f");
			break;
		case '\n':
			g_string_append(result, "\\n");
			break;
		case '\r':
			g_string_append(result, "\\r");
			break;
		case '\t':
			g_string_append(result, "\\t");
			break;
		default:
			/* Only escape control characters (< 32)
			 * UTF-8 bytes (>= 128) and regular ASCII (>= 32 && < 128)
			 * are passed through unchanged */
			if (*p < 32) {
				g_string_append_printf(result, "\\u%04x", *p);
			} else {
				/* Pass through: regular ASCII and UTF-8 bytes */
				g_string_append_c(result, *p);
			}
			break;
		}
	}

	return g_string_free(result, FALSE);
}

/* Convert message type enum to string */
static const char *fe_web_type_to_string(WEB_MESSAGE_TYPE type)
{
	switch (type) {
	case WEB_MSG_AUTH_OK:
		return "auth_ok";
	case WEB_MSG_MESSAGE:
		return "message";
	case WEB_MSG_SERVER_STATUS:
		return "server_status";
	case WEB_MSG_CHANNEL_JOIN:
		return "channel_join";
	case WEB_MSG_CHANNEL_PART:
		return "channel_part";
	case WEB_MSG_CHANNEL_KICK:
		return "channel_kick";
	case WEB_MSG_USER_QUIT:
		return "user_quit";
	case WEB_MSG_TOPIC:
		return "topic";
	case WEB_MSG_CHANNEL_MODE:
		return "channel_mode";
	case WEB_MSG_NICKLIST:
		return "nicklist";
	case WEB_MSG_NICKLIST_UPDATE:
		return "nicklist_update";
	case WEB_MSG_NICK_CHANGE:
		return "nick_change";
	case WEB_MSG_USER_MODE:
		return "user_mode";
	case WEB_MSG_AWAY:
		return "away";
	case WEB_MSG_WHOIS:
		return "whois";
	case WEB_MSG_CHANNEL_LIST:
		return "channel_list";
	case WEB_MSG_STATE_DUMP:
		return "state_dump";
	case WEB_MSG_ERROR:
		return "error";
	case WEB_MSG_PONG:
		return "pong";
	case WEB_MSG_QUERY_OPENED:
		return "query_opened";
	case WEB_MSG_QUERY_CLOSED:
		return "query_closed";
	case WEB_MSG_ACTIVITY_UPDATE:
		return "activity_update";
	case WEB_MSG_MARK_READ:
		return "mark_read";
	case WEB_MSG_NETWORK_LIST:
		return "network_list";
	case WEB_MSG_NETWORK_LIST_RESPONSE:
		return "network_list_response";
	case WEB_MSG_SERVER_LIST:
		return "server_list";
	case WEB_MSG_SERVER_LIST_RESPONSE:
		return "server_list_response";
	case WEB_MSG_NETWORK_ADD:
		return "network_add";
	case WEB_MSG_NETWORK_REMOVE:
		return "network_remove";
	case WEB_MSG_SERVER_ADD:
		return "server_add";
	case WEB_MSG_SERVER_REMOVE:
		return "server_remove";
	case WEB_MSG_COMMAND_RESULT:
		return "command_result";
	default:
		return "unknown";
	}
}

/* Create new message structure */
WEB_MESSAGE_REC *fe_web_message_new(WEB_MESSAGE_TYPE type)
{
	WEB_MESSAGE_REC *msg;

	msg = g_new0(WEB_MESSAGE_REC, 1);
	msg->type = type;
	msg->timestamp = time(NULL);
	msg->extra_data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	msg->is_own = FALSE;
	msg->is_highlight = FALSE;

	return msg;
}

/* Free message structure */
void fe_web_message_free(WEB_MESSAGE_REC *msg)
{
	if (msg == NULL) {
		return;
	}

	g_free(msg->id);
	g_free(msg->server_tag);
	g_free(msg->target);
	g_free(msg->nick);
	g_free(msg->text);
	g_free(msg->response_to);

	if (msg->extra_data != NULL) {
		g_hash_table_destroy(msg->extra_data);
	}

	g_free(msg);
}

/* Serialize message to JSON */
char *fe_web_message_to_json(WEB_MESSAGE_REC *msg)
{
	GString *json;
	char *escaped;

	json = g_string_new("{");

	/* id */
	if (msg->id != NULL) {
		escaped = fe_web_escape_json(msg->id);
		g_string_append_printf(json, "\"id\":\"%s\",", escaped);
		g_free(escaped);
	}

	/* type */
	g_string_append_printf(json, "\"type\":\"%s\"", fe_web_type_to_string(msg->type));

	/* response_to (for WHOIS, channel_list) */
	if (msg->response_to != NULL) {
		escaped = fe_web_escape_json(msg->response_to);
		g_string_append_printf(json, ",\"response_to\":\"%s\"", escaped);
		g_free(escaped);
	}

	/* server */
	if (msg->server_tag != NULL) {
		escaped = fe_web_escape_json(msg->server_tag);
		g_string_append_printf(json, ",\"server\":\"%s\"", escaped);
		g_free(escaped);
	}

	/* channel/target */
	if (msg->target != NULL) {
		escaped = fe_web_escape_json(msg->target);
		g_string_append_printf(json, ",\"channel\":\"%s\"", escaped);
		g_free(escaped);
	}

	/* nick */
	if (msg->nick != NULL) {
		escaped = fe_web_escape_json(msg->nick);
		g_string_append_printf(json, ",\"nick\":\"%s\"", escaped);
		g_free(escaped);
	}

	/* text (or "task" for nicklist_update, or raw JSON for network_list/server_list) */
	if (msg->text != NULL) {
		if (msg->type == WEB_MSG_NICKLIST_UPDATE) {
			/* For nicklist_update, serialize text field as "task" */
			escaped = fe_web_escape_json(msg->text);
			g_string_append_printf(json, ",\"task\":\"%s\"", escaped);
			g_free(escaped);
		} else if (msg->type == WEB_MSG_NETWORK_LIST_RESPONSE) {
			/* For network_list_response, text contains JSON array - insert raw */
			g_string_append_printf(json, ",\"networks\":%s", msg->text);
		} else if (msg->type == WEB_MSG_SERVER_LIST_RESPONSE) {
			/* For server_list_response, text contains JSON array - insert raw */
			g_string_append_printf(json, ",\"servers\":%s", msg->text);
		} else if (msg->type == WEB_MSG_COMMAND_RESULT) {
			/* For command_result, text contains JSON object - insert raw (without outer
			 * braces) */
			if (msg->text[0] == '{' && msg->text[strlen(msg->text) - 1] == '}') {
				/* Skip outer braces and insert content */
				char *content = g_strndup(msg->text + 1, strlen(msg->text) - 2);
				g_string_append_printf(json, ",%s", content);
				g_free(content);
			}
		} else {
			escaped = fe_web_escape_json(msg->text);
			g_string_append_printf(json, ",\"text\":\"%s\"", escaped);
			g_free(escaped);
		}
	}

	/* timestamp */
	g_string_append_printf(json, ",\"timestamp\":%ld", (long) msg->timestamp);

	/* level (for message types and activity_update) */
	/* IMPORTANT: For activity_update, level=0 means "read", so we MUST send it! */
	if (msg->level != 0 || msg->type == WEB_MSG_ACTIVITY_UPDATE) {
		g_string_append_printf(json, ",\"level\":%d", msg->level);
	}

	/* is_own */
	if (msg->type == WEB_MSG_MESSAGE) {
		g_string_append_printf(json, ",\"is_own\":%s", msg->is_own ? "true" : "false");
	}

	/* is_highlight */
	if (msg->type == WEB_MSG_MESSAGE) {
		g_string_append_printf(json, ",\"is_highlight\":%s",
		                       msg->is_highlight ? "true" : "false");
	}

	/* extra_data - serialize hash table if not empty */
	if (msg->extra_data != NULL && g_hash_table_size(msg->extra_data) > 0) {
		GHashTableIter iter;
		gpointer key;
		gpointer value;
		int first;

		g_string_append(json, ",\"extra\":{");

		first = 1;
		g_hash_table_iter_init(&iter, msg->extra_data);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			char *escaped_key;
			const char *key_str = (const char *) key;
			const char *value_str = (const char *) value;

			if (!first) {
				g_string_append_c(json, ',');
			}
			first = 0;

			escaped_key = fe_web_escape_json(key_str);

			/* Special handling for "params" field - it's already JSON array */
			if (g_strcmp0(key_str, "params") == 0 && value_str != NULL &&
			    value_str[0] == '[') {
				/* Raw JSON array - don't escape */
				g_string_append_printf(json, "\"%s\":%s", escaped_key, value_str);
			} else {
				/* Regular string value - escape it */
				char *escaped_value = fe_web_escape_json(value_str);
				g_string_append_printf(json, "\"%s\":\"%s\"", escaped_key,
				                       escaped_value);
				g_free(escaped_value);
			}

			g_free(escaped_key);
		}

		g_string_append_c(json, '}');
	}

	g_string_append_c(json, '}');

	return g_string_free(json, FALSE);
}

/* Send message to specific client */
void fe_web_send_message(WEB_CLIENT_REC *client, WEB_MESSAGE_REC *msg)
{
	char *json;
	guchar *frame;
	gsize frame_len;
	const char *type_str;

	type_str = fe_web_type_to_string(msg->type);

	if (client == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: ERROR: Cannot send %s - client is NULL", type_str);
		return;
	}

	/* auth_ok is special - can be sent before authenticated flag is set */
	if (msg->type != WEB_MSG_AUTH_OK) {
		if (!client->authenticated || !client->handshake_done) {
			return;
		}
	} else {
		/* For auth_ok, only check handshake */
		if (!client->handshake_done) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] ERROR: Cannot send %s - handshake not done",
			          client->id, type_str);
			return;
		}
	}

	if (client->handle == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: [%s] ERROR: Cannot send %s - handle is NULL", client->id,
		          type_str);
		return;
	}

	/* Serialize to JSON */
	json = fe_web_message_to_json(msg);

	/* Encrypt if encryption is enabled */
	if (client->encryption_enabled) {
		unsigned char *encrypted;
		int encrypted_len;
		const unsigned char *key;

		key = fe_web_crypto_get_key();
		if (key == NULL) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] Encryption key not available for %s", client->id,
			          type_str);
			g_free(json);
			return;
		}

		/* Allocate buffer for encrypted data (plaintext + IV + tag) */
		encrypted = g_malloc(strlen(json) + FE_WEB_CRYPTO_IV_SIZE + FE_WEB_CRYPTO_TAG_SIZE);

		/* Encrypt JSON */
		if (!fe_web_crypto_encrypt((const unsigned char *) json, strlen(json), key,
		                           encrypted, &encrypted_len)) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] Encryption failed for %s", client->id, type_str);
			g_free(encrypted);
			g_free(json);
			return;
		}

		/* Create WebSocket binary frame with encrypted data */
		frame = fe_web_websocket_create_frame(0x2, encrypted, encrypted_len, &frame_len);
		g_free(encrypted);
	} else {
		/* Create WebSocket text frame with plain JSON */
		frame = fe_web_websocket_create_frame(0x1, (const guchar *) json, strlen(json),
		                                      &frame_len);
	}

	/* Send frame - use SSL if enabled */
	if (client->use_ssl && client->ssl_channel != NULL) {
		int ssl_ret;
		ssl_ret = fe_web_ssl_write(client->ssl_channel, (const char *) frame, frame_len);
		if (ssl_ret < 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] SSL write failed for %s", client->id, type_str);
			g_free(frame);
			g_free(json);
			return;
		}
	} else {
		/* Plain connection */
		net_sendbuffer_send(client->handle, (const char *) frame, frame_len);
	}

	g_free(frame);
	g_free(json);
	client->messages_sent++;
}

/* Send message to all clients synced with specific server */
void fe_web_send_to_server_clients(IRC_SERVER_REC *server, WEB_MESSAGE_REC *msg)
{
	GSList *tmp;

	if (server == NULL) {
		return;
	}

	for (tmp = web_clients; tmp != NULL; tmp = tmp->next) {
		WEB_CLIENT_REC *client = tmp->data;

		/* Send ONLY to clients synced with this server or all servers */
		if (client->authenticated &&
		    (client->server == server || client->wants_all_servers)) {
			fe_web_send_message(client, msg);
		}
	}
}

/* Send message to all authenticated clients */
void fe_web_send_to_all_clients(WEB_MESSAGE_REC *msg)
{
	GSList *tmp;

	for (tmp = web_clients; tmp != NULL; tmp = tmp->next) {
		WEB_CLIENT_REC *client = tmp->data;

		if (client->authenticated) {
			fe_web_send_message(client, msg);
		}
	}
}
