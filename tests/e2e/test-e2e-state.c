/*
 test-e2e-state.c : the E2E state of a window, the waiting key requests

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <glib.h>
#include <string.h>

#include <irssi/src/e2e/e2e-json.h>
#include <irssi/src/e2e/e2e-keyring.h>
#include <irssi/src/e2e/e2e-proto.h>
#include <irssi/src/e2e/e2e-scope.h>
#include <irssi/src/e2e/e2e-state.h>

/* base64 of 32 bytes: 0x01 each (a real key), all zero (the placeholder) */
#define KEY_REAL "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="
#define KEY_ZERO "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="

static gint64 fake_now = 1791564129;

static gint64 fixed_now(void)
{
	return fake_now;
}

static E2E_JSON *store(E2E_JSON *kr, const char *name)
{
	return e2e_json_object_member(kr, name);
}

static void set_config(E2E_JSON *kr, const char *ctx, int enabled, const char *mode,
                       const char *nick)
{
	E2E_JSON *cfg = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_int(cfg, "enabled", enabled);
	e2e_json_set_string(cfg, "mode", mode);
	if (nick != NULL)
		e2e_json_set_string(cfg, "nick", nick);
	e2e_json_set(store(kr, "channels"), ctx, cfg);
}

static void set_session(E2E_JSON *kr, const char *handle, const char *ctx, const char *sk,
                        const char *status)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
	char *k = g_strconcat(handle, "|", ctx, NULL);

	e2e_json_set_string(row, "fp", "0123456789abcdef0123456789abcdef");
	e2e_json_set_string(row, "sk", sk);
	e2e_json_set_string(row, "status", status);
	e2e_json_set_int(row, "created_at", 1);
	e2e_json_set(store(kr, "incoming"), k, row);
	g_free(k);
}

/* a waiting request as e2e_handle_keyreq writes it; key NULL: the
   regular "<handle>|<ctx>" */
static void set_pending_key(E2E_JSON *kr, const char *key, const char *handle, const char *ctx,
                            const char *nick, gint64 at)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
	char *k = key != NULL ? g_strdup(key) : g_strconcat(handle, "|", ctx, NULL);

	e2e_json_set_string(row, "handle", handle);
	e2e_json_set_string(row, "channel", ctx);
	e2e_json_set_string(row, "sender_handle", handle);
	if (nick != NULL)
		e2e_json_set_string(row, "sender_nick", nick);
	e2e_json_set_string(row, "pubkey", KEY_REAL);
	e2e_json_set_string(row, "eph_x25519", KEY_REAL);
	e2e_json_set_int(row, "received_at", at);
	e2e_json_set(store(kr, "pending_inbound"), k, row);
	g_free(k);
}

static void set_pending(E2E_JSON *kr, const char *handle, const char *ctx, const char *nick,
                        gint64 at)
{
	set_pending_key(kr, NULL, handle, ctx, nick, at);
}

static void info_of(const E2E_JSON *kr, gboolean ok, const E2E_NET *net, const char *wire,
                    const char *own, const char *nick, E2E_WIN_INFO *info)
{
	e2e_window_info(kr, ok, net, wire, own, nick, info);
	g_assert_nonnull(info->pending);
	g_assert_nonnull(info->mode);
}

static void test_off_on_waiting(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_WIN_INFO info;
	E2E_NET net;

	e2e_net_init(&net, "E2E", "E2E", "ascii", FALSE);

	/* no config */
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_OFF);
	g_assert_cmpstr(info.ctx, ==, "E2E\x1f#test");
	g_assert_cmpstr(info.mode, ==, "normal");
	g_assert_cmpuint(info.sessions, ==, 0);
	g_assert_cmpuint(info.pending->len, ==, 0);
	e2e_window_info_clear(&info);

	/* enabled, nobody's key */
	set_config(kr, "E2E\x1f#test", 1, "normal", NULL);
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_WAITING);
	g_assert_cmpuint(info.sessions, ==, 0);
	e2e_window_info_clear(&info);

	/* a trusted session with a real key */
	set_session(kr, "bob@127.0.0.1", "E2E\x1f#test", KEY_REAL, "trusted");
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_ON);
	g_assert_cmpuint(info.sessions, ==, 1);
	e2e_window_info_clear(&info);

	/* a pending zero-key session only */
	set_session(kr, "bob@127.0.0.1", "E2E\x1f#test", KEY_ZERO, "pending");
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_WAITING);
	g_assert_cmpuint(info.sessions, ==, 0);
	e2e_window_info_clear(&info);

	/* trusted without a real key, or a real key revoked: no session;
	   another channel's or another network's session neither */
	set_session(kr, "bob@127.0.0.1", "E2E\x1f#test", KEY_ZERO, "trusted");
	set_session(kr, "carol@h", "E2E\x1f#test", KEY_REAL, "revoked");
	set_session(kr, "dave@h", "E2E\x1f#other", KEY_REAL, "trusted");
	set_session(kr, "erin@h", "E2E2\x1f#test", KEY_REAL, "trusted");
	set_session(kr, "frank@h", "#test", KEY_REAL, "trusted");
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_WAITING);
	g_assert_cmpuint(info.sessions, ==, 0);
	e2e_window_info_clear(&info);

	/* the mode of the config */
	set_config(kr, "E2E\x1f#test", 1, "quiet", NULL);
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpstr(info.mode, ==, "quiet");
	e2e_window_info_clear(&info);
	/* off, the mode kept */
	set_config(kr, "E2E\x1f#test", 0, "auto-accept", NULL);
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_OFF);
	g_assert_cmpstr(info.mode, ==, "auto-accept");
	e2e_window_info_clear(&info);

	/* a query: the peer's sessions to us live under "@<own>" */
	set_config(kr, "E2E\x1f@bob@h", 1, "normal", "bob");
	set_session(kr, "carol@h", "E2E\x1f@alice@a", KEY_REAL, "trusted");
	info_of(kr, TRUE, &net, "@bob@h", "@alice@a", "bob", &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_WAITING);
	g_assert_cmpstr(info.ctx, ==, "E2E\x1f@bob@h");
	e2e_window_info_clear(&info);
	set_session(kr, "bob@h", "E2E\x1f@alice@a", KEY_REAL, "trusted");
	info_of(kr, TRUE, &net, "@bob@h", "@alice@a", "bob", &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_ON);
	g_assert_cmpuint(info.sessions, ==, 1);
	e2e_window_info_clear(&info);
	/* our own address not known yet */
	info_of(kr, TRUE, &net, "@bob@h", NULL, "bob", &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_WAITING);
	g_assert_cmpuint(info.sessions, ==, 0);
	e2e_window_info_clear(&info);

	e2e_net_clear(&net);
	e2e_json_free(kr);
}

static void test_pending(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_WIN_INFO info;
	E2E_PENDING *p;
	GPtrArray *all;
	E2E_NET net;
	guint i;

	e2e_net_init(&net, "E2E", "E2E", "ascii", FALSE);
	set_config(kr, "E2E\x1f#test", 1, "normal", NULL);
	set_pending(kr, "carol@h", "E2E\x1f#test", "carol", 200);
	set_pending(kr, "bob@127.0.0.1", "E2E\x1f#test", "bob", 100);
	set_pending(kr, "dave@h", "E2E\x1f#x", "dave", 300);

	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpuint(info.pending->len, ==, 2);
	p = g_ptr_array_index(info.pending, 0);
	g_assert_cmpstr(p->nick, ==, "bob");
	g_assert_cmpstr(p->handle, ==, "bob@127.0.0.1");
	g_assert_cmpstr(p->ctx, ==, "E2E\x1f#test");
	g_assert_cmpuint(strlen(p->fp16), ==, 16);
	g_assert_cmpint(p->received_at, ==, 100);
	p = g_ptr_array_index(info.pending, 1);
	g_assert_cmpstr(p->handle, ==, "carol@h");
	e2e_window_info_clear(&info);

	all = e2e_pending_all(kr);
	g_assert_cmpuint(all->len, ==, 3);
	for (i = 1; i < all->len; i++)
		g_assert_cmpint(((E2E_PENDING *) g_ptr_array_index(all, i - 1))->received_at, <,
		                ((E2E_PENDING *) g_ptr_array_index(all, i))->received_at);
	g_ptr_array_unref(all);

	e2e_net_clear(&net);
	e2e_json_free(kr);
}

/* another network's, another context's, and rows whose key is not the
   "<handle>|<channel>" they claim are never a window's */
static void test_pending_scope(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_WIN_INFO info;
	GPtrArray *all;
	E2E_NET net;
	guint n = 99;

	e2e_net_init(&net, "E2E", "E2E", "ascii", FALSE);
	set_pending(kr, "bob@127.0.0.1", "E2E2\x1f#test", "bob", 100);
	set_pending(kr, "bob@127.0.0.1", "#test", "bob", 110);
	set_pending(kr, "carol@h", "E2E\x1f#Test", "carol", 120);
	set_pending_key(kr, "eve@h|E2E\x1f#test", "eve@h", "E2E\x1f#x", "eve", 130);
	set_pending_key(kr, "mal@h|E2E\x1f#test", "mallory@h", "E2E\x1f#test", "mal", 140);
	set_pending_key(kr, "zed@h|E2E\x1f#test", "zed@h", NULL, "zed", 150);

	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpuint(info.pending->len, ==, 0);
	e2e_window_info_clear(&info);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", NULL, E2E_CASEMAP_ASCII, &n));
	g_assert_cmpuint(n, ==, 0);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "bob", E2E_CASEMAP_ASCII, &n));
	g_assert_cmpuint(n, ==, 0);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "eve", E2E_CASEMAP_ASCII, &n));
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "mal", E2E_CASEMAP_ASCII, &n));
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "mallory@h", E2E_CASEMAP_ASCII, &n));
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#x", "eve", E2E_CASEMAP_ASCII, &n));
	g_assert_cmpuint(n, ==, 0);
	/* the only network: an old bare row is the old context's, never the
	   scoped one's */
	g_assert_cmpstr(e2e_pending_handle(kr, "#test", NULL, E2E_CASEMAP_ASCII, &n), ==,
	                "bob@127.0.0.1");
	g_assert_cmpuint(n, ==, 1);
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E2\x1f#test", "BOB", E2E_CASEMAP_ASCII, &n), ==,
	                "bob@127.0.0.1");

	/* the rows that hold together: bob twice and carol */
	all = e2e_pending_all(kr);
	g_assert_cmpuint(all->len, ==, 3);
	g_ptr_array_unref(all);

	e2e_net_clear(&net);
	e2e_json_free(kr);
}

static void test_blocked(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_WIN_INFO info;
	E2E_NET net, one;

	e2e_net_init(&net, "E2E", "E2E", "ascii", FALSE);
	e2e_net_init(&one, "E2E", "E2E", "ascii", TRUE);

	/* an old entry, several networks */
	set_config(kr, "#test", 1, "normal", NULL);
	info_of(kr, TRUE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_BLOCKED);
	e2e_window_info_clear(&info);
	/* the only network: used where it is, not taken over */
	info_of(kr, TRUE, &one, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_WAITING);
	g_assert_cmpstr(info.ctx, ==, "#test");
	e2e_window_info_clear(&info);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "E2E\x1f#test"));

	/* a query whose address is not known: E2E on for the nick */
	set_config(kr, "E2E\x1f@carol@h", 1, "normal", "carol");
	info_of(kr, TRUE, &net, NULL, NULL, "carol", &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_BLOCKED);
	e2e_window_info_clear(&info);
	info_of(kr, TRUE, &net, NULL, NULL, "dave", &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_OFF);
	e2e_window_info_clear(&info);
	/* the nick at another address now: the gate refuses */
	info_of(kr, TRUE, &net, "@carol@elsewhere", NULL, "carol", &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_BLOCKED);
	e2e_window_info_clear(&info);

	/* the keyring cannot be read */
	info_of(kr, FALSE, &net, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_ERROR);
	e2e_window_info_clear(&info);

	e2e_net_clear(&one);
	e2e_net_clear(&net);
	e2e_json_free(kr);
}

static E2E_PENDING *pending_new(const char *nick, const char *handle, const char *ctx,
                                const char *fp16, gint64 at)
{
	E2E_PENDING *p = g_new0(E2E_PENDING, 1);

	p->nick = g_strdup(nick);
	p->handle = g_strdup(handle);
	p->ctx = g_strdup(ctx);
	g_strlcpy(p->fp16, fp16, sizeof(p->fp16));
	p->received_at = at;
	return p;
}

static void info_init(E2E_WIN_INFO *info, E2E_WIN_STATE state, const char *ctx, const char *mode,
                      guint sessions)
{
	info->state = state;
	info->ctx = g_strdup(ctx);
	info->mode = g_strdup(mode);
	info->sessions = sessions;
	info->pending = g_ptr_array_new_with_free_func((GDestroyNotify) e2e_pending_free);
}

static void test_json(void)
{
	E2E_WIN_INFO info;
	char *json;

	info_init(&info, E2E_WIN_ON, "E2E\x1f#test", "normal", 1);
	g_ptr_array_add(info.pending,
	                pending_new("bob", "bob@127.0.0.1", "E2E\x1f#test", "0123456789abcdef", 1791564129));
	json = e2e_window_info_json(&info, "E2E");
	g_assert_cmpstr(json, ==, "{\"v\":1,\"state\":\"on\",\"network\":\"E2E\",\"context\":\"#test\",\"mode\":\"normal\","
	  "\"sessions\":1,\"pending\":[{\"nick\":\"bob\",\"handle\":\"bob@127.0.0.1\",\"fingerprint\":\"0123456789abcdef\","
	  "\"received_at\":1791564129}]}");
	g_free(json);
	e2e_window_info_clear(&info);

	/* nothing known */
	info_init(&info, E2E_WIN_ERROR, NULL, "normal", 0);
	json = e2e_window_info_json(&info, NULL);
	g_assert_cmpstr(json, ==, "{\"v\":1,\"state\":\"error\",\"network\":null,\"context\":null,\"mode\":\"normal\","
	  "\"sessions\":0,\"pending\":[]}");
	g_free(json);
	e2e_window_info_clear(&info);
}

static void test_json_escapes(void)
{
	E2E_WIN_INFO info;
	char *json;

	info_init(&info, E2E_WIN_WAITING, "E2E\x1f#\xc5\xbc" "aba", "nor%mal", 0);
	g_ptr_array_add(info.pending, pending_new("\xff", "x\x7f@h\x1f", "E2E\x1f#\xc5\xbc" "aba", "", 1));
	g_ptr_array_add(info.pending, pending_new("a\"b\\c\x01", "%n@h", "E2E\x1f#\xc5\xbc" "aba", "", 2));
	json = e2e_window_info_json(&info, "N\xc3" "et\n");
	g_assert_true(g_utf8_validate(json, -1, NULL));
	g_assert_nonnull(strstr(json, "\"nick\":\"\xef\xbf\xbd\""));
	g_assert_nonnull(strstr(json, "\"nick\":\"a\\\"b\\\\c\\u0001\""));
	g_assert_nonnull(strstr(json, "\"handle\":\"x\x7f@h\\u001f\""));
	g_assert_nonnull(strstr(json, "\"handle\":\"%n@h\""));
	g_assert_nonnull(strstr(json, "\"context\":\"#\xc5\xbc" "aba\""));
	g_assert_nonnull(strstr(json, "\"mode\":\"nor%mal\""));
	g_assert_nonnull(strstr(json, "\"network\":\"N\xef\xbf\xbd" "et\\u000a\""));
	g_assert_nonnull(strstr(json, "\"fingerprint\":\"\""));
	g_free(json);
	e2e_window_info_clear(&info);
}

static void test_handle_lookup(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	guint n = 99;

	set_pending(kr, "bob@127.0.0.1", "E2E\x1f#test", "bob", 100);
	set_pending(kr, "carol@h", "E2E\x1f#test", "carol", 200);

	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "Bob", E2E_CASEMAP_RFC1459, &n), ==,
	                "bob@127.0.0.1");
	g_assert_cmpuint(n, ==, 1);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", NULL, E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 2);
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "bob@127.0.0.1", E2E_CASEMAP_RFC1459, &n), ==,
	                "bob@127.0.0.1");
	g_assert_cmpuint(n, ==, 1);
	/* a handle is compared exactly */
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "BOB@127.0.0.1", E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "dave", E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);
	/* the only one */
	e2e_json_remove(e2e_json_get(kr, "pending_inbound"), "carol@h|E2E\x1f#test");
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", NULL, E2E_CASEMAP_RFC1459, &n), ==,
	                "bob@127.0.0.1");
	g_assert_cmpuint(n, ==, 1);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#other", NULL, E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);

	/* a nick of two addresses (two people, or one at two hosts) picks
	   neither, under the server's casemapping */
	set_pending(kr, "bob@other", "E2E\x1f#test", "BOB", 300);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "bob", E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 2);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "bob", E2E_CASEMAP_ASCII, &n));
	g_assert_cmpuint(n, ==, 2);
	/* by address it is one */
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "bob@other", E2E_CASEMAP_RFC1459, &n), ==,
	                "bob@other");
	g_assert_cmpuint(n, ==, 1);
	/* rfc1459 folds [ and {, ascii does not */
	set_pending(kr, "x@h", "E2E\x1f#test", "x[", 400);
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "X{", E2E_CASEMAP_RFC1459, &n), ==, "x@h");
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "X{", E2E_CASEMAP_ASCII, &n));
	g_assert_cmpuint(n, ==, 0);
	/* an address names its own request, never one whose (impossible)
	   nick looks like it */
	set_pending(kr, "y@h", "E2E\x1f#test", "x@h", 500);
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "x@h", E2E_CASEMAP_RFC1459, &n), ==, "x@h");
	g_assert_cmpuint(n, ==, 1);
	set_pending(kr, "z@h", "E2E\x1f#test", "w@h", 600);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "w@h", E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);
	/* count may be NULL */
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "y@h", E2E_CASEMAP_RFC1459, NULL), ==, "y@h");

	e2e_json_free(kr);
}

/* a query's request names its sender ("@" + its own address): one stored
   under another person's query (rpe2e.pl 0.2.0 keeps those) is
   damaged - never shown as waiting, never picked */
static void test_pending_foreign_dm(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_WIN_INFO info;
	GPtrArray *all, *bad;
	E2E_NET one;
	guint n = 99;

	e2e_net_init(&one, "E2E", "E2E", "rfc1459", TRUE);
	set_config(kr, "@x@hx", 1, "normal", "x");
	set_pending(kr, "y@hy", "@x@hx", "x", 100);
	set_pending(kr, "z@hz", "E2E\x1f@x@hx", "z", 110);
	set_pending(kr, "w@hw", "E2E\x1f" "barenick", "w", 120);

	info_of(kr, TRUE, &one, "@x@hx", "@me@m", "x", &info);
	g_assert_cmpuint(info.pending->len, ==, 0);
	e2e_window_info_clear(&info);
	g_assert_null(e2e_pending_handle(kr, "@x@hx", NULL, E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);
	g_assert_null(e2e_pending_handle(kr, "@x@hx", "x", E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);
	g_assert_null(e2e_pending_handle(kr, "@x@hx", "y@hy", E2E_CASEMAP_RFC1459, &n));
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f@x@hx", NULL, E2E_CASEMAP_RFC1459, &n));
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f" "barenick", NULL, E2E_CASEMAP_RFC1459, &n));
	all = e2e_pending_all(kr);
	g_assert_cmpuint(all->len, ==, 0);
	g_ptr_array_unref(all);
	/* told as damaged, once each */
	bad = e2e_pending_damaged(kr);
	g_assert_cmpuint(bad->len, ==, 3);
	g_assert_cmpstr(((E2E_PENDING *) g_ptr_array_index(bad, 0))->handle, ==, "y@hy");
	g_assert_cmpstr(((E2E_PENDING *) g_ptr_array_index(bad, 0))->ctx, ==, "@x@hx");
	g_ptr_array_unref(bad);

	/* x's own request is one */
	set_pending(kr, "x@hx", "@x@hx", "x", 130);
	g_assert_cmpstr(e2e_pending_handle(kr, "@x@hx", NULL, E2E_CASEMAP_RFC1459, &n), ==, "x@hx");
	g_assert_cmpuint(n, ==, 1);
	all = e2e_pending_all(kr);
	g_assert_cmpuint(all->len, ==, 1);
	g_ptr_array_unref(all);

	e2e_net_clear(&one);
	e2e_json_free(kr);
}

/* an empty who is no argument */
static void test_pending_empty_who(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	guint n = 99;

	set_pending(kr, "bob@127.0.0.1", "E2E\x1f#test", "bob", 100);
	g_assert_cmpstr(e2e_pending_handle(kr, "E2E\x1f#test", "", E2E_CASEMAP_RFC1459, &n), ==,
	                "bob@127.0.0.1");
	g_assert_cmpuint(n, ==, 1);
	set_pending(kr, "carol@h", "E2E\x1f#test", "", 200);
	g_assert_null(e2e_pending_handle(kr, "E2E\x1f#test", "", E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 2);
	e2e_json_free(kr);
}

static void test_notice_limit(void)
{
	GHashTable *stamps = e2e_stamps_new();
	int i;

	g_assert_true(e2e_pending_notice_allow(stamps, "E2E|bob@h|E2E\x1f#test"));
	g_assert_false(e2e_pending_notice_allow(stamps, "E2E|bob@h|E2E\x1f#test"));
	g_assert_true(e2e_pending_notice_allow(stamps, "E2E|carol@h|E2E\x1f#test"));
	fake_now += E2E_PENDING_NOTICE_INTERVAL - 1;
	g_assert_false(e2e_pending_notice_allow(stamps, "E2E|bob@h|E2E\x1f#test"));
	fake_now += 1;
	g_assert_true(e2e_pending_notice_allow(stamps, "E2E|bob@h|E2E\x1f#test"));
	/* many requests within the interval: the table stays bounded */
	for (i = 0; i < 4 * E2E_PENDING_NOTICE_MAX; i++) {
		char *k = g_strdup_printf("E2E|u%d@h|E2E\x1f#test", i);

		g_assert_true(e2e_pending_notice_allow(stamps, k));
		g_assert_cmpuint(g_hash_table_size(stamps), <=, E2E_PENDING_NOTICE_MAX);
		g_free(k);
		if (i % 7 == 0)
			fake_now++;
	}
	/* the newest are kept */
	g_assert_false(e2e_pending_notice_allow(stamps, "E2E|u1023@h|E2E\x1f#test"));
	g_hash_table_destroy(stamps);
}

static void test_age_and_names(void)
{
	char *s;

	s = e2e_age_text(0);
	g_assert_cmpstr(s, ==, "0s");
	g_free(s);
	s = e2e_age_text(59);
	g_assert_cmpstr(s, ==, "59s");
	g_free(s);
	s = e2e_age_text(60);
	g_assert_cmpstr(s, ==, "1m");
	g_free(s);
	s = e2e_age_text(3 * 3600 + 5);
	g_assert_cmpstr(s, ==, "3h");
	g_free(s);
	s = e2e_age_text(2 * 86400);
	g_assert_cmpstr(s, ==, "2d");
	g_free(s);
	/* a clock that went back */
	s = e2e_age_text(-5);
	g_assert_cmpstr(s, ==, "0s");
	g_free(s);

	g_assert_cmpstr(e2e_win_state_name(E2E_WIN_OFF), ==, "off");
	g_assert_cmpstr(e2e_win_state_name(E2E_WIN_ON), ==, "on");
	g_assert_cmpstr(e2e_win_state_name(E2E_WIN_WAITING), ==, "waiting");
	g_assert_cmpstr(e2e_win_state_name(E2E_WIN_BLOCKED), ==, "blocked");
	g_assert_cmpstr(e2e_win_state_name(E2E_WIN_ERROR), ==, "error");
}

/* reading the state never changes the keyring (no adoption) */
static void test_read_only(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_WIN_INFO info;
	char *before, *after;
	E2E_NET one;

	e2e_net_init(&one, "E2E", "E2E", "ascii", TRUE);
	set_config(kr, "#test", 1, "normal", NULL);
	set_session(kr, "bob@127.0.0.1", "#test", KEY_REAL, "trusted");
	set_pending(kr, "carol@h", "#test", "carol", 1);
	set_config(kr, "@bob@h", 1, "normal", "bob");
	before = e2e_keyring_snapshot(kr);
	info_of(kr, TRUE, &one, "#test", NULL, NULL, &info);
	g_assert_cmpint(info.state, ==, E2E_WIN_ON);
	g_assert_cmpuint(info.pending->len, ==, 1);
	e2e_window_info_clear(&info);
	info_of(kr, TRUE, &one, "@bob@h", "@alice@a", "bob", &info);
	e2e_window_info_clear(&info);
	info_of(kr, TRUE, &one, NULL, NULL, "bob", &info);
	e2e_window_info_clear(&info);
	g_ptr_array_unref(e2e_pending_all(kr));
	e2e_pending_handle(kr, "#test", "carol", E2E_CASEMAP_RFC1459, NULL);
	after = e2e_keyring_snapshot(kr);
	g_assert_cmpstr(before, ==, after);
	g_free(before);
	g_free(after);
	e2e_net_clear(&one);
	e2e_json_free(kr);
}

/* "e2e" among the modules of autoload_modules: words split at spaces and
   commas, as irssi splits the setting; "e2e:core" is the same module. The
   case counts: irssi builds the file name from the word as it is, so
   "E2E" loads nothing */
static void test_autoload_listed(void)
{
	g_assert_true(e2e_autoload_listed("irc dcc flood notifylist perl otr e2e"));
	g_assert_true(e2e_autoload_listed("e2e"));
	g_assert_false(e2e_autoload_listed("irc,E2E,otr"));
	g_assert_false(e2e_autoload_listed("irc E2E"));
	g_assert_true(e2e_autoload_listed("irc e2e"));
	g_assert_false(e2e_autoload_listed("irc E2E:core"));
	g_assert_true(e2e_autoload_listed("  irc   e2e:core "));
	g_assert_false(e2e_autoload_listed("irc dcc flood notifylist perl otr fe_web anti_floodnet"));
	g_assert_false(e2e_autoload_listed(""));
	g_assert_false(e2e_autoload_listed(NULL));
	g_assert_false(e2e_autoload_listed("e2ee e2e_x xe2e"));
}

/* channels and queries with E2E on, on any network, old entries too */
static void test_enabled_count(void)
{
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT);

	g_assert_cmpuint(e2e_enabled_count(NULL), ==, 0);
	g_assert_cmpuint(e2e_enabled_count(kr), ==, 0);
	set_config(kr, "#old", 1, "normal", NULL);
	set_config(kr, "E2E\x1f#test", 1, "normal", NULL);
	set_config(kr, "E2E\x1f@bob@h", 1, "quiet", "bob");
	set_config(kr, "E2E2\x1f#test", 0, "normal", NULL);
	g_assert_cmpuint(e2e_enabled_count(kr), ==, 3);
	set_config(kr, "#old", 0, "normal", NULL);
	g_assert_cmpuint(e2e_enabled_count(kr), ==, 2);
	e2e_json_free(kr);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	e2e_now = fixed_now;

	g_test_add_func("/e2e/state/off-on-waiting", test_off_on_waiting);
	g_test_add_func("/e2e/state/pending", test_pending);
	g_test_add_func("/e2e/state/pending-scope", test_pending_scope);
	g_test_add_func("/e2e/state/blocked", test_blocked);
	g_test_add_func("/e2e/state/json", test_json);
	g_test_add_func("/e2e/state/json-escapes", test_json_escapes);
	g_test_add_func("/e2e/state/read-only", test_read_only);
	g_test_add_func("/e2e/state/notice-limit", test_notice_limit);
	g_test_add_func("/e2e/state/age-and-names", test_age_and_names);
	g_test_add_func("/e2e/pending/handle-lookup", test_handle_lookup);
	g_test_add_func("/e2e/pending/foreign-dm", test_pending_foreign_dm);
	g_test_add_func("/e2e/pending/empty-who", test_pending_empty_who);
	g_test_add_func("/e2e/state/autoload-listed", test_autoload_listed);
	g_test_add_func("/e2e/state/enabled-count", test_enabled_count);

	return g_test_run();
}
