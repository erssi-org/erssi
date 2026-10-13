/*
 e2e-commands.c : /E2E

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The subcommands, arguments and messages of rpe2e.pl 0.2.0 (cmd_on,
 cmd_accept, ... there). They work in the context of the active window:
 a channel, or - in a query - the peer by ident@host.
*/

#include "module.h"
#include "e2e-core.h"
#include "e2e-crypto.h"
#include "e2e-keyring.h"
#include "e2e-wire.h"

#include <irssi/src/core/channels.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/window-item-def.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/fe-common/core/window-items.h>

#include <errno.h>

#define NOT_IN_CONTEXT "not in a channel or query (or peer handle unknown — wait for a message from them)"

static const char *const subcommands[] = {
	"on", "off", "mode", "fingerprint", "list", "status", "pending", "accept", "decline",
	"revoke", "unrevoke", "forget", "handshake", "verify", "reverify", "rotate", "export",
	"import", "autotrust", "adopt", "reset", "help", NULL
};

/* $witem->{name}: the channel, or the nick of a query */
static const char *item_name(WI_ITEM_REC *item)
{
	return item != NULL && item->name != NULL ? item->name : "";
}

/* a storage context as the user reads it: "IRCnet:#chan", a bare (old)
   one as it is - never a raw 0x1F. Valid until the fourth call after. */
static const char *shown(const char *ctx)
{
	static char *ring[4];
	static int at;

	at = (at + 1) % G_N_ELEMENTS(ring);
	g_free(ring[at]);
	ring[at] = e2e_ctx_display(ctx);
	return ring[at] != NULL ? ring[at] : "";
}

/* text from the keyring (a handle) as the user reads it: control bytes
   neutralised. Valid until the fourth call after. */
static const char *shown_text(const char *text)
{
	static char *ring[4];
	static int at;

	at = (at + 1) % G_N_ELEMENTS(ring);
	g_free(ring[at]);
	ring[at] = e2e_text_display(text);
	return ring[at] != NULL ? ring[at] : "";
}

/* the bare context of a window: the channel, or "@<peer handle>" in a
   query */
static char *window_wire(const E2E_JSON *kr, WI_ITEM_REC *item, const char *nick)
{
	char *handle, *ctx;

	if (item == NULL)
		return NULL;
	if (e2e_is_channel(item_name(item)))
		return g_strdup(item_name(item));
	handle = e2e_resolve_dm_handle(item->server, kr, nick != NULL ? nick : item_name(item));
	if (handle == NULL)
		return NULL;
	ctx = g_strconcat("@", handle, NULL);
	g_free(handle);
	return ctx;
}

/* why wire has no context on net: set up with no network (by rpe2e.pl)
   while erssi uses several, or a name that cannot be kept per network */
static void print_blocked(WI_ITEM_REC *item, const E2E_NET *net, const char *wire)
{
	char *scoped = e2e_ctx_scoped(net, wire);

	if (scoped != NULL)
		e2e_print_item(item, E2E_REFUSE_LEGACY, wire, net->label);
	else
		e2e_print_item(item, "E2E cannot be kept per network for %s (its name holds the control character 0x1F, or the network has no name)",
		               wire);
	g_free(scoped);
}

/* _resolve_ctx_for_command: the context of the window on its network;
   old rows are used where they are while erssi uses one network (only
   the user's own line, or /e2e adopt, takes them over). NULL when there
   is none; *refused: the reason is printed already (E2E there was set up
   with no network, or no server) */
static char *resolve_ctx(E2E_JSON *kr, WI_ITEM_REC *item, const char *nick, gboolean *refused)
{
	E2E_NET net;
	char *wire, *ctx;

	*refused = FALSE;
	wire = window_wire(kr, item, nick);
	if (wire == NULL)
		return NULL;
	if (item->server == NULL) {
		e2e_print_item(item, "not connected to a server — the network of %s is not known", wire);
		*refused = TRUE;
		g_free(wire);
		return NULL;
	}
	e2e_server_net(item->server, &net);
	ctx = e2e_ctx_find(kr, &net, wire);
	if (ctx == NULL) {
		print_blocked(item, &net, wire);
		*refused = TRUE;
	}
	e2e_net_clear(&net);
	g_free(wire);
	return ctx;
}

/* the context of the window on its network, nothing printed; NULL when
   there is none */
static char *window_ctx_quiet(E2E_JSON *kr, WI_ITEM_REC *item)
{
	E2E_NET net;
	char *wire, *ctx;

	if (item == NULL || item->server == NULL)
		return NULL;
	wire = window_wire(kr, item, NULL);
	if (wire == NULL)
		return NULL;
	e2e_server_net(item->server, &net);
	ctx = e2e_ctx_find(kr, &net, wire);
	e2e_net_clear(&net);
	g_free(wire);
	return ctx;
}

/* the context /e2e off writes for wire on net: the one in use; for an old
   one of unknown network a config of this network with E2E off (it hides
   the old one here); for a name that cannot be scoped the old row itself */
static char *off_ctx(E2E_JSON *kr, const E2E_NET *net, const char *wire, gboolean *on)
{
	char *ctx;

	ctx = e2e_ctx_find(kr, net, wire);
	if (ctx == NULL) {
		*on = e2e_ctx_enabled(kr, wire);
		ctx = e2e_ctx_scoped(net, wire);
		return ctx != NULL ? ctx : g_strdup(wire);
	}
	*on = e2e_ctx_enabled(kr, ctx);
	return ctx;
}

/* a nick through the keyring, an ident@host as it is */
static char *resolve_handle(const E2E_JSON *kr, const char *who)
{
	if (strchr(who, '@') != NULL)
		return g_strdup(who);
	return g_strdup(e2e_find_handle_by_nick(kr, who));
}

/* "@<own>" on the network of ctx, where its rows are: the old ones (the
   DM sessions of every peer) while erssi uses one network, so a trust
   change reaches them; this network's when old ones cannot be used here */
static char *own_ctx(const E2E_JSON *kr, SERVER_REC *server, const char *ctx)
{
	char *own = e2e_incoming_ctx_for(server, ctx), *found;
	E2E_NET net;

	if (own == NULL || server == NULL || strcmp(own, ctx) == 0)
		return own;
	e2e_server_net(server, &net);
	found = e2e_ctx_find(kr, &net, e2e_ctx_wire(own));
	e2e_net_clear(&net);
	if (found == NULL)
		return own;
	g_free(own);
	return found;
}

/* the "@<own>" context a DM trust change must touch as well; NULL (and the
   refusal printed) while our handle is unknown - guessing would miss the
   real session or touch another network's */
static char *own_ctx_or_warn(E2E_JSON *kr, WI_ITEM_REC *item, const char *ctx, const char *cmd,
                             gboolean *ok)
{
	char *own;

	*ok = TRUE;
	if (*e2e_ctx_wire(ctx) != '@')
		return g_strdup(ctx);
	own = own_ctx(kr, item != NULL ? item->server : NULL, ctx);
	if (own == NULL) {
		e2e_print_item(item, "%s: own identity not learned yet (waiting for the WHOIS reply) — try again in a moment",
		               cmd);
		*ok = FALSE;
	}
	return own;
}

static E2E_JSON *incoming_row(E2E_JSON *kr, const char *handle, const char *ctx)
{
	char *k = g_strconcat(handle, "|", ctx, NULL);
	E2E_JSON *row = e2e_json_get(e2e_json_get(kr, "incoming"), k);

	g_free(k);
	return row;
}

/* exactly the two context forms of this server, never handle-wide */
static void set_incoming_trust(E2E_JSON *kr, const char *handle, const char *ctx,
                               const char *own_ctx, const char *status)
{
	E2E_JSON *row = incoming_row(kr, handle, ctx);

	if (row != NULL)
		e2e_json_set_string(row, "status", status);
	if (own_ctx != NULL && strcmp(own_ctx, ctx) != 0) {
		row = incoming_row(kr, handle, own_ctx);
		if (row != NULL)
			e2e_json_set_string(row, "status", status);
	}
}

static int delete_rows(E2E_JSON *kr, const char *handle, const char *ctx, const char *own_ctx)
{
	static const char *const stores[] = { "incoming", "pending_inbound", NULL };
	int i, n = 0;
	char *k;

	for (i = 0; stores[i] != NULL; i++) {
		E2E_JSON *store = e2e_json_get(kr, stores[i]);

		k = g_strconcat(handle, "|", ctx, NULL);
		n += e2e_json_remove(store, k);
		g_free(k);
		if (own_ctx != NULL && strcmp(own_ctx, ctx) != 0) {
			k = g_strconcat(handle, "|", own_ctx, NULL);
			n += e2e_json_remove(store, k);
			g_free(k);
		}
	}
	return n;
}

/* keys "<handle>|..." or "...|<handle>" of a store */
static int delete_handle_keys(E2E_JSON *store, const char *handle)
{
	GPtrArray *keys = e2e_json_keys(store);
	GPtrArray *doomed = g_ptr_array_new_with_free_func(g_free);
	char *head = g_strconcat(handle, "|", NULL), *tail = g_strconcat("|", handle, NULL);
	guint i;
	int n;

	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);

		if (g_str_has_prefix(k, head) || g_str_has_suffix(k, tail))
			g_ptr_array_add(doomed, g_strdup(k));
	}
	g_ptr_array_unref(keys);
	for (i = 0; i < doomed->len; i++)
		e2e_json_remove(store, g_ptr_array_index(doomed, i));
	n = doomed->len;
	g_ptr_array_unref(doomed);
	g_free(head);
	g_free(tail);
	return n;
}

static void filter_trust_changes(E2E_JSON *kr, const char *handle, GPtrArray *taken)
{
	E2E_JSON *list = e2e_json_get(kr, "pending_trust_change");
	guint i;

	for (i = 0; i < list->array->len;) {
		E2E_JSON *row = g_ptr_array_index(list->array, i);

		if (g_strcmp0(e2e_json_get_string(row, "handle"), handle) == 0) {
			if (taken != NULL)
				g_ptr_array_add(taken, e2e_json_copy(row));
			g_ptr_array_remove_index(list->array, i);
		} else {
			i++;
		}
	}
}

/* nick: the query's nick for a "@<handle>" context (the gate refuses a
   DM to that nick at another ident@host), NULL for a channel */
static void set_channel(E2E_JSON *kr, const char *ctx, int enabled, const char *mode,
                        const char *nick)
{
	E2E_JSON *cfg = e2e_json_new(E2E_JSON_OBJECT);
	const char *adopted = e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "channels"), ctx),
	                                          E2E_ADOPTED_MEMBER);

	e2e_json_set_int(cfg, "enabled", enabled);
	e2e_json_set_string(cfg, "mode", mode);
	if (nick != NULL && *e2e_ctx_wire(ctx) == '@')
		e2e_json_set_string(cfg, "nick", nick);
	/* the network that took the name over stays marked */
	if (adopted != NULL)
		e2e_json_set_string(cfg, E2E_ADOPTED_MEMBER, adopted);
	e2e_json_set(e2e_json_get(kr, "channels"), ctx, cfg);
}

static const char *query_nick(WI_ITEM_REC *item)
{
	return item != NULL && !e2e_is_channel(item_name(item)) ? item_name(item) : NULL;
}

static guint e2e_dm_configs_of_count(const E2E_JSON *kr, const E2E_NET *net, const char *nick)
{
	GPtrArray *on = e2e_dm_configs_of(kr, net, nick);
	guint n = on->len;

	g_ptr_array_unref(on);
	return n;
}

#define LOST_REFUSAL "the keyring is lost (a corrupt one was moved aside) — /e2e reset starts a new one; until then nothing is sent and nothing saved"

static const char *str_or(const char *s, const char *def)
{
	return s != NULL ? s : def;
}

/* SYNTAX: E2E [HELP] */
/* SYNTAX: E2E ON */
static void cmd_on(WI_ITEM_REC *item)
{
	E2E_JSON *kr = e2e_load();
	gboolean refused;
	char *ctx = resolve_ctx(kr, item, NULL, &refused);

	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "%s", NOT_IN_CONTEXT);
	} else {
		set_channel(kr, ctx, 1, "normal", query_nick(item));
		e2e_save(kr);
		e2e_print_item(item, "enabled on %s (mode=normal)", shown(ctx));
		e2e_warn_session_only(item);
	}
	g_free(ctx);
	e2e_json_free(kr);
}

/* ctx off, its mode and query nick kept */
static void disable_ctx(E2E_JSON *kr, const char *ctx, const char *nick)
{
	E2E_JSON *cfg = e2e_json_get(e2e_json_get(kr, "channels"), ctx);
	char *mode = g_strdup(str_or(e2e_json_get_string(cfg, "mode"), "normal"));
	char *cnick = g_strdup(nick != NULL ? nick : e2e_json_get_string(cfg, "nick"));

	set_channel(kr, ctx, 0, mode, cnick);
	g_free(mode);
	g_free(cnick);
}

/* what is still on for the nick of a query */
static void print_still_on(WI_ITEM_REC *item, E2E_JSON *kr, const E2E_NET *net, const char *nick)
{
	GPtrArray *on = e2e_dm_configs_of(kr, net, nick);
	guint i;

	for (i = 0; i < on->len; i++)
		e2e_print_item(item, "E2E is still on for %s at %s — a message to %s is refused while their address is another; /e2e off %s turns it off",
		               nick, (char *) g_ptr_array_index(on, i), nick,
		               (char *) g_ptr_array_index(on, i));
	g_ptr_array_unref(on);
}

/* SYNTAX: E2E OFF [<ident@host>|<channel>] */
static void cmd_off(SERVER_REC *server, WI_ITEM_REC *item, const char *who)
{
	E2E_JSON *kr;
	E2E_NET net;
	const char *nick = query_nick(item);
	char *ctx = NULL, *wire = NULL, *handle;
	GPtrArray *targets;
	gboolean on;
	guint i;

	/* the network of the window (of the active server without one) */
	if (item != NULL && item->server != NULL)
		server = item->server;
	if (server == NULL) {
		e2e_print_item(item, "not connected to a server — the network is not known");
		return;
	}
	kr = e2e_load();
	e2e_server_net(server, &net);
	if (who != NULL) {
		/* an address of a query (or a channel), from any window */
		wire = *who == '@' || e2e_is_channel(who) ? g_strdup(who) : g_strconcat("@", who, NULL);
		if (*wire == '@' && strchr(wire + 1, '@') == NULL) {
			e2e_print_item(item, "usage: /e2e off [<ident@host>|<channel>]");
		} else {
			ctx = off_ctx(kr, &net, wire, &on);
			if (!on) {
				e2e_print_item(item, "E2E is not on for %s", shown(ctx));
			} else {
				disable_ctx(kr, ctx, NULL);
				e2e_print_item(item, "disabled on %s", shown(ctx));
			}
			e2e_save(kr);
		}
	} else if (nick == NULL) {
		wire = window_wire(kr, item, NULL);
		if (wire == NULL) {
			e2e_print_item(item, "%s", NOT_IN_CONTEXT);
		} else {
			ctx = off_ctx(kr, &net, wire, &on);
			disable_ctx(kr, ctx, NULL);
			e2e_save(kr);
			e2e_print_item(item, "disabled on %s", shown(ctx));
		}
	} else {
		/* a query: its address only (the live one, else the one config
		   of this query); other addresses of the nick are named */
		handle = e2e_resolve_dm_handle(item->server, kr, nick);
		targets = e2e_query_off_targets(kr, &net, nick, handle);
		g_free(handle);
		for (i = 0; i < targets->len; i++) {
			char *tctx = off_ctx(kr, &net, g_ptr_array_index(targets, i), &on);

			disable_ctx(kr, tctx, nick);
			e2e_print_item(item, "disabled on %s", shown(tctx));
			g_free(tctx);
		}
		if (targets->len > 0)
			e2e_save(kr);
		else if (e2e_dm_configs_of_count(kr, &net, nick) == 0)
			e2e_print_item(item, "%s", NOT_IN_CONTEXT);
		else
			e2e_print_item(item, "the address of %s is not known now — nothing turned off", nick);
		print_still_on(item, kr, &net, nick);
		g_ptr_array_unref(targets);
	}
	e2e_net_clear(&net);
	g_free(wire);
	g_free(ctx);
	e2e_json_free(kr);
}

/* SYNTAX: E2E MODE [normal|auto-accept|auto|quiet] */
static void cmd_mode(WI_ITEM_REC *item, const char *mode)
{
	E2E_JSON *kr;
	gboolean refused;
	char *ctx;

	if (mode == NULL)
		mode = "normal";
	if (strcmp(mode, "auto") == 0)
		mode = "auto-accept";
	if (strcmp(mode, "auto-accept") != 0 && strcmp(mode, "normal") != 0 &&
	    strcmp(mode, "quiet") != 0) {
		e2e_print_item(item, "invalid mode: %s", mode);
		return;
	}
	kr = e2e_load();
	ctx = resolve_ctx(kr, item, NULL, &refused);
	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "%s", NOT_IN_CONTEXT);
	} else {
		set_channel(kr, ctx, 1, mode, query_nick(item));
		e2e_save(kr);
		e2e_print_item(item, "mode=%s on %s", mode, shown(ctx));
		e2e_warn_session_only(item);
	}
	g_free(ctx);
	e2e_json_free(kr);
}

/* SYNTAX: E2E FINGERPRINT */
static void cmd_fingerprint(WI_ITEM_REC *item)
{
	E2E_JSON *kr = e2e_load();
	E2E_IDENTITY id;

	if (e2e_ensure_identity(kr, &id)) {
		e2e_print_item(item, "Fingerprint (mine):");
		e2e_print_item(item, "  hex  %s", id.fp_hex);
		e2e_identity_wipe(&id);
	}
	e2e_json_free(kr);
}

static guint enabled_count(const E2E_JSON *kr)
{
	E2E_JSON *channels = e2e_json_get(kr, "channels");
	GPtrArray *keys = e2e_json_keys(channels);
	guint i, n = 0;

	for (i = 0; i < keys->len; i++)
		n += e2e_ctx_enabled(kr, g_ptr_array_index(keys, i));
	g_ptr_array_unref(keys);
	return n;
}

/* SYNTAX: E2E STATUS */
static void cmd_status(WI_ITEM_REC *item)
{
	E2E_JSON *kr = e2e_load();
	const char *fp = e2e_json_get_string(e2e_json_get(kr, "identity"), "fp");
	GPtrArray *keys = e2e_json_keys(e2e_json_get(kr, "channels"));
	GString *on = g_string_new(NULL), *old = g_string_new(NULL);
	guint i;

	e2e_print_item(item, "identity=%s peers=%u enabled_channels=%u", str_or(fp, "(none)"),
	               e2e_json_size(e2e_json_get(kr, "incoming")), enabled_count(kr));
	/* where, by network; old entries (of no network) named as such */
	for (i = 0; i < keys->len; i++) {
		const char *ctx = g_ptr_array_index(keys, i);

		if (!e2e_ctx_enabled(kr, ctx))
			continue;
		g_string_append_printf(on, "%s%s", on->len > 0 ? ", " : "", shown(ctx));
		if (strchr(ctx, E2E_SCOPE_SEP) == NULL)
			g_string_append_printf(old, "%s%s", old->len > 0 ? ", " : "", ctx);
	}
	g_ptr_array_unref(keys);
	if (on->len > 0)
		e2e_print_item(item, "encrypted: %s", on->str);
	if (old->len > 0)
		e2e_print_item(item, "old entries of no network (set up by rpe2e.pl): %s - used where they are while erssi uses one network; /e2e adopt in their window binds them to its network",
		               old->str);
	g_string_free(on, TRUE);
	g_string_free(old, TRUE);
	if (e2e_keyring_is_lost())
		e2e_print_item(item, "%s", LOST_REFUSAL);
	e2e_json_free(kr);
}

static void split_key(const char *key, char **a, char **b)
{
	const char *bar = strchr(key, '|');

	*a = bar != NULL ? g_strndup(key, bar - key) : g_strdup(key);
	*b = bar != NULL ? g_strdup(bar + 1) : NULL;
}

/* "trusted" means we read them; whether they get our key is "accepted" */
static const char *session_label(const E2E_JSON *kr, const char *handle, const char *ctx,
                                 const E2E_JSON *row)
{
	const char *status = str_or(e2e_json_get_string(row, "status"), "pending");

	if (strcmp(status, "trusted") == 0 && ctx != NULL &&
	    !e2e_session_accepted(kr, handle, ctx, e2e_json_get_string(row, "fp")))
		return "key held, not accepted";
	return status;
}

/* the damaged entries of a query context (of ctx; NULL: of all): rows of
   another address than the query's own - a request
   rpe2e.pl kept, its acceptance, a session or recipient row. They get no
   key; listed once per address and context. How many. */
static guint print_damaged(WI_ITEM_REC *item, const E2E_JSON *kr, const char *ctx)
{
	static const char *const stores[] = { "accepted", "pending_inbound", "incoming", NULL };
	E2E_JSON *channels = e2e_json_get(kr, "channels"), *recipients;
	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray *found = g_ptr_array_new_with_free_func(g_free), *keys;
	guint i, j, n;

	for (i = 0; stores[i] != NULL; i++) {
		keys = e2e_json_keys(e2e_json_get(kr, stores[i]));
		for (j = 0; j < keys->len; j++) {
			char *h, *c;

			split_key(g_ptr_array_index(keys, j), &h, &c);
			/* an incoming row under "@<own>" is a session of ours:
			   only a query with a config of its own is a peer's */
			if (c != NULL && (ctx == NULL || strcmp(c, ctx) == 0) && e2e_dm_row_foreign(h, c) &&
			    (strcmp(stores[i], "incoming") != 0 || e2e_json_get(channels, c) != NULL)) {
				g_ptr_array_add(found, h);
				g_ptr_array_add(found, c);
			} else {
				g_free(h);
				g_free(c);
			}
		}
		g_ptr_array_unref(keys);
	}
	recipients = e2e_json_get(kr, "outgoing_recipients");
	keys = e2e_json_keys(recipients);
	for (j = 0; j < keys->len; j++) {
		E2E_JSON *row = e2e_json_get(recipients, g_ptr_array_index(keys, j));
		const char *h = e2e_json_get_string(row, "handle"), *c = e2e_json_get_string(row, "channel");

		if (h != NULL && c != NULL && (ctx == NULL || strcmp(c, ctx) == 0) &&
		    e2e_dm_row_foreign(h, c)) {
			g_ptr_array_add(found, g_strdup(h));
			g_ptr_array_add(found, g_strdup(c));
		}
	}
	g_ptr_array_unref(keys);
	for (i = 0; i + 1 < found->len; i += 2) {
		const char *h = g_ptr_array_index(found, i), *c = g_ptr_array_index(found, i + 1);
		char *k = g_strconcat(h, "\x1f", c, NULL);

		if (g_hash_table_contains(seen, k)) {
			g_free(k);
			continue;
		}
		g_hash_table_add(seen, k);
		e2e_print_item(item, "  damaged: %s on %s — an entry of another address for this query (a request rpe2e.pl kept): it gets no key; /e2e decline %s in that query removes it",
		               shown_text(h), shown(c), shown_text(h));
	}
	n = g_hash_table_size(seen);
	g_hash_table_destroy(seen);
	g_ptr_array_unref(found);
	return n;
}

/* damaged entries that may hold our key (ctx; NULL: all) make it rotate:
   saved, told once */
static void rotate_damaged(WI_ITEM_REC *item, E2E_JSON *kr, const char *ctx)
{
	GPtrArray *found = e2e_rotate_for_damaged(kr, ctx);

	if (found->len > 0 && e2e_save(kr))
		e2e_tell_damaged(item, found);
	g_ptr_array_unref(found);
}

/* SYNTAX: E2E LIST [-all] */
static void cmd_list(WI_ITEM_REC *item, char **args)
{
	E2E_JSON *kr = e2e_load(), *src;
	GPtrArray *keys, *pending;
	E2E_NET net;
	gboolean all = FALSE, any = FALSE, refused;
	char *ctx, *handle, *channel;
	guint i;

	for (i = 0; args[i] != NULL; i++)
		if (strcmp(args[i], "-all") == 0)
			all = TRUE;
	if (all) {
		e2e_print_item(item, "Keyring (all)");
		src = e2e_json_get(kr, "peers");
		keys = e2e_json_keys(src);
		if (keys->len > 0)
			e2e_print_item(item, "Peers");
		for (i = 0; i < keys->len; i++) {
			const char *fp = g_ptr_array_index(keys, i);
			E2E_JSON *p = e2e_json_get(src, fp);

			const char *status = str_or(e2e_json_get_string(p, "status"), "pending");

			if (strcmp(status, "trusted") == 0 && !e2e_fingerprint_accepted(kr, fp))
				status = "key held, not accepted";
			e2e_print_item(item, "  %s  [%s]  nick=%s fp=%.16s",
			               shown_text(str_or(e2e_json_get_string(p, "last_handle"), "—")), status,
			               shown_text(str_or(e2e_json_get_string(p, "last_nick"), "—")),
			               shown_text(fp));
			any = TRUE;
		}
		g_ptr_array_unref(keys);
		src = e2e_json_get(kr, "incoming");
		keys = e2e_json_keys(src);
		if (keys->len > 0)
			e2e_print_item(item, "Incoming Sessions");
		for (i = 0; i < keys->len; i++) {
			E2E_JSON *row = e2e_json_get(src, g_ptr_array_index(keys, i));

			split_key(g_ptr_array_index(keys, i), &handle, &channel);
			e2e_print_item(item, "  %s  %s  [%s]  fp=%.16s", shown_text(handle), shown(channel),
			               channel != NULL && e2e_dm_row_foreign(handle, channel) &&
			               e2e_json_get(e2e_json_get(kr, "channels"), channel) != NULL ? "damaged"
			               : session_label(kr, handle, channel, row),
			               shown_text(str_or(e2e_json_get_string(row, "fp"), "")));
			g_free(handle);
			g_free(channel);
			any = TRUE;
		}
		g_ptr_array_unref(keys);
		if (print_damaged(item, kr, NULL) > 0)
			any = TRUE;
		rotate_damaged(item, kr, NULL);
		if (!any)
			e2e_print_item(item, "(no remembered E2E state)");
		e2e_json_free(kr);
		return;
	}
	if (item == NULL) {
		e2e_print_item(NULL, "not in a chat buffer");
		e2e_json_free(kr);
		return;
	}
	ctx = resolve_ctx(kr, item, NULL, &refused);
	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "cannot resolve current context");
		e2e_json_free(kr);
		return;
	}
	/* a query lists everything from this peer on this network (DM
	   sessions live under "@<own>", trust markers under "@<peer>"), a
	   channel its sessions */
	e2e_server_net(item->server, &net);
	src = e2e_json_get(kr, "incoming");
	keys = e2e_json_keys(src);
	for (i = 0; i < keys->len; i++) {
		E2E_JSON *row = e2e_json_get(src, g_ptr_array_index(keys, i));
		const char *wire = e2e_ctx_wire(ctx);
		gboolean match;

		split_key(g_ptr_array_index(keys, i), &handle, &channel);
		match = *wire == '@' ? strcmp(handle, wire + 1) == 0 && channel != NULL &&
		                       e2e_ctx_on_net(channel, &net)
		                     : g_strcmp0(channel, ctx) == 0;
		if (match) {
			e2e_print_item(item, "  %s on %s  fp=%.16s  status=%s", shown_text(handle), shown(channel),
			               shown_text(str_or(e2e_json_get_string(row, "fp"), "")),
			               session_label(kr, handle, channel, row));
			any = TRUE;
		}
		g_free(handle);
		g_free(channel);
	}
	g_ptr_array_unref(keys);
	if (print_damaged(item, kr, ctx) > 0)
		any = TRUE;
	rotate_damaged(item, kr, ctx);
	/* the requests waiting here for /e2e accept */
	pending = e2e_pending_of(kr, ctx);
	for (i = 0; i < pending->len; i++) {
		E2E_PENDING *p = g_ptr_array_index(pending, i);

		e2e_print_item(item, "  pending: %s (%s)", shown_text(p->nick), shown_text(p->handle));
		any = TRUE;
	}
	g_ptr_array_unref(pending);
	if (!any)
		e2e_print_item(item, "no peers");
	e2e_net_clear(&net);
	g_free(ctx);
	e2e_json_free(kr);
}

/* SYNTAX: E2E HANDSHAKE <nick> */
static void cmd_handshake(SERVER_REC *server, WI_ITEM_REC *item, const char *nick)
{
	E2E_JSON *kr;
	E2E_IDENTITY id;
	gboolean refused;
	char *ctx, *kreq_ctx = NULL, *handle = NULL, *wire = NULL, *error = NULL;

	if (nick == NULL) {
		e2e_print_item(item, "usage: /e2e handshake <nick>");
		return;
	}
	if (item == NULL) {
		e2e_print_item(item, "not in a chat buffer");
		return;
	}
	if (!IS_IRC_SERVER(server) || !server->connected) {
		e2e_print_item(item, "not connected to a server");
		return;
	}
	kr = e2e_load();
	ctx = resolve_ctx(kr, item, nick, &refused);
	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "cannot resolve handle for %s", nick);
		goto out;
	}
	if (!e2e_ctx_enabled(kr, ctx)) {
		e2e_print_item(item, "e2e not enabled on %s", shown(ctx));
		goto out;
	}
	/* a KEYREQ asks for the direction WE receive: for a DM it is stamped
	   with our own handle; the peer-keyed ctx is only the config key */
	kreq_ctx = own_ctx(kr, server, ctx);
	if (kreq_ctx == NULL) {
		e2e_print_item(item, "own identity not learned yet (waiting for the WHOIS reply) — try again in a moment");
		goto out;
	}
	/* the live ident@host first: only an answer from that handle is taken */
	handle = e2e_resolve_dm_handle(server, kr, nick);
	if (handle == NULL) {
		e2e_print_item(item, "cannot resolve handle for %s — wait until they are in a common channel or send you a message",
		               nick);
		goto out;
	}
	if (!e2e_ensure_identity(kr, &id))
		goto out;
	wire = e2e_build_keyreq(kr, &id, kreq_ctx, handle, &error);
	e2e_identity_wipe(&id);
	if (wire == NULL) {
		e2e_print_item(item, "handshake failed: %s", error);
		goto out;
	}
	e2e_save(kr);
	e2e_send_notice(server, nick, wire);
	e2e_print_item(item, "KEYREQ sent to %s for %s", nick, shown(ctx));
	e2e_print_debug(server, ctx, nick, "TX KEYREQ to %s for %s", nick, shown(ctx));
out:
	g_free(error);
	g_free(wire);
	g_free(handle);
	g_free(kreq_ctx);
	g_free(ctx);
	e2e_json_free(kr);
}

/* the handle and context of /e2e <cmd> <nick>, or NULL with the reason
   printed */
static gboolean nick_context(E2E_JSON *kr, WI_ITEM_REC *item, const char *nick,
                             char **handle, char **ctx)
{
	gboolean refused;

	*ctx = NULL;
	*handle = resolve_handle(kr, nick);
	if (*handle == NULL) {
		e2e_print_item(item, "cannot resolve handle for %s", nick);
		return FALSE;
	}
	*ctx = resolve_ctx(kr, item, nick, &refused);
	if (*ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "cannot resolve context for %s", nick);
		return FALSE;
	}
	return TRUE;
}

/* /e2e pending: every request waiting for /e2e accept, of every network */
/* SYNTAX: E2E PENDING */
static void cmd_pending(WI_ITEM_REC *item)
{
	E2E_JSON *kr = e2e_load();
	GPtrArray *all = e2e_pending_all(kr), *bad;
	guint i;

	bad = e2e_pending_damaged(kr);
	if (all->len == 0)
		e2e_print_item(item, "No key requests wait for /e2e accept.");
	for (i = 0; i < bad->len; i++) {
		E2E_PENDING *p = g_ptr_array_index(bad, i);

		e2e_print_item(item, "  damaged: %s (%s) on %s — asks for the query of another address, never accepted; /e2e decline in that query removes it",
		               shown_text(p->nick), shown_text(p->handle), shown(p->ctx));
	}
	g_ptr_array_unref(bad);
	for (i = 0; i < all->len; i++) {
		E2E_PENDING *p = g_ptr_array_index(all, i);
		char *age = e2e_age_text(e2e_now() - p->received_at);

		e2e_print_item(item, "  %s (%s) on %s  fp=%s  %s ago — /e2e accept %s", shown_text(p->nick),
		               shown_text(p->handle), shown(p->ctx), p->fp16, age, shown_text(p->handle));
		g_free(age);
	}
	g_ptr_array_unref(all);
	e2e_json_free(kr);
}

/* the context of /e2e accept|decline [who]: without who, or for a nick,
   the window's (a nick in a query: that nick's query). An ident@host
   names a request of that address on this server: in a channel window
   the channel's when one waits there, else - in any window - the query
   context of that address; never the channel's sessions. NULL when there
   is none (*refused: said why) */
static char *who_ctx(E2E_JSON *kr, SERVER_REC *server, WI_ITEM_REC *item, const char *who,
                     gboolean *refused)
{
	E2E_NET net;
	char *wire, *ctx;
	guint n;

	*refused = FALSE;
	if (who == NULL || strchr(who, '@') == NULL)
		return resolve_ctx(kr, item, who, refused);
	wire = g_strconcat("@", who, NULL);
	if (server == NULL) {
		e2e_print_item(item, "not connected to a server — the network of %s is not known", wire);
		*refused = TRUE;
		g_free(wire);
		return NULL;
	}
	e2e_server_net(server, &net);
	if (item != NULL && e2e_is_channel(item_name(item))) {
		ctx = e2e_ctx_find(kr, &net, item_name(item));
		if (ctx != NULL && e2e_pending_handle(kr, ctx, who, net.casemap, &n) != NULL) {
			e2e_net_clear(&net);
			g_free(wire);
			return ctx;
		}
		g_free(ctx);
	}
	ctx = e2e_ctx_find(kr, &net, wire);
	if (ctx == NULL) {
		print_blocked(item, &net, wire);
		*refused = TRUE;
	}
	e2e_net_clear(&net);
	g_free(wire);
	return ctx;
}

/* the damaged requests of ctx (a query's request from another address) */
static GPtrArray *damaged_of(const E2E_JSON *kr, const char *ctx)
{
	GPtrArray *all = e2e_pending_damaged(kr);
	guint i;

	for (i = 0; i < all->len;) {
		if (strcmp(((E2E_PENDING *) g_ptr_array_index(all, i))->ctx, ctx) != 0)
			g_ptr_array_remove_index(all, i);
		else
			i++;
	}
	return all;
}

/* the requests of ctx that who names (every one for NULL), listed */
static void print_matches(WI_ITEM_REC *item, const E2E_JSON *kr, const char *ctx, const char *who,
                          E2E_CASEMAP map)
{
	GPtrArray *all = e2e_pending_of(kr, ctx);
	char *fwho = who != NULL && strchr(who, '@') == NULL ? e2e_casefold(who, map) : NULL;
	guint i;

	/* as e2e_pending_handle() matches */
	for (i = 0; i < all->len; i++) {
		E2E_PENDING *p = g_ptr_array_index(all, i);
		char *fnick = e2e_casefold(p->nick, map);

		if (who == NULL || strcmp(p->handle, who) == 0 || (fwho != NULL && strcmp(fnick, fwho) == 0))
			e2e_print_item(item, "  %s (%s)  fp=%s", shown_text(p->nick), shown_text(p->handle),
			               p->fp16);
		g_free(fnick);
	}
	g_free(fwho);
	g_ptr_array_unref(all);
}

/* a nick the server takes as one target: no space, comma, line break or
   control byte, not empty, not ":"-led (a NOTICE to it stays one line) */
static gboolean nick_sendable(const char *nick)
{
	const unsigned char *p;

	if (nick == NULL || *nick == '\0' || *nick == ':')
		return FALSE;
	for (p = (const unsigned char *) nick; *p != '\0'; p++)
		if (*p <= ' ' || *p == ',' || *p == 0x7f)
			return FALSE;
	return TRUE;
}

/*
 The ident@host /e2e <cmd> [who] acts on in ctx, NULL (said why) when it
 is none:
  - the only request waiting in ctx that who names - by its address, or
    its nick under the server's casemapping; without who the only one
    there. Never another context's or network's (ctx is the window's).
  - several: none, they are listed;
  - none, who given: who's address (an ident@host as it is, a nick from
    the keyring) for a session without a request - unless a request of
    that address waits here, which then came under another nick
 *nick: the nick a KEYRSP goes to (who's, else the request's), or NULL.
*/
static char *pick_handle(const E2E_JSON *kr, SERVER_REC *server, WI_ITEM_REC *item,
                         const char *ctx, const char *who, const char *cmd, char **nick)
{
	E2E_NET net;
	E2E_JSON *row;
	const char *found;
	char *handle, *k;
	guint n;

	*nick = NULL;
	e2e_server_net(server, &net);
	found = e2e_pending_handle(kr, ctx, who, net.casemap, &n);
	if (n > 1) {
		e2e_print_item(item, "%u key requests wait on %s%s%s:", n, shown(ctx),
		               who != NULL ? " for " : "", who != NULL ? shown_text(who) : "");
		print_matches(item, kr, ctx, who, net.casemap);
		e2e_print_item(item, "usage: /e2e %s [<nick>|<ident@host>] — name one of them by its ident@host",
		               cmd);
		e2e_net_clear(&net);
		return NULL;
	}
	e2e_net_clear(&net);
	if (n == 1) {
		k = g_strconcat(found, "|", ctx, NULL);
		row = e2e_json_get(e2e_json_get(kr, "pending_inbound"), k);
		g_free(k);
		*nick = g_strdup(who != NULL && strchr(who, '@') == NULL ? who
		                 : e2e_json_get_string(row, "sender_nick"));
		return g_strdup(found);
	}
	if (who == NULL) {
		GPtrArray *bad = damaged_of(kr, ctx);
		guint i;

		/* told here, never picked: /e2e decline (alone) removes them */
		for (i = 0; i < bad->len; i++) {
			E2E_PENDING *p = g_ptr_array_index(bad, i);

			e2e_print_item(item, "a damaged key request from %s (%s) on %s is ignored: it asks for the query of another address — /e2e decline here removes it",
			               shown_text(p->nick), shown_text(p->handle), shown(ctx));
		}
		g_ptr_array_unref(bad);
		e2e_print_item(item, "no key request waits on %s (/e2e pending lists all of them)", shown(ctx));
		e2e_print_item(item, "usage: /e2e %s [<nick>|<ident@host>]", cmd);
		return NULL;
	}
	handle = resolve_handle(kr, who);
	if (handle == NULL) {
		e2e_print_item(item, "cannot resolve handle for %s", shown_text(who));
		return NULL;
	}
	k = g_strconcat(handle, "|", ctx, NULL);
	row = e2e_json_get(e2e_json_get(kr, "pending_inbound"), k);
	g_free(k);
	if (row != NULL) {
		if (strchr(who, '@') == NULL)
			/* a nick of the keyring, but the request came under another */
			e2e_print_item(item, "the key request of %s on %s came under another nick — /e2e %s %s",
			               shown_text(handle), shown(ctx), cmd, shown_text(handle));
		else
			/* its members do not name it: never acted on */
			e2e_print_item(item, "the stored key request of %s on %s is damaged — /e2e forget %s removes it",
			               shown_text(handle), shown(ctx), shown_text(handle));
		g_free(handle);
		return NULL;
	}
	if (strchr(who, '@') == NULL)
		*nick = g_strdup(who);
	return handle;
}

/* /e2e accept of handle in ctx, picked already: answered (KEYRSP to
   nick) or trusted, saved, told in item's window. NULL when done, else
   why not, for "e2e action refused": "no_identity", "bad_nick" (the
   request's nick is no target), "no_key", "damaged", "failed",
   "no_request" (nothing waits any more) or "not_peer" */
static const char *accept_handle(E2E_JSON *kr, SERVER_REC *server, WI_ITEM_REC *item,
                                 const char *ctx, const char *handle, const char *nick)
{
	E2E_IDENTITY id;
	char *own = NULL, *rsp = NULL, *recip = NULL;
	const char *name, *refused = NULL;

	name = shown_text(nick != NULL ? nick : handle);
	if (!e2e_ensure_identity(kr, &id))
		return "no_identity";
	own = *e2e_ctx_wire(ctx) == '@' ? own_ctx(kr, server, ctx) : NULL;
	switch (e2e_accept(kr, &id, handle, ctx, own, e2e_own_handle(server), &rsp, &recip)) {
	case E2E_ACCEPT_SENT:
		if (!nick_sendable(nick)) {
			/* not saved: the request still waits */
			e2e_print_item(item, "cannot answer %s on %s: the nick of the request is not known or not one a message can go to",
			               shown_text(handle), shown(ctx));
			refused = "bad_nick";
			break;
		}
		e2e_save(kr);
		e2e_send_notice(server, nick, rsp);
		if (recip != NULL)
			e2e_send_notice(server, nick, recip);
		e2e_print_item(item, "accepted %s (%s) on %s — KEYRSP sent", name, shown_text(handle),
		               shown(ctx));
		e2e_print_debug(server, ctx, nick, "TX KEYRSP to %s for %s", name, shown(ctx));
		if (recip != NULL)
			e2e_print_debug(server, ctx, nick, "TX KEYREQ to %s for %s", name, shown(ctx));
		break;
	case E2E_ACCEPT_TRUSTED:
		e2e_save(kr);
		e2e_print_item(item, "accepted %s (%s) on %s — their next key request is answered", name,
		               shown_text(handle), shown(ctx));
		break;
	case E2E_ACCEPT_NO_KEY:
		e2e_print_item(item, "cannot accept %s on %s: no key from them is stored (only a request that is gone) — wait for their next request, or /e2e handshake %s",
		               name, shown(ctx), name);
		refused = "no_key";
		break;
	case E2E_ACCEPT_DAMAGED:
		e2e_print_item(item, "the pending exchange from %s on %s is damaged — /e2e decline %s removes it",
		               name, shown(ctx), shown_text(handle));
		refused = "damaged";
		break;
	case E2E_ACCEPT_FAILED:
		e2e_print_item(item, "cannot answer the key exchange from %s on %s (key material damaged?)",
		               name, shown(ctx));
		refused = "failed";
		break;
	case E2E_ACCEPT_NOTHING:
		e2e_print_item(item, "no pending exchange or session for %s on %s", name, shown(ctx));
		refused = "no_request";
		break;
	case E2E_ACCEPT_NOT_PEER:
		e2e_print_item(item, "cannot accept %s (%s) on %s: the key of a query goes to its own address only — nothing sent",
		               name, shown_text(handle), shown(ctx));
		refused = "not_peer";
		break;
	}
	e2e_identity_wipe(&id);
	g_free(own);
	g_free(rsp);
	g_free(recip);
	return refused;
}

/* SYNTAX: E2E ACCEPT [<nick>|<ident@host>] */
void e2e_do_accept(SERVER_REC *server, WI_ITEM_REC *item, const char *who)
{
	E2E_JSON *kr;
	gboolean refused;
	char *handle = NULL, *ctx, *nick = NULL;

	/* the window's server; the active one only for a window without an
	   item (the status window) */
	if (item != NULL)
		server = item->server;
	if (who != NULL && *who == '\0')
		who = NULL;
	if (!IS_IRC_SERVER(server) || !server->connected) {
		e2e_print_item(item, "not connected to a server");
		return;
	}
	kr = e2e_load();
	ctx = who_ctx(kr, server, item, who, &refused);
	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "%s", NOT_IN_CONTEXT);
		goto out;
	}
	handle = pick_handle(kr, server, item, ctx, who, "accept", &nick);
	if (handle != NULL)
		accept_handle(kr, server, item, ctx, handle, nick);
out:
	g_free(nick);
	g_free(handle);
	g_free(ctx);
	e2e_json_free(kr);
}

/* /e2e decline of handle in ctx, picked already: its request goes, its
   session is revoked and its acceptance forgotten; saved, told */
static void decline_handle(E2E_JSON *kr, WI_ITEM_REC *item, const char *ctx, const char *handle,
                           const char *nick)
{
	E2E_JSON *row;
	char *k;

	k = g_strconcat(handle, "|", ctx, NULL);
	e2e_json_remove(e2e_json_get(kr, "pending_inbound"), k);
	g_free(k);
	row = incoming_row(kr, handle, ctx);
	if (row != NULL)
		e2e_json_set_string(row, "status", "revoked");
	e2e_forget_acceptance(kr, handle, ctx);
	e2e_save(kr);
	e2e_print_item(item, "declined %s (%s) on %s", shown_text(nick != NULL ? nick : handle),
	               shown_text(handle), shown(ctx));
}

/* SYNTAX: E2E DECLINE [<nick>|<ident@host>] */
void e2e_do_decline(SERVER_REC *server, WI_ITEM_REC *item, const char *who)
{
	E2E_JSON *kr;
	gboolean refused, rotated = FALSE;
	char *handle = NULL, *ctx = NULL, *nick = NULL;
	guint n, dropped = 0;

	if (item != NULL)
		server = item->server;
	if (who != NULL && *who == '\0')
		who = NULL;
	if (server == NULL) {
		e2e_print_item(item, "not connected to a server — the network is not known");
		return;
	}
	kr = e2e_load();
	/* in a query, an address with damaged entries there (another
	   address's request or acceptance for this query):
	   those go */
	if (who != NULL && strchr(who, '@') != NULL && query_nick(item) != NULL &&
	    (ctx = window_ctx_quiet(kr, item)) != NULL && e2e_dm_row_foreign(who, ctx))
		dropped = e2e_drop_damaged_dm(kr, ctx, who, &rotated);
	if (dropped == 0) {
		g_free(ctx);
		ctx = who_ctx(kr, server, item, who, &refused);
		if (ctx == NULL) {
			if (!refused)
				e2e_print_item(item, "%s", NOT_IN_CONTEXT);
			goto out;
		}
		/* alone, with nothing waiting: every damaged entry here */
		if (who == NULL && e2e_pending_handle(kr, ctx, NULL, E2E_CASEMAP_RFC1459, &n) == NULL &&
		    n == 0)
			dropped = e2e_drop_damaged_dm(kr, ctx, NULL, &rotated);
	}
	if (dropped > 0) {
		e2e_save(kr);
		e2e_print_item(item, "removed %u damaged entr%s on %s (another address's request or acceptance for this query)%s",
		               dropped, dropped == 1 ? "y" : "ies", shown(ctx),
		               rotated ? " — your key here rotates with your next message" : "");
		goto out;
	}
	handle = pick_handle(kr, server, item, ctx, who, "decline", &nick);
	if (handle != NULL)
		decline_handle(kr, item, ctx, handle, nick);
out:
	g_free(nick);
	g_free(handle);
	g_free(ctx);
	e2e_json_free(kr);
}

/* told to whoever asked for the action (a frontend can answer its client
   with it): "e2e action refused" with a reason */
static void web_refused(SERVER_REC *server, const char *target, const char *handle,
                        const char *reason)
{
	signal_emit("e2e action refused", 4, server, target, handle, reason);
}

/* the fingerprint (16 hex digits) of the request of handle in ctx, as
   e2e_state lists it; "" when its key is damaged */
static char *pending_fp16(const E2E_JSON *kr, const char *ctx, const char *handle)
{
	GPtrArray *all = e2e_pending_of(kr, ctx);
	char *fp = NULL;
	guint i;

	for (i = 0; i < all->len && fp == NULL; i++) {
		E2E_PENDING *p = g_ptr_array_index(all, i);

		if (strcmp(p->handle, handle) == 0)
			fp = g_strdup(p->fp16);
	}
	g_ptr_array_unref(all);
	return fp != NULL ? fp : g_strdup("");
}

/*
 A frontend's accept or decline (e2e-web.c, "e2e action"): only of the
 request that waits in the window of target on server with exactly this
 ident@host - the window's own context, never another one the address
 also has (/e2e accept <ident@host> in a channel window may take a query
 request; this never does), never a session without a request, never a
 damaged one - and, with fingerprint, only when its key's fingerprint is
 that one (what the client showed). Looked up again in the keyring the
 action works on; nothing sent or changed otherwise. Not while rpe2e.pl
 is loaded (the script owns the keyring then) or while the keyring is
 lost. A refusal is also told with "e2e action refused".
*/
void e2e_web_action(SERVER_REC *server, const char *target, gboolean accept, const char *handle,
                    const char *fingerprint)
{
	const char *verb = accept ? "accept" : "decline", *found;
	WI_ITEM_REC *item;
	E2E_JSON *kr, *row;
	E2E_NET net;
	char *ctx, *dtarget, *picked, *nick, *k, *fp;
	const char *reason = NULL;
	guint n = 0;

	if (server == NULL || target == NULL || handle == NULL)
		return;
	/* a channel or query of this server (window_item_find() takes any
	   server for NULL - server is not NULL here) */
	item = window_item_find(server, target);
	if (item != NULL && (item->server != server || (!IS_CHANNEL(item) && !IS_QUERY(item))))
		item = NULL;
	dtarget = e2e_text_display(target);
	if (!e2e_native_active()) {
		e2e_print_item(item, "a frontend asked to %s %s on %s, but the native e2e module is inactive while rpe2e.pl is loaded — nothing sent",
		               verb, shown_text(handle), dtarget);
		g_free(dtarget);
		web_refused(server, target, handle, "inactive");
		return;
	}
	if (e2e_keyring_is_lost()) {
		e2e_print_item(item, "a frontend asked to %s %s on %s: %s", verb, shown_text(handle),
		               dtarget, LOST_REFUSAL);
		g_free(dtarget);
		web_refused(server, target, handle, "lost");
		return;
	}
	if (!IS_IRC_SERVER(server) || !server->connected) {
		e2e_print_item(item, "a frontend asked to %s %s on %s: not connected to the server — nothing sent",
		               verb, shown_text(handle), dtarget);
		g_free(dtarget);
		web_refused(server, target, handle, "not_connected");
		return;
	}
	kr = e2e_load();
	ctx = window_ctx_quiet(kr, item);
	found = NULL;
	if (ctx != NULL) {
		e2e_server_net(server, &net);
		found = e2e_pending_handle(kr, ctx, handle, net.casemap, &n);
		e2e_net_clear(&net);
	}
	if (found == NULL || n != 1 || strcmp(found, handle) != 0) {
		e2e_print_item(item, "a frontend asked to %s %s on %s, but no such request waits — nothing sent",
		               verb, shown_text(handle), dtarget);
		reason = "no_request";
		goto out;
	}
	/* the key the client showed: a request that came again with
	   another key since is not the one the user decided on */
	if (fingerprint != NULL) {
		fp = pending_fp16(kr, ctx, found);
		if (*fp == '\0' || strcmp(fp, fingerprint) != 0) {
			e2e_print_item(item, "a frontend asked to %s %s on %s for the key %s, but the request that waits has the key %s — nothing sent",
			               verb, shown_text(handle), dtarget, shown_text(fingerprint),
			               *fp != '\0' ? fp : "(damaged)");
			g_free(fp);
			reason = "mismatch";
			goto out;
		}
		g_free(fp);
	}
	/* found points into kr, into the row accept and decline remove */
	picked = g_strdup(found);
	k = g_strconcat(picked, "|", ctx, NULL);
	row = e2e_json_get(e2e_json_get(kr, "pending_inbound"), k);
	g_free(k);
	nick = g_strdup(e2e_json_get_string(row, "sender_nick"));
	e2e_print_item(item, "frontend: %s %s on %s", verb, shown_text(picked), shown(ctx));
	if (accept)
		reason = accept_handle(kr, server, item, ctx, picked, nick);
	else
		decline_handle(kr, item, ctx, picked, nick);
	g_free(nick);
	g_free(picked);
out:
	g_free(ctx);
	g_free(dtarget);
	/* told once the keyring (secret keys) is let go */
	e2e_json_free(kr);
	if (reason != NULL)
		web_refused(server, target, handle, reason);
}

/* SYNTAX: E2E REVOKE <nick> */
/* SYNTAX: E2E UNREVOKE <nick> */
static void cmd_revoke(WI_ITEM_REC *item, const char *nick, gboolean revoke)
{
	E2E_JSON *kr, *out, *peer;
	char *handle, *ctx, *own = NULL, *k;
	gboolean ok;

	if (nick == NULL) {
		e2e_print_item(item, "%s", revoke ? "usage: /e2e revoke <nick>" : "usage: /e2e unrevoke <nick>");
		return;
	}
	kr = e2e_load();
	if (!nick_context(kr, item, nick, &handle, &ctx))
		goto out;
	own = own_ctx_or_warn(kr, item, ctx, revoke ? "/e2e revoke" : "/e2e unrevoke", &ok);
	if (!ok)
		goto out;
	if (!revoke && e2e_unrevoke(kr, handle, ctx, own) == 0) {
		/* trusted without a key would show anything encrypted under
		   the placeholder as genuine */
		e2e_print_item(item, "cannot unrevoke %s on %s: no key from them is stored — wait for their key, or /e2e handshake %s",
		               nick, shown(ctx), nick);
		goto out;
	}
	if (revoke) {
		set_incoming_trust(kr, handle, ctx, own, "revoked");
		e2e_forget_acceptance(kr, handle, ctx);
		/* the peer must not read along any more: drop it from the REKEY
		   list and rotate our key */
		k = g_strconcat(ctx, "|", handle, NULL);
		e2e_json_remove(e2e_json_get(kr, "outgoing_recipients"), k);
		g_free(k);
		out = e2e_json_get(e2e_json_get(kr, "outgoing"), ctx);
		if (out != NULL)
			e2e_json_set_int(out, "pending_rotation", 1);
	}
	e2e_find_peer_by_handle(kr, handle, &peer);
	if (peer != NULL)
		e2e_json_set_string(peer, "status", revoke ? "revoked" : "trusted");
	e2e_save(kr);
	if (revoke)
		e2e_print_item(item, "revoked %s on %s — key will rotate", nick, shown(ctx));
	else
		e2e_print_item(item, "unrevoked %s on %s — you read them again; they get your key after /e2e accept %s",
		               nick, shown(ctx), nick);
out:
	g_free(own);
	g_free(handle);
	g_free(ctx);
	e2e_json_free(kr);
}

/* SYNTAX: E2E FORGET [-all] <nick|ident@host> */
static void cmd_forget(WI_ITEM_REC *item, char **args)
{
	static const char *const stores[] = {
		"incoming", "pending_inbound", "outgoing_recipients", "pending", "accepted", NULL
	};
	E2E_JSON *kr;
	guint n = g_strv_length(args);
	gboolean all = FALSE, ok, refused;
	const char *who, *fp;
	char *handle = NULL, *ctx = NULL, *own = NULL;
	int i, removed = 0;

	if (n == 0) {
		e2e_print_item(item, "usage: /e2e forget [-all] <nick|handle>");
		return;
	}
	/* -all at the start or at the end */
	who = args[0];
	if (n > 1 && strcmp(args[0], "-all") == 0) {
		all = TRUE;
		who = args[1];
	} else if (n > 1 && strcmp(args[n - 1], "-all") == 0) {
		all = TRUE;
	}
	kr = e2e_load();
	handle = resolve_handle(kr, who);
	if (handle == NULL) {
		e2e_print_item(item, "cannot resolve handle for %s", who);
		goto out;
	}
	if (all) {
		fp = e2e_find_peer_by_handle(kr, handle, NULL);
		if (fp != NULL) {
			char *copy = g_strdup(fp);

			removed += e2e_json_remove(e2e_json_get(kr, "peers"), copy);
			delete_handle_keys(e2e_json_get(kr, "seen_rekeys"), copy);
			g_free(copy);
		}
		for (i = 0; stores[i] != NULL; i++)
			removed += delete_handle_keys(e2e_json_get(kr, stores[i]), handle);
		filter_trust_changes(kr, handle, NULL);
		e2e_save(kr);
		e2e_print_item(item, "forgot %s (%s) globally — removed %d row(s)", who, handle, removed);
		goto out;
	}
	ctx = resolve_ctx(kr, item, who, &refused);
	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "cannot resolve context for %s", who);
		goto out;
	}
	own = own_ctx_or_warn(kr, item, ctx, "/e2e forget", &ok);
	if (!ok)
		goto out;
	delete_rows(kr, handle, ctx, own);
	e2e_forget_acceptance(kr, handle, ctx);
	e2e_save(kr);
	e2e_print_item(item, "forgotten %s on %s", who, shown(ctx));
out:
	g_free(own);
	g_free(ctx);
	g_free(handle);
	e2e_json_free(kr);
}

/* SYNTAX: E2E VERIFY <nick> */
static void cmd_verify(WI_ITEM_REC *item, const char *nick)
{
	E2E_JSON *kr, *row;
	E2E_IDENTITY id;
	char *handle, *ctx, *own;

	if (nick == NULL) {
		e2e_print_item(item, "usage: /e2e verify <nick>");
		return;
	}
	kr = e2e_load();
	if (!nick_context(kr, item, nick, &handle, &ctx))
		goto out;
	/* real DM sessions are recipient-keyed under "@<own>"; the peer-keyed
	   trust marker if only that exists */
	own = own_ctx(kr, item != NULL ? item->server : NULL, ctx);
	row = own != NULL ? incoming_row(kr, handle, own) : NULL;
	if (row == NULL)
		row = incoming_row(kr, handle, ctx);
	g_free(own);
	if (row == NULL) {
		e2e_print_item(item, "no session for %s on %s", nick, shown(ctx));
		goto out;
	}
	if (!e2e_ensure_identity(kr, &id))
		goto out;
	e2e_print_item(item, "Fingerprint Verification");
	e2e_print_item(item, "  You  ( local): %.16s", id.fp_hex);
	e2e_print_item(item, "  Them (%s): %.16s", nick, str_or(e2e_json_get_string(row, "fp"), ""));
	e2e_identity_wipe(&id);
out:
	g_free(handle);
	g_free(ctx);
	e2e_json_free(kr);
}

/* SYNTAX: E2E REVERIFY <nick> */
static void cmd_reverify(WI_ITEM_REC *item, const char *nick)
{
	E2E_JSON *kr, *applied = NULL, *peer;
	GPtrArray *taken;
	const char *old_fp;
	char *handle;
	guint i;

	if (nick == NULL) {
		e2e_print_item(item, "usage: /e2e reverify <nick>");
		return;
	}
	kr = e2e_load();
	handle = resolve_handle(kr, nick);
	if (handle == NULL) {
		e2e_print_item(item, "cannot resolve handle for %s", nick);
		e2e_json_free(kr);
		return;
	}
	taken = g_ptr_array_new_with_free_func((GDestroyNotify) e2e_json_free);
	filter_trust_changes(kr, handle, taken);
	/* what was accepted was the old key */
	delete_handle_keys(e2e_json_get(kr, "accepted"), handle);
	for (i = 0; i < taken->len && applied == NULL; i++) {
		E2E_JSON *row = g_ptr_array_index(taken, i);

		if (g_strcmp0(e2e_json_get_string(row, "change"), "fingerprint_changed") == 0 &&
		    e2e_json_get_string(row, "new_pubkey") != NULL &&
		    e2e_json_get_string(row, "new_fp") != NULL)
			applied = row;
	}
	if (applied != NULL) {
		old_fp = e2e_json_get_string(applied, "old_fp");
		if (old_fp != NULL)
			e2e_json_remove(e2e_json_get(kr, "peers"), old_fp);
		delete_handle_keys(e2e_json_get(kr, "incoming"), handle);
		delete_handle_keys(e2e_json_get(kr, "outgoing_recipients"), handle);
		peer = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set_string(peer, "pk", e2e_json_get_string(applied, "new_pubkey"));
		e2e_json_set_string(peer, "last_handle", handle);
		e2e_json_set_string(peer, "last_nick", nick);
		e2e_json_set_int(peer, "first_seen", e2e_now());
		e2e_json_set_int(peer, "last_seen", e2e_now());
		e2e_json_set_string(peer, "status", "trusted");
		e2e_json_set(e2e_json_get(kr, "peers"), e2e_json_get_string(applied, "new_fp"), peer);
		e2e_save(kr);
		e2e_print_item(item, "reverified %s: accepted new key fp=%.16s", nick,
		               e2e_json_get_string(applied, "new_fp"));
	} else {
		old_fp = e2e_find_peer_by_handle(kr, handle, NULL);
		if (old_fp != NULL) {
			char *copy = g_strdup(old_fp);

			e2e_json_remove(e2e_json_get(kr, "peers"), copy);
			g_free(copy);
		}
		delete_handle_keys(e2e_json_get(kr, "incoming"), handle);
		delete_handle_keys(e2e_json_get(kr, "outgoing_recipients"), handle);
		e2e_save(kr);
		e2e_print_item(item, "reverified %s: purged stale state; re-handshake to TOFU-pin the new key",
		               nick);
	}
	g_ptr_array_unref(taken);
	g_free(handle);
	e2e_json_free(kr);
}

/* SYNTAX: E2E ROTATE */
static void cmd_rotate(WI_ITEM_REC *item)
{
	E2E_JSON *kr, *out;
	unsigned char key[32];
	gboolean refused;
	char *ctx, *b64;

	if (item == NULL) {
		e2e_print_item(NULL, "not in a chat buffer");
		return;
	}
	kr = e2e_load();
	ctx = resolve_ctx(kr, item, NULL, &refused);
	if (ctx == NULL) {
		if (!refused)
			e2e_print_item(item, "cannot resolve current context");
		e2e_json_free(kr);
		return;
	}
	out = e2e_json_get(e2e_json_get(kr, "outgoing"), ctx);
	if (out != NULL) {
		e2e_json_set_int(out, "pending_rotation", 1);
	} else if (e2e_random_bytes(key, sizeof(key))) {
		out = e2e_json_new(E2E_JSON_OBJECT);
		b64 = e2e_b64_encode(key, sizeof(key));
		e2e_json_set_string(out, "sk", b64);
		e2e_wipe(b64, strlen(b64));
		g_free(b64);
		e2e_wipe(key, sizeof(key));
		e2e_json_set_int(out, "created_at", e2e_now());
		e2e_json_set_int(out, "pending_rotation", 1);
		e2e_json_set(e2e_json_get(kr, "outgoing"), ctx, out);
	}
	e2e_save(kr);
	e2e_print_item(item, "rotation scheduled for %s", shown(ctx));
	g_free(ctx);
	e2e_json_free(kr);
}

/* SYNTAX: E2E EXPORT <file> */
static void cmd_export(WI_ITEM_REC *item, const char *path)
{
	E2E_JSON *kr, *doc;
	char *data, *error = NULL;

	if (path == NULL) {
		e2e_print_item(item, "usage: /e2e export <file>");
		return;
	}
	kr = e2e_load();
	doc = e2e_keyring_export(kr);
	data = e2e_json_encode(doc, TRUE);
	/* private keys: created 0600, never over a file or through a symlink */
	if (e2e_write_new_file(path, data, strlen(data), &error))
		e2e_print_item(item, "exported keyring to %s", path);
	else
		e2e_print_item(item, "export failed: %s: %s%s", path, error,
		               strcmp(error, g_strerror(EEXIST)) == 0 ? " (choose a new file name)" : "");
	e2e_wipe(data, strlen(data));
	g_free(data);
	g_free(error);
	e2e_json_free(doc);
	e2e_json_free(kr);
}

/* SYNTAX: E2E IMPORT <file> */
static void cmd_import(WI_ITEM_REC *item, const char *path)
{
	E2E_JSON *doc, *kr;
	GError *gerror = NULL;
	const char *perr = NULL;
	char *data, *error = NULL, *bak;
	gboolean lost, saved;
	gsize len;

	if (path == NULL) {
		e2e_print_item(item, "usage: /e2e import <file>");
		return;
	}
	if (!g_file_get_contents(path, &data, &len, &gerror)) {
		e2e_print_item(item, "import failed: %s", gerror->message);
		g_error_free(gerror);
		return;
	}
	doc = e2e_json_parse(data, len, &perr);
	e2e_wipe(data, len);
	g_free(data);
	if (doc == NULL) {
		e2e_print_item(item, "import failed: %s", perr);
		return;
	}
	kr = e2e_keyring_import(doc, &error);
	e2e_json_free(doc);
	if (kr == NULL) {
		e2e_print_item(item, "import failed: %s", error);
		g_free(error);
		return;
	}
	/* the whole keyring is replaced: a private copy first. A lost
	   keyring (moved aside) is replaced too: that ends the lost state. */
	lost = e2e_keyring_is_lost();
	bak = e2e_keyring_backup(e2e_keyring_file(), &error);
	if (bak != NULL) {
		if (lost) {
			saved = e2e_keyring_start_fresh(e2e_keyring_file(), kr, &error);
			if (saved)
				e2e_state_changed();
			if (!saved)
				e2e_print_item(item, "%s", error);
			else if (e2e_drop_queued_privmsgs() > 0)
				e2e_print_item(item, "lines still waiting to be sent (messages typed while the keyring was lost, parts of encrypted messages) were dropped");
		} else {
			saved = e2e_save(kr);
		}
	} else {
		saved = FALSE;
	}
	if (bak == NULL) {
		e2e_print_item(item, "import failed: cannot back up the current keyring (%s) — nothing changed",
		               error);
	} else if (!saved) {
		if (*bak != '\0')
			e2e_print_item(item, "import failed: keyring not saved (backup: %s)", bak);
		else
			e2e_print_item(item, "import failed: keyring not saved");
	} else if (*bak != '\0') {
		e2e_print_item(item, "imported keyring from %s (previous keyring: %s)", path, bak);
	} else {
		e2e_print_item(item, "imported keyring from %s", path);
	}
	g_free(bak);
	g_free(error);
	e2e_json_free(kr);
}

/* SYNTAX: E2E AUTOTRUST [LIST] */
/* SYNTAX: E2E AUTOTRUST ADD <scope> <pattern> */
/* SYNTAX: E2E AUTOTRUST REMOVE <pattern> */
static void cmd_autotrust(SERVER_REC *server, WI_ITEM_REC *item, char **args)
{
	E2E_JSON *kr = e2e_load(), *rules = e2e_json_get(kr, "autotrust"), *rule;
	char *op = g_ascii_strdown(args[0] != NULL ? args[0] : "list", -1), *scope;
	guint n = g_strv_length(args), i;
	E2E_NET net;

	if (strcmp(op, "list") == 0) {
		if (e2e_json_size(rules) == 0)
			e2e_print_item(item, "(no autotrust rules)");
		for (i = 0; i < e2e_json_size(rules); i++) {
			const char *sc;

			rule = g_ptr_array_index(rules->array, i);
			sc = e2e_json_get_string(rule, "scope");
			/* an old rule (a bare name) holds on every network */
			e2e_print_item(item, "  %s%s  %s", shown(str_or(sc, "")),
			               sc != NULL && strcmp(sc, "global") != 0 &&
			               strchr(sc, E2E_SCOPE_SEP) == NULL ? " (every network)" : "",
			               str_or(e2e_json_get_string(rule, "handle_pattern"), ""));
		}
	} else if (strcmp(op, "add") == 0) {
		if (n < 3) {
			e2e_print_item(item, "usage: /e2e autotrust add <scope> <pattern>");
		} else if (strchr(args[1], E2E_SCOPE_SEP) != NULL) {
			e2e_print_item(item, "autotrust: a scope cannot hold the control character 0x1F");
		} else {
			/* a channel or address is the window's network's; without
			   a network it would hold on every network: refused */
			if (strcmp(args[1], "global") == 0) {
				scope = g_strdup(args[1]);
			} else if (server == NULL) {
				scope = NULL;
				e2e_print_item(item, "autotrust: %s is kept per network - add it in a window of that network, or use the scope global",
				               args[1]);
			} else {
				e2e_server_net(server, &net);
				scope = e2e_ctx_scoped(&net, args[1]);
				e2e_net_clear(&net);
			}
			if (scope == NULL) {
				if (server != NULL)
					e2e_print_item(item, "autotrust: the network has no name - nothing added");
			} else {
				rule = e2e_json_new(E2E_JSON_OBJECT);
				e2e_json_set_string(rule, "scope", scope);
				e2e_json_set_string(rule, "handle_pattern", args[2]);
				e2e_json_set_int(rule, "created_at", e2e_now());
				e2e_json_array_add(rules, rule);
				e2e_save(kr);
				e2e_print_item(item, "autotrust add %s %s", shown(scope), args[2]);
			}
			g_free(scope);
		}
	} else if (strcmp(op, "remove") == 0) {
		if (n < 2) {
			e2e_print_item(item, "usage: /e2e autotrust remove <pattern>");
		} else {
			for (i = 0; i < rules->array->len;) {
				rule = g_ptr_array_index(rules->array, i);
				if (g_strcmp0(str_or(e2e_json_get_string(rule, "handle_pattern"), ""), args[1]) == 0)
					g_ptr_array_remove_index(rules->array, i);
				else
					i++;
			}
			e2e_save(kr);
			e2e_print_item(item, "autotrust removed %s", args[1]);
		}
	} else {
		e2e_print_item(item, "usage: /e2e autotrust <list|add|remove>");
	}
	g_free(op);
	e2e_json_free(kr);
}

/* SYNTAX: E2E ADOPT */
static void cmd_adopt(WI_ITEM_REC *item)
{
	E2E_JSON *kr;
	E2E_NET net;
	char *wire;
	guint moved;

	if (item == NULL) {
		e2e_print_item(NULL, "%s", NOT_IN_CONTEXT);
		return;
	}
	kr = e2e_load();
	wire = window_wire(kr, item, NULL);
	if (wire == NULL) {
		e2e_print_item(item, "%s", NOT_IN_CONTEXT);
		e2e_json_free(kr);
		return;
	}
	if (item->server == NULL) {
		e2e_print_item(item, "not connected to a server — the network of %s is not known", wire);
		g_free(wire);
		e2e_json_free(kr);
		return;
	}
	/* the user's decision: old rows become this network's, whatever the
	   number of networks; another network's rows are never moved */
	e2e_server_net(item->server, &net);
	switch (e2e_adopt_explicit(kr, &net, wire, query_nick(item), &moved)) {
	case E2E_ADOPT_MOVED:
		if (!e2e_save(kr))
			break;
		if (moved > 0)
			e2e_print_item(item, "adopted %s for %s: %u entries", wire, net.label, moved);
		else
			e2e_print_item(item, "adopted %s for %s: %s had entries of its own for each old one - those are kept, the old ones are dropped",
			               wire, net.label, net.label);
		break;
	case E2E_ADOPT_OWN:
		if (e2e_save(kr))
			e2e_print_item(item, "E2E for %s is on for %s now (mode normal), set up here on its own: the old entries belong to another network and stay there; keys are exchanged anew",
			               wire, net.label);
		break;
	case E2E_ADOPT_ALREADY:
		e2e_print_item(item, "E2E is already set up for %s on %s; the old entries stay (to use them here instead: /e2e export, remove this network's entries for %s from the file, /e2e import it, then /e2e adopt)",
		               wire, net.label, wire);
		break;
	case E2E_ADOPT_NONE:
		e2e_print_item(item, "no old entries of %s to adopt for %s", wire, net.label);
		break;
	case E2E_ADOPT_UNSCOPABLE:
		print_blocked(item, &net, wire);
		break;
	}
	e2e_net_clear(&net);
	g_free(wire);
	e2e_json_free(kr);
}

/* SYNTAX: E2E RESET */
static void cmd_reset(WI_ITEM_REC *item)
{
	E2E_LOAD_STATUS status;
	E2E_IDENTITY id;
	E2E_JSON *kr;
	gboolean created;
	char *error = NULL;

	if (!e2e_keyring_is_lost()) {
		kr = e2e_keyring_load_checked(e2e_keyring_file(), &status, &error);
		e2e_json_free(kr);
		if (status == E2E_LOAD_OK)
			e2e_print_item(item, "nothing to reset: the keyring %s is fine", e2e_keyring_file());
		else
			e2e_print_item(item, "nothing to reset: the keyring %s exists but cannot be read (%s) — it is never replaced; fix it or move it aside",
			               e2e_keyring_file(), error);
		g_free(error);
		return;
	}
	kr = e2e_keyring_new();
	if (!e2e_identity_get(kr, &id, TRUE, &created, &error)) {
		e2e_print_item(item, "reset failed: %s", error);
	} else {
		if (e2e_keyring_start_fresh(e2e_keyring_file(), kr, &error)) {
			int dropped = e2e_drop_queued_privmsgs();

			e2e_state_changed();
			e2e_print_item(item, "a new keyring is started with a new identity (fingerprint %s). E2E is off everywhere until /e2e on; peers see a new key and need /e2e reverify <your nick>. The corrupt file stays as %s.corrupt-*",
			               id.fp_hex, e2e_keyring_file());
			if (dropped > 0)
				e2e_print_item(item, "%d line(s) still waiting to be sent (messages typed while the keyring was lost, parts of encrypted messages) were dropped",
				               dropped);
		} else {
			e2e_print_item(item, "reset failed: %s", error);
		}
		e2e_identity_wipe(&id);
	}
	g_free(error);
	e2e_json_free(kr);
}

/* "/e2e" alone: how to start, with the state of the current window */
static void cmd_guide(WI_ITEM_REC *item)
{
	E2E_JSON *kr = e2e_load(), *incoming;
	const char *fp = e2e_json_get_string(e2e_json_get(kr, "identity"), "fp");
	gboolean refused;
	char *ctx = resolve_ctx(kr, item, NULL, &refused);

	e2e_print_item(item, "End-to-end encryption (RPE2E, compatible with repartee)");
	if (e2e_keyring_is_lost())
		e2e_print_item(item, "%s", LOST_REFUSAL);
	e2e_print_item(item, "Your key fingerprint: %s", str_or(fp, "(none yet - created on first use)"));
	if (ctx != NULL) {
		E2E_JSON *cfg = e2e_json_get(e2e_json_get(kr, "channels"), ctx);
		char *own = own_ctx(kr, item->server, ctx);
		const char *peer = *e2e_ctx_wire(ctx) == '@' ? e2e_ctx_wire(ctx) + 1 : NULL;
		GPtrArray *keys, *trusted = g_ptr_array_new_with_free_func(g_free);
		GString *who = g_string_new(NULL);
		guint i, j;

		/* a query's DM sessions live under "@<own>", its trust markers
		   under "@<peer>": both, for this peer only */
		incoming = e2e_json_get(kr, "incoming");
		keys = e2e_json_keys(incoming);
		for (i = 0; i < keys->len; i++) {
			char *h, *c;
			gboolean dup = FALSE;

			split_key(g_ptr_array_index(keys, i), &h, &c);
			if (c != NULL && (strcmp(c, ctx) == 0 || g_strcmp0(c, own) == 0) &&
			    (peer == NULL || strcmp(h, peer) == 0) &&
			    g_strcmp0(e2e_json_get_string(e2e_json_get(incoming, g_ptr_array_index(keys, i)),
			                                  "status"), "trusted") == 0) {
				for (j = 0; j < trusted->len && !dup; j++)
					dup = strcmp(g_ptr_array_index(trusted, j), h) == 0;
				if (!dup) {
					g_ptr_array_add(trusted, h);
					h = NULL;
				}
			}
			g_free(h);
			g_free(c);
		}
		g_ptr_array_unref(keys);
		for (i = 0; i < trusted->len; i++)
			g_string_append_printf(who, "%s%s", i > 0 ? ", " : "",
			                       (char *) g_ptr_array_index(trusted, i));
		if (e2e_json_truthy(e2e_json_get(cfg, "enabled")))
			e2e_print_item(item, "Here (%s): encryption ON, mode %s", shown(ctx),
			               str_or(e2e_json_get_string(cfg, "mode"), "normal"));
		else
			e2e_print_item(item, "Here (%s): encryption off", shown(ctx));
		e2e_print_item(item, "Keys from: %s", who->len > 0 ? who->str : "nobody yet");
		g_string_free(who, TRUE);
		g_ptr_array_unref(trusted);
		g_free(own);
	} else if (refused) {
		/* said why above */
	} else if (item != NULL && IS_QUERY(item)) {
		e2e_print_item(item, "Here: the address (ident@host) of %s is not known yet - wait for a message from them or join a common channel, then /e2e on.",
		               item_name(item));
	} else {
		e2e_print_item(item, "Run /e2e in a channel or query window to see its state.");
	}
	e2e_print_item(item, "Getting started on a channel:");
	e2e_print_item(item, "  1. in the channel window: /e2e on");
	e2e_print_item(item, "  2. the other person does the same (repartee, irssi, erssi, WeeChat: /e2e on)");
	e2e_print_item(item, "  3. keys are exchanged with the first message; on \"Pending key exchange from <nick>\"");
	e2e_print_item(item, "     accept it in that window: /e2e accept  (/e2e pending lists every request;");
	e2e_print_item(item, "     or start it yourself: /e2e handshake <nick>)");
	e2e_print_item(item, "     in normal mode each of you accepts the other once: your key goes only to people you accepted");
	e2e_print_item(item, "  4. check: /e2e list - the peer shows as [trusted]");
	e2e_print_item(item, "  5. compare fingerprints over another channel (e.g. by phone): /e2e verify <nick>");
	e2e_print_item(item, "Turn off: /e2e off. Modes: /e2e mode normal (asks first) | auto-accept | quiet.");
	g_free(ctx);
	e2e_json_free(kr);
}

static void cmd_e2e(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	char **parts, **args, *sub;
	GPtrArray *words;
	guint i;

	if (!e2e_native_active()) {
		/* rpe2e.pl answers this command itself */
		e2e_print_item(item, "(the native e2e module is inactive while the rpe2e.pl script is loaded)");
		return;
	}
	if (item != NULL && server == NULL)
		server = item->server;

	/* split /\s+/, empty words dropped */
	words = g_ptr_array_new_with_free_func(g_free);
	parts = g_strsplit_set(data != NULL ? data : "", " \t\n\r\f\v", -1);
	for (i = 0; parts[i] != NULL; i++)
		if (*parts[i] != '\0')
			g_ptr_array_add(words, g_strdup(parts[i]));
	g_strfreev(parts);
	g_ptr_array_add(words, NULL);
	args = (char **) words->pdata;
	sub = g_ascii_strdown(args[0] != NULL ? args[0] : "", -1);
	if (args[0] != NULL)
		args++;

	if (e2e_keyring_is_lost() && *sub != '\0' && strcmp(sub, "help") != 0 &&
	    strcmp(sub, "reset") != 0 && strcmp(sub, "status") != 0 && strcmp(sub, "import") != 0) {
		/* nothing may be saved over the lost state */
		e2e_print_item(item, "%s", LOST_REFUSAL);
	} else if (*sub == '\0' || strcmp(sub, "help") == 0) {
		cmd_guide(item);
		e2e_print_item(item, "Encryption commands: on off mode fingerprint list status pending accept decline revoke unrevoke forget handshake verify reverify rotate export import autotrust adopt reset");
	} else if (strcmp(sub, "on") == 0) {
		cmd_on(item);
	} else if (strcmp(sub, "off") == 0) {
		cmd_off(server, item, args[0]);
	} else if (strcmp(sub, "mode") == 0) {
		cmd_mode(item, args[0]);
	} else if (strcmp(sub, "fingerprint") == 0) {
		cmd_fingerprint(item);
	} else if (strcmp(sub, "list") == 0) {
		cmd_list(item, args);
	} else if (strcmp(sub, "status") == 0) {
		cmd_status(item);
	} else if (strcmp(sub, "pending") == 0) {
		cmd_pending(item);
	} else if (strcmp(sub, "accept") == 0) {
		e2e_do_accept(server, item, args[0]);
	} else if (strcmp(sub, "decline") == 0) {
		e2e_do_decline(server, item, args[0]);
	} else if (strcmp(sub, "revoke") == 0) {
		cmd_revoke(item, args[0], TRUE);
	} else if (strcmp(sub, "unrevoke") == 0) {
		cmd_revoke(item, args[0], FALSE);
	} else if (strcmp(sub, "forget") == 0) {
		cmd_forget(item, args);
	} else if (strcmp(sub, "handshake") == 0) {
		cmd_handshake(server, item, args[0]);
	} else if (strcmp(sub, "verify") == 0) {
		cmd_verify(item, args[0]);
	} else if (strcmp(sub, "reverify") == 0) {
		cmd_reverify(item, args[0]);
	} else if (strcmp(sub, "rotate") == 0) {
		cmd_rotate(item);
	} else if (strcmp(sub, "export") == 0) {
		cmd_export(item, args[0]);
	} else if (strcmp(sub, "import") == 0) {
		cmd_import(item, args[0]);
	} else if (strcmp(sub, "autotrust") == 0) {
		cmd_autotrust(server, item, args);
	} else if (strcmp(sub, "adopt") == 0) {
		cmd_adopt(item);
	} else if (strcmp(sub, "reset") == 0) {
		cmd_reset(item);
	} else {
		e2e_print_item(item, "unknown subcommand");
	}
	g_free(sub);
	g_ptr_array_unref(words);
}

/* the subcommands are dispatched by cmd_e2e (as in rpe2e.pl); they are
   bound only for completion and /help */
static void cmd_e2e_sub(void)
{
}

void e2e_commands_init(void)
{
	int i;

	command_bind("e2e", NULL, (SIGNAL_FUNC) cmd_e2e);
	for (i = 0; subcommands[i] != NULL; i++) {
		char *cmd = g_strconcat("e2e ", subcommands[i], NULL);

		command_bind(cmd, NULL, (SIGNAL_FUNC) cmd_e2e_sub);
		g_free(cmd);
	}
}

void e2e_commands_deinit(void)
{
	int i;

	command_unbind("e2e", (SIGNAL_FUNC) cmd_e2e);
	for (i = 0; subcommands[i] != NULL; i++) {
		char *cmd = g_strconcat("e2e ", subcommands[i], NULL);

		command_unbind(cmd, (SIGNAL_FUNC) cmd_e2e_sub);
		g_free(cmd);
	}
}
