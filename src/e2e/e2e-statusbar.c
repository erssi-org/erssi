/*
 e2e-statusbar.c : the statusbar item "e2e"

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The E2E state of the active window (of the split window, in a bar of
 that type): "e2e off", "E2E", "E2E waiting for keys", "E2E blocked",
 "E2E keyring error" and "N to accept" - the e2e_sb_* formats of the
 current theme. Nothing in a window that is no channel or query, and
 nothing while rpe2e.pl is loaded (the module is inactive then; the
 script's state is not the module's to show).

 The item draws from a cache: "<tag>\n<name>" -> state and pending count
 of every channel and query in a window. It is filled at refresh time
 (e2e_statusbar_redraw), never while drawing, and the keyring is parsed
 only when something may have changed: its file (device, inode, size,
 mtime, ctime - another process, rpe2e.pl, writes it too), the E2E state
 ("e2e state changed": the module saved the keyring, rpe2e.pl came or
 went, our own address changed), what a window's state is read from
 besides the keyring (a query's address, a nick, a join, the server's
 CASEMAPPING, the networks in use: e2e_legacy_ok()), or an item not cached
 yet. A file changed in the second it was read is read again (its stat
 may not show the change). A window switch with nothing
 changed only looks at the file (stat). The parsed keyring holds secret
 keys: it is freed (and wiped) right after the refresh, never kept.
*/

#include "module.h"
#include "e2e-core.h"
#include "e2e-formats.h"
#include "e2e-keyring.h"
#include "e2e-statusbar.h"

#include <irssi/src/core/channels.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-ansi/statusbar.h>

#include <sys/stat.h>

typedef struct {
	gboolean shown;		/* a channel or query: the item shows it */
	E2E_WIN_STATE state;
	guint pending;
} SB_ENTRY;

static GHashTable *sb_cache;	/* "<tag>\n<name>" -> SB_ENTRY */
/* the cache was filled from the file as sb_stat says (sb_have_stat:
   it existed) and nothing changed since */
static gboolean sb_valid;
static gboolean sb_have_stat;
static struct stat sb_stat;
/* when the cache was filled (seconds): a file changed in that second may
   still have the stat seen then (git's racy timestamps) */
static time_t sb_read_at;
/* e2e_legacy_ok() then: an old entry of no network is BLOCKED without it */
static gboolean sb_legacy_ok;
static gboolean sb_script;	/* rpe2e.pl loaded at the last refresh */
static gboolean sb_gone;	/* the module is being unloaded */
static guint sb_idle_id;

static char *sb_key(WI_ITEM_REC *item)
{
	if (item == NULL || item->server == NULL || item->server->tag == NULL || item->name == NULL)
		return NULL;
	return g_strconcat(item->server->tag, "\n", item->name, NULL);
}

/* every item of every window has its entry */
static gboolean all_cached(void)
{
	GSList *w, *i;

	for (w = windows; w != NULL; w = w->next) {
		for (i = ((WINDOW_REC *) w->data)->items; i != NULL; i = i->next) {
			char *key = sb_key(i->data);
			gboolean found = key == NULL || g_hash_table_contains(sb_cache, key);

			g_free(key);
			if (!found)
				return FALSE;
		}
	}
	return TRUE;
}

static void fill(const E2E_JSON *kr, gboolean kr_ok)
{
	GSList *w, *i;

	g_hash_table_remove_all(sb_cache);
	for (w = windows; w != NULL; w = w->next) {
		for (i = ((WINDOW_REC *) w->data)->items; i != NULL; i = i->next) {
			E2E_WIN_INFO info;
			SB_ENTRY *e;
			char *key = sb_key(i->data);

			if (key == NULL)
				continue;
			e = g_new0(SB_ENTRY, 1);
			if (e2e_window_info_with(kr, kr_ok, i->data, &info)) {
				e->shown = TRUE;
				e->state = info.state;
				e->pending = info.pending != NULL ? info.pending->len : 0;
			}
			e2e_window_info_clear(&info);
			g_hash_table_replace(sb_cache, key, e);
		}
	}
}

static void refresh(void)
{
	const char *file = e2e_keyring_file();
	E2E_LOAD_STATUS status;
	E2E_JSON *kr;
	struct stat st;
	gboolean have, legacy_ok;
	char *error = NULL;

	sb_script = e2e_script_loaded();
	/* nothing is shown: nothing is read */
	if (sb_script || file == NULL)
		return;
	/* the file first: a change while it is read is seen next time */
	have = stat(file, &st) == 0;
	legacy_ok = e2e_legacy_ok();
	if (sb_valid && have == sb_have_stat && legacy_ok == sb_legacy_ok &&
	    (!have || (e2e_same_file(&st, &sb_stat) && st.st_mtim.tv_sec < sb_read_at)) &&
	    all_cached())
		return;
	sb_read_at = time(NULL);
	kr = e2e_keyring_load_checked(file, &status, &error);
	g_free(error);
	fill(kr, status == E2E_LOAD_OK);
	e2e_json_free(kr);
	sb_have_stat = have;
	if (have)
		sb_stat = st;
	sb_legacy_ok = legacy_ok;
	sb_valid = TRUE;
}

void e2e_statusbar_redraw(void)
{
	refresh();
	statusbar_items_redraw("e2e");
}

/* the format of the current theme, else the module's own */
static const char *sb_format(int formatnum)
{
	MODULE_THEME_REC *rec = current_theme == NULL ? NULL :
	                        g_hash_table_lookup(current_theme->modules, MODULE_NAME);

	if (rec != NULL && formatnum < rec->count && rec->formats[formatnum] != NULL)
		return rec->formats[formatnum];
	return e2e_formats[formatnum].def;
}

static int state_format(E2E_WIN_STATE state)
{
	switch (state) {
	case E2E_WIN_ON:
		return TXT_E2E_SB_ON;
	case E2E_WIN_WAITING:
		return TXT_E2E_SB_WAITING;
	case E2E_WIN_BLOCKED:
		return TXT_E2E_SB_BLOCKED;
	case E2E_WIN_ERROR:
		return TXT_E2E_SB_ERROR;
	case E2E_WIN_OFF:
	default:
		return TXT_E2E_SB_OFF;
	}
}

/* "{sb <state>[<pending>]}": counts only, no IRC text */
static char *sb_text(const SB_ENTRY *e)
{
	GString *str = g_string_new("{sb ");

	g_string_append(str, sb_format(state_format(e->state)));
	if (e->pending > 0) {
		char **parts = g_strsplit(sb_format(TXT_E2E_SB_PENDING), "$0", -1);
		char *count = g_strdup_printf("%u", e->pending);
		char *text = g_strjoinv(count, parts);

		g_string_append(str, text);
		g_free(text);
		g_free(count);
		g_strfreev(parts);
	}
	g_string_append_c(str, '}');
	return g_string_free(str, FALSE);
}

static void item_e2e(SBAR_ITEM_REC *item, int get_size_only)
{
	WINDOW_REC *window = item->bar->parent_window != NULL ? item->bar->parent_window->active :
	                                                       active_win;
	SB_ENTRY *e = NULL;
	char *key, *str;

	key = sb_script || sb_gone || window == NULL ? NULL : sb_key(window->active);
	if (key != NULL)
		e = g_hash_table_lookup(sb_cache, key);
	g_free(key);
	if (e == NULL || !e->shown) {
		/* "" and not NULL: no value from the config either */
		statusbar_item_default_handler(item, get_size_only, "", "", FALSE);
		return;
	}
	str = sb_text(e);
	statusbar_item_default_handler(item, get_size_only, str, "", FALSE);
	g_free(str);
}

/* ---- what tells it to look again ---- */

static void sig_state_changed(void)
{
	sb_valid = FALSE;
	e2e_statusbar_redraw();
}

static gboolean sb_idle(gpointer data)
{
	sb_idle_id = 0;
	e2e_statusbar_redraw();
	return FALSE;
}

/* something a window's state is read from besides the keyring changed:
   looked at once the main loop is idle (a netjoin is one refresh) */
static void invalidate(void)
{
	sb_valid = FALSE;
	if (sb_idle_id == 0)
		sb_idle_id = g_idle_add(sb_idle, NULL);
}

/* a nick that a query's address or our own may be read from */
static void nick_matters(CHANNEL_REC *channel, const char *nick)
{
	SERVER_REC *server = channel != NULL ? channel->server : NULL;

	if (server == NULL || nick == NULL)
		return;
	/* compared as the server compares nicks */
	if (query_find(server, nick) != NULL ||
	    (server->nick != NULL &&
	     (IS_IRC_SERVER(server) && IRC_SERVER(server)->nick_comp_func != NULL ?
	      IRC_SERVER(server)->nick_comp_func(server->nick, nick) :
	      g_ascii_strcasecmp(server->nick, nick)) == 0))
		invalidate();
}

static void sig_nicklist(CHANNEL_REC *channel, NICK_REC *nick)
{
	if (nick != NULL)
		nick_matters(channel, nick->nick);
}

static void sig_nicklist_changed(CHANNEL_REC *channel, NICK_REC *nick, const char *oldnick)
{
	sig_nicklist(channel, nick);
	nick_matters(channel, oldnick);
}

static void sig_theme_changed(void)
{
	statusbar_items_redraw("e2e");
}

void e2e_statusbar_init(void)
{
	sb_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	sb_valid = sb_gone = FALSE;

	statusbar_item_register("e2e", NULL, item_e2e);

	signal_add("window changed", (SIGNAL_FUNC) e2e_statusbar_redraw);
	signal_add("window item changed", (SIGNAL_FUNC) e2e_statusbar_redraw);
	signal_add("e2e state changed", (SIGNAL_FUNC) sig_state_changed);
	signal_add("theme changed", (SIGNAL_FUNC) sig_theme_changed);
	signal_add("query address changed", (SIGNAL_FUNC) invalidate);
	signal_add("query nick changed", (SIGNAL_FUNC) invalidate);
	signal_add("window item name changed", (SIGNAL_FUNC) invalidate);
	signal_add("window item server changed", (SIGNAL_FUNC) invalidate);
	signal_add("window item remove", (SIGNAL_FUNC) invalidate);
	signal_add("channel joined", (SIGNAL_FUNC) invalidate);
	signal_add("event 005", (SIGNAL_FUNC) invalidate);
	signal_add("event 376", (SIGNAL_FUNC) invalidate);
	signal_add("event 422", (SIGNAL_FUNC) invalidate);
	signal_add("nicklist new", (SIGNAL_FUNC) sig_nicklist);
	signal_add("nicklist remove", (SIGNAL_FUNC) sig_nicklist);
	signal_add("nicklist host changed", (SIGNAL_FUNC) sig_nicklist);
	signal_add("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
	/* the networks in use (e2e_legacy_ok) */
	signal_add("server looking", (SIGNAL_FUNC) invalidate);
	signal_add("server connected", (SIGNAL_FUNC) invalidate);
	signal_add("server connect failed", (SIGNAL_FUNC) invalidate);
	signal_add("server disconnected", (SIGNAL_FUNC) invalidate);
	signal_add("server quit", (SIGNAL_FUNC) invalidate);
	signal_add("server destroyed", (SIGNAL_FUNC) invalidate);
	signal_add("server setup updated", (SIGNAL_FUNC) invalidate);
	signal_add("server setup destroyed", (SIGNAL_FUNC) invalidate);
	signal_add("server reconnect remove", (SIGNAL_FUNC) invalidate);
	signal_add("chatnet created", (SIGNAL_FUNC) invalidate);
	signal_add("chatnet removed", (SIGNAL_FUNC) invalidate);
	signal_add("chatnet destroyed", (SIGNAL_FUNC) invalidate);

	e2e_statusbar_redraw();
}

void e2e_statusbar_deinit(void)
{
	signal_remove("window changed", (SIGNAL_FUNC) e2e_statusbar_redraw);
	signal_remove("window item changed", (SIGNAL_FUNC) e2e_statusbar_redraw);
	signal_remove("e2e state changed", (SIGNAL_FUNC) sig_state_changed);
	signal_remove("theme changed", (SIGNAL_FUNC) sig_theme_changed);
	signal_remove("query address changed", (SIGNAL_FUNC) invalidate);
	signal_remove("query nick changed", (SIGNAL_FUNC) invalidate);
	signal_remove("window item name changed", (SIGNAL_FUNC) invalidate);
	signal_remove("window item server changed", (SIGNAL_FUNC) invalidate);
	signal_remove("window item remove", (SIGNAL_FUNC) invalidate);
	signal_remove("channel joined", (SIGNAL_FUNC) invalidate);
	signal_remove("event 005", (SIGNAL_FUNC) invalidate);
	signal_remove("event 376", (SIGNAL_FUNC) invalidate);
	signal_remove("event 422", (SIGNAL_FUNC) invalidate);
	signal_remove("nicklist new", (SIGNAL_FUNC) sig_nicklist);
	signal_remove("nicklist remove", (SIGNAL_FUNC) sig_nicklist);
	signal_remove("nicklist host changed", (SIGNAL_FUNC) sig_nicklist);
	signal_remove("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
	signal_remove("server looking", (SIGNAL_FUNC) invalidate);
	signal_remove("server connected", (SIGNAL_FUNC) invalidate);
	signal_remove("server connect failed", (SIGNAL_FUNC) invalidate);
	signal_remove("server disconnected", (SIGNAL_FUNC) invalidate);
	signal_remove("server quit", (SIGNAL_FUNC) invalidate);
	signal_remove("server destroyed", (SIGNAL_FUNC) invalidate);
	signal_remove("server setup updated", (SIGNAL_FUNC) invalidate);
	signal_remove("server setup destroyed", (SIGNAL_FUNC) invalidate);
	signal_remove("server reconnect remove", (SIGNAL_FUNC) invalidate);
	signal_remove("chatnet created", (SIGNAL_FUNC) invalidate);
	signal_remove("chatnet removed", (SIGNAL_FUNC) invalidate);
	signal_remove("chatnet destroyed", (SIGNAL_FUNC) invalidate);
	if (sb_idle_id != 0)
		g_source_remove(sb_idle_id);
	sb_idle_id = 0;

	/* nothing left on the screen, then no item to draw it */
	sb_gone = TRUE;
	statusbar_items_redraw("e2e");
	statusbar_item_unregister("e2e");

	g_hash_table_destroy(sb_cache);
	sb_cache = NULL;
	/* sb_gone stays: nothing is drawn from here on */
	sb_valid = sb_script = FALSE;
}
