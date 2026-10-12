/*
 e2e-web.c : the E2E state of channels and queries for other modules

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 What a frontend such as fe-web can tell its clients, as signals any
 module or script can use:

   "e2e state" (server, target, json) - emitted for every channel and
     query whose state differs from the one emitted for it last: after
     e2e_state_changed() (the keyring saved, rpe2e.pl came or went, our
     own address changed), after what a window's state is read from
     changed (a join, a query's address, the server's CASEMAPPING, the
     networks in use), when another process changed the keyring file, and
     for a channel or query that is new. json is e2e_window_info_json():
     version 1, valid UTF-8, every string escaped.
   "e2e state request" (server, target) - answered at once with an
     "e2e state" for that channel or query (nothing for anything else);
     what was emitted last for the others is not changed by it, so a
     change not emitted yet still goes to everyone.
   "e2e action" (server, target, action, handle, fingerprint) - "accept"
     or "decline" of the key request that waits in the window of target
     with exactly that ident@host (and, with fingerprint, that key):
     e2e_web_action() (e2e-commands.c), which tells a refusal with
     "e2e action refused" (server, target, handle, reason).

 While rpe2e.pl is loaded the module is inactive and the keyring is the
 script's: every channel and query is "off" with no requests (the
 script's state is not the module's to tell - as the statusbar item shows
 nothing then), nothing is read for it, and an action is refused with a
 message. A frontend would show no lock then and offer nothing to accept.
 When the module is unloaded, every channel and query it told a state
 (either way) is told "off": with the module gone nothing is encrypted
 any more.

 The parsed keyring holds secret keys: it is freed (and wiped) before any
 "e2e state" is emitted, never kept.
*/

#include "module.h"
#include "e2e-core.h"
#include "e2e-keyring.h"
#include "e2e-web.h"
#include "e2e-wire.h"

#include <irssi/src/core/channels.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/fe-common/core/window-items.h>

#include <string.h>
#include <sys/stat.h>

/* how often the keyring file is looked at (stat) for a change another
   process made (seconds) */
#define WEB_FILE_CHECK 5

static GHashTable *web_cache;	/* "<tag>\x1f<name>" -> json emitted last */
/* "<tag>\x1f<name>" of every item a state was emitted for, by a refresh
   or as the answer to a request: each is told "off" on unload */
static GHashTable *web_told;
static guint web_idle_id;
static guint web_check_id;
/* the keyring file as the last refresh read it (web_have_stat: it was
   there), and when (a change in that second may not show in the stat) */
static gboolean web_have_stat;
static struct stat web_stat;
static time_t web_read_at;

typedef struct {
	SERVER_REC *server;	/* referenced */
	char *name;
	char *json;
} WEB_EMIT;

static gboolean web_item(WI_ITEM_REC *item)
{
	return item != NULL && item->server != NULL && item->server->tag != NULL &&
	       item->name != NULL && (e2e_is_channel(item->name) || IS_QUERY(item));
}

static char *web_key(WI_ITEM_REC *item)
{
	return g_strconcat(item->server->tag, "\x1f", item->name, NULL);
}

/* "off" with nothing else known: rpe2e.pl loaded, the module unloaded */
static char *off_json(SERVER_REC *server)
{
	E2E_WIN_INFO info;
	E2E_NET net;
	char *json;

	memset(&info, 0, sizeof(info));
	info.state = E2E_WIN_OFF;
	e2e_server_net(server, &net);
	json = e2e_window_info_json(&info, net.label);
	e2e_net_clear(&net);
	return json;
}

static char *info_json(SERVER_REC *server, E2E_WIN_INFO *info)
{
	E2E_NET net;
	char *json;

	e2e_server_net(server, &net);
	json = e2e_window_info_json(info, net.label);
	e2e_net_clear(&net);
	return json;
}

/* the state of item from a keyring read already; NULL for an item that
   has none */
static char *item_json(const E2E_JSON *kr, gboolean kr_ok, gboolean script, WI_ITEM_REC *item)
{
	E2E_WIN_INFO info;
	char *json = NULL;

	if (!web_item(item))
		return NULL;
	if (script)
		return off_json(item->server);
	if (e2e_window_info_with(kr, kr_ok, item, &info))
		json = info_json(item->server, &info);
	e2e_window_info_clear(&info);
	return json;
}

static void emit_add(GPtrArray *list, WI_ITEM_REC *item, const char *json)
{
	WEB_EMIT *e = g_new0(WEB_EMIT, 1);

	e->server = item->server;
	server_ref(e->server);
	e->name = g_strdup(item->name);
	e->json = g_strdup(json);
	g_ptr_array_add(list, e);
}

/* emitted after every list and the keyring are let go: a handler may
   close windows, disconnect, or run commands that read the keyring */
static void emit_all(GPtrArray *list)
{
	guint i;

	for (i = 0; i < list->len; i++) {
		WEB_EMIT *e = g_ptr_array_index(list, i);

		if (g_slist_find(servers, e->server) == NULL)
			continue;
		if (web_told != NULL && e->server->tag != NULL)
			g_hash_table_add(web_told, g_strconcat(e->server->tag, "\x1f", e->name, NULL));
		signal_emit("e2e state", 3, e->server, e->name, e->json);
	}
	for (i = 0; i < list->len; i++) {
		WEB_EMIT *e = g_ptr_array_index(list, i);

		server_unref(e->server);
		g_free(e->name);
		g_free(e->json);
		g_free(e);
	}
	g_ptr_array_free(list, TRUE);
}

static void collect(GPtrArray *list, GHashTable *now, const E2E_JSON *kr, gboolean kr_ok,
                    gboolean script, GSList *items)
{
	GSList *tmp;

	for (tmp = items; tmp != NULL; tmp = tmp->next) {
		WI_ITEM_REC *item = tmp->data;
		char *json = item_json(kr, kr_ok, script, item), *key;

		if (json == NULL)
			continue;
		key = web_key(item);
		if (g_strcmp0(g_hash_table_lookup(web_cache, key), json) != 0)
			emit_add(list, item, json);
		g_hash_table_replace(now, key, json);
	}
}

static gboolean told_gone(gpointer key, gpointer value, gpointer now)
{
	return !g_hash_table_contains(now, key);
}

void e2e_web_refresh(void)
{
	const char *file = e2e_keyring_file();
	E2E_LOAD_STATUS status;
	E2E_JSON *kr = NULL;
	GHashTable *now;
	GPtrArray *list;
	gboolean script, kr_ok = FALSE;
	struct stat st;
	GSList *tmp;
	char *error = NULL;

	if (web_cache == NULL || file == NULL)
		return;
	script = e2e_script_loaded();
	if (!script) {
		/* the file first: a change while it is read is seen next time */
		web_have_stat = stat(file, &st) == 0;
		if (web_have_stat)
			web_stat = st;
		web_read_at = time(NULL);
		/* rpe2e.pl's _load_keyring_checked: nothing moved, nothing
		   saved, nothing taken over */
		kr = e2e_keyring_load_checked(file, &status, &error);
		g_free(error);
		kr_ok = status == E2E_LOAD_OK;
	}
	now = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	list = g_ptr_array_new();
	for (tmp = servers; tmp != NULL; tmp = tmp->next) {
		SERVER_REC *server = tmp->data;

		collect(list, now, kr, kr_ok, script, server->channels);
		collect(list, now, kr, kr_ok, script, server->queries);
	}
	e2e_json_free(kr);
	g_hash_table_destroy(web_cache);
	web_cache = now;
	/* an item that is gone needs no "off" (a frontend saw it closed) */
	g_hash_table_foreach_remove(web_told, told_gone, now);
	emit_all(list);
}

/* ---- "e2e state request", "e2e action" ---- */

static void sig_state_request(SERVER_REC *server, const char *target)
{
	WI_ITEM_REC *item;
	E2E_WIN_INFO info;
	GPtrArray *list;
	char *json = NULL;

	if (web_cache == NULL || server == NULL || target == NULL)
		return;
	/* only this server's (window_item_find() takes any for NULL) */
	item = window_item_find(server, target);
	if (item == NULL || item->server != server || !web_item(item))
		return;
	if (e2e_script_loaded()) {
		json = off_json(server);
	} else {
		if (e2e_window_info_for(server, item, &info))
			json = info_json(server, &info);
		e2e_window_info_clear(&info);
	}
	if (json == NULL)
		return;
	list = g_ptr_array_new();
	emit_add(list, item, json);
	g_free(json);
	emit_all(list);
}

static void sig_action(SERVER_REC *server, const char *target, const char *action,
                       const char *handle, const char *fingerprint)
{
	if (server == NULL || target == NULL || action == NULL || handle == NULL)
		return;
	if (strcmp(action, "accept") == 0)
		e2e_web_action(server, target, TRUE, handle, fingerprint);
	else if (strcmp(action, "decline") == 0)
		e2e_web_action(server, target, FALSE, handle, fingerprint);
}

/* ---- what tells it to look again ---- */

static gboolean web_idle(gpointer data)
{
	web_idle_id = 0;
	e2e_web_refresh();
	return FALSE;
}

/* looked at once the main loop is idle (a netjoin is one refresh) */
static void invalidate(void)
{
	if (web_idle_id == 0 && web_cache != NULL)
		web_idle_id = g_idle_add(web_idle, NULL);
}

/* another process (rpe2e.pl of another erssi, an editor) wrote the
   keyring: only a stat, the file is read when it changed */
static gboolean web_check(gpointer data)
{
	const char *file = e2e_keyring_file();
	struct stat st;
	gboolean have;

	if (file == NULL || e2e_script_loaded())
		return TRUE;
	have = stat(file, &st) == 0;
	if (have != web_have_stat ||
	    (have && (!e2e_same_file(&st, &web_stat) || st.st_mtim.tv_sec >= web_read_at)))
		invalidate();
	return TRUE;
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

/* the same as the statusbar item's (e2e-statusbar.c), and new items */
static const char *const web_signals[] = {
	"query created",
	"query address changed",
	"query nick changed",
	"window item name changed",
	"window item server changed",
	"channel joined",
	"event 005",
	"event 376",
	"event 422",
	"server looking",
	"server connected",
	"server connect failed",
	"server disconnected",
	"server quit",
	"server destroyed",
	"server setup updated",
	"server setup destroyed",
	"server reconnect remove",
	"chatnet created",
	"chatnet removed",
	"chatnet destroyed",
	NULL
};

void e2e_web_init(void)
{
	int i;

	web_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	web_told = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	web_have_stat = FALSE;
	web_read_at = 0;

	signal_add("e2e state changed", (SIGNAL_FUNC) e2e_web_refresh);
	signal_add("e2e state request", (SIGNAL_FUNC) sig_state_request);
	signal_add("e2e action", (SIGNAL_FUNC) sig_action);
	for (i = 0; web_signals[i] != NULL; i++)
		signal_add(web_signals[i], (SIGNAL_FUNC) invalidate);
	signal_add("nicklist new", (SIGNAL_FUNC) sig_nicklist);
	signal_add("nicklist remove", (SIGNAL_FUNC) sig_nicklist);
	signal_add("nicklist host changed", (SIGNAL_FUNC) sig_nicklist);
	signal_add("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
	web_check_id = g_timeout_add_seconds(WEB_FILE_CHECK, web_check, NULL);

	/* loaded mid-session: the state of what is open already */
	invalidate();
}

void e2e_web_deinit(void)
{
	GPtrArray *list;
	GSList *tmp, *i;
	int n;

	signal_remove("e2e state changed", (SIGNAL_FUNC) e2e_web_refresh);
	signal_remove("e2e state request", (SIGNAL_FUNC) sig_state_request);
	signal_remove("e2e action", (SIGNAL_FUNC) sig_action);
	for (n = 0; web_signals[n] != NULL; n++)
		signal_remove(web_signals[n], (SIGNAL_FUNC) invalidate);
	signal_remove("nicklist new", (SIGNAL_FUNC) sig_nicklist);
	signal_remove("nicklist remove", (SIGNAL_FUNC) sig_nicklist);
	signal_remove("nicklist host changed", (SIGNAL_FUNC) sig_nicklist);
	signal_remove("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
	if (web_idle_id != 0)
		g_source_remove(web_idle_id);
	web_idle_id = 0;
	if (web_check_id != 0)
		g_source_remove(web_check_id);
	web_check_id = 0;

	/* without the module nothing is encrypted: every item that was told
	   a state (by a refresh or a request answer - which the cache does
	   not hold) is told "off" */
	list = g_ptr_array_new();
	for (tmp = servers; tmp != NULL; tmp = tmp->next) {
		SERVER_REC *server = tmp->data;
		GSList *lists[2] = { server->channels, server->queries };

		for (n = 0; n < 2; n++) {
			for (i = lists[n]; i != NULL; i = i->next) {
				WI_ITEM_REC *item = i->data;
				char *key, *json;

				if (!web_item(item))
					continue;
				key = web_key(item);
				if (g_hash_table_contains(web_told, key)) {
					json = off_json(server);
					emit_add(list, item, json);
					g_free(json);
				}
				g_free(key);
			}
		}
	}
	g_hash_table_destroy(web_cache);
	web_cache = NULL;
	g_hash_table_destroy(web_told);
	web_told = NULL;
	emit_all(list);
}
