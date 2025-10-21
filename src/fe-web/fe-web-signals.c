/*
 fe-web-signals.c : IRC signal handlers for fe-web

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
#include <irssi/src/core/nicklist.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/misc.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/hilight-text.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/irc/core/irc-channels.h>
#include <irssi/src/irc/core/irc-nicklist.h>
/* Forward declarations for WHOIS handlers used in dispatch table */
static void event_whois(IRC_SERVER_REC *server, const char *data);
static void event_whois_server(IRC_SERVER_REC *server, const char *data);
static void event_whois_idle(IRC_SERVER_REC *server, const char *data);
static void event_whois_channels(IRC_SERVER_REC *server, const char *data);
static void event_whois_account(IRC_SERVER_REC *server, const char *data);
static void event_whois_secure(IRC_SERVER_REC *server, const char *data);
static void event_whois_oper(IRC_SERVER_REC *server, const char *data);
static void event_whois_away(IRC_SERVER_REC *server, const char *data);

/* Forward declarations for activity handlers */
static void sig_window_hilight(WINDOW_REC *window);
static void sig_window_activity(WINDOW_REC *window, int old_level);
static void sig_window_dehilight(WINDOW_REC *window);
static void sig_window_changed(WINDOW_REC *new_window, WINDOW_REC *old_window);

/* Forward declarations for window lifecycle handlers */
static void sig_window_item_remove(WINDOW_REC *window, WI_ITEM_REC *item);
static void sig_window_destroyed(WINDOW_REC *window);

/* fe-web WHOIS event dispatch table (file-scope) */
typedef void (*FEWEB_WHOIS_HANDLER)(IRC_SERVER_REC *server, const char *data);
static struct {
	int num;
	FEWEB_WHOIS_HANDLER func;
} feweb_whois_events[] = { { 311, event_whois },
	                   { 312, event_whois_server },
	                   { 317, event_whois_idle },
	                   { 319, event_whois_channels },
	                   { 330, event_whois_account },
	                   { 671, event_whois_secure },
	                   { 0, NULL } };

/* Global hash table for tracking active WHOIS requests */
/* Key: "server_tag:nick", Value: WHOIS_REC* */
static GHashTable *active_whois = NULL;

/* Helper: Create WHOIS key */
static char *whois_key(IRC_SERVER_REC *server, const char *nick)
{
	return g_strdup_printf("%s:%s", server->tag, nick);
}

/* Helper: Create new WHOIS record */
static WHOIS_REC *whois_rec_new(const char *nick)
{
	WHOIS_REC *rec = g_new0(WHOIS_REC, 1);
	rec->nick = g_strdup(nick);
	rec->timestamp = time(NULL);
	return rec;
}

/* Helper: Free WHOIS record */
static void whois_rec_free(WHOIS_REC *rec)
{
	if (rec == NULL)
		return;

	g_free(rec->nick);
	g_free(rec->user);
	g_free(rec->host);
	g_free(rec->realname);
	g_free(rec->server);
	g_free(rec->server_info);
	g_free(rec->idle);
	g_free(rec->signon);
	g_free(rec->channels);
	g_free(rec->account);

	/* Free special list */
	if (rec->special != NULL) {
		g_slist_free_full(rec->special, g_free);
	}

	g_free(rec);
}

/* Helper: Get or create WHOIS record */
static WHOIS_REC *whois_get_or_create(IRC_SERVER_REC *server, const char *nick)
{
	char *key;
	WHOIS_REC *rec;

	if (active_whois == NULL)
		return NULL;

	key = whois_key(server, nick);
	rec = g_hash_table_lookup(active_whois, key);

	if (rec == NULL) {
		rec = whois_rec_new(nick);
		g_hash_table_insert(active_whois, key, rec);
	} else {
		g_free(key);
	}

	return rec;
}

/* Helper: Send nicklist for a channel (full list) - used by NAMES command */
void fe_web_send_nicklist_for_channel(IRC_SERVER_REC *server, IRC_CHANNEL_REC *channel)
{
	WEB_MESSAGE_REC *msg;
	GString *nicklist;
	GSList *nicks, *nick_tmp;

	if (server == NULL || channel == NULL) {
		return;
	}

	msg = fe_web_message_new(WEB_MSG_NICKLIST);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(channel->name);

	/* Build nicklist JSON */
	nicklist = g_string_new("[");
	nicks = nicklist_getnicks(CHANNEL(channel));
	for (nick_tmp = nicks; nick_tmp != NULL; nick_tmp = nick_tmp->next) {
		NICK_REC *nick = nick_tmp->data;
		char *escaped_nick;
		char prefix[8];

		if (nicklist->len > 1) {
			g_string_append_c(nicklist, ',');
		}

		/* Build prefix string (@, +, etc) */
		prefix[0] = '\0';
		if (nick->op) {
			strcat(prefix, "@");
		}
		if (nick->halfop) {
			strcat(prefix, "%");
		}
		if (nick->voice) {
			strcat(prefix, "+");
		}

		escaped_nick = fe_web_escape_json(nick->nick);
		g_string_append_printf(nicklist, "{\"nick\":\"%s\",\"prefix\":\"%s\"}",
		                       escaped_nick, prefix);
		g_free(escaped_nick);
	}
	g_slist_free(nicks);
	g_string_append_c(nicklist, ']');

	msg->text = g_string_free(nicklist, FALSE);
	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);
}

/* Helper: Send nicklist update (delta: add/remove/mode) */
static void fe_web_send_nicklist_update(IRC_SERVER_REC *server, IRC_CHANNEL_REC *channel,
                                        const char *nick, const char *task)
{
	WEB_MESSAGE_REC *msg;

	if (server == NULL || channel == NULL || nick == NULL || task == NULL) {
		return;
	}

	msg = fe_web_message_new(WEB_MSG_NICKLIST_UPDATE);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(channel->name);
	msg->nick = g_strdup(nick);
	msg->text = g_strdup(task); /* task field: add, remove, +o, -o, +v, -v, +h, -h */

	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);
}

/* Signal: "message public" */
static void sig_message_public(IRC_SERVER_REC *server, const char *msg, const char *nick,
                               const char *address, const char *target)
{
	WEB_MESSAGE_REC *web_msg;
	HILIGHT_REC *hilight;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_MESSAGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(target);
	web_msg->nick = g_strdup(nick);
	web_msg->text = g_strdup(msg);
	web_msg->level = MSGLEVEL_PUBLIC;
	web_msg->is_own = FALSE;

	/* Check if message is a highlight (mentions user's nick) */
	hilight =
	    hilight_match(SERVER(server), target, nick, address, MSGLEVEL_PUBLIC, msg, NULL, NULL);
	web_msg->is_highlight = (hilight != NULL);

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "message own_public" */
static void sig_message_own_public(IRC_SERVER_REC *server, const char *msg, const char *target)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_MESSAGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(target);
	web_msg->nick = g_strdup(server->nick);
	web_msg->text = g_strdup(msg);
	web_msg->level = MSGLEVEL_PUBLIC;
	web_msg->is_own = TRUE;

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "message private" */
static void sig_message_private(IRC_SERVER_REC *server, const char *msg, const char *nick,
                                const char *address)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_MESSAGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(nick); /* Private message - target is the sender */
	web_msg->nick = g_strdup(nick);
	web_msg->text = g_strdup(msg);
	web_msg->level = MSGLEVEL_MSGS;
	web_msg->is_own = FALSE;

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "message own_private" */
static void sig_message_own_private(IRC_SERVER_REC *server, const char *msg, const char *target,
                                    const char *orig_target)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_MESSAGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(target);
	web_msg->nick = g_strdup(server->nick);
	web_msg->text = g_strdup(msg);
	web_msg->level = MSGLEVEL_MSGS;
	web_msg->is_own = TRUE;

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "message irc action" */
static void sig_message_irc_action(IRC_SERVER_REC *server, const char *msg, const char *nick,
                                   const char *address, const char *target)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_MESSAGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(target);
	web_msg->nick = g_strdup(nick);
	web_msg->text = g_strdup(msg);
	web_msg->level = MSGLEVEL_ACTIONS; /* Mark as ACTION */
	web_msg->is_own = FALSE;

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "message irc own_action" */
static void sig_message_irc_own_action(IRC_SERVER_REC *server, const char *msg, const char *target)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_MESSAGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(target);
	web_msg->nick = g_strdup(server->nick);
	web_msg->text = g_strdup(msg);
	web_msg->level = MSGLEVEL_ACTIONS; /* Mark as ACTION */
	web_msg->is_own = TRUE;

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "message join" */
static void sig_message_join(IRC_SERVER_REC *server, const char *channel, const char *nick,
                             const char *address, const char *account, const char *realname)
{
	WEB_MESSAGE_REC *web_msg;
	IRC_CHANNEL_REC *chanrec;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_CHANNEL_JOIN);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(channel);
	web_msg->nick = g_strdup(nick);

	/* Add hostname (user@host) to extra_data */
	if (address != NULL && *address != '\0') {
		g_hash_table_insert(web_msg->extra_data, g_strdup("hostname"), g_strdup(address));
	}

	/* Add account name from extended-join (IRCv3) */
	if (account != NULL && *account != '\0' && g_strcmp0(account, "*") != 0) {
		g_hash_table_insert(web_msg->extra_data, g_strdup("account"), g_strdup(account));
	}

	/* Add realname (GECOS) from extended-join (IRCv3) */
	if (realname != NULL && *realname != '\0') {
		g_hash_table_insert(web_msg->extra_data, g_strdup("realname"), g_strdup(realname));
	}

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist update (delta: add) after join */
	chanrec = irc_channel_find(server, channel);
	if (chanrec != NULL) {
		fe_web_send_nicklist_update(server, chanrec, nick, "add");
	}
}

/* Signal: "message part" */
static void sig_message_part(IRC_SERVER_REC *server, const char *channel, const char *nick,
                             const char *address, const char *reason)
{
	WEB_MESSAGE_REC *web_msg;
	IRC_CHANNEL_REC *chanrec;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_CHANNEL_PART);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(channel);
	web_msg->nick = g_strdup(nick);
	if (reason != NULL && *reason != '\0') {
		web_msg->text = g_strdup(reason);
	}

	/* Add hostname (user@host) to extra_data */
	if (address != NULL && *address != '\0') {
		g_hash_table_insert(web_msg->extra_data, g_strdup("hostname"), g_strdup(address));
	}

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist update (delta: remove) after part */
	chanrec = irc_channel_find(server, channel);
	if (chanrec != NULL) {
		fe_web_send_nicklist_update(server, chanrec, nick, "remove");
	}
}

/* Signal: "message kick" */
static void sig_message_kick(IRC_SERVER_REC *server, const char *channel, const char *nick,
                             const char *kicker, const char *address, const char *reason)
{
	WEB_MESSAGE_REC *web_msg;
	IRC_CHANNEL_REC *chanrec;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_CHANNEL_KICK);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(channel);
	web_msg->nick = g_strdup(nick);
	if (reason != NULL && *reason != '\0') {
		web_msg->text = g_strdup(reason);
	}

	/* Add kicker to extra_data */
	g_hash_table_insert(web_msg->extra_data, g_strdup("kicker"), g_strdup(kicker));

	/* Add hostname (user@host) of kicked user to extra_data */
	if (address != NULL && *address != '\0') {
		g_hash_table_insert(web_msg->extra_data, g_strdup("hostname"), g_strdup(address));
	}

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist update (delta: remove) after kick */
	chanrec = irc_channel_find(server, channel);
	if (chanrec != NULL) {
		fe_web_send_nicklist_update(server, chanrec, nick, "remove");
	}
}

/* Signal: "message quit" */
static void sig_message_quit(IRC_SERVER_REC *server, const char *nick, const char *address,
                             const char *reason)
{
	WEB_MESSAGE_REC *web_msg;
	GSList *tmp;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_USER_QUIT);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->nick = g_strdup(nick);
	if (reason != NULL && *reason != '\0') {
		web_msg->text = g_strdup(reason);
	}

	/* Add hostname (user@host) to extra_data */
	if (address != NULL && *address != '\0') {
		g_hash_table_insert(web_msg->extra_data, g_strdup("hostname"), g_strdup(address));
	}

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist update (delta: remove) for all channels the user was in */
	for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
		IRC_CHANNEL_REC *channel = tmp->data;
		/* The user has already been removed from the nicklist by irssi core,
		   so we just need to send delta update for each channel */
		fe_web_send_nicklist_update(server, channel, nick, "remove");
	}
}

/* Signal: "message topic" */
static void sig_message_topic(IRC_SERVER_REC *server, const char *channel, const char *topic,
                              const char *nick, const char *address)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_TOPIC);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(channel);
	web_msg->nick = g_strdup(nick);
	web_msg->text = g_strdup(topic);

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);
}

/* Parse IRC MODE string into mode and params
 * Example: "+o kfn" -> mode="+o", params=["kfn"]
 *          "+l 100" -> mode="+l", params=["100"]
 *          "+nt" -> mode="+nt", params=[]
 */
static void parse_mode_string(const char *mode_str, char **mode_out, char ***params_out,
                              int *params_count)
{
	char **parts;
	int i;
	GSList *params_list = NULL;
	int count = 0;

	*mode_out = NULL;
	*params_out = NULL;
	*params_count = 0;

	if (mode_str == NULL || *mode_str == '\0') {
		return;
	}

	/* Split by whitespace */
	parts = g_strsplit(mode_str, " ", -1);
	if (parts == NULL || parts[0] == NULL) {
		g_strfreev(parts);
		return;
	}

	/* First part is the mode string (e.g., "+o", "-b", "+nt") */
	*mode_out = g_strdup(parts[0]);

	/* Rest are parameters */
	for (i = 1; parts[i] != NULL; i++) {
		if (*parts[i] != '\0') { /* Skip empty strings */
			params_list = g_slist_append(params_list, g_strdup(parts[i]));
			count++;
		}
	}

	/* Convert GSList to array */
	if (count > 0) {
		GSList *tmp;

		*params_out = g_new0(char *, count + 1); /* NULL-terminated */
		i = 0;
		for (tmp = params_list; tmp != NULL; tmp = tmp->next) {
			(*params_out)[i++] = tmp->data; /* Transfer ownership */
		}
		g_slist_free(params_list); /* Free list but not data */
		*params_count = count;
	}

	g_strfreev(parts);
}

/* Signal: "message irc mode" */
static void sig_message_irc_mode(IRC_SERVER_REC *server, const char *channel, const char *nick,
                                 const char *address, const char *mode)
{
	WEB_MESSAGE_REC *web_msg;
	char *mode_str = NULL;
	char **params = NULL;
	int params_count = 0;
	GString *params_json;
	int i;

	if (server == NULL) {
		return;
	}

	/* Parse mode string into mode and params */
	parse_mode_string(mode, &mode_str, &params, &params_count);

	web_msg = fe_web_message_new(WEB_MSG_CHANNEL_MODE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->target = g_strdup(channel);
	web_msg->nick = g_strdup(nick);

	/* Add mode as extra_data field */
	if (mode_str != NULL) {
		g_hash_table_insert(web_msg->extra_data, g_strdup("mode"), g_strdup(mode_str));
	}

	/* Add params as JSON array in extra_data */
	if (params_count > 0) {
		params_json = g_string_new("[");
		for (i = 0; i < params_count; i++) {
			char *escaped = fe_web_escape_json(params[i]);
			if (i > 0) {
				g_string_append_c(params_json, ',');
			}
			g_string_append_printf(params_json, "\"%s\"", escaped);
			g_free(escaped);
		}
		g_string_append_c(params_json, ']');
		g_hash_table_insert(web_msg->extra_data, g_strdup("params"),
		                    g_string_free(params_json, FALSE));
	} else {
		/* Empty array for no params */
		g_hash_table_insert(web_msg->extra_data, g_strdup("params"), g_strdup("[]"));
	}

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist delta updates for user modes (o, v, h) */
	if (mode_str != NULL && params != NULL) {
		IRC_CHANNEL_REC *chanrec = irc_channel_find(server, channel);
		if (chanrec != NULL) {
			char current_sign = '+'; /* Default to + */
			int param_idx = 0;

			for (i = 0; mode_str[i] != '\0'; i++) {
				char c = mode_str[i];

				/* Track + or - */
				if (c == '+' || c == '-') {
					current_sign = c;
					continue;
				}

				/* Check if this is a user mode that affects nicklist */
				if (c == 'o' || c == 'v' || c == 'h') {
					/* These modes take a nick parameter */
					if (param_idx < params_count) {
						char task[3];
						task[0] = current_sign;
						task[1] = c;
						task[2] = '\0';
						fe_web_send_nicklist_update(
						    server, chanrec, params[param_idx], task);
						param_idx++;
					}
				} else if (c == 'q' || c == 'a') {
					/* Owner/admin modes also take nick but we may not handle
					 * them */
					if (param_idx < params_count) {
						param_idx++;
					}
				} else if (c == 'l') {
					/* +l takes param, -l doesn't */
					if (current_sign == '+' && param_idx < params_count) {
						param_idx++;
					}
				} else if (c == 'k') {
					/* +k/-k both take param */
					if (param_idx < params_count) {
						param_idx++;
					}
				} else if (c == 'b' || c == 'e' || c == 'I') {
					/* Ban/exempt/invite modes take mask parameter */
					if (param_idx < params_count) {
						param_idx++;
					}
				}
				/* Other modes (n, t, m, i, s, p, etc.) don't take parameters */
			}
		}
	}

	/* Cleanup */
	g_free(mode_str);
	if (params != NULL) {
		for (i = 0; i < params_count; i++) {
			g_free(params[i]);
		}
		g_free(params);
	}
}

/* Signal: "nick mode changed" */
static void sig_nick_mode_changed(IRC_CHANNEL_REC *channel, NICK_REC *nick)
{
	/* NOTE: We now send nicklist delta updates from sig_message_irc_mode
	 * instead of sending full nicklist here. This signal is kept for
	 * compatibility but does nothing. Delta updates are sent when the
	 * MODE message arrives, which happens before this signal fires.
	 */
	(void) channel;
	(void) nick;
}

/* Signal: "message nick" - Someone else changed nick */
static void sig_message_nick(IRC_SERVER_REC *server, const char *newnick, const char *oldnick,
                             const char *address)
{
	WEB_MESSAGE_REC *web_msg;
	GSList *tmp;

	if (server == NULL) {
		return;
	}

	/* Send nick_change message (global event) */
	web_msg = fe_web_message_new(WEB_MSG_NICK_CHANGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->nick = g_strdup(oldnick);
	web_msg->text = g_strdup(newnick);

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist_update (delta: change) for each channel the user is in */
	for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
		IRC_CHANNEL_REC *channel = tmp->data;
		NICK_REC *nick_rec;

		/* Check if the user is in this channel (using NEW nick, as irssi already renamed)
		 */
		nick_rec = nicklist_find(CHANNEL(channel), newnick);
		if (nick_rec != NULL) {
			WEB_MESSAGE_REC *update_msg;

			update_msg = fe_web_message_new(WEB_MSG_NICKLIST_UPDATE);
			update_msg->id = fe_web_generate_message_id();
			update_msg->server_tag = g_strdup(server->tag);
			update_msg->target = g_strdup(channel->name);
			update_msg->nick = g_strdup(oldnick);  /* Old nick */
			update_msg->text = g_strdup("change"); /* task */

			/* Add new nick to extra_data */
			g_hash_table_insert(update_msg->extra_data, g_strdup("new_nick"),
			                    g_strdup(newnick));

			fe_web_send_to_server_clients(server, update_msg);
			fe_web_message_free(update_msg);
		}
	}
}

/* Signal: "message own_nick" - We changed our own nick */
static void sig_message_own_nick(IRC_SERVER_REC *server, const char *newnick, const char *oldnick,
                                 const char *address)
{
	WEB_MESSAGE_REC *web_msg;
	GSList *tmp;

	if (server == NULL) {
		return;
	}

	/* Send nick_change message (global event) */
	web_msg = fe_web_message_new(WEB_MSG_NICK_CHANGE);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->nick = g_strdup(oldnick);
	web_msg->text = g_strdup(newnick);

	fe_web_send_to_server_clients(server, web_msg);
	fe_web_message_free(web_msg);

	/* Send nicklist_update (delta: change) for each channel we are in */
	for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
		IRC_CHANNEL_REC *channel = tmp->data;
		NICK_REC *nick_rec;

		/* Check if we are in this channel (using NEW nick, as irssi already renamed)
		 */
		nick_rec = nicklist_find(CHANNEL(channel), newnick);
		if (nick_rec != NULL) {
			WEB_MESSAGE_REC *update_msg;

			update_msg = fe_web_message_new(WEB_MSG_NICKLIST_UPDATE);
			update_msg->id = fe_web_generate_message_id();
			update_msg->server_tag = g_strdup(server->tag);
			update_msg->target = g_strdup(channel->name);
			update_msg->nick = g_strdup(oldnick);  /* Old nick */
			update_msg->text = g_strdup("change"); /* task */

			/* Add new nick to extra_data */
			g_hash_table_insert(update_msg->extra_data, g_strdup("new_nick"),
			                    g_strdup(newnick));

			fe_web_send_to_server_clients(server, update_msg);
			fe_web_message_free(update_msg);
		}
	}
}

/* Signal: "server connected" */
static void sig_server_connected(IRC_SERVER_REC *server)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_SERVER_STATUS);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->text = g_strdup("connected");

	fe_web_send_to_all_clients(web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "server disconnected" */
static void sig_server_disconnected(IRC_SERVER_REC *server)
{
	WEB_MESSAGE_REC *web_msg;

	if (server == NULL) {
		return;
	}

	web_msg = fe_web_message_new(WEB_MSG_SERVER_STATUS);
	web_msg->id = fe_web_generate_message_id();
	web_msg->server_tag = g_strdup(server->tag);
	web_msg->text = g_strdup("disconnected");

	fe_web_send_to_all_clients(web_msg);
	fe_web_message_free(web_msg);
}

/* Signal: "channel joined" - YOU joined a channel */
static void sig_channel_joined(IRC_CHANNEL_REC *channel)
{
	IRC_SERVER_REC *server;
	WEB_MESSAGE_REC *msg;
	GString *nicklist;

	if (channel == NULL) {
		return;
	}

	server = IRC_SERVER(channel->server);
	if (server == NULL) {
		return;
	}

	/* Send nicklist */
	msg = fe_web_message_new(WEB_MSG_NICKLIST);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(channel->name);

	/* Build nicklist JSON */
	nicklist = g_string_new("[");
	{
		GSList *nicks;
		GSList *nick_tmp;

		nicks = nicklist_getnicks(CHANNEL(channel));
		for (nick_tmp = nicks; nick_tmp != NULL; nick_tmp = nick_tmp->next) {
			NICK_REC *nick = nick_tmp->data;
			char *escaped_nick;
			char prefix[8];

			if (nicklist->len > 1) {
				g_string_append_c(nicklist, ',');
			}

			/* Build prefix string (@, +, etc) */
			prefix[0] = '\0';
			if (nick->op) {
				strcat(prefix, "@");
			}
			if (nick->halfop) {
				strcat(prefix, "%");
			}
			if (nick->voice) {
				strcat(prefix, "+");
			}

			escaped_nick = fe_web_escape_json(nick->nick);
			g_string_append_printf(nicklist, "{\"nick\":\"%s\",\"prefix\":\"%s\"}",
			                       escaped_nick, prefix);
			g_free(escaped_nick);
		}
		g_slist_free(nicks);
	}
	g_string_append_c(nicklist, ']');

	msg->text = g_string_free(nicklist, FALSE);
	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);
}

/* Signal: "query created" - Query window opened */
static void sig_query_created(QUERY_REC *query, gpointer automatic)
{
	IRC_SERVER_REC *server;
	WEB_MESSAGE_REC *msg;

	if (query == NULL) {
		return;
	}

	server = IRC_SERVER(query->server);
	if (server == NULL) {
		return;
	}

	/* Send query_opened event */
	msg = fe_web_message_new(WEB_MSG_QUERY_OPENED);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->nick = g_strdup(query->name);

	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);
}

/* Signal: "query destroyed" - Query window closed */
static void sig_query_destroyed(QUERY_REC *query)
{
	IRC_SERVER_REC *server;
	WEB_MESSAGE_REC *msg;

	if (query == NULL) {
		return;
	}

	server = IRC_SERVER(query->server);
	if (server == NULL) {
		return;
	}

	/* Send query_closed event */
	msg = fe_web_message_new(WEB_MSG_QUERY_CLOSED);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->nick = g_strdup(query->name);

	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);
}

/* Signal: "event 311" - WHOIS user/host */
static void event_whois(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *user, *host, *realname;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 6, NULL, &nick, &user, &host, NULL, &realname);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		g_free(rec->user);
		g_free(rec->host);
		g_free(rec->realname);
		rec->user = g_strdup(user);
		rec->host = g_strdup(host);
		rec->realname = g_strdup(realname);
	}

	g_free(params);
}

/* Signal: "event 312" - WHOIS server */
static void event_whois_server(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *whoserver, *desc;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 4, NULL, &nick, &whoserver, &desc);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		g_free(rec->server);
		g_free(rec->server_info);
		rec->server = g_strdup(whoserver);
		rec->server_info = g_strdup(desc);
	}

	g_free(params);
}

/* Signal: "event 317" - WHOIS idle */
static void event_whois_idle(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *secstr, *signonstr, *rest;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params =
	    event_get_params(data, 5 | PARAM_FLAG_GETREST, NULL, &nick, &secstr, &signonstr, &rest);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		g_free(rec->idle);
		g_free(rec->signon);
		rec->idle = g_strdup(secstr);
		/* Only set signon if "signon time" is in rest */
		if (strstr(rest, "signon time") != NULL) {
			rec->signon = g_strdup(signonstr);
		}
	}

	g_free(params);
}

/* Signal: "event 319" - WHOIS channels */
static void event_whois_channels(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *chans;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 3, NULL, &nick, &chans);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		g_free(rec->channels);
		rec->channels = g_strdup(chans);
	}

	g_free(params);
}

/* Signal: "event 330" - WHOIS account */
static void event_whois_account(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *account;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 3, NULL, &nick, &account);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		g_free(rec->account);
		rec->account = g_strdup(account);
	}

	g_free(params);
}

/* Signal: "event 671" - WHOIS secure connection */
static void event_whois_secure(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 2, NULL, &nick);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		rec->secure = TRUE;
	}

	g_free(params);
}

/* Signal: "whois oper" or "event 313" - WHOIS oper */
static void event_whois_oper(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *type;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 3, NULL, &nick, &type);
	if (type == NULL || *type == '\0')
		type = "IRC Operator";

	rec = whois_get_or_create(server, nick);
	if (rec != NULL) {
		/* mark oper flag and add to special list for client visibility */
		rec->oper = TRUE;
		rec->special = g_slist_append(rec->special, g_strdup(type));
	}

	g_free(params);
}

/* Signal: "whois away" - WHOIS away message */
static void event_whois_away(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *awaymsg;
	WHOIS_REC *rec;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 3, NULL, &nick, &awaymsg);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL && awaymsg != NULL && *awaymsg != '\0') {
		GString *line = g_string_new(NULL);
		g_string_printf(line, "is away: %s", awaymsg);
		rec->special = g_slist_append(rec->special, g_strdup(line->str));
		g_string_free(line, TRUE);
	}

	g_free(params);
}

/* Signal: "user mode changed" - User mode change */
static void sig_user_mode_changed(IRC_SERVER_REC *server, const char *oldmode)
{
	WEB_MESSAGE_REC *msg;

	if (server == NULL || server->usermode == NULL)
		return;

	msg = fe_web_message_new(WEB_MSG_USER_MODE);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->nick = g_strdup(server->nick);
	msg->text = g_strdup(server->usermode);

	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);
}

/* Signal: "event 301" - AWAY status */
static void event_away_status(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *awaymsg;
	WEB_MESSAGE_REC *msg;

	if (server == NULL || data == NULL)
		return;

	params = event_get_params(data, 3, NULL, &nick, &awaymsg);

	msg = fe_web_message_new(WEB_MSG_AWAY);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->nick = g_strdup(nick);
	msg->text = g_strdup(awaymsg);

	fe_web_send_to_server_clients(server, msg);
	fe_web_message_free(msg);

	g_free(params);
}

/* Signal: "whois default event" - Catch-all for non-standard WHOIS events */
static void event_whois_default(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *text;
	WHOIS_REC *rec;
	int num;

	if (server == NULL || data == NULL)
		return;

	/* Get event number from current_server_event */
	num = atoi(current_server_event);

	/* Dispatch standard WHOIS numerics via our handlers (redirect sends them here) */
	for (int i = 0; feweb_whois_events[i].num != 0; i++) {
		if (feweb_whois_events[i].num == num) {
			feweb_whois_events[i].func(server, data);
			return;
		}
	}

	params = event_get_params(data, 3 | PARAM_FLAG_GETREST, NULL, &nick, &text);

	rec = whois_get_or_create(server, nick);
	if (rec != NULL && text != NULL && *text != '\0') {
		/* Add to special list - only non-standard events */
		rec->special = g_slist_append(rec->special, g_strdup(text));
	}

	g_free(params);
}

/* Signal: "event 318" - End of WHOIS */
static void event_end_of_whois(IRC_SERVER_REC *server, const char *data)
{
	char *params, *nick, *key;
	WHOIS_REC *rec;
	WEB_MESSAGE_REC *msg;

	if (server == NULL || data == NULL || server->tag == NULL)
		return;

	params = event_get_params(data, 2, NULL, &nick);

	if (nick == NULL || nick[0] == '\0') {
		g_free(params);
		return;
	}

	key = whois_key(server, nick);

	rec = g_hash_table_lookup(active_whois, key);
	/* Check if we have any WHOIS data (don't rely on whois_found flag) */
	if (rec != NULL && (rec->user != NULL || rec->channels != NULL || rec->idle != NULL ||
	                    rec->account != NULL || rec->special != NULL)) {
		/* Send WHOIS message to clients */
		msg = fe_web_message_new(WEB_MSG_WHOIS);
		msg->id = fe_web_generate_message_id();
		msg->server_tag = g_strdup(server->tag);
		msg->nick = g_strdup(rec->nick);

		/* Add extra data */
		if (rec->user != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("user"), g_strdup(rec->user));
		if (rec->host != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("host"), g_strdup(rec->host));
		if (rec->realname != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("realname"),
			                    g_strdup(rec->realname));
		if (rec->server != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("server"),
			                    g_strdup(rec->server));
		if (rec->server_info != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("server_info"),
			                    g_strdup(rec->server_info));
		if (rec->channels != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("channels"),
			                    g_strdup(rec->channels));
		if (rec->idle != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("idle"), g_strdup(rec->idle));
		if (rec->signon != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("signon"),
			                    g_strdup(rec->signon));
		if (rec->account != NULL)
			g_hash_table_insert(msg->extra_data, g_strdup("account"),
			                    g_strdup(rec->account));
		if (rec->secure)
			g_hash_table_insert(msg->extra_data, g_strdup("secure"), g_strdup("true"));

		/* Add special WHOIS lines as JSON array */
		if (rec->special != NULL) {
			GString *special_json = g_string_new("[");
			GSList *tmp;
			gboolean first = TRUE;

			for (tmp = rec->special; tmp != NULL; tmp = tmp->next) {
				char *escaped;
				if (!first)
					g_string_append(special_json, ",");
				escaped = fe_web_escape_json((char *) tmp->data);
				g_string_append_printf(special_json, "\"%s\"", escaped);
				g_free(escaped);
				first = FALSE;
			}
			g_string_append(special_json, "]");

			g_hash_table_insert(msg->extra_data, g_strdup("special"),
			                    g_string_free(special_json, FALSE));
		}

		fe_web_send_to_server_clients(server, msg);
		fe_web_message_free(msg);

		/* Remove from active_whois */
		g_hash_table_remove(active_whois, key);
	}

	g_free(key);
	g_free(params);
}

/* Activity tracking: Send activity update when window gets highlighted */
static void sig_window_hilight(WINDOW_REC *window)
{
	WEB_MESSAGE_REC *msg;
	WI_ITEM_REC *item;
	IRC_SERVER_REC *server;
	int data_level;

	if (window == NULL || window->active == NULL) {
		return;
	}

	item = window->active;
	server = IRC_SERVER(item->server);
	if (server == NULL) {
		return;
	}

	/* Get highest data_level (from item or window) */
	data_level = item->data_level > 0 ? item->data_level : window->data_level;

	/* CRITICAL FIX: Skip if window->data_level is 0 (being cleared by core)
	 * This prevents sending stale activity_update when core is clearing activity.
	 * Core calls window_activity(window, 0, NULL) which:
	 * 1. Sets window->data_level = 0
	 * 2. Emits "window hilight" signal
	 * But item->data_level might not be cleared yet, so we check window level.
	 */
	if (window->data_level == 0) {
		return;
	}

	/* OPTIMIZATION: Skip if this is the active window in irssi
	 * User is already viewing this window locally, no need to notify browsers
	 * This prevents unnecessary activity markers when reading messages in irssi
	 */
	if (window == active_win) {
		return;
	}

	/* Send ACTIVITY_UPDATE to all clients */
	msg = fe_web_message_new(WEB_MSG_ACTIVITY_UPDATE);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(item->visible_name);
	msg->level = data_level;
	fe_web_send_to_all_clients(msg);
	fe_web_message_free(msg);
}

/* Activity tracking: Send activity update when window activity changes */
static void sig_window_activity(WINDOW_REC *window, int old_level)
{
	WEB_MESSAGE_REC *msg;
	WI_ITEM_REC *item;
	IRC_SERVER_REC *server;
	int data_level;

	if (window == NULL || window->active == NULL) {
		return;
	}

	item = window->active;
	server = IRC_SERVER(item->server);
	if (server == NULL) {
		return;
	}

	/* Get highest data_level (from item or window) */
	data_level = item->data_level > 0 ? item->data_level : window->data_level;

	/* OPTIMIZATION: Skip if this is the active window in irssi
	 * User is already viewing this window locally, no need to notify browsers
	 * This prevents unnecessary activity markers when reading messages in irssi
	 */
	if (window == active_win) {
		return;
	}

	/* Skip if level DECREASED (e.g. from hilight to text) - sig_window_hilight already sent
	 * update */
	/* But ALWAYS send if level stayed same or increased - this counts new messages */
	if (data_level < old_level) {
		return;
	}

	/* Send ACTIVITY_UPDATE to all clients */
	msg = fe_web_message_new(WEB_MSG_ACTIVITY_UPDATE);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(item->visible_name);
	msg->level = data_level;
	fe_web_send_to_all_clients(msg);
	fe_web_message_free(msg);
}

/* Activity tracking: Send activity clear when window is dehighlighted (read) */
static void sig_window_dehilight(WINDOW_REC *window)
{
	WEB_MESSAGE_REC *msg;
	WI_ITEM_REC *item;
	IRC_SERVER_REC *server;

	if (window == NULL || window->active == NULL) {
		return;
	}

	item = window->active;
	server = IRC_SERVER(item->server);
	if (server == NULL) {
		return;
	}

	/* Send ACTIVITY_UPDATE with level=0 (read) */
	msg = fe_web_message_new(WEB_MSG_ACTIVITY_UPDATE);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(item->visible_name);
	msg->level = 0; /* DATA_LEVEL_NONE = read */
	fe_web_send_to_all_clients(msg);
	fe_web_message_free(msg);
}

/* Activity tracking: Handle window change (user switched to different window in irssi) */
static void sig_window_changed(WINDOW_REC *new_window, WINDOW_REC *old_window)
{
	WEB_MESSAGE_REC *msg;
	WI_ITEM_REC *item;
	IRC_SERVER_REC *server;
	int data_level;

	/* Clear activity for the NEW active window (user is now viewing it) */
	if (new_window == NULL || new_window->active == NULL) {
		return;
	}

	item = new_window->active;
	server = IRC_SERVER(item->server);
	if (server == NULL) {
		return;
	}

	data_level = item->data_level > 0 ? item->data_level : new_window->data_level;

	/* Only send if there was activity to clear */
	if (data_level > 0) {
		/* Send ACTIVITY_UPDATE with level=0 (read) */
		msg = fe_web_message_new(WEB_MSG_ACTIVITY_UPDATE);
		msg->id = fe_web_generate_message_id();
		msg->server_tag = g_strdup(server->tag);
		msg->target = g_strdup(item->visible_name);
		msg->level = 0; /* DATA_LEVEL_NONE = read */
		fe_web_send_to_all_clients(msg);
		fe_web_message_free(msg);
	}

	/* ALWAYS send mark_read to sync read status across all clients (even if no activity) */
	msg = fe_web_message_new(WEB_MSG_MARK_READ);
	msg->id = fe_web_generate_message_id();
	msg->server_tag = g_strdup(server->tag);
	msg->target = g_strdup(item->visible_name);
	fe_web_send_to_all_clients(msg);
	fe_web_message_free(msg);
}

/* Window lifecycle: Handle window item removal (channel/query closed in irssi) */
static void sig_window_item_remove(WINDOW_REC *window, WI_ITEM_REC *item)
{
	WEB_MESSAGE_REC *msg;
	IRC_SERVER_REC *server;
	IRC_CHANNEL_REC *channel;
	QUERY_REC *query;

	if (item == NULL) {
		return;
	}

	server = IRC_SERVER(item->server);
	if (server == NULL) {
		return;
	}

	/* Check if it's a channel */
	channel = IRC_CHANNEL(item);
	if (channel != NULL) {
		/* DON'T send CHANNEL_PART here - it was already sent by sig_message_part()
		 * when the IRC PART message was received from server. Sending it again
		 * causes:
		 * 1. Duplicate channel_part events (first with hostname, second without)
		 * 2. Second event is ignored because channel already removed from backend
		 * 3. Confusion and potential race conditions
		 * The window cleanup is internal to irssi, frontend doesn't need
		 * notification. */

		return;
	}

	/* Check if it's a query */
	query = QUERY(item);
	if (query != NULL) {
		/* Send QUERY_CLOSED */
		msg = fe_web_message_new(WEB_MSG_QUERY_CLOSED);
		msg->id = fe_web_generate_message_id();
		msg->server_tag = g_strdup(server->tag);
		msg->nick = g_strdup(item->visible_name);

		fe_web_send_to_server_clients(server, msg);
		fe_web_message_free(msg);
		return;
	}
}

/* Window lifecycle: Handle window destruction (entire window closed) */
static void sig_window_destroyed(WINDOW_REC *window)
{
	/* Note: "window item remove" is emitted BEFORE "window destroyed"
	 * for each item in the window, so we don't need to send part/close
	 * messages here - they were already sent by sig_window_item_remove().
	 * This handler is here for completeness and future use. */

	if (window == NULL) {
		return;
	}
}

/* Initialize signal handlers */
void fe_web_signals_init(void)
{
	/* Initialize active_whois hash table FIRST - before registering signal handlers! */
	active_whois =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) whois_rec_free);

	/* Message signals */
	signal_add("message public", (SIGNAL_FUNC) sig_message_public);
	signal_add("message own_public", (SIGNAL_FUNC) sig_message_own_public);
	signal_add("message private", (SIGNAL_FUNC) sig_message_private);
	signal_add("message own_private", (SIGNAL_FUNC) sig_message_own_private);
	signal_add("message irc action", (SIGNAL_FUNC) sig_message_irc_action);
	signal_add("message irc own_action", (SIGNAL_FUNC) sig_message_irc_own_action);

	/* Channel events */
	signal_add("message join", (SIGNAL_FUNC) sig_message_join);
	signal_add("message part", (SIGNAL_FUNC) sig_message_part);
	signal_add("message kick", (SIGNAL_FUNC) sig_message_kick);
	signal_add("message quit", (SIGNAL_FUNC) sig_message_quit);

	/* Channel info */
	signal_add("message topic", (SIGNAL_FUNC) sig_message_topic);
	signal_add("message irc mode", (SIGNAL_FUNC) sig_message_irc_mode);
	signal_add("nick mode changed", (SIGNAL_FUNC) sig_nick_mode_changed);

	/* Nick changes */
	signal_add("message nick", (SIGNAL_FUNC) sig_message_nick);
	signal_add("message own_nick", (SIGNAL_FUNC) sig_message_own_nick);

	/* Server events */
	signal_add("server connected", (SIGNAL_FUNC) sig_server_connected);
	signal_add("server disconnected", (SIGNAL_FUNC) sig_server_disconnected);

	/* Channel lifecycle (YOU joined/parted) */
	signal_add("channel joined", (SIGNAL_FUNC) sig_channel_joined);

	/* Query lifecycle */
	signal_add("query created", (SIGNAL_FUNC) sig_query_created);
	signal_add("query destroyed", (SIGNAL_FUNC) sig_query_destroyed);

	/* WHOIS events - use signal_add_last to run after fe-common/irc handlers */
	signal_add_last("whois event", (SIGNAL_FUNC) event_whois);
	signal_add_last("event 311", (SIGNAL_FUNC) event_whois);
	signal_add_last("event 312", (SIGNAL_FUNC) event_whois_server);
	signal_add_last("event 317", (SIGNAL_FUNC) event_whois_idle);
	signal_add_last("event 319", (SIGNAL_FUNC) event_whois_channels);
	signal_add_last("event 330", (SIGNAL_FUNC) event_whois_account);
	signal_add_last("whois account", (SIGNAL_FUNC) event_whois_account);
	signal_add_last("event 671", (SIGNAL_FUNC) event_whois_secure);
	/* Extra WHOIS info */
	signal_add_last("event 313", (SIGNAL_FUNC) event_whois_oper);
	signal_add_last("whois oper", (SIGNAL_FUNC) event_whois_oper);
	signal_add_last("whois away", (SIGNAL_FUNC) event_whois_away);
	/* Catch-all and end */
	signal_add_last("whois default event", (SIGNAL_FUNC) event_whois_default);
	signal_add_last("whois end", (SIGNAL_FUNC) event_end_of_whois);
	signal_add_last("event 318", (SIGNAL_FUNC) event_end_of_whois);

	/* User mode and away */
	signal_add("user mode changed", (SIGNAL_FUNC) sig_user_mode_changed);
	signal_add("event 301", (SIGNAL_FUNC) event_away_status);

	/* Activity tracking (unread markers) */
	signal_add("window hilight", (SIGNAL_FUNC) sig_window_hilight);
	signal_add("window activity", (SIGNAL_FUNC) sig_window_activity);
	signal_add("window dehilight", (SIGNAL_FUNC) sig_window_dehilight);
	signal_add("window changed", (SIGNAL_FUNC) sig_window_changed);

	/* Window lifecycle (channel/query closed in irssi) */
	signal_add("window item remove", (SIGNAL_FUNC) sig_window_item_remove);
	signal_add("window destroyed", (SIGNAL_FUNC) sig_window_destroyed);
}

/* Deinitialize signal handlers */
void fe_web_signals_deinit(void)
{
	/* Message signals */
	signal_remove("message public", (SIGNAL_FUNC) sig_message_public);
	signal_remove("message own_public", (SIGNAL_FUNC) sig_message_own_public);
	signal_remove("message private", (SIGNAL_FUNC) sig_message_private);
	signal_remove("message own_private", (SIGNAL_FUNC) sig_message_own_private);
	signal_remove("message irc action", (SIGNAL_FUNC) sig_message_irc_action);
	signal_remove("message irc own_action", (SIGNAL_FUNC) sig_message_irc_own_action);

	/* Channel events */
	signal_remove("message join", (SIGNAL_FUNC) sig_message_join);
	signal_remove("message part", (SIGNAL_FUNC) sig_message_part);
	signal_remove("message kick", (SIGNAL_FUNC) sig_message_kick);
	signal_remove("message quit", (SIGNAL_FUNC) sig_message_quit);

	/* Channel info */
	signal_remove("message topic", (SIGNAL_FUNC) sig_message_topic);
	signal_remove("message irc mode", (SIGNAL_FUNC) sig_message_irc_mode);
	signal_remove("nick mode changed", (SIGNAL_FUNC) sig_nick_mode_changed);

	/* Nick changes */
	signal_remove("message nick", (SIGNAL_FUNC) sig_message_nick);
	signal_remove("message own_nick", (SIGNAL_FUNC) sig_message_own_nick);

	/* Server events */
	signal_remove("server connected", (SIGNAL_FUNC) sig_server_connected);
	signal_remove("server disconnected", (SIGNAL_FUNC) sig_server_disconnected);

	/* Channel lifecycle */
	signal_remove("channel joined", (SIGNAL_FUNC) sig_channel_joined);

	/* Query lifecycle */
	signal_remove("query created", (SIGNAL_FUNC) sig_query_created);
	signal_remove("query destroyed", (SIGNAL_FUNC) sig_query_destroyed);

	/* WHOIS events */
	signal_remove("whois event", (SIGNAL_FUNC) event_whois);
	signal_remove("event 311", (SIGNAL_FUNC) event_whois);
	signal_remove("event 312", (SIGNAL_FUNC) event_whois_server);
	signal_remove("event 317", (SIGNAL_FUNC) event_whois_idle);
	signal_remove("event 319", (SIGNAL_FUNC) event_whois_channels);
	signal_remove("event 330", (SIGNAL_FUNC) event_whois_account);
	signal_remove("whois account", (SIGNAL_FUNC) event_whois_account);
	signal_remove("event 671", (SIGNAL_FUNC) event_whois_secure);
	/* Extra WHOIS info */
	signal_remove("event 313", (SIGNAL_FUNC) event_whois_oper);
	signal_remove("whois oper", (SIGNAL_FUNC) event_whois_oper);
	signal_remove("whois away", (SIGNAL_FUNC) event_whois_away);
	/* Catch-all and end */
	signal_remove("whois default event", (SIGNAL_FUNC) event_whois_default);
	signal_remove("whois end", (SIGNAL_FUNC) event_end_of_whois);
	signal_remove("event 318", (SIGNAL_FUNC) event_end_of_whois);

	/* User mode and away */
	signal_remove("user mode changed", (SIGNAL_FUNC) sig_user_mode_changed);
	signal_remove("event 301", (SIGNAL_FUNC) event_away_status);

	/* Activity tracking (unread markers) */
	signal_remove("window hilight", (SIGNAL_FUNC) sig_window_hilight);
	signal_remove("window activity", (SIGNAL_FUNC) sig_window_activity);
	signal_remove("window dehilight", (SIGNAL_FUNC) sig_window_dehilight);
	signal_remove("window changed", (SIGNAL_FUNC) sig_window_changed);

	/* Window lifecycle (channel/query closed in irssi) */
	signal_remove("window item remove", (SIGNAL_FUNC) sig_window_item_remove);
	signal_remove("window destroyed", (SIGNAL_FUNC) sig_window_destroyed);

	/* Cleanup active_whois hash table */
	if (active_whois != NULL) {
		g_hash_table_destroy(active_whois);
		active_whois = NULL;
	}
}

/* Helper function to dump single server state */
static void fe_web_dump_server_state(WEB_CLIENT_REC *client, IRC_SERVER_REC *server)
{
	GSList *tmp;
	WEB_MESSAGE_REC *state_msg;

	if (server == NULL) {
		return;
	}

	/* Send state_dump marker message first */
	state_msg = fe_web_message_new(WEB_MSG_STATE_DUMP);
	state_msg->id = fe_web_generate_message_id();
	state_msg->server_tag = g_strdup(server->tag);
	fe_web_send_message(client, state_msg);
	fe_web_message_free(state_msg);

	/* Dump channels */
	for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
		IRC_CHANNEL_REC *channel = tmp->data;
		WEB_MESSAGE_REC *msg;
		GString *nicklist;
		WINDOW_REC *window;
		WI_ITEM_REC *item;
		int data_level;

		/* Send channel join */
		msg = fe_web_message_new(WEB_MSG_CHANNEL_JOIN);
		msg->id = fe_web_generate_message_id();
		msg->server_tag = g_strdup(server->tag);
		msg->target = g_strdup(channel->name);
		msg->nick = g_strdup(server->nick);
		fe_web_send_message(client, msg);
		fe_web_message_free(msg);

		/* Send topic */
		if (channel->topic != NULL && *channel->topic != '\0') {
			msg = fe_web_message_new(WEB_MSG_TOPIC);
			msg->id = fe_web_generate_message_id();
			msg->server_tag = g_strdup(server->tag);
			msg->target = g_strdup(channel->name);
			msg->text = g_strdup(channel->topic);
			fe_web_send_message(client, msg);
			fe_web_message_free(msg);
		}

		/* Send nicklist */
		msg = fe_web_message_new(WEB_MSG_NICKLIST);
		msg->id = fe_web_generate_message_id();
		msg->server_tag = g_strdup(server->tag);
		msg->target = g_strdup(channel->name);

		/* Build nicklist JSON */
		nicklist = g_string_new("[");
		{
			GSList *nicks;
			GSList *nick_tmp;

			nicks = nicklist_getnicks(CHANNEL(channel));
			for (nick_tmp = nicks; nick_tmp != NULL; nick_tmp = nick_tmp->next) {
				NICK_REC *nick = nick_tmp->data;
				char *escaped_nick;
				char prefix[8];

				if (nicklist->len > 1) {
					g_string_append_c(nicklist, ',');
				}

				/* Build prefix string (@, +, etc) */
				prefix[0] = '\0';
				if (nick->op) {
					strcat(prefix, "@");
				}
				if (nick->halfop) {
					strcat(prefix, "%");
				}
				if (nick->voice) {
					strcat(prefix, "+");
				}

				escaped_nick = fe_web_escape_json(nick->nick);
				g_string_append_printf(nicklist,
				                       "{\"nick\":\"%s\",\"prefix\":\"%s\"}",
				                       escaped_nick, prefix);
				g_free(escaped_nick);
			}
			g_slist_free(nicks);
		}
		g_string_append_c(nicklist, ']');

		msg->text = g_string_free(nicklist, FALSE);
		fe_web_send_message(client, msg);
		fe_web_message_free(msg);

		/* Send activity status if channel has unread activity */
		window = window_find_item(SERVER(server), channel->name);
		if (window != NULL && window->active != NULL) {
			item = window->active;
			data_level = item->data_level > 0 ? item->data_level : window->data_level;

			if (data_level > 0) {
				/* Channel has unread activity - send ACTIVITY_UPDATE */
				msg = fe_web_message_new(WEB_MSG_ACTIVITY_UPDATE);
				msg->id = fe_web_generate_message_id();
				msg->server_tag = g_strdup(server->tag);
				msg->target = g_strdup(channel->name);
				msg->level = data_level;
				fe_web_send_message(client, msg);
				fe_web_message_free(msg);
			}
		}
	}
}

/* Dump current state to client */
void fe_web_dump_state(WEB_CLIENT_REC *client)
{
	IRC_SERVER_REC *server;
	GSList *tmp;
	extern GSList *servers;

	if (client == NULL) {
		return;
	}

	/* If wants all servers, dump all */
	if (client->wants_all_servers) {
		for (tmp = servers; tmp != NULL; tmp = tmp->next) {
			server = IRC_SERVER(tmp->data);
			if (server != NULL && server->connected) {
				fe_web_dump_server_state(client, server);
			}
		}
		return;
	}

	/* Dump specific server */
	server = client->server;
	if (server == NULL) {
		return;
	}

	fe_web_dump_server_state(client, server);
}
