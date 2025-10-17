/*
 * sidepanels-activity.c : erssi
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
#include <irssi/src/fe-text/sidepanels-activity.h>
#include <irssi/src/fe-text/sidepanels-render.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-common/core/windows-layout.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-text/module-formats.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <stdarg.h>
#include <stdlib.h>

/* Window Priority State - Global tracking */
GHashTable *window_priorities = NULL;

/* External functions we need */
extern void sp_logf(const char *fmt, ...);

/* Forward declarations */
static int window_sort_compare(gconstpointer a, gconstpointer b);

void handle_new_activity(WINDOW_REC *window, int data_level)
{
	window_priority_state *state;
	int new_priority = 0;

	if (!window || !window_priorities)
		return;

	/* Map data levels to priority levels - restored from v0.0.4 logic */
	if (data_level == DATA_LEVEL_EVENT) {
		new_priority = 1; /* Events (join/part/quit/nick) */
	} else if (data_level == DATA_LEVEL_HILIGHT) {
		new_priority = 4; /* PRIORITY 4: Nick mentions (magenta) */
	} else if (data_level == DATA_LEVEL_MSG) {
		new_priority = 4; /* PRIORITY 4: Query messages (magenta) */
	} else if (data_level == DATA_LEVEL_TEXT) {
		new_priority = 3; /* PRIORITY 3: Channel activity (yellow) */
	} else {
		new_priority = 0; /* No activity */
	}

	/* Get or create state for this window */
	state = g_hash_table_lookup(window_priorities, window);
	if (!state) {
		state = g_new0(window_priority_state, 1);
		state->window = window;
		state->current_priority = 0;
		g_hash_table_insert(window_priorities, window, state);
	}

	/* Update priority if new level is higher */
	if (new_priority > state->current_priority) {
		sp_logf("ACTIVITY: Window %d priority %d -> %d (data_level=%d)", 
		        window->refnum, state->current_priority, new_priority, data_level);
		state->current_priority = new_priority;
	}
}

GSList *build_sorted_window_list(void)
{
	GSList *w, *sort_list = NULL;

	/* Create sorted list of all windows according to user rules */
	for (w = windows; w; w = w->next) {
		WINDOW_REC *win = w->data;
		WINDOW_SORT_REC *sort_rec = g_new0(WINDOW_SORT_REC, 1);
		const char *win_name;

		sort_rec->win = win;
		/* Try to find server for this window */
		if (win->active && win->active->server) {
			sort_rec->server = win->active->server;
		} else {
			/* For server status windows, try to find server by servertag */
			sort_rec->server = win->servertag ? server_find_tag(win->servertag) : NULL;
		}

		win_name = window_get_active_name(win);

		/* Determine sort group and key according to user rules */
		if (win_name && g_ascii_strcasecmp(win_name, "Notices") == 0) {
			/* 1. Notices window - always first */
			sort_rec->sort_group = 0;
			sort_rec->sort_key = g_strdup("Notices");
		} else if (sort_rec->server && !win->active) {
			/* Check if this is a kicked channel (has server, no active, but name starts with #) */
			if (win_name && win_name[0] == '#') {
				/* Kicked channel - treat as channel for sorting */
				sort_rec->sort_group = 2;
				sort_rec->sort_key = g_strdup(win_name);
			} else {
				/* 2. Server status windows - no active channel/query */
				const char *server_name;
				sort_rec->sort_group = 1;
				/* Use server tag as display name instead of chatnet/address */
				server_name = sort_rec->server->tag ? sort_rec->server->tag : "server";
				sort_rec->sort_key = g_strdup(server_name);
			}
		} else if (win->active && win->active->server) {
			/* Windows with active channel/query items */
			WI_ITEM_REC *item = win->active;
			sort_rec->server = item->server; /* Use server from active item */
			if (IS_CHANNEL(item)) {
				/* 3. Channels - alphabetically within server */
				sort_rec->sort_group = 2;
				sort_rec->sort_key = g_strdup(item->name ? item->name : "channel");
			} else if (IS_QUERY(item)) {
				/* 4. Queries - alphabetically within server */
				sort_rec->sort_group = 3;
				sort_rec->sort_key = g_strdup(item->name ? item->name : "query");
			} else {
				/* Other server-related items */
				sort_rec->sort_group = 2;
				sort_rec->sort_key = g_strdup(item->name ? item->name : "item");
			}
		} else {
			/* Orphaned windows without server */
			if (win_name && strlen(win_name) > 0) {
				/* Named orphan */
				sort_rec->sort_group = 4;
				sort_rec->sort_key = g_strdup(win_name);
			} else {
				/* Unnamed orphan */
				sort_rec->sort_group = 5;
				sort_rec->sort_key = g_strdup_printf("window%d", win->refnum);
			}
		}

		sort_list = g_slist_prepend(sort_list, sort_rec);
	}

	/* Sort the list */
	sort_list = g_slist_sort(sort_list, (GCompareFunc) window_sort_compare);

	return sort_list;
}

void free_sorted_window_list(GSList *sort_list)
{
	GSList *s;
	for (s = sort_list; s; s = s->next) {
		WINDOW_SORT_REC *sort_rec = s->data;
		g_free(sort_rec->sort_key);
		g_free(sort_rec);
	}
	g_slist_free(sort_list);
}

/* Window comparison function for sorting */
static int window_sort_compare(gconstpointer a, gconstpointer b)
{
	WINDOW_SORT_REC *w1 = (WINDOW_SORT_REC *) a;
	WINDOW_SORT_REC *w2 = (WINDOW_SORT_REC *) b;
	const char *server_tag1, *server_tag2;
	int server_cmp;

	/* 1. Notices always comes first */
	if (w1->sort_group == 0)
		return -1; /* w1 is Notices */
	if (w2->sort_group == 0)
		return 1; /* w2 is Notices */

	/* 2. Sort by server (alphabetically) */
	if (w1->server && w2->server) {
		server_tag1 = w1->server->tag ? w1->server->tag : "";
		server_tag2 = w2->server->tag ? w2->server->tag : "";
		server_cmp = g_ascii_strcasecmp(server_tag1, server_tag2);
		if (server_cmp != 0)
			return server_cmp;
	} else if (w1->server && !w2->server) {
		return -1; /* w1 has server, w2 doesn't */
	} else if (!w1->server && w2->server) {
		return 1; /* w2 has server, w1 doesn't */
	}

	/* 3. Sort by group (server status < channels < queries < orphans) */
	if (w1->sort_group != w2->sort_group)
		return w1->sort_group - w2->sort_group;

	/* 4. Within same group and server, sort alphabetically by sort_key */
	return g_ascii_strcasecmp(w1->sort_key ? w1->sort_key : "",
	                          w2->sort_key ? w2->sort_key : "");
}

int ci_nick_compare(gconstpointer a, gconstpointer b)
{
	NICK_REC *nick1 = (NICK_REC *) a;
	NICK_REC *nick2 = (NICK_REC *) b;
	return g_ascii_strcasecmp(nick1->nick, nick2->nick);
}

/* Get or create window priority state - STATIC like in original */
static window_priority_state *get_window_priority_state(WINDOW_REC *window)
{
	window_priority_state *state;

	if (!window_priorities)
		window_priorities =
		    g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

	state = g_hash_table_lookup(window_priorities, window);
	if (!state) {
		state = g_new0(window_priority_state, 1);
		state->window = window;
		state->current_priority = 0;
		g_hash_table_insert(window_priorities, window, state);
	}
	return state;
}

/* Get current priority for window - public interface */
int get_window_current_priority(WINDOW_REC *win)
{
	window_priority_state *state = get_window_priority_state(win);
	return state->current_priority;
}

/* Reset window priority when user switches to it */
void reset_window_priority(WINDOW_REC *win)
{
	window_priority_state *state;

	if (!win || !window_priorities)
		return;

	state = g_hash_table_lookup(window_priorities, win);
	if (state) {
		sp_logf("ACTIVITY: Resetting priority for window %d", win->refnum);
		state->current_priority = 0;
	}
}

/* Print text signal handler - get accurate data levels */
void sig_print_text(TEXT_DEST_REC *dest, const char *msg)
{
	WINDOW_REC *window;
	WI_ITEM_REC *item;
	int data_level;
	const char *item_name, *item_type;

	if (!dest || !dest->window)
		return;

	window = dest->window;

	/* Skip if this is the active window */
	if (window == active_win)
		return;

	/* Filter out message levels that are handled by dedicated signals to avoid duplicates */
	if (dest->level & (MSGLEVEL_JOINS | MSGLEVEL_PARTS | MSGLEVEL_QUITS | MSGLEVEL_KICKS |
	                   MSGLEVEL_MODES | MSGLEVEL_TOPICS | MSGLEVEL_NICKS | MSGLEVEL_ACTIONS)) {
		/* These are handled by message_join/part/quit/nick signals - skip to avoid
		 * duplicates */
		return;
	}

	/* Convert MSGLEVEL to DATA_LEVEL - restored from v0.0.4 logic */
	if (dest->level & MSGLEVEL_HILIGHT) {
		data_level = DATA_LEVEL_HILIGHT;
	} else if (dest->level & (MSGLEVEL_MSGS | MSGLEVEL_NOTICES)) {
		/* Check if this is a query or channel */
		item = window->active;
		if (item && IS_QUERY(item)) {
			data_level = DATA_LEVEL_MSG; /* Query messages */
		} else {
			data_level = DATA_LEVEL_TEXT; /* Channel messages */
		}
	} else if (dest->level & MSGLEVEL_PUBLIC) {
		data_level = DATA_LEVEL_TEXT; /* Public channel text */
	} else {
		data_level = DATA_LEVEL_TEXT; /* Default to text level */
	}

	/* Get item information for debugging */
	item = window->active;
	item_name = item ? item->visible_name : "none";
	item_type = item ? (IS_CHANNEL(item) ? "channel" : IS_QUERY(item) ? "query" : "other") : "none";

	/* Debug: Print text events - tracks all message activity */
	sp_logf("SIGNAL: print_text level=%d window=%d item='%s' type=%s active_win=%d msg='%.50s'",
	        data_level, window->refnum, item_name, item_type,
	        active_win ? active_win->refnum : -1, msg ? msg : "(null)");

	/* Handle the activity */
	handle_new_activity(window, data_level);

	/* Schedule redraw for left panel (activity changes) */
	redraw_left_panels_only("print_text");
}

void sidepanels_activity_init(void)
{
	window_priorities = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
}

void sidepanels_activity_deinit(void)
{
	if (window_priorities) {
		g_hash_table_destroy(window_priorities);
		window_priorities = NULL;
	}
}
