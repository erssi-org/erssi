/*
 * sidepanels-signals.c : erssi
 *
 * Copyright (C) 2024-2025 erssi-org team
 * Lead Developer: Jerzy (kofany) Dąbrowski <https://github.com/kofany>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "module.h"
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/fe-text/mainwindows.h>
#include <irssi/src/fe-text/sidepanels.h>
#include <irssi/src/fe-text/sidepanels-signals.h>
#include <irssi/src/fe-text/sidepanels-render.h>
#include <irssi/src/fe-text/sidepanels-activity.h>
#include <irssi/src/fe-text/sidepanels-layout.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-text/module-formats.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <stdarg.h>
#include <stdlib.h>

/* External functions we need */
extern void sp_logf(const char *fmt, ...);
extern void update_left_selection_to_active(void);
extern int get_auto_create_separators(void);

/* Helper function to find server separator window */
static WINDOW_REC *find_server_separator_window(const char *server_tag)
{
	GSList *tmp;
	
	if (!server_tag)
		return NULL;
		
	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *win = tmp->data;
		const char *win_name = window_get_active_name(win);
		
		/* Check if this is a server separator window:
		 * - Has server tag but no active channel/query
		 * - Window name matches server tag */
		if (win->servertag && g_ascii_strcasecmp(win->servertag, server_tag) == 0 && 
		    !win->active && win_name && g_ascii_strcasecmp(win_name, server_tag) == 0) {
			return win;
		}
	}
	return NULL;
}

/* Initialize Notices window programmatically using direct functions */
void initialize_notices_window(void)
{
	WINDOW_REC *window1;
	
	sp_logf("AUTO-SEPARATOR: Initializing Notices window programmatically");
	
	/* Get window 1 - it should already exist */
	window1 = window_find_refnum(1);
	if (!window1) {
		sp_logf("AUTO-SEPARATOR: Window 1 not found - this should not happen");
		return;
	}
	
	sp_logf("AUTO-SEPARATOR: Setting up window 1 as Notices window using direct functions");
	
	/* Set window name */
	if (window1->name)
		g_free(window1->name);
	window1->name = g_strdup("Notices");
	
	/* Set level to NOTICES + client messages (help, errors, command output) */
	window_set_level(window1, MSGLEVEL_NOTICES | MSGLEVEL_CLIENTNOTICE | MSGLEVEL_CLIENTCRAP | MSGLEVEL_CLIENTERROR);
	
	/* Set immortal */
	window_set_immortal(window1, TRUE);
	
	/* Set servertag directly to "*" */
	if (window1->servertag)
		g_free(window1->servertag);
	window1->servertag = g_strdup("*");
	
	sp_logf("AUTO-SEPARATOR: Notices window initialization completed - name='%s', level=%d, servertag='%s', immortal=%s",
	        window1->name ? window1->name : "NULL",
	        window1->level,
	        window1->servertag ? window1->servertag : "NULL",
	        window1->immortal ? "yes" : "no");
}

/* Helper function to create server separator window */
static void create_server_separator_window(const char *server_tag)
{
	char *cmd;
	
	if (!server_tag)
		return;
	
	sp_logf("AUTO-SEPARATOR: Creating separator window for server '%s'", server_tag);
	
	/* Execute commands separately - irssi needs individual commands */
	
	/* 1. Create new hidden window */
	signal_emit("send command", 3, "/WINDOW NEW HIDE", NULL, NULL);
	
	/* 2. Set window name */
	cmd = g_strdup_printf("/WINDOW NAME %s", server_tag);
	sp_logf("AUTO-SEPARATOR: Executing: %s", cmd);
	signal_emit("send command", 3, cmd, NULL, NULL);
	g_free(cmd);
	
	/* 3. Set window level - ALL except client messages and notices (those go to Notices window) */
	signal_emit("send command", 3, "/WINDOW LEVEL ALL -NOTICES -CLIENTNOTICE -CLIENTCRAP -CLIENTERROR", NULL, NULL);
	
	/* 4. Set server sticky */
	cmd = g_strdup_printf("/WINDOW SERVER -sticky %s", server_tag);
	sp_logf("AUTO-SEPARATOR: Executing: %s", cmd);
	signal_emit("send command", 3, cmd, NULL, NULL);
	g_free(cmd);
	
	sp_logf("AUTO-SEPARATOR: Separator window creation completed for '%s'", server_tag);
}

/* Server connection handler - auto-create separator windows */
void sig_server_connected(SERVER_REC *server)
{
	const char *server_tag;
	
	if (!server || !get_auto_create_separators())
		return;
		
	server_tag = server->tag;
	if (!server_tag) {
		sp_logf("AUTO-SEPARATOR: Server has no tag, skipping");
		return;
	}
	
	sp_logf("AUTO-SEPARATOR: Server '%s' connected, checking for separator window", server_tag);
	
	if (!find_server_separator_window(server_tag)) {
		sp_logf("AUTO-SEPARATOR: No separator window found for '%s', creating one", server_tag);
		create_server_separator_window(server_tag);
	} else {
		sp_logf("AUTO-SEPARATOR: Separator window already exists for '%s'", server_tag);
	}
}

void sig_window_changed(WINDOW_REC *old, WINDOW_REC *new)
{
	(void) old;

	/* Reset priority when user opens/switches to window */
	if (new) {
		/* Debug: Window switching - tracks user navigation */
		// sp_logf("SIGNAL: window_changed from %d to %d '%s' (USER SWITCHED)",
		//        old ? old->refnum : -1, new->refnum, item_name);
		reset_window_priority(new);
	}

	update_left_selection_to_active();
	redraw_both_panels_only("window_changed"); /* Window change affects both activity (left) and nicklist (right) */
}

void sig_window_item_changed(WINDOW_REC *w, WI_ITEM_REC *item)
{
	(void) w;
	(void) item;
	redraw_both_panels_only("window_item_changed");
}

void sig_window_created(WINDOW_REC *window)
{
	(void) window;
	renumber_windows_by_position();
	redraw_both_panels_only("window_created"); /* Window creation affects both panels */
}

void sig_window_destroyed(WINDOW_REC *window)
{
	/* Clean up activity state for destroyed window */
	if (window && window_priorities) {
		g_hash_table_remove(window_priorities, window);
	}
	renumber_windows_by_position();
	redraw_both_panels_only("window_destroyed"); /* Window destruction affects both panels */
}

void sig_window_item_new(WI_ITEM_REC *item)
{
	(void) item;
	renumber_windows_by_position();
	redraw_both_panels_only("window_item_new");
}

void sig_query_created(QUERY_REC *query)
{
	(void) query;
	renumber_windows_by_position();
	redraw_both_panels_only("query_created"); /* Query creation affects both panels */
}

void sig_channel_joined(CHANNEL_REC *channel)
{
	(void) channel;
	renumber_windows_by_position();
	redraw_both_panels_only("channel_joined");
}

void sig_channel_sync(CHANNEL_REC *channel)
{
	(void) channel;
	redraw_right_panels_only("channel_sync");
}

void sig_channel_wholist(CHANNEL_REC *channel)
{
	(void) channel;
	redraw_right_panels_only("channel_wholist");
}

void sig_nicklist_new(CHANNEL_REC *ch, NICK_REC *nick)
{
	(void) ch;
	(void) nick;
	schedule_batched_redraw("nicklist_new");
}

void sig_nicklist_changed(CHANNEL_REC *channel, NICK_REC *nick, const char *old_nick)
{
	/* NOTE: Don't redraw here to avoid conflict with message_nick
	 *
	 * Signal flow for nick changes:
	 *   1. nicklist_changed fires PER CHANNEL (low-level IRC core)
	 *   2. message_nick fires PER SERVER (high-level UI event)
	 *
	 * message_nick handles both activity updates + redraw,
	 * so batching here would cause duplicate/conflicting redraws.
	 *
	 * See WEECHAT-ANALYSIS.md for details on proper batching architecture.
	 */
	(void) channel;
	(void) nick;
	(void) old_nick;
}

/* Wrapper functions for signals that don't provide event names */
void sig_nicklist_remove(void) { schedule_batched_redraw("nicklist_remove"); }
void sig_nicklist_gone_changed(void) { schedule_batched_redraw("nicklist_gone_changed"); }
void sig_nicklist_serverop_changed(void) { schedule_batched_redraw("nicklist_serverop_changed"); }
void sig_nicklist_host_changed(void) { schedule_batched_redraw("nicklist_host_changed"); }
void sig_nicklist_account_changed(void) { schedule_batched_redraw("nicklist_account_changed"); }
void sig_message_kick(void) { schedule_batched_redraw("message_kick"); }
void sig_message_own_nick(void) { schedule_batched_redraw("message_own_nick"); }

/* Event signal handlers for join/part/quit/nick - priority 1 events */
void sig_message_join(SERVER_REC *server, const char *channel, const char *nick,
                             const char *address, const char *account, const char *realname)
{
	WINDOW_REC *window = window_find_item(server, channel);
	if (window) {
		/* Debug: Join events - tracks channel join activity */
		sp_logf("SIGNAL: message_join %s on %s (Window %d) active_win=%d", nick, channel,
		        window->refnum, active_win ? active_win->refnum : -1);
		handle_new_activity(window, DATA_LEVEL_EVENT);
	}
	schedule_batched_redraw("message_join"); /* Join affects both activity (left) and nicklist (right) */
}

void sig_message_part(SERVER_REC *server, const char *channel, const char *nick,
                             const char *address, const char *reason)
{
	WINDOW_REC *window = window_find_item(server, channel);
	if (window) {
		/* Debug: Part events - tracks channel part activity */
		sp_logf("SIGNAL: message_part %s from %s (Window %d) active_win=%d", nick, channel,
		        window->refnum, active_win ? active_win->refnum : -1);
		handle_new_activity(window, DATA_LEVEL_EVENT);
	}
	schedule_batched_redraw("message_part"); /* Part affects both activity (left) and nicklist (right) */
}

void sig_message_quit(SERVER_REC *server, const char *nick, const char *address,
                             const char *reason)
{
	/* Handle quit for all windows where this nick was present */
	GSList *tmp;
	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *window = tmp->data;
		if (window->active && window->active->server == server) {
			handle_new_activity(window, DATA_LEVEL_EVENT);
		}
	}
	schedule_batched_redraw("message_quit"); /* Quit affects activity (left) and nicklists (right) across channels */
}

void sig_message_nick(SERVER_REC *server, const char *newnick, const char *oldnick,
                             const char *address)
{
	/* Handle nick change for all windows on this server */
	GSList *tmp;
	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *window = tmp->data;
		if (window->active && window->active->server == server) {
			handle_new_activity(window, DATA_LEVEL_EVENT);
		}
	}
	schedule_batched_redraw("message_nick"); /* Nick change affects activity (left) and nicklists (right) */
}

void sig_message_kick_own(SERVER_REC *server, const char *channel, const char *nick,
                         const char *kicker, const char *address, const char *reason)
{
	WINDOW_REC *window;
	GSList *tmp;
	
	/* Check if we were kicked (our nick matches kicked nick) */
	if (!server || !channel || !nick || !server->nick)
		return;
		
	if (g_ascii_strcasecmp(nick, server->nick) != 0)
		return; /* Not our nick, ignore */
		
	sp_logf("KICK: We were kicked from %s by %s (reason: %s)", channel, kicker ? kicker : "unknown", reason ? reason : "no reason");
	
	/* Find window for this channel by searching all windows */
	window = NULL;
	for (tmp = windows; tmp; tmp = tmp->next) {
		WINDOW_REC *win = tmp->data;
		if (win->active && win->active->server == server && win->active->name &&
		    g_ascii_strcasecmp(win->active->name, channel) == 0) {
			window = win;
			break;
		}
	}
	
	if (!window) {
		sp_logf("KICK: Could not find window for channel %s", channel);
		return;
	}
	
	sp_logf("KICK: Found window %d for channel %s", window->refnum, channel);
	
	/* Preserve channel name in window name if not already set */
	if (!window->name || strlen(window->name) == 0) {
		if (window->name)
			g_free(window->name);
		window->name = g_strdup(channel);
		sp_logf("KICK: Set window %d name to '%s'", window->refnum, channel);
	}
	
	/* Ensure window has correct servertag for sorting */
	if (!window->servertag && server->tag) {
		if (window->servertag)
			g_free(window->servertag);
		window->servertag = g_strdup(server->tag);
		sp_logf("KICK: Set window %d servertag to '%s'", window->refnum, server->tag);
	}
	
	/* Set highest priority (4) to highlight this in theme */
	handle_new_activity(window, MSGLEVEL_MSGS); /* Use MSGS level for highest priority (4) */

	sp_logf("KICK: Set window %d priority to maximum for kick from %s", window->refnum, channel);

	/* Redraw panels to show the change */
	schedule_batched_redraw("message_kick_own");
}

void sig_nick_mode_filter(CHANNEL_REC *channel, NICK_REC *nick,
					  const char *setby, const char *modestr, const char *typestr)
{
	/* Redraw for all nick prefixes: ~ & @ % + (owner, admin, op, halfop, voice) */
	(void) channel;
	(void) nick;
	(void) setby;
	(void) typestr;

	if (modestr && (strchr(modestr, '~') || strchr(modestr, '&') ||
	                strchr(modestr, '@') || strchr(modestr, '%') ||
	                strchr(modestr, '+'))) {
		redraw_right_panels_only("nick_mode_changed");
	}
}

void sig_mainwindow_resized(MAIN_WINDOW_REC *mw)
{
	redraw_one(mw);
}

void sidepanels_signals_register(void)
{
	signal_add("mainwindow resized", (SIGNAL_FUNC) sig_mainwindow_resized);
	signal_add("server connected", (SIGNAL_FUNC) sig_server_connected);
	signal_add("window changed", (SIGNAL_FUNC) sig_window_changed);
	signal_add("window item changed", (SIGNAL_FUNC) sig_window_item_changed);
	signal_add("window created", (SIGNAL_FUNC) sig_window_created);
	signal_add("window destroyed", (SIGNAL_FUNC) sig_window_destroyed);
	signal_add("window item new", (SIGNAL_FUNC) sig_window_item_new);
	signal_add("query created", (SIGNAL_FUNC) sig_query_created);
	signal_add("channel joined", (SIGNAL_FUNC) sig_channel_joined);
	signal_add("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
	signal_add("nicklist new", (SIGNAL_FUNC) sig_nicklist_new);
	signal_add("nicklist remove", (SIGNAL_FUNC) sig_nicklist_remove);
	signal_add("nicklist gone changed", (SIGNAL_FUNC) sig_nicklist_gone_changed);
	signal_add("nicklist serverop changed", (SIGNAL_FUNC) sig_nicklist_serverop_changed);
	signal_add("nicklist host changed", (SIGNAL_FUNC) sig_nicklist_host_changed);
	signal_add("nicklist account changed", (SIGNAL_FUNC) sig_nicklist_account_changed);
	/* Message events for live nicklist updates */
	signal_add("message join", (SIGNAL_FUNC) sig_message_join);
	signal_add("message part", (SIGNAL_FUNC) sig_message_part);
	signal_add("message quit", (SIGNAL_FUNC) sig_message_quit);
	signal_add("message kick", (SIGNAL_FUNC) sig_message_kick_own);
	signal_add("message nick", (SIGNAL_FUNC) sig_message_nick);
	signal_add("message own_nick", (SIGNAL_FUNC) sig_message_own_nick);
	/* Channel state changes */
	signal_add("channel created", (SIGNAL_FUNC) redraw_both_panels_only); /* Affects both window list and nicklist */
	signal_add("channel destroyed", (SIGNAL_FUNC) redraw_both_panels_only); /* Affects both window list and nicklist */
	signal_add("channel topic changed", (SIGNAL_FUNC) redraw_left_panels_only);
	signal_add("channel sync", (SIGNAL_FUNC) sig_channel_sync);
	signal_add("channel wholist", (SIGNAL_FUNC) sig_channel_wholist);
	/* Mode changes affecting nicklist */
	signal_add("nick mode changed", (SIGNAL_FUNC) sig_nick_mode_filter);
	/* Live activity notifications */
	signal_add("print text", (SIGNAL_FUNC) sig_print_text);
}

void sidepanels_signals_unregister(void)
{
	signal_remove("mainwindow resized", (SIGNAL_FUNC) sig_mainwindow_resized);
	signal_remove("server connected", (SIGNAL_FUNC) sig_server_connected);
	signal_remove("window changed", (SIGNAL_FUNC) sig_window_changed);
	signal_remove("window item changed", (SIGNAL_FUNC) sig_window_item_changed);
	signal_remove("window created", (SIGNAL_FUNC) sig_window_created);
	signal_remove("window destroyed", (SIGNAL_FUNC) sig_window_destroyed);
	signal_remove("window item new", (SIGNAL_FUNC) sig_window_item_new);
	signal_remove("query created", (SIGNAL_FUNC) sig_query_created);
	signal_remove("channel joined", (SIGNAL_FUNC) sig_channel_joined);
	signal_remove("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
	signal_remove("nicklist new", (SIGNAL_FUNC) sig_nicklist_new);
	signal_remove("nicklist remove", (SIGNAL_FUNC) sig_nicklist_remove);
	signal_remove("nicklist gone changed", (SIGNAL_FUNC) sig_nicklist_gone_changed);
	signal_remove("nicklist serverop changed", (SIGNAL_FUNC) sig_nicklist_serverop_changed);
	signal_remove("nicklist host changed", (SIGNAL_FUNC) sig_nicklist_host_changed);
	signal_remove("nicklist account changed", (SIGNAL_FUNC) sig_nicklist_account_changed);
	/* Message events for live nicklist updates */
	signal_remove("message join", (SIGNAL_FUNC) sig_message_join);
	signal_remove("message part", (SIGNAL_FUNC) sig_message_part);
	signal_remove("message quit", (SIGNAL_FUNC) sig_message_quit);
	signal_remove("message kick", (SIGNAL_FUNC) sig_message_kick_own);
	signal_remove("message nick", (SIGNAL_FUNC) sig_message_nick);
	signal_remove("message own_nick", (SIGNAL_FUNC) sig_message_own_nick);
	/* Channel state changes */
	signal_remove("channel created", (SIGNAL_FUNC) redraw_both_panels_only);
	signal_remove("channel destroyed", (SIGNAL_FUNC) redraw_both_panels_only);
	signal_remove("channel topic changed", (SIGNAL_FUNC) redraw_left_panels_only);
	signal_remove("channel sync", (SIGNAL_FUNC) sig_channel_sync);
	signal_remove("channel wholist", (SIGNAL_FUNC) sig_channel_wholist);
	/* Mode changes affecting nicklist */
	signal_remove("nick mode changed", (SIGNAL_FUNC) sig_nick_mode_filter);
	/* Live activity notifications */
	signal_remove("print text", (SIGNAL_FUNC) sig_print_text);
}

void sidepanels_signals_init(void)
{
	/* Nothing to initialize */
}

void sidepanels_signals_deinit(void)
{
	/* Nothing to clean up */
}
