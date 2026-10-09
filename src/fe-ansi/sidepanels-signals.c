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
#include <irssi/src/fe-ansi/mainwindows.h>
#include <irssi/src/fe-ansi/sidepanels.h>
#include <irssi/src/fe-ansi/sidepanels-signals.h>
#include <irssi/src/fe-ansi/sidepanels-render.h>
#include <irssi/src/fe-ansi/sidepanels-activity.h>
#include <irssi/src/fe-ansi/sidepanels-layout.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-ansi/term.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-ansi/module-formats.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-ansi/gui-printtext.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-ansi/resize-debug.h>
#include <stdarg.h>
#include <stdlib.h>

/* External functions we need */
extern void sp_logf(const char *fmt, ...);
extern void update_left_selection_to_active(void);
extern int get_auto_create_separators(void);
extern SP_MAINWIN_CTX *get_ctx(MAIN_WINDOW_REC *mw, gboolean create);

/* Helper function to find server separator window */
static WINDOW_REC *find_server_separator_window(const char *server_tag)
{
	GSList *tmp;
	
	if (!server_tag)
		return NULL;
		
	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *win = tmp->data;

		/* A server separator window: the window itself is named after the
		 * server tag and is sticky to that server. Its items do not matter -
		 * a script may keep one there (e.g. a query); judging
		 * by the active item's name created a second network window on every
		 * reconnect. */
		if (win->servertag != NULL && g_ascii_strcasecmp(win->servertag, server_tag) == 0 &&
		    win->name != NULL && g_ascii_strcasecmp(win->name, server_tag) == 0) {
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
	
	/* Pick the Notices window without clobbering anyone else's window.
	 * Scripts loaded at startup (e.g. a "Mentions" or "hilight" window)
	 * may already sit at refnum 1 after sidepanel sorting; renaming it
	 * would silently destroy that window's identity. Order:
	 *   1. an existing window already named "Notices",
	 *   2. the startup window wherever sorting put it: lowest refnum that is
	 *      unnamed or the irssi status window "(status)" and holds no
	 *      channel/query,
	 *   3. a new window - sidepanel sorting moves "Notices" to the top. */
	window1 = window_find_name("Notices");
	if (window1 == NULL) {
		GSList *tmp;

		for (tmp = windows; tmp != NULL; tmp = tmp->next) {
			WINDOW_REC *win = tmp->data;
			gboolean unnamed = win->name == NULL || *win->name == '\0' ||
			                   g_strcmp0(win->name, "(status)") == 0;

			if (unnamed && win->items == NULL &&
			    (window1 == NULL || win->refnum < window1->refnum))
				window1 = win;
		}
	}
	if (window1 == NULL) {
		sp_logf("AUTO-SEPARATOR: no free startup window - creating a separate Notices window");
		window1 = window_create(NULL, TRUE);
	}
	if (!window1) {
		sp_logf("AUTO-SEPARATOR: no window available for Notices");
		return;
	}

	sp_logf("AUTO-SEPARATOR: Setting up window %d as Notices window", window1->refnum);

	/* Set window name */
	if (window1->name == NULL || g_strcmp0(window1->name, "Notices") != 0)
		window_set_name(window1, "Notices");
	
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

/* A window named after the network may become the network window when it
 * is empty or already was that network's window */
static gboolean network_window_ok(WINDOW_REC *window, const char *server_tag)
{
	return window->items == NULL ||
	       (window->servertag != NULL && g_ascii_strcasecmp(window->servertag, server_tag) == 0);
}

static WINDOW_REC *find_unnamed_network_window(const char *server_tag)
{
	GSList *tmp;

	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *rec = tmp->data;

		if ((rec->name == NULL || *rec->name == '\0') && rec->items == NULL &&
		    rec->servertag != NULL && g_ascii_strcasecmp(rec->servertag, server_tag) == 0)
			return rec;
	}
	return NULL;
}

static WINDOW_REC *create_network_window(const char *name)
{
	WINDOW_REC *window;

	/* the same as /WINDOW NEW HIDE */
	signal_emit("gui window create override", 1, GINT_TO_POINTER(MAIN_WINDOW_TYPE_HIDDEN));
	window = window_create(NULL, FALSE);
	if (name != NULL)
		window_set_name(window, name);
	return window;
}

/* Helper function to create server separator window.
   Through the window API, not /WINDOW commands: each of those printed a
   line into the new window at every connect ("Window level is now ...",
   "Window's server set sticky", "Changed to ... server"). */
static void create_server_separator_window(const char *server_tag)
{
	SERVER_REC *server;
	WINDOW_REC *window;

	if (!server_tag)
		return;

	server = server_find_tag(server_tag);
	if (server == NULL)
		return;

	sp_logf("AUTO-SEPARATOR: Creating separator window for server '%s'", server_tag);

	/* A window already named after the network (e.g. it lost its sticky
	 * server) becomes the network window again - window names are unique,
	 * and a new unnamed window would never be found at the next connect. */
	window = window_find_name(server_tag);
	if (window != NULL && !network_window_ok(window, server_tag)) {
		/* the user's own window that only shares the name (it has
		 * channels or queries of something else): leave it alone and
		 * use an unnamed network window, found by its server tag */
		window = find_unnamed_network_window(server_tag);
		if (window == NULL)
			window = create_network_window(NULL);
	} else if (window == NULL) {
		window = create_network_window(server_tag);
	}
	window_change_server(window, server);
	/* everything except client messages and notices (the Notices window has those) */
	window_set_level(window, MSGLEVEL_ALL & ~(MSGLEVEL_NOTICES | MSGLEVEL_CLIENTNOTICE |
	                                          MSGLEVEL_CLIENTCRAP | MSGLEVEL_CLIENTERROR));
	g_free(window->servertag);
	window->servertag = g_strdup(server->tag);

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
	GSList *t;

	(void) old;

	/* Reset priority when user opens/switches to window */
	if (new) {
		/* Debug: Window switching - tracks user navigation */
		// sp_logf("SIGNAL: window_changed from %d to %d '%s' (USER SWITCHED)",
		//        old ? old->refnum : -1, new->refnum, item_name);
		reset_window_priority(new);
	}

	/* Clear right panel cache for all main windows when switching windows.
	 * The cache doesn't track which channel it belongs to, so switching
	 * between channels with same dimensions wouldn't trigger a redraw.
	 * This ensures the nicklist is properly refreshed on window change. */
	for (t = mainwindows; t; t = t->next) {
		MAIN_WINDOW_REC *mw = t->data;
		SP_MAINWIN_CTX *ctx;

		if (!mw)
			continue;
		ctx = get_ctx(mw, FALSE);
		if (ctx && ctx->right_cache) {
			sp_cache_clear(ctx->right_cache);
		}
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

/* TRUE if one of the window's channels has the nick, or it is a query
 * with that nick: a quit or nick change marks only those windows, not
 * every window of the network. */
static gboolean window_has_nick(WINDOW_REC *window, SERVER_REC *server,
                                const char *nick, const char *other_nick)
{
	GSList *tmp;

	for (tmp = window->items; tmp != NULL; tmp = tmp->next) {
		WI_ITEM_REC *item = tmp->data;

		if (item->server != server)
			continue;
		if (IS_CHANNEL(item) && nicklist_find(CHANNEL(item), nick) != NULL)
			return TRUE;
		if (IS_QUERY(item) &&
		    (g_ascii_strcasecmp(item->visible_name, nick) == 0 ||
		     (other_nick != NULL && g_ascii_strcasecmp(item->visible_name, other_nick) == 0)))
			return TRUE;
	}
	return FALSE;
}

void sig_message_quit(SERVER_REC *server, const char *nick, const char *address,
                             const char *reason)
{
	/* Handle quit for all windows where this nick was present (the nick
	 * is still in the nick lists when "message quit" is sent) */
	GSList *tmp;
	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *window = tmp->data;
		if (window_has_nick(window, server, nick, NULL)) {
			handle_new_activity(window, DATA_LEVEL_EVENT);
		}
	}
	schedule_batched_redraw("message_quit"); /* Quit affects activity (left) and nicklists (right) across channels */
}

void sig_message_nick(SERVER_REC *server, const char *newnick, const char *oldnick,
                             const char *address)
{
	/* Handle nick change for the windows where the nick is (the nick
	 * lists already have the new nick, a query may still have the old) */
	GSList *tmp;
	for (tmp = windows; tmp != NULL; tmp = tmp->next) {
		WINDOW_REC *window = tmp->data;
		if (window_has_nick(window, server, newnick, oldnick)) {
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
	SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
	int mw_id = mw ? (int)(long)mw : -1;

	resize_debug_log("SP_MW_RESIZED", "mainwindow %p resized, ctx=%p", (void*)mw, (void*)ctx);

	if (ctx) {
		/* CRITICAL: Clear both caches on resize to force full redraw.
		 * Without this, differential rendering would skip unchanged lines,
		 * but after resize the line positions have changed! */
		if (ctx->left_cache) {
			sp_cache_clear(ctx->left_cache);
			resize_debug_cache("CLEAR", "left", mw_id);
		}
		if (ctx->right_cache) {
			sp_cache_clear(ctx->right_cache);
			resize_debug_cache("CLEAR", "right", mw_id);
		}

		/* Redraw panel CONTENTS only - do NOT call position_tw here!
		 * position_tw emits "mainwindow resized" when destroying panels,
		 * which would cause infinite recursion if we called it from here. */
		resize_debug_redraw("draw_contents_only", mw_id);
		if (ctx->left_tw && ctx->left_h > 0) {
			draw_left_contents(mw, ctx);
		}
		if (ctx->right_tw && ctx->right_h > 0) {
			draw_right_contents(mw, ctx);
		}
		draw_main_window_borders(mw);
		irssi_set_dirty();
	}

	resize_debug_log("SP_MW_RESIZED", "contents redraw complete");
}

/* Clear ALL sidepanel caches when terminal is resized.
 * This is a safety net in addition to per-mainwindow cache clearing. */
static void sig_terminal_resized(void)
{
	GSList *tmp;
	int count = 0;

	resize_debug_log("SP_TERM_RESIZED", "'terminal resized' signal received, term_width=%d, term_height=%d",
	                 term_width, term_height);

	for (tmp = mainwindows; tmp != NULL; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
		int mw_id = mw ? (int)(long)mw : -1;
		if (ctx) {
			if (ctx->left_cache) {
				sp_cache_clear(ctx->left_cache);
				resize_debug_cache("CLEAR_ALL", "left", mw_id);
			}
			if (ctx->right_cache) {
				sp_cache_clear(ctx->right_cache);
				resize_debug_cache("CLEAR_ALL", "right", mw_id);
			}
			count++;
		}
	}

	resize_debug_log("SP_TERM_RESIZED", "cleared caches for %d mainwindows", count);

	/* Recalculate sidepanel geometry for all mainwindows.
	 * This is critical after resize - setup_ctx_for recalculates widths
	 * and position_tw handles collapsed mode if terminal is too small. */
	resize_debug_log("SP_TERM_RESIZED", "recalculating sidepanel geometry...");
	for (tmp = mainwindows; tmp != NULL; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		setup_ctx_for(mw);
	}

	/* Force full redraw of all sidepanels */
	resize_debug_redraw("redraw_all", -1);
	redraw_all();
	resize_debug_log("SP_TERM_RESIZED", "redraw_all complete");
}

void sidepanels_signals_register(void)
{
	signal_add("terminal resized", (SIGNAL_FUNC) sig_terminal_resized);
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
	/* Network name changes (ISUPPORT NETWORK) */
	signal_add("server network changed", (SIGNAL_FUNC) redraw_left_panels_only);
}

void sidepanels_signals_unregister(void)
{
	signal_remove("terminal resized", (SIGNAL_FUNC) sig_terminal_resized);
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
	/* Network name changes (ISUPPORT NETWORK) */
	signal_remove("server network changed", (SIGNAL_FUNC) redraw_left_panels_only);
}

void sidepanels_signals_init(void)
{
	/* Nothing to initialize */
}

void sidepanels_signals_deinit(void)
{
	/* Nothing to clean up */
}
