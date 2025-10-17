/*
 fe-web-netserver.c : Network and Server management handlers for fe-web

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
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/core/chatnets.h>
#include <irssi/src/irc/core/irc-chatnets.h>
#include <irssi/src/core/servers-setup.h>
#include <irssi/src/irc/core/irc-servers-setup.h>
#include <irssi/src/core/channels-setup.h>
#include <stdio.h>
#include <string.h>

/* Send command result response */
static void send_command_result(WEB_CLIENT_REC *client, const char *request_id,
                                gboolean success, const char *message,
                                const char *error_code)
{
	WEB_MESSAGE_REC *msg;
	GString *json_content;

	json_content = fe_web_build_command_result_json(success, message, error_code);
	if (json_content == NULL) {
		return;
	}

	/* Create message using WEB_MESSAGE_REC for proper SSL/encryption handling */
	msg = fe_web_message_new(WEB_MSG_COMMAND_RESULT);
	if (msg == NULL) {
		g_string_free(json_content, TRUE);
		return;
	}

	msg->response_to = request_id ? g_strdup(request_id) : NULL;
	
	/* Store the JSON content in text field */
	msg->text = g_strdup(json_content->str);

	/* Send using standard fe-web message infrastructure (handles SSL+encryption) */
	fe_web_send_message(client, msg);
	
	fe_web_message_free(msg);
	g_string_free(json_content, TRUE);
}

/* Handle network_list request */
void fe_web_handle_network_list(WEB_CLIENT_REC *client, const char *json_str)
{
	WEB_MESSAGE_REC *msg;
	GString *networks_array;
	GSList *tmp;
	gboolean first;
	char *request_id;

	request_id = fe_web_json_get_string(json_str, "id");

	/* Build networks JSON array */
	networks_array = g_string_new("[");
	first = TRUE;

	for (tmp = chatnets; tmp != NULL; tmp = tmp->next) {
		IRC_CHATNET_REC *rec = IRC_CHATNET(tmp->data);
		GString *network_json;

		if (rec == NULL) {
			continue;
		}

		if (!first) {
			g_string_append(networks_array, ",");
		}
		first = FALSE;
		
		network_json = fe_web_build_network_json(rec);
		if (network_json != NULL) {
			g_string_append(networks_array, network_json->str);
			g_string_free(network_json, TRUE);
		}
	}
	
	g_string_append(networks_array, "]");

	/* Create message using WEB_MESSAGE_REC for proper SSL/encryption handling */
	msg = fe_web_message_new(WEB_MSG_NETWORK_LIST_RESPONSE);
	if (msg == NULL) {
		g_free(request_id);
		g_string_free(networks_array, TRUE);
		return;
	}

	msg->response_to = request_id;  /* Transfer ownership */
	msg->text = g_strdup(networks_array->str);

	/* Send using standard fe-web message infrastructure (handles SSL+encryption) */
	fe_web_send_message(client, msg);

	fe_web_message_free(msg);
	g_string_free(networks_array, TRUE);
}

/* Handle server_list request */
void fe_web_handle_server_list(WEB_CLIENT_REC *client, const char *json_str)
{
	WEB_MESSAGE_REC *msg;
	GString *servers_array;
	GSList *tmp;
	gboolean first;
	char *request_id;
	char *filter_network;

	request_id = fe_web_json_get_string(json_str, "id");
	filter_network = fe_web_json_get_string(json_str, "network");

	/* Build servers JSON array */
	servers_array = g_string_new("[");
	first = TRUE;

	for (tmp = setupservers; tmp != NULL; tmp = tmp->next) {
		IRC_SERVER_SETUP_REC *rec = IRC_SERVER_SETUP(tmp->data);
		GString *server_json;

		if (rec == NULL) {
			continue;
		}

		/* Apply network filter if specified */
		if (filter_network != NULL && rec->chatnet != NULL) {
			if (g_strcmp0(rec->chatnet, filter_network) != 0) {
				continue;
			}
		}

		if (!first) {
			g_string_append(servers_array, ",");
		}
		first = FALSE;
		
		server_json = fe_web_build_server_json(rec);
		if (server_json != NULL) {
			g_string_append(servers_array, server_json->str);
			g_string_free(server_json, TRUE);
		}
	}
	
	g_string_append(servers_array, "]");

	/* Create message using WEB_MESSAGE_REC for proper SSL/encryption handling */
	msg = fe_web_message_new(WEB_MSG_SERVER_LIST_RESPONSE);
	if (msg == NULL) {
		g_free(request_id);
		g_free(filter_network);
		g_string_free(servers_array, TRUE);
		return;
	}

	msg->response_to = request_id;  /* Transfer ownership */
	msg->text = g_strdup(servers_array->str);

	/* Send using standard fe-web message infrastructure (handles SSL+encryption) */
	fe_web_send_message(client, msg);

	fe_web_message_free(msg);
	g_free(filter_network);
	g_string_free(servers_array, TRUE);
}

/* Handle network_add request */
void fe_web_handle_network_add(WEB_CLIENT_REC *client, const char *json_str)
{
	char *request_id;
	char *name;
	IRC_CHATNET_REC *existing;
	IRC_CHATNET_REC *rec;
	gboolean is_new;
	char *nick;
	char *alternate_nick;
	char *username;
	char *realname;
	char *own_host;
	char *autosendcmd;
	char *usermode;
	char *sasl_mechanism;
	char *sasl_username;
	char *sasl_password;
	char *msg;

	request_id = fe_web_json_get_string(json_str, "id");

	/* Extract network name from nested "network" object */
	/* Simplified JSON parsing - looking for "network":{"name":"..." */
	name = fe_web_json_get_string(json_str, "name");
	
	if (name == NULL || *name == '\0') {
		send_command_result(client, request_id, FALSE,
		                   "Network name is required", "MISSING_REQUIRED_FIELD");
		g_free(name);
		g_free(request_id);
		return;
	}

	/* Check if network already exists */
	existing = irc_chatnet_find(name);
	is_new = (existing == NULL);
	
	if (is_new) {
		rec = g_new0(IRC_CHATNET_REC, 1);
		rec->name = g_strdup(name);
	} else {
		rec = existing;
	}

	/* Parse and update all fields from JSON */
	/* Note: This is simplified - real implementation would parse nested "network" object */
	
	nick = fe_web_json_get_string(json_str, "nick");
	if (nick != NULL) {
		g_free_not_null(rec->nick);
		rec->nick = g_strdup(nick);
		g_free(nick);
	}
	
	alternate_nick = fe_web_json_get_string(json_str, "alternate_nick");
	if (alternate_nick != NULL) {
		g_free_not_null(rec->alternate_nick);
		rec->alternate_nick = g_strdup(alternate_nick);
		g_free(alternate_nick);
	}
	
	username = fe_web_json_get_string(json_str, "username");
	if (username != NULL) {
		g_free_not_null(rec->username);
		rec->username = g_strdup(username);
		g_free(username);
	}
	
	realname = fe_web_json_get_string(json_str, "realname");
	if (realname != NULL) {
		g_free_not_null(rec->realname);
		rec->realname = g_strdup(realname);
		g_free(realname);
	}
	
	own_host = fe_web_json_get_string(json_str, "own_host");
	if (own_host != NULL) {
		g_free_not_null(rec->own_host);
		rec->own_host = g_strdup(own_host);
		rec->own_ip4 = rec->own_ip6 = NULL;
		g_free(own_host);
	}
	
	autosendcmd = fe_web_json_get_string(json_str, "autosendcmd");
	if (autosendcmd != NULL) {
		g_free_not_null(rec->autosendcmd);
		rec->autosendcmd = g_strdup(autosendcmd);
		g_free(autosendcmd);
	}
	
	usermode = fe_web_json_get_string(json_str, "usermode");
	if (usermode != NULL) {
		g_free_not_null(rec->usermode);
		rec->usermode = g_strdup(usermode);
		g_free(usermode);
	}
	
	sasl_mechanism = fe_web_json_get_string(json_str, "sasl_mechanism");
	if (sasl_mechanism != NULL) {
		g_free_not_null(rec->sasl_mechanism);
		rec->sasl_mechanism = g_strdup(sasl_mechanism);
		g_free(sasl_mechanism);
	}
	
	sasl_username = fe_web_json_get_string(json_str, "sasl_username");
	if (sasl_username != NULL) {
		g_free_not_null(rec->sasl_username);
		rec->sasl_username = g_strdup(sasl_username);
		g_free(sasl_username);
	}
	
	sasl_password = fe_web_json_get_string(json_str, "sasl_password");
	if (sasl_password != NULL) {
		g_free_not_null(rec->sasl_password);
		rec->sasl_password = g_strdup(sasl_password);
		g_free(sasl_password);
	}
	
	/* Numeric fields */
	if (fe_web_json_has_key(json_str, "max_kicks")) {
		rec->max_kicks = fe_web_json_get_int(json_str, "max_kicks", 0);
	}
	if (fe_web_json_has_key(json_str, "max_msgs")) {
		rec->max_msgs = fe_web_json_get_int(json_str, "max_msgs", 0);
	}
	if (fe_web_json_has_key(json_str, "max_modes")) {
		rec->max_modes = fe_web_json_get_int(json_str, "max_modes", 0);
	}
	if (fe_web_json_has_key(json_str, "max_whois")) {
		rec->max_whois = fe_web_json_get_int(json_str, "max_whois", 0);
	}
	if (fe_web_json_has_key(json_str, "max_cmds_at_once")) {
		rec->max_cmds_at_once = fe_web_json_get_int(json_str, "max_cmds_at_once", 0);
	}
	if (fe_web_json_has_key(json_str, "cmd_queue_speed")) {
		rec->cmd_queue_speed = fe_web_json_get_int(json_str, "cmd_queue_speed", 0);
	}
	if (fe_web_json_has_key(json_str, "max_query_chans")) {
		rec->max_query_chans = fe_web_json_get_int(json_str, "max_query_chans", 0);
	}

	/* Create/update network */
	ircnet_create(rec);
	
	/* Auto-save configuration */
	signal_emit("save config", 0);

	msg = g_strdup_printf("Network '%s' %s successfully", name,
	                           is_new ? "added" : "modified");
	send_command_result(client, request_id, TRUE, msg, NULL);
	g_free(msg);

	g_free(name);
	g_free(request_id);
}

/* Handle network_remove request */
void fe_web_handle_network_remove(WEB_CLIENT_REC *client, const char *json_str)
{
	char *request_id;
	char *name;
	IRC_CHATNET_REC *rec;
	char *msg;

	request_id = fe_web_json_get_string(json_str, "id");
	name = fe_web_json_get_string(json_str, "name");
	
	if (name == NULL || *name == '\0') {
		send_command_result(client, request_id, FALSE,
		                   "Network name is required", "MISSING_REQUIRED_FIELD");
		g_free(name);
		g_free(request_id);
		return;
	}

	rec = irc_chatnet_find(name);
	if (rec == NULL) {
		msg = g_strdup_printf("Network '%s' not found", name);
		send_command_result(client, request_id, FALSE, msg, "NETWORK_NOT_FOUND");
		g_free(msg);
		g_free(name);
		g_free(request_id);
		return;
	}

	/* Remove associated servers and channels */
	server_setup_remove_chatnet(name);
	channel_setup_remove_chatnet(name);
	
	/* Remove network */
	chatnet_remove(CHATNET(rec));
	
	/* Auto-save configuration */
	signal_emit("save config", 0);

	msg = g_strdup_printf("Network '%s' removed successfully", name);
	send_command_result(client, request_id, TRUE, msg, NULL);
	g_free(msg);

	g_free(name);
	g_free(request_id);
}

/* Handle server_add request */
void fe_web_handle_server_add(WEB_CLIENT_REC *client, const char *json_str)
{
	char *request_id;
	char *address;
	int port;
	IRC_SERVER_SETUP_REC *existing;
	IRC_SERVER_SETUP_REC *rec;
	gboolean is_new;
	char *chatnet;
	char *password;
	char *tls_cert;
	char *tls_pkey;
	char *tls_cafile;
	char *msg;

	request_id = fe_web_json_get_string(json_str, "id");

	/* Extract required fields */
	address = fe_web_json_get_string(json_str, "address");
	port = fe_web_json_get_int(json_str, "port", 6667);
	
	if (address == NULL || *address == '\0') {
		send_command_result(client, request_id, FALSE,
		                   "Server address is required", "MISSING_REQUIRED_FIELD");
		g_free(address);
		g_free(request_id);
		return;
	}

	/* Check if server already exists */
	chatnet = fe_web_json_get_string(json_str, "chatnet");
	existing = IRC_SERVER_SETUP(server_setup_find(address, port, chatnet));
	is_new = (existing == NULL);
	
	if (is_new) {
		rec = g_new0(IRC_SERVER_SETUP_REC, 1);
		rec->address = g_strdup(address);
		rec->port = port;
		rec->chatnet = chatnet != NULL ? g_strdup(chatnet) : NULL;
	} else {
		rec = existing;
	}

	/* Update fields from JSON (simplified parsing) */
	password = fe_web_json_get_string(json_str, "password");
	if (password != NULL && g_strcmp0(password, "***") != 0) {
		g_free_not_null(rec->password);
		rec->password = g_strdup(password);
		g_free(password);
	}
	
	if (fe_web_json_has_key(json_str, "autoconnect")) {
		rec->autoconnect = fe_web_json_get_int(json_str, "autoconnect", 0);
	}
	if (fe_web_json_has_key(json_str, "use_tls")) {
		rec->use_tls = fe_web_json_get_int(json_str, "use_tls", 0);
	}
	if (fe_web_json_has_key(json_str, "tls_verify")) {
		rec->tls_verify = fe_web_json_get_int(json_str, "tls_verify", 0);
	}
	
	/* TLS certificate fields */
	tls_cert = fe_web_json_get_string(json_str, "tls_cert");
	if (tls_cert != NULL) {
		g_free_not_null(rec->tls_cert);
		rec->tls_cert = g_strdup(tls_cert);
		g_free(tls_cert);
	}
	
	tls_pkey = fe_web_json_get_string(json_str, "tls_pkey");
	if (tls_pkey != NULL) {
		g_free_not_null(rec->tls_pkey);
		rec->tls_pkey = g_strdup(tls_pkey);
		g_free(tls_pkey);
	}
	
	tls_cafile = fe_web_json_get_string(json_str, "tls_cafile");
	if (tls_cafile != NULL) {
		g_free_not_null(rec->tls_cafile);
		rec->tls_cafile = g_strdup(tls_cafile);
		g_free(tls_cafile);
	}
	
	/* Numeric settings */
	if (fe_web_json_has_key(json_str, "max_cmds_at_once")) {
		rec->max_cmds_at_once = fe_web_json_get_int(json_str, "max_cmds_at_once", 0);
	}
	if (fe_web_json_has_key(json_str, "cmd_queue_speed")) {
		rec->cmd_queue_speed = fe_web_json_get_int(json_str, "cmd_queue_speed", 0);
	}
	if (fe_web_json_has_key(json_str, "starttls")) {
		rec->starttls = fe_web_json_get_int(json_str, "starttls", 0);
	}
	if (fe_web_json_has_key(json_str, "no_cap")) {
		rec->no_cap = fe_web_json_get_int(json_str, "no_cap", 0);
	}

	/* Add/modify server */
	if (is_new) {
		server_setup_add(SERVER_SETUP(rec));
	}
	
	/* Auto-save configuration */
	signal_emit("save config", 0);

	msg = g_strdup_printf("Server '%s:%d' %s successfully",
	                           address, port, is_new ? "added" : "modified");
	send_command_result(client, request_id, TRUE, msg, NULL);
	g_free(msg);

	g_free(address);
	g_free(chatnet);
	g_free(request_id);
}

/* Handle server_remove request */
void fe_web_handle_server_remove(WEB_CLIENT_REC *client, const char *json_str)
{
	char *request_id;
	char *address;
	char *chatnet;
	int port;
	SERVER_SETUP_REC *rec;
	char *msg;

	request_id = fe_web_json_get_string(json_str, "id");
	address = fe_web_json_get_string(json_str, "address");
	port = fe_web_json_get_int(json_str, "port", 6667);
	chatnet = fe_web_json_get_string(json_str, "chatnet");
	
	if (address == NULL || *address == '\0') {
		send_command_result(client, request_id, FALSE,
		                   "Server address is required", "MISSING_REQUIRED_FIELD");
		g_free(address);
		g_free(chatnet);
		g_free(request_id);
		return;
	}

	rec = server_setup_find(address, port, chatnet);
	if (rec == NULL) {
		msg = g_strdup_printf("Server '%s:%d' not found", address, port);
		send_command_result(client, request_id, FALSE, msg, "SERVER_NOT_FOUND");
		g_free(msg);
		g_free(address);
		g_free(chatnet);
		g_free(request_id);
		return;
	}

	/* Remove server */
	server_setup_remove(rec);
	
	/* Auto-save configuration */
	signal_emit("save config", 0);

	msg = g_strdup_printf("Server '%s:%d' removed successfully", address, port);
	send_command_result(client, request_id, TRUE, msg, NULL);
	g_free(msg);

	g_free(address);
	g_free(chatnet);
	g_free(request_id);
}
