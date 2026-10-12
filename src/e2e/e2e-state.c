/*
 e2e-state.c : the E2E state of a window, the waiting key requests

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 Read only: nothing here changes the keyring (no old row is taken
 over), so the statusbar and other modules can ask as often as they like.
*/

#include <string.h>

#include "e2e-state.h"
#include "e2e-proto.h"
#include "e2e-wire.h"

/* ---- waiting requests ---- */

typedef enum {
	ROW_BAD,		/* does not hold together: never shown */
	ROW_WAITING,		/* a request waiting for /e2e accept */
	ROW_FOREIGN_DM		/* a query's request naming another address */
} ROW_KIND;

/* row (pending_inbound[key]) holds together when its "handle" and
   "channel" members name exactly its key, "<handle>|<channel>" - the row
   /e2e accept <handle> acts on in the window of <channel>. A request
   for a query asks for the direction to its sender: its context must be
   "@" + its own address (e2e_handle_keyreq; rpe2e.pl did not check it),
   else our key for another person's query would go to the sender */
static ROW_KIND pending_row_parts(const char *key, const E2E_JSON *row, const char **handle,
                                  const char **ctx)
{
	size_t hl;

	*handle = e2e_json_get_string(row, "handle");
	*ctx = e2e_json_get_string(row, "channel");
	if (*handle == NULL || *ctx == NULL || **handle == '\0' || **ctx == '\0')
		return ROW_BAD;
	hl = strlen(*handle);
	if (strncmp(key, *handle, hl) != 0 || key[hl] != '|' || strcmp(key + hl + 1, *ctx) != 0)
		return ROW_BAD;
	if (e2e_dm_row_foreign(*handle, *ctx))
		return ROW_FOREIGN_DM;
	return ROW_WAITING;
}

void e2e_pending_free(E2E_PENDING *p)
{
	if (p == NULL)
		return;
	g_free(p->nick);
	g_free(p->handle);
	g_free(p->ctx);
	g_free(p);
}

static E2E_PENDING *pending_new(const E2E_JSON *row, const char *handle, const char *ctx)
{
	E2E_PENDING *p = g_new0(E2E_PENDING, 1);
	const char *pub = e2e_json_get_string(row, "pubkey"), *nick;
	unsigned char *pk;
	size_t len = 0;

	nick = e2e_json_get_string(row, "sender_nick");
	p->nick = g_strdup(nick != NULL ? nick : "");
	p->handle = g_strdup(handle);
	p->ctx = g_strdup(ctx);
	p->received_at = e2e_json_get_int(row, "received_at", 0);
	pk = pub != NULL ? e2e_b64_decode(pub, &len) : NULL;
	if (pk != NULL && len == 32) {
		char *fp = e2e_fingerprint_hex(pk);

		g_strlcpy(p->fp16, fp, sizeof(p->fp16));
		g_free(fp);
	}
	g_free(pk);
	return p;
}

/* oldest first; the same time by address, then context */
static gint pending_cmp(gconstpointer a, gconstpointer b)
{
	const E2E_PENDING *x = *(E2E_PENDING *const *) a, *y = *(E2E_PENDING *const *) b;
	int c;

	if (x->received_at != y->received_at)
		return x->received_at < y->received_at ? -1 : 1;
	c = strcmp(x->handle, y->handle);
	return c != 0 ? c : strcmp(x->ctx, y->ctx);
}

/* the rows of kind of ctx, of every context for NULL */
static GPtrArray *pending_collect(const E2E_JSON *kr, const char *ctx, ROW_KIND kind)
{
	GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify) e2e_pending_free);
	E2E_JSON *store = e2e_json_get(kr, "pending_inbound");
	GPtrArray *keys = e2e_json_keys(store);
	guint i;

	for (i = 0; i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i), *handle, *rctx;
		E2E_JSON *row = e2e_json_get(store, key);

		if (pending_row_parts(key, row, &handle, &rctx) != kind)
			continue;
		if (ctx != NULL && strcmp(rctx, ctx) != 0)
			continue;
		g_ptr_array_add(out, pending_new(row, handle, rctx));
	}
	g_ptr_array_unref(keys);
	g_ptr_array_sort(out, pending_cmp);
	return out;
}

GPtrArray *e2e_pending_all(const E2E_JSON *kr)
{
	return pending_collect(kr, NULL, ROW_WAITING);
}

GPtrArray *e2e_pending_damaged(const E2E_JSON *kr)
{
	return pending_collect(kr, NULL, ROW_FOREIGN_DM);
}

GPtrArray *e2e_pending_of(const E2E_JSON *kr, const char *ctx)
{
	if (ctx == NULL)
		return g_ptr_array_new_with_free_func((GDestroyNotify) e2e_pending_free);
	return pending_collect(kr, ctx, ROW_WAITING);
}

const char *e2e_pending_handle(const E2E_JSON *kr, const char *ctx, const char *who,
                               E2E_CASEMAP map, guint *count)
{
	E2E_JSON *store = e2e_json_get(kr, "pending_inbound");
	GPtrArray *keys = e2e_json_keys(store);
	const char *found = NULL;
	char *fwho;
	guint i, n = 0;

	/* "" is no argument */
	if (who != NULL && *who == '\0')
		who = NULL;
	/* an ident@host names its own request only (no nick holds '@') */
	fwho = who != NULL && strchr(who, '@') == NULL ? e2e_casefold(who, map) : NULL;

	for (i = 0; ctx != NULL && i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i), *handle, *rctx, *nick;
		E2E_JSON *row = e2e_json_get(store, key);
		gboolean match;

		if (pending_row_parts(key, row, &handle, &rctx) != ROW_WAITING || strcmp(rctx, ctx) != 0)
			continue;
		match = who == NULL || strcmp(handle, who) == 0;
		nick = e2e_json_get_string(row, "sender_nick");
		if (!match && nick != NULL && fwho != NULL) {
			char *fnick = e2e_casefold(nick, map);

			match = g_strcmp0(fnick, fwho) == 0;
			g_free(fnick);
		}
		if (match) {
			found = handle;
			n++;
		}
	}
	g_ptr_array_unref(keys);
	g_free(fwho);
	if (count != NULL)
		*count = n;
	return n == 1 ? found : NULL;
}

/* ---- the state of a window ---- */

static guint dm_configs_count(const E2E_JSON *kr, const E2E_NET *net, const char *nick)
{
	GPtrArray *on;
	guint n;

	if (nick == NULL || *nick == '\0')
		return 0;
	on = e2e_dm_configs_of(kr, net, nick);
	n = on->len;
	g_ptr_array_unref(on);
	return n;
}

/* the config of wire as the gate reads it (lookup_enabled): for an old
   context on the only network the scoped one when there is one */
static E2E_JSON *config_of(const E2E_JSON *kr, const E2E_NET *net, E2E_CTX_KIND kind,
                           const char *lctx, const char *wire)
{
	E2E_JSON *channels = e2e_json_get(kr, "channels"), *cfg = NULL;
	char *scoped;

	if (kind == E2E_CTX_LEGACY) {
		scoped = e2e_ctx_scoped(net, wire);
		if (scoped != NULL)
			cfg = e2e_json_get(channels, scoped);
		g_free(scoped);
	}
	return cfg != NULL ? cfg : e2e_json_get(channels, lctx);
}

/* the contexts where the rows of wire on net are: the one the commands
   use, and for an old one on the only network both its forms (the user's
   own line takes them over, until then either may hold a row) */
static GPtrArray *row_ctxs(const E2E_JSON *kr, const E2E_NET *net, const char *wire)
{
	GPtrArray *ctxs = g_ptr_array_new_with_free_func(g_free);
	char *ctx = NULL, *found;

	switch (e2e_ctx_lookup(kr, net, wire, &ctx)) {
	case E2E_CTX_BLOCKED:
		/* our own address: this network's rows only */
		g_free(ctx);
		ctx = e2e_ctx_scoped(net, wire);
		if (ctx != NULL)
			g_ptr_array_add(ctxs, ctx);
		return ctxs;
	case E2E_CTX_LEGACY:
		g_ptr_array_add(ctxs, ctx);
		ctx = e2e_ctx_scoped(net, wire);
		if (ctx != NULL)
			g_ptr_array_add(ctxs, ctx);
		return ctxs;
	default:
		found = e2e_ctx_find(kr, net, wire);
		g_free(ctx);
		if (found != NULL)
			g_ptr_array_add(ctxs, found);
		return ctxs;
	}
}

/* trusted rows with a real key "<handle>|<c>", c one of ctxs, of peer
   (NULL: anyone); each handle counted once */
static guint count_sessions(const E2E_JSON *kr, const GPtrArray *ctxs, const char *peer)
{
	E2E_JSON *incoming = e2e_json_get(kr, "incoming");
	GPtrArray *keys = e2e_json_keys(incoming);
	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint i, j, n;

	for (i = 0; i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i);
		E2E_JSON *row = e2e_json_get(incoming, key);
		size_t kl = strlen(key);

		if (g_strcmp0(e2e_json_get_string(row, "status"), "trusted") != 0 ||
		    !e2e_session_has_key(row))
			continue;
		for (j = 0; j < ctxs->len; j++) {
			const char *c = g_ptr_array_index(ctxs, j);
			size_t cl = strlen(c), hl;

			if (kl <= cl + 1 || strcmp(key + kl - cl, c) != 0 || key[kl - cl - 1] != '|')
				continue;
			hl = kl - cl - 1;
			if (peer != NULL && (strlen(peer) != hl || strncmp(key, peer, hl) != 0))
				continue;
			g_hash_table_add(seen, g_strndup(key, hl));
			break;
		}
	}
	n = g_hash_table_size(seen);
	g_hash_table_destroy(seen);
	g_ptr_array_unref(keys);
	return n;
}

void e2e_window_info(const E2E_JSON *kr, gboolean kr_ok, const E2E_NET *net, const char *wire,
                     const char *own_wire, const char *nick, E2E_WIN_INFO *out)
{
	E2E_CTX_KIND kind;
	E2E_JSON *cfg;
	GPtrArray *ctxs;
	const char *mode;
	char *lctx = NULL;
	gboolean channel;

	memset(out, 0, sizeof(*out));
	out->state = E2E_WIN_OFF;
	out->mode = g_strdup("normal");
	out->pending = g_ptr_array_new_with_free_func((GDestroyNotify) e2e_pending_free);
	if (!kr_ok) {
		out->state = E2E_WIN_ERROR;
		return;
	}
	if (wire == NULL) {
		/* a query whose address is not known: the gate refuses when E2E
		   is on for the nick at the address it had */
		if (dm_configs_count(kr, net, nick) > 0)
			out->state = E2E_WIN_BLOCKED;
		return;
	}
	kind = e2e_ctx_lookup(kr, net, wire, &lctx);
	if (kind == E2E_CTX_BLOCKED) {
		out->state = E2E_WIN_BLOCKED;
		out->ctx = lctx;
		return;
	}
	cfg = config_of(kr, net, kind, lctx, wire);
	g_free(lctx);
	out->ctx = e2e_ctx_find(kr, net, wire);
	if (out->ctx == NULL) {
		out->state = E2E_WIN_BLOCKED;
		return;
	}
	mode = e2e_json_get_string(cfg, "mode");
	if (mode != NULL) {
		g_free(out->mode);
		out->mode = g_strdup(mode);
	}
	g_ptr_array_unref(out->pending);
	out->pending = e2e_pending_of(kr, out->ctx);

	channel = e2e_is_channel(wire);
	if (!e2e_json_truthy(e2e_json_get(cfg, "enabled"))) {
		/* off at this address, on at another of the nick: refused */
		if (!channel && dm_configs_count(kr, net, nick) > 0)
			out->state = E2E_WIN_BLOCKED;
		return;
	}
	if (channel) {
		ctxs = row_ctxs(kr, net, wire);
		out->sessions = count_sessions(kr, ctxs, NULL);
	} else if (own_wire != NULL && *wire == '@') {
		/* their messages to us: "<peer>|@<own>" */
		ctxs = row_ctxs(kr, net, own_wire);
		out->sessions = count_sessions(kr, ctxs, wire + 1);
	} else {
		ctxs = NULL;
	}
	if (ctxs != NULL)
		g_ptr_array_unref(ctxs);
	out->state = out->sessions > 0 ? E2E_WIN_ON : E2E_WIN_WAITING;
}

void e2e_window_info_clear(E2E_WIN_INFO *info)
{
	if (info == NULL)
		return;
	g_free(info->ctx);
	g_free(info->mode);
	if (info->pending != NULL)
		g_ptr_array_unref(info->pending);
	memset(info, 0, sizeof(*info));
}

const char *e2e_win_state_name(E2E_WIN_STATE state)
{
	switch (state) {
	case E2E_WIN_ON:
		return "on";
	case E2E_WIN_WAITING:
		return "waiting";
	case E2E_WIN_BLOCKED:
		return "blocked";
	case E2E_WIN_ERROR:
		return "error";
	case E2E_WIN_OFF:
	default:
		return "off";
	}
}

/* ---- the "e2e state" object ---- */

/* a JSON string of s (null for NULL): valid UTF-8 (invalid bytes as
   U+FFFD), '"' and '\' escaped, control characters as \u00XX - not
   e2e_json_encode(), which writes each byte as a code point (the keyring
   form) */
static void json_string(GString *out, const char *s)
{
	char *clean;
	const unsigned char *p;

	if (s == NULL) {
		g_string_append(out, "null");
		return;
	}
	clean = e2e_utf8_clean(s, strlen(s));
	g_string_append_c(out, '"');
	for (p = (const unsigned char *) clean; *p != '\0'; p++) {
		if (*p == '"' || *p == '\\') {
			g_string_append_c(out, '\\');
			g_string_append_c(out, *p);
		} else if (*p < 0x20) {
			g_string_append_printf(out, "\\u%04x", *p);
		} else {
			g_string_append_c(out, *p);
		}
	}
	g_string_append_c(out, '"');
	g_free(clean);
}

char *e2e_window_info_json(const E2E_WIN_INFO *info, const char *network)
{
	GString *out = g_string_new("{\"v\":1,\"state\":");
	guint i;

	json_string(out, e2e_win_state_name(info->state));
	g_string_append(out, ",\"network\":");
	json_string(out, network);
	g_string_append(out, ",\"context\":");
	json_string(out, info->ctx != NULL ? e2e_ctx_wire(info->ctx) : NULL);
	g_string_append(out, ",\"mode\":");
	json_string(out, info->mode);
	g_string_append_printf(out, ",\"sessions\":%u,\"pending\":[", info->sessions);
	for (i = 0; info->pending != NULL && i < info->pending->len; i++) {
		const E2E_PENDING *p = g_ptr_array_index(info->pending, i);

		g_string_append(out, i > 0 ? ",{\"nick\":" : "{\"nick\":");
		json_string(out, p->nick);
		g_string_append(out, ",\"handle\":");
		json_string(out, p->handle);
		g_string_append(out, ",\"fingerprint\":");
		json_string(out, p->fp16);
		g_string_append_printf(out, ",\"received_at\":%" G_GINT64_FORMAT "}", p->received_at);
	}
	g_string_append(out, "]}");
	return g_string_free(out, FALSE);
}

/* ---- for the user ---- */

char *e2e_age_text(gint64 seconds)
{
	if (seconds < 0)
		seconds = 0;
	if (seconds < 60)
		return g_strdup_printf("%" G_GINT64_FORMAT "s", seconds);
	if (seconds < 3600)
		return g_strdup_printf("%" G_GINT64_FORMAT "m", seconds / 60);
	if (seconds < 86400)
		return g_strdup_printf("%" G_GINT64_FORMAT "h", seconds / 3600);
	return g_strdup_printf("%" G_GINT64_FORMAT "d", seconds / 86400);
}

/* a stamp older than the interval, or from the future (the clock went
   back) */
static gboolean stamp_expired(gint64 now, gint64 stamp)
{
	return now - stamp >= E2E_PENDING_NOTICE_INTERVAL || now < stamp;
}

gboolean e2e_pending_notice_allow(GHashTable *stamps, const char *key)
{
	gint64 now = e2e_now(), *stamp;
	GHashTableIter iter;
	gpointer k, value;

	stamp = g_hash_table_lookup(stamps, key);
	if (stamp != NULL && !stamp_expired(now, *stamp))
		return FALSE;
	if (stamp == NULL && g_hash_table_size(stamps) >= E2E_PENDING_NOTICE_MAX) {
		g_hash_table_iter_init(&iter, stamps);
		while (g_hash_table_iter_next(&iter, NULL, &value))
			if (stamp_expired(now, *(gint64 *) value))
				g_hash_table_iter_remove(&iter);
	}
	/* a flood of requests: the oldest notices are forgotten (a request
	   of theirs may be told again), never the table grows */
	while (stamp == NULL && g_hash_table_size(stamps) >= E2E_PENDING_NOTICE_MAX) {
		gpointer oldest = NULL;
		gint64 best = 0;

		g_hash_table_iter_init(&iter, stamps);
		while (g_hash_table_iter_next(&iter, &k, &value))
			if (oldest == NULL || *(gint64 *) value < best) {
				oldest = k;
				best = *(gint64 *) value;
			}
		g_hash_table_remove(stamps, oldest);
	}
	stamp = g_new(gint64, 1);
	*stamp = now;
	g_hash_table_replace(stamps, g_strdup(key), stamp);
	return TRUE;
}

gboolean e2e_autoload_listed(const char *autoload_modules)
{
	char **words;
	gboolean found = FALSE;
	int i;

	if (autoload_modules == NULL)
		return FALSE;
	words = g_strsplit_set(autoload_modules, " ,", -1);
	for (i = 0; words[i] != NULL && !found; i++) {
		char *colon = strchr(words[i], ':');

		if (colon != NULL)
			*colon = '\0';
		/* as it is: irssi loads lib<word>_core, case and all */
		found = strcmp(words[i], "e2e") == 0;
	}
	g_strfreev(words);
	return found;
}

guint e2e_enabled_count(const E2E_JSON *kr)
{
	E2E_JSON *channels = e2e_json_get(kr, "channels");
	GPtrArray *keys;
	guint i, n = 0;

	if (channels == NULL)
		return 0;
	keys = e2e_json_keys(channels);
	for (i = 0; i < keys->len; i++)
		if (e2e_json_truthy(e2e_json_get(e2e_json_get(channels, keys->pdata[i]), "enabled")))
			n++;
	g_ptr_array_unref(keys);
	return n;
}
