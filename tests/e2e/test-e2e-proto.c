/*
 test-e2e-proto.c : key exchange, outbound gate and decryption of RPE2E

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 Two in-memory keyrings talk through the functions the module calls,
 and the native side reads messages rpe2e.pl made (rpe2e-vectors.h). The
 behaviour is rpe2e.pl 0.2.0's, with the rules of e2e-proto.c's header.
*/

#include <glib.h>
#include <string.h>

#include <irssi/src/e2e/e2e-crypto.h>
#include <irssi/src/e2e/e2e-keyring.h>
#include <irssi/src/e2e/e2e-proto.h>
#include <irssi/src/e2e/e2e-wire.h>

#include <irssi/src/e2e/e2e-state.h>
#include "e2e-legacy-fixture.h"
#include "rpe2e-vectors.h"

#define ALICE "alice@127.0.0.1"
#define BOB   "bob@127.0.0.1"

static gint64 fake_now = 1791564129;

static gint64 fixed_now(void)
{
	return fake_now;
}

typedef struct {
	E2E_JSON *kr;
	E2E_IDENTITY id;
	GHashTable *stamps;
	const char *nick, *handle;
} PEER;

static void peer_init(PEER *p, const char *nick, const char *handle, int seed_byte)
{
	gboolean created = FALSE;
	char *error = NULL;

	p->kr = e2e_keyring_new();
	p->stamps = e2e_stamps_new();
	p->nick = nick;
	p->handle = handle;
	if (seed_byte >= 0) {
		unsigned char seed[32], pk[32], sk[64];
		E2E_JSON *ident = e2e_json_new(E2E_JSON_OBJECT);
		char *s;

		memset(seed, seed_byte, sizeof(seed));
		g_assert_true(e2e_ed25519_seed_keypair(seed, pk, sk));
		s = e2e_b64_encode(pk, 32);
		e2e_json_set_string(ident, "pk", s);
		g_free(s);
		s = e2e_b64_encode(sk, 64);
		e2e_json_set_string(ident, "sk", s);
		g_free(s);
		s = e2e_fingerprint_hex(pk);
		e2e_json_set_string(ident, "fp", s);
		g_free(s);
		e2e_json_set_int(ident, "created_at", 1);
		e2e_json_set(p->kr, "identity", ident);
	}
	g_assert_true(e2e_identity_get(p->kr, &p->id, TRUE, &created, &error));
	g_assert_true(created == (seed_byte < 0));
}

static void peer_free(PEER *p)
{
	e2e_json_free(p->kr);
	g_hash_table_destroy(p->stamps);
}

static void enable(PEER *p, const char *ctx, const char *mode)
{
	E2E_JSON *cfg = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_int(cfg, "enabled", 1);
	e2e_json_set_string(cfg, "mode", mode);
	e2e_json_set(e2e_json_get(p->kr, "channels"), ctx, cfg);
}

static E2E_JSON *path_get(E2E_JSON *node, ...)
{
	va_list va;
	const char *key;

	va_start(va, node);
	while (node != NULL && (key = va_arg(va, const char *)) != NULL)
		node = e2e_json_get(node, key);
	va_end(va);
	return node;
}

/* the CTCP body without the \001 framing */
static char *unframe(const char *ctcp)
{
	g_assert_true(ctcp[0] == '\001' && ctcp[strlen(ctcp) - 1] == '\001');
	return g_strndup(ctcp + 1, strlen(ctcp) - 2);
}

static void deliver_keyreq(PEER *to, PEER *from, const char *ctcp, const char *own,
                           char **rsp, char **reciprocal)
{
	char *body = unframe(ctcp);

	e2e_handle_keyreq(to->kr, &to->id, to->stamps, NULL, from->handle, from->nick, body, own,
	                  rsp, reciprocal);
	g_free(body);
}

static gboolean deliver_keyrsp(PEER *to, PEER *from, const char *ctcp)
{
	char *body = unframe(ctcp);
	gboolean ok = e2e_handle_keyrsp(to->kr, NULL, from->handle, from->nick, body);

	g_free(body);
	return ok;
}

static char *no_resolve(const char *nick, const E2E_JSON *kr, void *data)
{
	return NULL;
}

static char *table_resolve(const char *nick, const E2E_JSON *kr, void *data)
{
	const char *h = g_hash_table_lookup(data, nick);

	return h != NULL ? g_strdup(h) : NULL;
}

/* run one channel message through the gate of from and the decryption of to */
static char *send_channel(PEER *from, PEER *to, const char *chan, const char *text)
{
	E2E_GATE_RESULT g;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	GString *got = g_string_new(NULL);
	guint i;

	e2e_gate_decide(from->kr, TRUE, &from->id, NULL, chan, text, no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	memset(&in, 0, sizeof(in));
	in.nick = from->nick;
	in.handle = from->handle;
	in.target = chan;
	in.resolve = no_resolve;
	in.keyreq_stamps = to->stamps;
	for (i = 0; i < g.wires->len; i++) {
		e2e_decrypt_incoming(to->kr, &to->id, &in, g_ptr_array_index(g.wires, i), &r);
		g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
		g_string_append(got, r.plain);
		e2e_in_result_clear(&r);
	}
	e2e_gate_result_clear(&g);
	return g_string_free(got, FALSE);
}

static void test_identity(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_IDENTITY id, id2;
	gboolean created;
	char *error = NULL;
	E2E_JSON *ident;

	g_assert_false(e2e_identity_get(kr, &id, FALSE, &created, &error));
	g_free(error);
	error = NULL;
	g_assert_true(e2e_identity_get(kr, &id, TRUE, &created, &error));
	g_assert_true(created);
	g_assert_true(e2e_identity_get(kr, &id2, FALSE, &created, &error));
	g_assert_false(created);
	g_assert_cmpmem(id.sk, 64, id2.sk, 64);
	g_assert_cmpstr(id.fp_hex, ==, e2e_json_get_string(e2e_json_get(kr, "identity"), "fp"));

	/* a damaged identity is an error, never silently replaced */
	ident = e2e_json_get(kr, "identity");
	e2e_json_set_string(ident, "fp", "00000000000000000000000000000000");
	g_assert_false(e2e_identity_get(kr, &id2, TRUE, &created, &error));
	g_assert_nonnull(error);
	g_free(error);
	g_assert_cmpstr(e2e_json_get_string(ident, "fp"), ==, "00000000000000000000000000000000");
	e2e_json_free(kr);
}

/* the whole channel exchange in "normal" mode, both sides native */
static void test_handshake_normal_mode(void)
{
	PEER a, b;
	char *req, *rsp = NULL, *recip = NULL, *rsp2 = NULL, *recip2 = NULL, *error = NULL;
	char *got;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#test", "normal");
	enable(&b, "#test", "normal");

	/* /e2e handshake bob (on alice's side) */
	req = e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error);
	g_assert_nonnull(req);
	g_assert_nonnull(path_get(a.kr, "pending", "#test|" BOB, "eph_sk", NULL));
	/* a second one within 120 s is refused */
	g_assert_null(e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error));
	g_assert_cmpstr(error, ==, "key exchange already pending for #test|" BOB);
	g_free(error);

	/* bob is in normal mode: it waits for /e2e accept */
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(recip);
	g_assert_nonnull(path_get(b.kr, "pending_inbound", ALICE "|#test", NULL));
	g_assert_cmpstr(e2e_json_get_string(path_get(b.kr, "incoming", ALICE "|#test", NULL), "status"), ==, "pending");
	g_free(req);

	/* /e2e accept alice: the KEYRSP from the stored request + our KEYREQ */
	g_assert_cmpint(e2e_accept(b.kr, &b.id, ALICE, "#test", NULL, BOB, &rsp, &recip), ==,
	                E2E_ACCEPT_SENT);
	g_assert_null(path_get(b.kr, "pending_inbound", ALICE "|#test", NULL));
	g_assert_true(e2e_peer_accepted(b.kr, ALICE, "#test", a.id.fp_hex));
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_assert_true(deliver_keyrsp(&a, &b, rsp));
	g_assert_cmpstr(e2e_json_get_string(path_get(a.kr, "incoming", BOB "|#test", NULL), "status"), ==, "trusted");
	g_assert_null(path_get(a.kr, "pending", "#test|" BOB, NULL));
	/* the same KEYRSP again: nothing pending any more */
	g_assert_false(deliver_keyrsp(&a, &b, rsp));

	/* bob -> alice works now */
	got = send_channel(&b, &a, "#test", "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84");
	g_assert_cmpstr(got, ==, "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84");
	g_free(got);

	/* bob's reciprocal KEYREQ: alice holds bob's key, but holding it is
	   no decision of hers to give him her own - it waits for her
	   /e2e accept as well, without touching the session she has */
	deliver_keyreq(&a, &b, recip, ALICE, &rsp2, &recip2);
	g_assert_null(rsp2);
	g_assert_null(recip2);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|#test", NULL));
	g_assert_cmpstr(e2e_json_get_string(path_get(a.kr, "incoming", BOB "|#test", NULL), "status"), ==, "trusted");
	g_assert_true(e2e_session_has_key(path_get(a.kr, "incoming", BOB "|#test", NULL)));
	g_assert_cmpint(e2e_accept(a.kr, &a.id, BOB, "#test", NULL, ALICE, &rsp2, &recip2), ==,
	                E2E_ACCEPT_SENT);
	g_assert_nonnull(rsp2);
	g_assert_null(recip2);	/* she has bob's key already */
	g_assert_true(deliver_keyrsp(&b, &a, rsp2));
	got = send_channel(&a, &b, "#test", "hello bob");
	g_assert_cmpstr(got, ==, "hello bob");
	g_free(got);

	/* a long message: several chunks, each decrypted on its own */
	{
		char *longtext = g_strnfill(500, 'q');

		got = send_channel(&a, &b, "#test", longtext);
		g_assert_cmpstr(got, ==, longtext);
		g_free(got);
		g_free(longtext);
	}

	/* accepted once: bob's next request (e.g. after he lost his keys) is
	   answered without asking again */
	g_free(rsp2);
	g_free(recip2);
	e2e_json_remove(e2e_json_get(b.kr, "pending"), "#test|" ALICE);
	req = e2e_build_keyreq(b.kr, &b.id, "#test", ALICE, &error);
	g_assert_nonnull(req);
	g_hash_table_remove_all(a.stamps);
	deliver_keyreq(&a, &b, req, ALICE, &rsp2, &recip2);
	g_assert_nonnull(rsp2);
	g_free(req);
	g_free(rsp);
	g_free(recip);
	g_free(rsp2);
	g_free(recip2);
	peer_free(&a);
	peer_free(&b);
}

static void test_handshake_modes(void)
{
	PEER a, b;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL;
	E2E_JSON *rule;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);

	/* not enabled on bob's side: ignored, nothing stored */
	req = e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error);
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(b.kr, "peers")), ==, 0);

	/* quiet: ignored as well, the peer is remembered */
	enable(&b, "#test", "quiet");
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(path_get(b.kr, "pending_inbound", ALICE "|#test", NULL));
	g_assert_cmpuint(e2e_json_size(e2e_json_get(b.kr, "peers")), ==, 1);

	/* auto-accept: KEYRSP and our own KEYREQ back */
	enable(&b, "#test", "auto-accept");
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_assert_nonnull(path_get(b.kr, "outgoing_recipients", "#test|" ALICE, NULL));
	g_assert_true(e2e_peer_accepted(b.kr, ALICE, "#test", a.id.fp_hex));
	g_free(rsp);
	g_free(recip);
	/* no second reciprocal KEYREQ while ours is pending */
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_null(recip);
	g_free(rsp);
	g_free(req);

	/* normal mode, but an autotrust rule matches: answered at once */
	enable(&b, "#other", "normal");
	rule = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(rule, "scope", "global");
	e2e_json_set_string(rule, "handle_pattern", "ALICE@127.0.0.*");
	e2e_json_array_add(e2e_json_get(b.kr, "autotrust"), rule);
	req = e2e_build_keyreq(a.kr, &a.id, "#other", BOB, &error);
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_true(e2e_peer_accepted(b.kr, ALICE, "#other", a.id.fp_hex));
	g_free(rsp);
	g_free(recip);
	g_free(req);
	/* the rule removed: what it accepted stays accepted (as /e2e accept) */
	e2e_json_set(b.kr, "autotrust", e2e_json_new(E2E_JSON_ARRAY));
	e2e_json_remove(e2e_json_get(a.kr, "pending"), "#other|" BOB);
	req = e2e_build_keyreq(a.kr, &a.id, "#other", BOB, &error);
	g_hash_table_remove_all(b.stamps);
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_free(rsp);
	g_free(recip);
	g_free(req);
	/* another key under the same handle is not what was accepted */
	g_assert_false(e2e_peer_accepted(b.kr, ALICE, "#other", "00000000000000000000000000000000"));
	peer_free(&a);
	peer_free(&b);
}

static void test_trust_changes(void)
{
	PEER a, b, mallory;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL;
	E2E_JSON *ptc;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&b, "#test", "auto-accept");
	req = e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error);
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_free(rsp);
	g_free(recip);
	g_free(req);

	/* a new key for the same ident@host: blocked, recorded for reverify */
	peer_init(&mallory, "alice", ALICE, -1);
	req = e2e_build_keyreq(mallory.kr, &mallory.id, "#test", BOB, &error);
	g_hash_table_remove_all(b.stamps);
	deliver_keyreq(&b, &mallory, req, BOB, &rsp, &recip);
	g_assert_null(rsp);
	ptc = e2e_json_get(b.kr, "pending_trust_change");
	g_assert_cmpuint(e2e_json_size(ptc), ==, 1);
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(ptc->array, 0), "change"), ==, "fingerprint_changed");
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(ptc->array, 0), "new_fp"), ==, mallory.id.fp_hex);
	g_free(req);

	/* a revoked peer stays revoked */
	e2e_json_set_string(path_get(b.kr, "peers", a.id.fp_hex, NULL), "status", "revoked");
	req = e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error);
	g_assert_null(req);	/* still pending on alice's side */
	g_free(error);
	e2e_json_remove(e2e_json_get(a.kr, "pending"), "#test|" BOB);
	req = e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error);
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_null(rsp);
	/* one record per handle and context: the newest */
	g_assert_cmpuint(e2e_json_size(ptc), ==, 1);
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(ptc->array, 0), "change"), ==, "revoked");
	g_free(req);
	peer_free(&a);
	peer_free(&b);
	peer_free(&mallory);
}

static void unhex(const char *hex, unsigned char *out, gsize len)
{
	g_assert_true(e2e_hex_decode(hex, out, len));
}

/* the native side ("alice", seed 0x41) against messages rpe2e.pl made */
static void test_rpe2e_messages(void)
{
	PEER a;
	E2E_JSON *pend, *peer;
	unsigned char eph[32], key[32], *sk;
	char *s, *rsp = NULL, *recip = NULL, *body;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	gsize len, i;
	GString *joined;

	peer_init(&a, "alice", ALICE, 0x41);
	enable(&a, "#test", "auto-accept");

	/* rpe2e.pl's KEYRSP to alice's KEYREQ (ephemeral key 0x55...) */
	unhex(vec_alice_eph_sk_hex, eph, 32);
	pend = e2e_json_new(E2E_JSON_OBJECT);
	s = e2e_b64_encode(eph, 32);
	e2e_json_set_string(pend, "eph_sk", s);
	g_free(s);
	e2e_json_set_string(pend, "handle", BOB);
	e2e_json_set_string(pend, "channel", "#test");
	e2e_json_set_int(pend, "created_at", fake_now);
	e2e_json_set(e2e_json_get(a.kr, "pending"), "#test|" BOB, pend);
	g_assert_true(e2e_handle_keyrsp(a.kr, NULL, BOB, "bob", vec_keyrsp));
	sk = e2e_b64_decode(e2e_json_get_string(path_get(a.kr, "incoming", BOB "|#test", NULL), "sk"), &len);
	memset(key, 0x11, 32);
	g_assert_cmpmem(sk, len, key, 32);
	g_free(sk);

	/* and rpe2e.pl's messages on #test decrypt */
	memset(&in, 0, sizeof(in));
	in.nick = "bob";
	in.handle = BOB;
	in.target = "#test";
	in.resolve = no_resolve;
	in.keyreq_stamps = a.stamps;
	for (i = 0; i < 3; i++) {
		gsize j;

		joined = g_string_new(NULL);
		for (j = 0; vec_wire[i].wire[j] != NULL; j++) {
			e2e_decrypt_incoming(a.kr, &a.id, &in, vec_wire[i].wire[j], &r);
			g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
			g_string_append(joined, r.plain);
			e2e_in_result_clear(&r);
		}
		g_assert_cmpstr(joined->str, ==, vec_wire[i].plain);
		g_string_free(joined, TRUE);
	}

	/* rpe2e.pl's REKEY: accepted from the known peer bob */
	g_assert_true(e2e_handle_rekey(a.kr, &a.id, NULL, BOB, "bob", vec_rekey));
	sk = e2e_b64_decode(e2e_json_get_string(path_get(a.kr, "incoming", BOB "|#test", NULL), "sk"), &len);
	unhex(vec_rekey_key_hex, key, 32);
	g_assert_cmpmem(sk, len, key, 32);
	g_free(sk);

	/* the same REKEY again is a replay: refused */
	g_assert_false(e2e_handle_rekey(a.kr, &a.id, NULL, BOB, "bob", vec_rekey));

	/* rpe2e.pl's KEYREQ (bob, #test): alice answers (auto-accept), and the
	   KEYRSP opens with the ephemeral secret rpe2e.pl kept */
	g_hash_table_remove_all(a.stamps);
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, NULL, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_null(recip);	/* bob's session is trusted already */
	body = unframe(rsp);
	{
		E2E_HANDSHAKE *hs = e2e_handshake_parse(body, E2E_HS_KEYRSP);
		unsigned char *bob_eph, shared[32], wrap[32], got[32], *ours;
		const char *info = "RPE2E01-WRAP:#test";

		g_assert_nonnull(hs);
		g_assert_true(e2e_handshake_verify(hs));
		bob_eph = e2e_b64_decode(vec_keyreq_eph_sk_b64, &len);
		g_assert_true(e2e_x25519(shared, bob_eph, hs->eph));
		g_assert_true(e2e_wrap_key(wrap, shared, info, strlen(info)));
		g_assert_true(e2e_xchacha_decrypt(got, hs->wrap_ct, hs->wrap_ctlen,
		                                  (const unsigned char *) info, strlen(info),
		                                  hs->wrap_nonce, wrap));
		ours = e2e_b64_decode(e2e_json_get_string(path_get(a.kr, "outgoing", "#test", NULL), "sk"), &len);
		g_assert_cmpmem(got, 32, ours, len);
		g_free(ours);
		g_free(bob_eph);
		e2e_handshake_free(hs);
	}
	g_free(body);
	g_free(rsp);

	/* a REKEY from someone never seen is ignored */
	peer = path_get(a.kr, "peers", NULL);
	g_hash_table_remove_all(peer->object);
	g_assert_false(e2e_handle_rekey(a.kr, &a.id, NULL, BOB, "bob", vec_rekey));
	peer_free(&a);
}

static void test_gate(void)
{
	PEER a;
	E2E_GATE_RESULT g;
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	E2E_JSON *out;

	peer_init(&a, "alice", ALICE, -1);
	g_hash_table_insert(nicks, (char *) "bob", (char *) BOB);

#define GATE(target, body) e2e_gate_decide(a.kr, TRUE, &a.id, NULL, target, body, table_resolve, nicks, &g)
	/* no E2E state: plain text */
	GATE("#test", "hello");
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	GATE("bob", "hello");
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);

	enable(&a, "#test", "normal");
	GATE("#test", "hello");
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, "#test");
	g_assert_true(g.save_needed);	/* the first key was generated */
	e2e_gate_result_clear(&g);
	GATE("#test", "again");
	g_assert_false(g.save_needed);
	e2e_gate_result_clear(&g);

	/* bot commands go out in clear text, with a warning */
	GATE("#test", ".op me");
	g_assert_cmpint(g.action, ==, E2E_GATE_BYPASS);
	g_assert_nonnull(strstr(g.message, "sent in CLEARTEXT"));
	e2e_gate_result_clear(&g);
	GATE("#test", "...so what");
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	GATE("#test", "!!");
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	/* on a channel without E2E silently */
	GATE("#plain", "!seen bob");
	g_assert_cmpint(g.action, ==, E2E_GATE_BYPASS);
	g_assert_null(g.message);
	e2e_gate_result_clear(&g);

	/* CTCP: ACTION is encrypted, anything else is not */
	GATE("#test", "\001ACTION waves\001");
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	GATE("#test", "\001VERSION\001");
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);

	/* STATUSMSG: @#test is #test */
	GATE("@#test", "ops only");
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, "#test");
	e2e_gate_result_clear(&g);
	/* +#test with both readings enabled cannot be decided */
	enable(&a, "+#test", "normal");
	GATE("+#test", "x");
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "ambiguous target +#test"));
	e2e_gate_result_clear(&g);

	/* several targets in one line with an encrypted one among them */
	GATE("#plain,#test", "x");
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	GATE("#plain,#other", "x");
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);

	/* channel names are case-insensitive on IRC: another spelling of an
	   encrypted channel is refused, never sent in clear text */
	GATE("#TEST", "hello");
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "#test"));
	e2e_gate_result_clear(&g);

	/* an empty line on an encrypted channel is not sent as plain text */
	GATE("#test", "");
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_cmpstr(g.message, ==, E2E_REFUSE_ENCRYPT);
	e2e_gate_result_clear(&g);

	/* queries: keyed by the peer's handle */
	enable(&a, "@" BOB, "normal");
	GATE("bob", "private");
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, "@" BOB);
	e2e_gate_result_clear(&g);
	/* an old config row by bare nick, handle unknown: refused */
	enable(&a, "carol", "normal");
	GATE("carol", "private");
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_cmpstr(g.message, ==, E2E_REFUSE_NO_HANDLE);
	e2e_gate_result_clear(&g);

	/* a keyring that could not be read: nothing but bot commands */
	e2e_gate_decide(a.kr, FALSE, &a.id, NULL, "#plain", "hello", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_cmpstr(g.message, ==, E2E_REFUSE_KEYRING);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, FALSE, &a.id, NULL, "#plain", ".op", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_BYPASS);
	g_assert_nonnull(g.message);
	e2e_gate_result_clear(&g);

	/* a scheduled rotation: a new key, REKEY to the recipients first */
	{
		E2E_JSON *rec = e2e_json_new(E2E_JSON_OBJECT), *peer = e2e_json_new(E2E_JSON_OBJECT);
		unsigned char seed[32], pk[32], sk[64];
		char *pkb, *fp;
		const char *before;

		memset(seed, 9, sizeof(seed));
		e2e_ed25519_seed_keypair(seed, pk, sk);
		pkb = e2e_b64_encode(pk, 32);
		fp = e2e_fingerprint_hex(pk);
		e2e_json_set_string(peer, "pk", pkb);
		e2e_json_set_string(peer, "last_nick", "bob");
		e2e_json_set_string(peer, "last_handle", BOB);
		e2e_json_set(e2e_json_get(a.kr, "peers"), fp, peer);
		e2e_json_set_string(rec, "channel", "#test");
		e2e_json_set_string(rec, "handle", BOB);
		e2e_json_set_string(rec, "fingerprint", fp);
		e2e_json_set(e2e_json_get(a.kr, "outgoing_recipients"), "#test|" BOB, rec);
		/* REKEYs go to accepted recipients only */
		{
			E2E_JSON *acc = e2e_json_new(E2E_JSON_OBJECT);

			e2e_json_set_string(acc, "fp", fp);
			e2e_json_set(e2e_json_object_member(a.kr, "accepted"), BOB "|#test", acc);
		}
		out = path_get(a.kr, "outgoing", "#test", NULL);
		before = g_strdup(e2e_json_get_string(out, "sk"));
		e2e_json_set_int(out, "pending_rotation", 1);
		GATE("#test", "rotated");
		g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
		g_assert_true(g.save_needed);
		g_assert_cmpuint(g.notices->len, ==, 2);
		g_assert_cmpstr(g_ptr_array_index(g.notices, 0), ==, "bob");
		g_assert_true(g_str_has_prefix(g_ptr_array_index(g.notices, 1), "\001RPEE2E REKEY v=1 c=#test "));
		out = path_get(a.kr, "outgoing", "#test", NULL);
		g_assert_cmpstr(e2e_json_get_string(out, "sk"), !=, before);
		g_assert_false(e2e_json_truthy(e2e_json_get(out, "pending_rotation")));
		e2e_gate_result_clear(&g);
		g_free((char *) before);
		g_free(pkb);
		g_free(fp);
	}
#undef GATE
	g_hash_table_destroy(nicks);
	peer_free(&a);
}

static void test_incoming(void)
{
	PEER a, b;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	E2E_GATE_RESULT g;
	const char *wire;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&b, "#test", "normal");
	e2e_gate_decide(b.kr, TRUE, &b.id, NULL, "#test", "hi", no_resolve, NULL, &g);
	wire = g_ptr_array_index(g.wires, 0);

	memset(&in, 0, sizeof(in));
	in.nick = "bob";
	in.handle = BOB;
	in.target = "#test";
	in.resolve = no_resolve;
	in.keyreq_stamps = a.stamps;

	/* plain text is not ours */
	e2e_decrypt_incoming(a.kr, &a.id, &in, "hello", &r);
	g_assert_cmpint(r.action, ==, E2E_IN_NOT_WIRE);
	e2e_in_result_clear(&r);
	/* no session, E2E off here: hidden, no KEYREQ */
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_null(r.keyreq);
	e2e_in_result_clear(&r);
	/* E2E on: hidden, and a KEYREQ once per 30 s */
	g_hash_table_remove_all(a.stamps);
	enable(&a, "#test", "normal");
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_nonnull(r.keyreq);
	g_assert_true(g_str_has_prefix(r.keyreq, "\001RPEE2E KEYREQ v=1 c=#test "));
	g_assert_true(r.save_needed);
	e2e_in_result_clear(&r);
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_null(r.keyreq);
	e2e_in_result_clear(&r);

	/* too old: dropped before any lookup */
	fake_now += 1000;
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_nonnull(strstr(r.debug, "timestamp skew"));
	e2e_in_result_clear(&r);
	fake_now -= 1000;

	/* our own message relayed back: opened with our outgoing key */
	in.nick = "bob";
	in.handle = BOB;
	in.is_own_line = TRUE;
	e2e_decrypt_incoming(b.kr, &b.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
	g_assert_cmpstr(r.plain, ==, "hi");
	e2e_in_result_clear(&r);
	e2e_gate_result_clear(&g);

	/* a DM before our own handle is known waits */
	in.is_own_line = FALSE;
	in.target = "bob";
	in.own_handle = NULL;
	enable(&b, "@" ALICE, "normal");
	{
		GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);

		g_hash_table_insert(nicks, (char *) "alice", (char *) ALICE);
		e2e_gate_decide(b.kr, TRUE, &b.id, NULL, "alice", "secret", table_resolve, nicks, &g);
		g_hash_table_destroy(nicks);
	}
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	wire = g_ptr_array_index(g.wires, 0);
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_WAIT_OWN);
	e2e_in_result_clear(&r);
	/* with it, the KEYREQ asks for the "@<own>" direction */
	in.own_handle = ALICE;
	enable(&a, "@" BOB, "normal");
	g_hash_table_remove_all(a.stamps);
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_nonnull(r.keyreq);
	g_assert_true(g_str_has_prefix(r.keyreq, "\001RPEE2E KEYREQ v=1 c=@" ALICE " "));
	e2e_in_result_clear(&r);
	e2e_gate_result_clear(&g);
	peer_free(&a);
	peer_free(&b);
}

static void test_stamps_and_glob(void)
{
	GHashTable *s = e2e_stamps_new();
	int i;

	g_assert_true(e2e_stamp_allow(s, "x", 10));
	g_assert_false(e2e_stamp_allow(s, "x", 10));
	g_assert_true(e2e_stamp_allow(s, "y", 10));
	fake_now += 10;
	g_assert_true(e2e_stamp_allow(s, "x", 10));
	/* expired stamps do not pile up */
	for (i = 0; i < 300; i++) {
		char *k = g_strdup_printf("k%d", i);

		e2e_stamp_allow(s, k, 10);
		g_free(k);
	}
	fake_now += 20;
	g_assert_true(e2e_stamp_allow(s, "last", 10));
	g_assert_cmpuint(g_hash_table_size(s), <=, 2);
	g_hash_table_destroy(s);

	{
		E2E_JSON *kr = e2e_keyring_new(), *p = e2e_json_new(E2E_JSON_OBJECT);

		e2e_json_set_string(p, "pk", "x");
		e2e_json_set(e2e_json_get(kr, "peers"), "00", p);
		g_assert_cmpstr(e2e_find_peer_by_handle(kr, "", NULL), ==, "00");
		g_assert_null(e2e_find_peer_by_handle(kr, "a@b", NULL));
		e2e_json_free(kr);
	}

	g_assert_true(e2e_glob_match("*@*.example.org", "Bob@Host.EXAMPLE.org"));
	g_assert_true(e2e_glob_match("b?b@*", "bob@x"));
	g_assert_false(e2e_glob_match("b?b@*", "bb@x"));
	g_assert_false(e2e_glob_match("*.example.org", "x.example.org.evil"));
	g_assert_true(e2e_glob_match("*", ""));
	g_assert_true(e2e_glob_match("a*b*c*d", "aXXbYYcZZd"));
	g_assert_false(e2e_glob_match("a*b*c*d*e", "aXXbYYcZZd"));
}

static void test_export_import(void)
{
	PEER a, b;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL, *s1, *s2;
	E2E_JSON *doc, *kr;
	E2E_GATE_RESULT g;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&b, "#test", "auto-accept");
	req = e2e_build_keyreq(a.kr, &a.id, "#test", BOB, &error);
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	e2e_gate_decide(b.kr, TRUE, &b.id, NULL, "#test", "hi", no_resolve, NULL, &g);
	e2e_gate_result_clear(&g);

	doc = e2e_keyring_export(b.kr);
	g_assert_cmpint(e2e_json_get_int(doc, "version", 0), ==, 1);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(doc, "peers")), ==, 1);
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(e2e_json_get(doc, "peers")->array, 0), "fingerprint"), ==, a.id.fp_hex);
	kr = e2e_keyring_import(doc, &error);
	g_assert_nonnull(kr);
	/* what an export carries comes back the same (pending state is not
	   exported, as in rpe2e.pl) */
	e2e_json_set(b.kr, "pending", e2e_json_new(E2E_JSON_OBJECT));
	e2e_json_set(b.kr, "pending_inbound", e2e_json_new(E2E_JSON_OBJECT));
	/* nor are the decisions of this client (accepted peers, used REKEY
	   nonces): the format is rpe2e.pl's / repartee's */
	e2e_json_remove(b.kr, "accepted");
	e2e_json_remove(b.kr, "seen_rekeys");
	e2e_json_remove(kr, "accepted");
	e2e_json_remove(kr, "seen_rekeys");
	s1 = e2e_keyring_snapshot(b.kr);
	s2 = e2e_keyring_snapshot(kr);
	g_assert_cmpstr(s1, ==, s2);
	g_free(s1);
	g_free(s2);
	e2e_json_free(kr);
	e2e_json_free(doc);

	/* not an export */
	doc = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_int(doc, "peers", 3);
	g_assert_null(e2e_keyring_import(doc, &error));
	g_free(error);
	e2e_json_free(doc);
	g_free(req);
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
	peer_free(&b);
}

/* ---- trust and robustness rules stricter than rpe2e.pl ---- */

#define MAL   "mallory@127.0.0.1"
#define THIRD "@bob@b.example.net"

/* the first wire of a message from's gate makes for target */
static char *gate_wire(PEER *from, const char *target, const char *text)
{
	E2E_GATE_RESULT g;
	char *wire;

	e2e_gate_decide(from->kr, TRUE, &from->id, NULL, target, text, no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	wire = g_strdup(g_ptr_array_index(g.wires, 0));
	e2e_gate_result_clear(&g);
	return wire;
}

/* a channel wire from from, received by to */
static void recv_wire(PEER *to, PEER *from, const char *chan, const char *wire, E2E_IN_RESULT *r)
{
	E2E_IN_PARAMS in;

	memset(&in, 0, sizeof(in));
	in.nick = from->nick;
	in.handle = from->handle;
	in.target = chan;
	in.own_handle = to->handle;
	in.resolve = no_resolve;
	in.keyreq_stamps = to->stamps;
	e2e_decrypt_incoming(to->kr, &to->id, &in, wire, r);
}

static gboolean deliver_rekey(PEER *to, PEER *from, const char *ctcp)
{
	char *body = unframe(ctcp);
	gboolean ok = e2e_handle_rekey(to->kr, &to->id, NULL, from->handle, from->nick, body);

	g_free(body);
	return ok;
}

static gboolean session_key(PEER *p, const char *handle, const char *ctx, unsigned char key[32])
{
	char *k = g_strconcat(handle, "|", ctx, NULL);
	const char *b64 = e2e_json_get_string(path_get(p->kr, "incoming", k, NULL), "sk");
	unsigned char *raw;
	gsize len = 0;
	gboolean ok;

	g_free(k);
	raw = b64 != NULL ? e2e_b64_decode(b64, &len) : NULL;
	ok = raw != NULL && len == 32;
	if (ok)
		memcpy(key, raw, 32);
	g_free(raw);
	return ok;
}

/* to and from exchange keys for ctx, both in auto-accept mode; to holds
   from's key afterwards */
static void exchange(PEER *to, PEER *from, const char *ctx)
{
	char *req, *rsp = NULL, *recip = NULL, *error = NULL, *pk;

	pk = g_strconcat(ctx, "|", from->handle, NULL);
	e2e_json_remove(e2e_json_get(to->kr, "pending"), pk);
	g_free(pk);
	req = e2e_build_keyreq(to->kr, &to->id, ctx, from->handle, &error);
	g_assert_nonnull(req);
	deliver_keyreq(from, to, req, from->handle, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_true(deliver_keyrsp(to, from, rsp));
	g_free(req);
	g_free(rsp);
	g_free(recip);
}

/* a session we asked for is no permission to give our key away */
static void test_hardening_keyrsp_is_no_acceptance(void)
{
	PEER a, m;
	E2E_IN_RESULT r;
	char *wire, *rsp = NULL, *recip = NULL, *rsp2 = NULL, *recip2 = NULL, *req, *error = NULL;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&m, "mallory", MAL, -1);
	enable(&a, "#secret", "normal");
	enable(&m, "#secret", "auto-accept");
	g_free(gate_wire(&a, "#secret", "warm-up"));

	/* any wire from mallory: alice asks him for his key */
	wire = gate_wire(&m, "#secret", "x");
	recv_wire(&a, &m, "#secret", wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_nonnull(r.keyreq);
	deliver_keyreq(&m, &a, r.keyreq, MAL, &rsp, &recip);
	e2e_in_result_clear(&r);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	/* his answer: alice can read him now */
	g_assert_true(deliver_keyrsp(&a, &m, rsp));
	g_assert_false(e2e_peer_accepted(a.kr, MAL, "#secret", m.id.fp_hex));
	/* his request for alice's key waits for /e2e accept */
	deliver_keyreq(&a, &m, recip, ALICE, &rsp2, &recip2);
	g_assert_null(rsp2);
	g_assert_null(recip2);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", MAL "|#secret", NULL));
	g_assert_cmpstr(e2e_json_get_string(path_get(a.kr, "incoming", MAL "|#secret", NULL), "status"), ==, "trusted");
	g_assert_true(e2e_session_has_key(path_get(a.kr, "incoming", MAL "|#secret", NULL)));
	g_free(rsp);
	g_free(recip);

	/* quiet mode: the same, it is no acceptance either */
	enable(&a, "#secret", "quiet");
	e2e_json_remove(e2e_json_get(a.kr, "pending_inbound"), MAL "|#secret");
	e2e_json_remove(e2e_json_get(m.kr, "pending"), "#secret|" ALICE);
	req = e2e_build_keyreq(m.kr, &m.id, "#secret", ALICE, &error);
	g_assert_nonnull(req);
	deliver_keyreq(&a, &m, req, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", MAL "|#secret", NULL));

	/* /e2e accept mallory: now he gets the key, and later requests too */
	g_assert_cmpint(e2e_accept(a.kr, &a.id, MAL, "#secret", NULL, ALICE, &rsp, &recip), ==,
	                E2E_ACCEPT_SENT);
	g_assert_nonnull(rsp);
	g_assert_true(e2e_peer_accepted(a.kr, MAL, "#secret", m.id.fp_hex));
	g_free(rsp);
	g_free(recip);
	deliver_keyreq(&a, &m, req, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_free(rsp);
	g_free(recip);
	g_free(req);
	g_free(wire);
	peer_free(&a);
	peer_free(&m);
}

/* a REKEY replaces a trusted session of that handle and context, it
   never creates one; a DM request names its sender */
static void test_hardening_rekey_needs_session(void)
{
	static const char *const ctxs[] = { "#secret", THIRD, NULL };
	PEER a, m, b;
	unsigned char key[32], got[32];
	gboolean rot, gen;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL, *rk, *pk;
	int i;

	for (i = 0; ctxs[i] != NULL; i++) {
		peer_init(&a, "alice", ALICE, -1);
		peer_init(&m, "mallory", MAL, -1);
		enable(&a, ctxs[i], "normal");
		g_assert_true(e2e_outgoing_key(a.kr, ctxs[i], key, &rot, &gen));
		req = e2e_build_keyreq(m.kr, &m.id, ctxs[i], ALICE, &error);
		deliver_keyreq(&a, &m, req, ALICE, &rsp, &recip);
		g_assert_null(rsp);
		g_free(req);
		/* his REKEY wrapped to alice's public key */
		memset(key, 0x41, sizeof(key));
		rk = e2e_build_rekey(&m.id, ctxs[i], a.id.pk, key);
		g_assert_false(deliver_rekey(&a, &m, rk));
		g_assert_false(session_key(&a, MAL, ctxs[i], got) && memcmp(got, key, 32) == 0);
		g_free(rk);
		/* so a second request still gets nothing */
		pk = g_strconcat(ctxs[i], "|" ALICE, NULL);
		e2e_json_remove(e2e_json_get(m.kr, "pending"), pk);
		g_free(pk);
		fake_now += E2E_PENDING_KEYREQ_TTL + 1;
		req = e2e_build_keyreq(m.kr, &m.id, ctxs[i], ALICE, &error);
		g_assert_nonnull(req);
		g_hash_table_remove_all(a.stamps);
		deliver_keyreq(&a, &m, req, ALICE, &rsp, &recip);
		g_assert_null(rsp);
		g_free(req);
		peer_free(&a);
		peer_free(&m);
	}

	/* a DM context is "@" + the requester's own handle: a request naming
	   a third party is ignored, nothing is stored */
	peer_init(&a, "alice", ALICE, -1);
	peer_init(&m, "mallory", MAL, -1);
	enable(&a, THIRD, "normal");
	req = e2e_build_keyreq(m.kr, &m.id, THIRD, ALICE, &error);
	deliver_keyreq(&a, &m, req, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, 0);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "peers")), ==, 0);
	g_free(req);
	peer_free(&a);
	peer_free(&m);

	/* a real session: the REKEY replaces it, but only while E2E is on */
	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#x", "auto-accept");
	enable(&b, "#x", "auto-accept");
	exchange(&a, &b, "#x");
	memset(key, 0x42, sizeof(key));
	rk = e2e_build_rekey(&b.id, "#x", a.id.pk, key);
	g_assert_true(deliver_rekey(&a, &b, rk));
	g_assert_true(session_key(&a, BOB, "#x", got));
	g_assert_cmpmem(got, 32, key, 32);
	g_free(rk);
	e2e_json_set_int(path_get(a.kr, "channels", "#x", NULL), "enabled", 0);
	memset(key, 0x43, sizeof(key));
	rk = e2e_build_rekey(&b.id, "#x", a.id.pk, key);
	g_assert_false(deliver_rekey(&a, &b, rk));
	g_assert_true(session_key(&a, BOB, "#x", got));
	g_assert_cmpint(got[0], ==, 0x42);
	g_free(rk);
	/* no session on another channel of the same peer: none is created */
	enable(&a, "#y", "auto-accept");
	rk = e2e_build_rekey(&b.id, "#y", a.id.pk, key);
	g_assert_false(deliver_rekey(&a, &b, rk));
	g_assert_false(session_key(&a, BOB, "#y", got));
	g_free(rk);

	/* the DM direction bob -> alice: "@<alice>", config "@<bob>" */
	enable(&a, "@" BOB, "auto-accept");
	enable(&b, "@" ALICE, "auto-accept");
	exchange(&a, &b, "@" ALICE);
	memset(key, 0x44, sizeof(key));
	rk = e2e_build_rekey(&b.id, "@" ALICE, a.id.pk, key);
	g_assert_true(deliver_rekey(&a, &b, rk));
	g_assert_true(session_key(&a, BOB, "@" ALICE, got));
	g_assert_cmpint(got[0], ==, 0x44);
	g_free(rk);
	peer_free(&a);
	peer_free(&b);
}

/* a REKEY is used once */
static void test_hardening_rekey_replay(void)
{
	PEER a, b;
	unsigned char k1[32], k2[32], got[32];
	char *rk1, *rk2;
	int i;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#x", "auto-accept");
	enable(&b, "#x", "auto-accept");
	exchange(&a, &b, "#x");
	memset(k1, 1, sizeof(k1));
	memset(k2, 2, sizeof(k2));
	rk1 = e2e_build_rekey(&b.id, "#x", a.id.pk, k1);
	rk2 = e2e_build_rekey(&b.id, "#x", a.id.pk, k2);
	g_assert_true(deliver_rekey(&a, &b, rk1));
	g_assert_true(deliver_rekey(&a, &b, rk2));
	/* the old one again would roll the session back */
	g_assert_false(deliver_rekey(&a, &b, rk1));
	g_assert_false(deliver_rekey(&a, &b, rk2));
	g_assert_true(session_key(&a, BOB, "#x", got));
	g_assert_cmpmem(got, 32, k2, 32);
	g_free(rk1);
	g_free(rk2);
	/* the used nonces are kept, never forgotten: once there are
	   E2E_MAX_SEEN_REKEYS, further REKEYs are refused (the next wire
	   then asks for a key) */
	for (i = 0; i < E2E_MAX_SEEN_REKEYS + 20; i++) {
		char *rk = e2e_build_rekey(&b.id, "#x", a.id.pk, k1);

		fake_now++;
		g_assert_true(deliver_rekey(&a, &b, rk) == (i < E2E_MAX_SEEN_REKEYS - 2));
		g_free(rk);
	}
	{
		E2E_JSON *seen = e2e_json_get(a.kr, "seen_rekeys");
		GPtrArray *keys = e2e_json_keys(seen);

		g_assert_cmpuint(keys->len, ==, 1);
		g_assert_cmpuint(e2e_json_size(e2e_json_get(seen, g_ptr_array_index(keys, 0))), ==,
		                 E2E_MAX_SEEN_REKEYS);
		g_ptr_array_unref(keys);
	}
	peer_free(&a);
	peer_free(&b);
}

static char *fixed_resolve(const char *nick, const E2E_JSON *kr, void *data)
{
	return g_strdup(data);
}

/* E2E is on for a nick's old ident@host: refused, not clear text */
static void test_hardening_dm_handle_changed(void)
{
	E2E_JSON *kr = e2e_keyring_new(), *peer;
	E2E_GATE_RESULT g;

	enable(&(PEER) { .kr = kr }, "@bob@old.example.net", "normal");
	peer = e2e_json_object_member(e2e_json_get(kr, "peers"), "00");
	e2e_json_set_string(peer, "last_nick", "bob");
	e2e_json_set_string(peer, "last_handle", "bob@old.example.net");

	e2e_gate_decide(kr, TRUE, NULL, NULL, "bob", "my secret", fixed_resolve, (char *) "bob@NEW.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "bob@old.example.net"));
	e2e_gate_result_clear(&g);
	/* only the case of the host differs: the context would not match */
	e2e_gate_decide(kr, TRUE, NULL, NULL, "BOB", "my secret", fixed_resolve, (char *) "bob@OLD.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	/* the address E2E is on for: encrypted */
	e2e_gate_decide(kr, TRUE, NULL, NULL, "bob", "my secret", fixed_resolve, (char *) "bob@old.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	/* another nick: clear text as before */
	e2e_gate_decide(kr, TRUE, NULL, NULL, "carol", "hello", fixed_resolve, (char *) "carol@c.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	/* E2E turned off for the old address: clear text */
	e2e_json_set_int(path_get(kr, "channels", "@bob@old.example.net", NULL), "enabled", 0);
	e2e_gate_decide(kr, TRUE, NULL, NULL, "bob", "hello", fixed_resolve, (char *) "bob@NEW.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_json_free(kr);
}

/* a session row without a real key is never trusted, never used */
static void test_hardening_no_key_no_trust(void)
{
	PEER a, m;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL;
	unsigned char zero[32] = { 0 };
	const char *err = NULL;
	GPtrArray *w;
	E2E_JSON *row;
	E2E_IN_RESULT r;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&m, "mallory", MAL, -1);
	enable(&a, "#c", "normal");
	req = e2e_build_keyreq(m.kr, &m.id, "#c", ALICE, &error);
	deliver_keyreq(&a, &m, req, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	row = path_get(a.kr, "incoming", MAL "|#c", NULL);
	g_assert_nonnull(row);
	g_assert_false(e2e_session_has_key(row));
	g_assert_false(e2e_session_has_key(NULL));
	/* /e2e decline, then /e2e accept: no request, no key - refused */
	e2e_json_remove(e2e_json_get(a.kr, "pending_inbound"), MAL "|#c");
	g_assert_cmpint(e2e_accept(a.kr, &a.id, MAL, "#c", NULL, ALICE, &rsp, &recip), ==,
	                E2E_ACCEPT_NO_KEY);
	g_assert_null(rsp);
	g_assert_cmpstr(e2e_json_get_string(row, "status"), !=, "trusted");
	/* /e2e unrevoke: the same */
	e2e_json_set_string(row, "status", "revoked");
	g_assert_cmpint(e2e_unrevoke(a.kr, MAL, "#c", NULL), ==, 0);
	g_assert_cmpstr(e2e_json_get_string(row, "status"), ==, "revoked");

	/* even marked trusted, an all-zero key decrypts nothing (anyone
	   could encrypt under it), and the peer is asked for a real one */
	e2e_json_set_string(row, "status", "trusted");
	w = e2e_encrypt_plain(zero, "#c", "forged under the zero key", fake_now, &err);
	g_hash_table_remove_all(a.stamps);
	recv_wire(&a, &m, "#c", g_ptr_array_index(w, 0), &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_nonnull(r.keyreq);
	e2e_in_result_clear(&r);
	g_ptr_array_unref(w);

	/* with a real key both work */
	enable(&m, "#c", "auto-accept");
	exchange(&a, &m, "#c");
	row = path_get(a.kr, "incoming", MAL "|#c", NULL);
	g_assert_true(e2e_session_has_key(row));
	e2e_json_set_string(row, "status", "revoked");
	g_assert_cmpint(e2e_unrevoke(a.kr, MAL, "#c", NULL), ==, 1);
	g_assert_cmpstr(e2e_json_get_string(row, "status"), ==, "trusted");
	e2e_json_set_string(row, "status", "pending");
	g_assert_cmpint(e2e_accept(a.kr, &a.id, MAL, "#c", NULL, ALICE, &rsp, &recip), ==,
	                E2E_ACCEPT_TRUSTED);
	g_assert_cmpstr(e2e_json_get_string(row, "status"), ==, "trusted");
	g_assert_true(e2e_peer_accepted(a.kr, MAL, "#c", m.id.fp_hex));
	g_free(req);
	peer_free(&a);
	peer_free(&m);
}

/* no line breaks or NUL out of a decrypted message */
static void test_hardening_control_characters(void)
{
	PEER a, b;
	E2E_WIRE w;
	E2E_IN_RESULT r;
	GByteArray *aad;
	unsigned char key[32];
	static const char pt[] = "one\rtwo\nthree\0four \002bold\002 \00304red";
	char *line;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#c", "auto-accept");
	enable(&b, "#c", "auto-accept");
	exchange(&a, &b, "#c");
	g_assert_true(e2e_outgoing_key(b.kr, "#c", key, &(gboolean) { 0 }, &(gboolean) { 0 }));

	memset(&w, 0, sizeof(w));
	g_assert_true(e2e_random_bytes(w.msgid, 8));
	g_assert_true(e2e_random_bytes(w.nonce, 24));
	w.ts = fake_now;
	w.part = w.total = 1;
	w.ctlen = sizeof(pt) - 1 + E2E_TAG_LEN;
	w.ct = g_malloc(w.ctlen);
	aad = e2e_aad("#c", w.msgid, w.ts, 1, 1);
	g_assert_true(e2e_xchacha_encrypt(w.ct, (const unsigned char *) pt, sizeof(pt) - 1,
	                                  aad->data, aad->len, w.nonce, key));
	g_byte_array_unref(aad);
	line = e2e_wire_encode(&w);
	g_free(w.ct);
	recv_wire(&a, &b, "#c", line, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
	g_assert_cmpstr(r.plain, ==, "one two three four \002bold\002 \00304red");
	e2e_in_result_clear(&r);
	g_free(line);
	peer_free(&a);
	peer_free(&b);
}

/* a KEYRSP answers our request to that handle, and only one that
   opens counts */
static void test_hardening_keyrsp_binding(void)
{
	PEER a, b, m;
	unsigned char eph[32], eph_sk[32];
	char *req, *junk, *rsp = NULL, *recip = NULL, *error = NULL;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	peer_init(&m, "mallory", MAL, -1);
	enable(&a, "#t", "normal");
	enable(&b, "#t", "auto-accept");
	enable(&m, "#t", "auto-accept");
	req = e2e_build_keyreq(a.kr, &a.id, "#t", BOB, &error);
	/* signed by bob, but wrapped for another request */
	g_assert_true(e2e_x25519_keypair(eph_sk, eph));
	junk = e2e_build_keyrsp_for_req(b.kr, &b.id, "#t", ALICE, a.id.pk, eph);
	g_assert_false(deliver_keyrsp(&a, &b, junk));
	g_assert_nonnull(path_get(a.kr, "pending", "#t|" BOB, NULL));
	/* bob's real answer still works */
	deliver_keyreq(&b, &a, req, BOB, &rsp, &recip);
	g_assert_true(deliver_keyrsp(&a, &b, rsp));
	g_free(rsp);
	g_free(recip);
	g_free(junk);
	g_free(req);

	/* a request sent without a known handle (an old keyring): nobody may
	   answer it */
	req = e2e_build_keyreq(a.kr, &a.id, "#t", NULL, &error);
	g_assert_nonnull(path_get(a.kr, "pending", "#t", NULL));
	deliver_keyreq(&m, &a, req, MAL, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_false(deliver_keyrsp(&a, &m, rsp));
	g_assert_null(path_get(a.kr, "incoming", MAL "|#t", NULL));
	g_free(rsp);
	g_free(recip);
	g_free(req);
	peer_free(&a);
	peer_free(&b);
	peer_free(&m);
}

/* what strangers can make us store is bounded, the handshakes are
   rate-limited */
static void test_hardening_limits(void)
{
	PEER a;
	GHashTable *stamps = e2e_stamps_new();
	int i;

	peer_init(&a, "alice", ALICE, -1);
	enable(&a, "#c", "normal");
	for (i = 0; i < E2E_MAX_UNACCEPTED_PEERS + 40; i++) {
		E2E_JSON *kr = e2e_keyring_new();
		E2E_IDENTITY id;
		gboolean created;
		char *handle, *req, *rsp = NULL, *recip = NULL, *error = NULL, *body;

		g_assert_true(e2e_identity_get(kr, &id, TRUE, &created, &error));
		handle = g_strdup_printf("u%d@h%d", i, i);
		req = e2e_build_keyreq(kr, &id, "#c", ALICE, &error);
		body = unframe(req);
		fake_now++;
		e2e_handle_keyreq(a.kr, &a.id, a.stamps, NULL, handle, "u", body, ALICE, &rsp, &recip);
		g_assert_null(rsp);
		g_free(body);
		g_free(req);
		g_free(handle);
		e2e_identity_wipe(&id);
		e2e_json_free(kr);
	}
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), <=, E2E_MAX_PENDING_INBOUND);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "incoming")), <=, E2E_MAX_PENDING_INBOUND);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "peers")), <=, E2E_MAX_UNACCEPTED_PEERS);
	/* the newest is kept, the oldest went */
	{
		char *k = g_strdup_printf("u%d@h%d|#c", E2E_MAX_UNACCEPTED_PEERS + 39, E2E_MAX_UNACCEPTED_PEERS + 39);

		g_assert_nonnull(path_get(a.kr, "pending_inbound", k, NULL));
		g_free(k);
		g_assert_null(path_get(a.kr, "pending_inbound", "u0@h0|#c", NULL));
	}

	/* KEYRSP and REKEY like KEYREQ: once per 10 s per sender and context */
	g_assert_true(e2e_handshake_allow(stamps, E2E_HS_KEYRSP, "x@y", "#c"));
	g_assert_false(e2e_handshake_allow(stamps, E2E_HS_KEYRSP, "x@y", "#c"));
	g_assert_true(e2e_handshake_allow(stamps, E2E_HS_KEYRSP, "x@y", "#d"));
	g_assert_true(e2e_handshake_allow(stamps, E2E_HS_REKEY, "x@y", "#c"));
	g_assert_false(e2e_handshake_allow(stamps, E2E_HS_REKEY, "x@y", "#c"));
	g_assert_true(e2e_handshake_allow(stamps, E2E_HS_KEYREQ, "x@y", "#c"));
	g_assert_false(e2e_handshake_allow(stamps, E2E_HS_KEYREQ, "x@y", "#c"));
	fake_now += E2E_KEYREQ_INBOUND_MIN_INTERVAL;
	g_assert_true(e2e_handshake_allow(stamps, E2E_HS_KEYRSP, "x@y", "#c"));
	g_hash_table_destroy(stamps);
	peer_free(&a);
}

/* timestamps from an imported keyring, rotations, long targets */
static void test_hardening_robustness(void)
{
	PEER a, b;
	E2E_GATE_RESULT g;
	E2E_JSON *row, *out;
	unsigned char before[32], key[32], eph[32], eph_sk[32];
	char *req, *rsp, *error = NULL, *chan, *sk, *xs, *ys;
	gboolean rot, gen;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#t", "normal");
	/* a pending request with an absurd created_at does not block a new
	   one (and does not overflow) */
	req = e2e_build_keyreq(a.kr, &a.id, "#t", BOB, &error);
	g_free(req);
	row = path_get(a.kr, "pending", "#t|" BOB, NULL);
	e2e_json_set_int(row, "created_at", G_MININT64);
	req = e2e_build_keyreq(a.kr, &a.id, "#t", BOB, &error);
	g_assert_nonnull(req);
	g_free(req);
	row = path_get(a.kr, "pending", "#t|" BOB, NULL);
	e2e_json_set_int(row, "created_at", G_MAXINT64);
	req = e2e_build_keyreq(a.kr, &a.id, "#t", BOB, &error);
	g_assert_nonnull(req);
	g_free(req);

	/* a KEYRSP does not consume a scheduled rotation (the REKEYs to the
	   other recipients would be missed): it hands out the current key,
	   the rotation stays due */
	g_assert_true(e2e_outgoing_key(a.kr, "#t", before, &rot, &gen));
	out = path_get(a.kr, "outgoing", "#t", NULL);
	e2e_json_set_int(out, "pending_rotation", 1);
	g_assert_true(e2e_x25519_keypair(eph_sk, eph));
	rsp = e2e_build_keyrsp_for_req(a.kr, &a.id, "#t", BOB, b.id.pk, eph);
	g_assert_nonnull(rsp);
	g_free(rsp);
	out = path_get(a.kr, "outgoing", "#t", NULL);
	g_assert_true(e2e_json_truthy(e2e_json_get(out, "pending_rotation")));
	sk = e2e_b64_encode(before, 32);
	g_assert_cmpstr(e2e_json_get_string(out, "sk"), ==, sk);
	g_free(sk);
	/* the next message rotates */
	e2e_gate_decide(a.kr, TRUE, &a.id, NULL, "#t", "x", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_true(e2e_outgoing_key(a.kr, "#t", key, &rot, &gen));
	g_assert_false(memcmp(key, before, 32) == 0);
	g_assert_true(e2e_gate_lines_fit(&g, "", "#t", 510));
	e2e_gate_result_clear(&g);

	/* a target so long the line would not fit: refused, not cut */
	xs = g_strnfill(200, 'x');
	ys = g_strnfill(180, 'y');
	chan = g_strconcat("#", xs, NULL);
	enable(&a, chan, "normal");
	e2e_gate_decide(a.kr, TRUE, &a.id, NULL, chan, ys, no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_false(e2e_gate_lines_fit(&g, "", chan, 510));
	g_assert_false(e2e_gate_lines_fit(&g, "@label=x ", "#t", 300));
	e2e_gate_result_clear(&g);
	g_free(chan);
	g_free(xs);
	g_free(ys);
	peer_free(&a);
	peer_free(&b);
}

/* one outgoing buffer, several IRC lines */
static void test_hardening_multiline_buffer(void)
{
	static const char nul_buf[] = "PRIVMSG #pub :a\0PRIVMSG #secret :b";
	E2E_JSON *kr = e2e_keyring_new();
	char *msg;

	enable(&(PEER) { .kr = kr }, "#secret", "normal");
#define MULTI(text) e2e_gate_multiline(kr, TRUE, NULL, text, strlen(text), no_resolve, NULL)
	msg = MULTI("PRIVMSG #pub :hi\r\nPRIVMSG #secret :clear text secret");
	g_assert_nonnull(msg);
	g_free(msg);
	msg = MULTI("NICK me\r\nPRIVMSG #secret :clear text secret");
	g_assert_nonnull(msg);
	g_free(msg);
	msg = MULTI("PRIVMSG #pub :hi\rPRIVMSG #secret :CR only");
	g_assert_nonnull(msg);
	g_free(msg);
	msg = MULTI("privmsg #pub :hi\nprivmsg #SECRET :other case");
	g_assert_nonnull(msg);
	g_free(msg);
	msg = e2e_gate_multiline(kr, TRUE, NULL, nul_buf, sizeof(nul_buf) - 1, no_resolve, NULL);
	g_assert_nonnull(msg);
	g_free(msg);
	/* no encrypted conversation in it: it goes as it is */
	g_assert_null(MULTI("PRIVMSG #pub :a\r\nPRIVMSG #pub :b"));
	g_assert_null(MULTI("NICK a\r\nJOIN #b"));
	/* an unreadable keyring: no PRIVMSG at all */
	msg = e2e_gate_multiline(kr, FALSE, NULL, "PRIVMSG #pub :a\r\nPRIVMSG #pub :b",
	                         strlen("PRIVMSG #pub :a\r\nPRIVMSG #pub :b"), no_resolve, NULL);
	g_assert_nonnull(msg);
	g_free(msg);
#undef MULTI
	e2e_json_free(kr);
}

/* ---- more of those rules ---- */

#define CAROL "carol@127.0.0.1"

/* E2E on for a nick whose address is not known at all now */
static void test_hardening_unresolved_nick(void)
{
	E2E_JSON *kr = e2e_keyring_new(), *cfg, *peer;
	E2E_GATE_RESULT g;

	enable(&(PEER) { .kr = kr }, "@carol@c.example.net", "normal");
	cfg = path_get(kr, "channels", "@carol@c.example.net", NULL);
	e2e_json_set_string(cfg, "nick", "carol");
	/* the query was closed and carol left the common channels */
	e2e_gate_decide(kr, TRUE, NULL, NULL, "carol", "unresolved secret", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "carol@c.example.net"));
	e2e_gate_result_clear(&g);
	/* known through a peer record only */
	enable(&(PEER) { .kr = kr }, "@dave@d.example.net", "normal");
	peer = e2e_json_object_member(e2e_json_get(kr, "peers"), "01");
	e2e_json_set_string(peer, "last_nick", "dave");
	e2e_json_set_string(peer, "last_handle", "dave@d.example.net");
	e2e_gate_decide(kr, TRUE, NULL, NULL, "DAVE", "unresolved secret", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	/* anybody else: clear text as before */
	e2e_gate_decide(kr, TRUE, NULL, NULL, "erin", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_json_free(kr);
}

/* replaying an old REKEY after many newer ones */
static void test_hardening_rekey_replay_after_many(void)
{
	PEER a, b;
	unsigned char k1[32], k[32], got[32];
	char *rk1;
	int i;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#x", "auto-accept");
	enable(&b, "#x", "auto-accept");
	exchange(&a, &b, "#x");
	/* an all-zero key is no key: refused, the session stays */
	memset(k, 0, sizeof(k));
	rk1 = e2e_build_rekey(&b.id, "#x", a.id.pk, k);
	g_assert_false(deliver_rekey(&a, &b, rk1));
	g_assert_true(e2e_session_has_key(path_get(a.kr, "incoming", BOB "|#x", NULL)));
	g_free(rk1);
	memset(k1, 1, sizeof(k1));
	rk1 = e2e_build_rekey(&b.id, "#x", a.id.pk, k1);
	g_assert_true(deliver_rekey(&a, &b, rk1));
	/* as many newer ones as are remembered, and more */
	for (i = 0; i < E2E_MAX_SEEN_REKEYS + 5; i++) {
		char *rk;

		memset(k, 0x5a, sizeof(k));
		k[0] = i & 0xff;
		k[1] = i >> 8;
		fake_now++;
		rk = e2e_build_rekey(&b.id, "#x", a.id.pk, k);
		/* rk1 and the zero-key REKEY used two of them */
		g_assert_true(deliver_rekey(&a, &b, rk) == (i < E2E_MAX_SEEN_REKEYS - 2));
		g_free(rk);
	}
	/* rolling the session back to k1 */
	g_assert_false(deliver_rekey(&a, &b, rk1));
	g_assert_true(session_key(&a, BOB, "#x", got));
	g_assert_cmpint(got[0], ==, (E2E_MAX_SEEN_REKEYS - 3) & 0xff);
	g_assert_cmpint(got[1], ==, (E2E_MAX_SEEN_REKEYS - 3) >> 8);
	g_free(rk1);
	peer_free(&a);
	peer_free(&b);
}

/* wires from many handles, half of them answering our KEYREQ */
static void test_hardening_growth_from_wires(void)
{
	PEER a, b;
	E2E_IN_RESULT r;
	unsigned char key[32];
	char *req, *rsp = NULL, *recip = NULL, *error = NULL, *last_fp = NULL;
	int i, n = 2 * E2E_MAX_UNACCEPTED_PEERS + 100;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "#g", "normal");
	enable(&b, "#g", "auto-accept");
	/* bob: alice holds his key and accepted him */
	exchange(&a, &b, "#g");
	e2e_json_remove(e2e_json_get(b.kr, "pending"), "#g|" ALICE);
	req = e2e_build_keyreq(b.kr, &b.id, "#g", ALICE, &error);
	deliver_keyreq(&a, &b, req, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_cmpint(e2e_accept(a.kr, &a.id, BOB, "#g", NULL, ALICE, &rsp, &recip), ==,
	                E2E_ACCEPT_SENT);
	g_free(req);
	g_free(rsp);
	g_free(recip);

	for (i = 0; i < n; i++) {
		PEER m;
		char *handle = g_strdup_printf("u%d@h%d", i, i), *wire;

		peer_init(&m, "m", handle, -1);
		enable(&m, "#g", "auto-accept");
		wire = gate_wire(&m, "#g", "x");
		fake_now++;
		recv_wire(&a, &m, "#g", wire, &r);
		g_assert_nonnull(r.keyreq);
		if (i % 2 == 0) {
			deliver_keyreq(&m, &a, r.keyreq, handle, &rsp, &recip);
			g_assert_true(deliver_keyrsp(&a, &m, rsp));
			g_free(last_fp);
			last_fp = g_strdup(m.id.fp_hex);
			g_free(rsp);
			g_free(recip);
		}
		e2e_in_result_clear(&r);
		g_free(wire);
		peer_free(&m);
		g_free(handle);
	}
	/* our own requests nobody answered, and keys of people never accepted */
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending")), <=, E2E_MAX_PENDING_OUT);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "peers")), <=, E2E_MAX_UNACCEPTED_PEERS + 1);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "incoming")), <=,
	                 E2E_MAX_UNACCEPTED_PEERS + 1 + E2E_MAX_PENDING_INBOUND);
	/* the accepted peer and its session stay */
	g_assert_nonnull(path_get(a.kr, "peers", b.id.fp_hex, NULL));
	g_assert_true(session_key(&a, BOB, "#g", key));
	g_assert_true(e2e_peer_accepted(a.kr, BOB, "#g", b.id.fp_hex));
	/* /e2e list tells the two apart */
	g_assert_true(e2e_session_accepted(a.kr, BOB, "#g", b.id.fp_hex));
	req = g_strdup_printf("u%d@h%d", n - 2, n - 2);
	g_assert_false(e2e_session_accepted(a.kr, req, "#g", last_fp));
	g_free(req);
	g_free(last_fp);
	peer_free(&a);
	peer_free(&b);
}

/* REKEYs go only to recipients that were accepted */
static void test_hardening_rekey_to_accepted_only(void)
{
	PEER a, b, c;
	E2E_GATE_RESULT g;
	unsigned char eph[32], eph_sk[32], key[32];
	gboolean rot, gen;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL, *legacy;
	guint i;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	peer_init(&c, "carol", CAROL, -1);
	enable(&a, "#t", "normal");
	enable(&b, "#t", "auto-accept");
	g_assert_true(e2e_outgoing_key(a.kr, "#t", key, &rot, &gen));
	/* bob asked, alice accepted */
	req = e2e_build_keyreq(b.kr, &b.id, "#t", ALICE, &error);
	deliver_keyreq(&a, &b, req, ALICE, &rsp, &recip);
	g_assert_cmpint(e2e_accept(a.kr, &a.id, BOB, "#t", NULL, ALICE, &rsp, &recip), ==,
	                E2E_ACCEPT_SENT);
	g_free(req);
	g_free(rsp);
	g_free(recip);
	/* carol got the key without acceptance (a keyring from rpe2e.pl) */
	g_assert_true(e2e_x25519_keypair(eph_sk, eph));
	legacy = e2e_build_keyrsp_for_req(a.kr, &a.id, "#t", CAROL, c.id.pk, eph);
	g_free(legacy);
	e2e_json_set_string(path_get(a.kr, "peers", c.id.fp_hex, NULL), "last_nick", "carol");
	g_assert_nonnull(path_get(a.kr, "outgoing_recipients", "#t|" CAROL, NULL));

	e2e_json_set_int(path_get(a.kr, "outgoing", "#t", NULL), "pending_rotation", 1);
	e2e_gate_decide(a.kr, TRUE, &a.id, NULL, "#t", "rotated", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_nonnull(g.notices);
	g_assert_cmpuint(g.notices->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(g.notices, 0), ==, "bob");
	/* carol is told about, and no longer a recipient */
	g_assert_nonnull(g.warnings);
	for (i = 0; i < g.warnings->len && strstr(g_ptr_array_index(g.warnings, i), "carol") == NULL; i++)
		;
	g_assert_cmpuint(i, <, g.warnings->len);
	g_assert_null(path_get(a.kr, "outgoing_recipients", "#t|" CAROL, NULL));
	e2e_gate_result_clear(&g);
	peer_free(&a);
	peer_free(&b);
	peer_free(&c);
}

/* /e2e reset drops queued PRIVMSGs: every line of a queued buffer counts */
static void test_hardening_queued_buffer_privmsg(void)
{
	static const char nul_buf[] = "NICK a\0PRIVMSG #s :b";

#define HAS(text) e2e_buffer_has_privmsg(text, strlen(text))
	g_assert_true(HAS("NICK x\r\nPRIVMSG #secret :queued"));
	g_assert_true(HAS("PING x\rprivmsg #c :d"));
	g_assert_true(HAS("PRIVMSG #a :b\r\n"));
	g_assert_true(e2e_buffer_has_privmsg(nul_buf, sizeof(nul_buf) - 1));
	g_assert_false(HAS("NICK a\r\nJOIN #b\r\n"));
#undef HAS
}

/* /e2e off in a query turns off the address of that query only */
static void test_hardening_query_off_this_address(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	GPtrArray *t;
	PEER p = { .kr = kr };

	enable(&p, "@bob@new.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@bob@new.example.net", NULL), "nick", "bob");
	enable(&p, "@bob@old.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@bob@old.example.net", NULL), "nick", "bob");
	t = e2e_query_off_targets(kr, NULL, "bob", "bob@new.example.net");
	g_assert_cmpuint(t->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(t, 0), ==, "@bob@new.example.net");
	g_ptr_array_unref(t);
	/* address unknown and two configs for the nick: none chosen */
	t = e2e_query_off_targets(kr, NULL, "bob", NULL);
	g_assert_cmpuint(t->len, ==, 0);
	g_ptr_array_unref(t);
	/* address unknown, one config for the nick: that one */
	enable(&p, "@carol@c.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@carol@c.example.net", NULL), "nick", "carol");
	t = e2e_query_off_targets(kr, NULL, "carol", NULL);
	g_assert_cmpuint(t->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(t, 0), ==, "@carol@c.example.net");
	g_ptr_array_unref(t);
	e2e_json_free(kr);
}

/* ---- network scope (keyring entries per network, the wire bare) ---- */

static E2E_NET net_a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
static E2E_NET net_b = { (char *) "E2E2", E2E_CASEMAP_RFC1459, FALSE };
/* the only network erssi uses: old rows may be taken over */
static E2E_NET net_one = { (char *) "E2E", E2E_CASEMAP_RFC1459, TRUE };

#define A_TEST "E2E\x1f#test"
#define B_TEST "E2E2\x1f#test"

static void enable_scoped(PEER *p, const E2E_NET *net, const char *wire)
{
	char *ctx = e2e_ctx_scoped(net, wire);

	g_assert_nonnull(ctx);
	enable(p, ctx, "normal");
	g_free(ctx);
}

static const char *path_str(E2E_JSON *kr, const char *store, const char *key, const char *field)
{
	return e2e_json_get_string(path_get(kr, store, key, NULL), field);
}

static void deliver_keyreq_on(PEER *to, PEER *from, const E2E_NET *net, const char *ctcp,
                              const char *own, char **rsp, char **reciprocal)
{
	char *body = unframe(ctcp);

	e2e_handle_keyreq(to->kr, &to->id, to->stamps, net, from->handle, from->nick, body, own,
	                  rsp, reciprocal);
	g_free(body);
}

/* a KEYREQ for #test from a new identity at handle h, to to on net */
static void keyreq_from(PEER *to, const E2E_NET *net, const char *h)
{
	E2E_JSON *kr = e2e_keyring_new();
	E2E_IDENTITY id;
	gboolean created;
	char *req, *body, *rsp = NULL, *recip = NULL, *error = NULL;

	g_assert_true(e2e_identity_get(kr, &id, TRUE, &created, &error));
	req = e2e_build_keyreq(kr, &id, "#test", to->handle, &error);
	body = unframe(req);
	e2e_handle_keyreq(to->kr, &to->id, to->stamps, net, h, "u", body, to->handle, &rsp, &recip);
	g_assert_null(rsp);
	g_free(body);
	g_free(req);
	e2e_identity_wipe(&id);
	e2e_json_free(kr);
}

static gboolean deliver_rekey_on(PEER *to, PEER *from, const E2E_NET *net, const char *ctcp)
{
	char *body = unframe(ctcp);
	gboolean ok = e2e_handle_rekey(to->kr, &to->id, net, from->handle, from->nick, body);

	g_free(body);
	return ok;
}

static gboolean deliver_keyrsp_on(PEER *to, PEER *from, const E2E_NET *net, const char *ctcp)
{
	char *body = unframe(ctcp);
	gboolean ok = e2e_handle_keyrsp(to->kr, net, from->handle, from->nick, body);

	g_free(body);
	return ok;
}

/* the c= of a handshake */
static char *hs_channel(const char *ctcp, E2E_HS_TYPE type)
{
	char *body = unframe(ctcp), *c;
	E2E_HANDSHAKE *hs = e2e_handshake_parse(body, type);

	g_assert_nonnull(hs);
	g_assert_true(e2e_handshake_verify(hs));
	c = g_strdup(hs->channel);
	e2e_handshake_free(hs);
	g_free(body);
	return c;
}

static void in_params(E2E_IN_PARAMS *in, PEER *to, const char *nick, const char *handle,
                      const char *target, const E2E_NET *net)
{
	memset(in, 0, sizeof(*in));
	in->nick = nick;
	in->handle = handle;
	in->target = target;
	in->own_handle = to->handle;
	in->resolve = no_resolve;
	in->keyreq_stamps = to->stamps;
	in->net = net;
}

static void set_trusted_session(E2E_JSON *kr, const char *key, const char *sk)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_string(row, "fp", LEGACY_BOB_FP);
	e2e_json_set_string(row, "sk", sk);
	e2e_json_set_string(row, "status", "trusted");
	e2e_json_set_int(row, "created_at", fake_now);
	e2e_json_set(e2e_json_get(kr, "incoming"), key, row);
}

/* the plain text of rpe2e.pl's message vec_wire[i] decrypted by to */
static E2E_IN_ACTION vec_decrypt(PEER *to, const E2E_NET *net, int i, char **plain)
{
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	E2E_IN_ACTION action = E2E_IN_PLAIN;
	GString *joined = g_string_new(NULL);
	int j;

	/* the time rpe2e.pl made them (other tests move the clock) */
	fake_now = 1791564129;
	in_params(&in, to, "bob", BOB, vec_wire[i].ctx, net);
	for (j = 0; vec_wire[i].wire[j] != NULL; j++) {
		e2e_decrypt_incoming(to->kr, &to->id, &in, vec_wire[i].wire[j], &r);
		if (r.action != E2E_IN_PLAIN)
			action = r.action;
		else
			g_string_append(joined, r.plain);
		g_assert_null(r.keyreq);
		e2e_in_result_clear(&r);
	}
	*plain = g_string_free(joined, FALSE);
	return action;
}

/* the same channel name on two networks: two contexts, two keys */
static void test_scope_two_networks(void)
{
	PEER a, c;
	E2E_GATE_RESULT ra, rb;
	char *ctx = NULL;

	peer_init(&a, "alice", ALICE, -1);
	enable_scoped(&a, &net_a, "#test");
	enable_scoped(&a, &net_b, "#test");
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "a", no_resolve, NULL, &ra);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#test", "b", no_resolve, NULL, &rb);
	g_assert_cmpint(ra.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpint(rb.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(ra.ctx, ==, A_TEST);
	g_assert_cmpstr(rb.ctx, ==, B_TEST);
	g_assert_nonnull(path_str(a.kr, "outgoing", A_TEST, "sk"));
	g_assert_nonnull(path_str(a.kr, "outgoing", B_TEST, "sk"));
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "sk"), !=,
	                path_str(a.kr, "outgoing", B_TEST, "sk"));
	g_assert_null(path_get(a.kr, "outgoing", "#test", NULL));
	g_assert_null(path_get(a.kr, "channels", "#test", NULL));
	e2e_gate_result_clear(&ra);
	e2e_gate_result_clear(&rb);

	/* E2E on only on A: B's #test passes in clear, A's is encrypted */
	peer_init(&c, "carol", CAROL, -1);
	enable_scoped(&c, &net_a, "#test");
	e2e_gate_decide(c.kr, TRUE, &c.id, &net_b, "#test", "b", no_resolve, NULL, &rb);
	g_assert_cmpint(rb.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&rb);
	g_assert_null(path_get(c.kr, "outgoing", B_TEST, NULL));
	e2e_gate_decide(c.kr, TRUE, &c.id, &net_a, "#test", "a", no_resolve, NULL, &ra);
	g_assert_cmpint(ra.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&ra);
	/* nothing of A's is B's */
	g_assert_cmpint(e2e_ctx_lookup(c.kr, &net_b, "#test", &ctx), ==, E2E_CTX_NEW);
	g_assert_cmpstr(ctx, ==, B_TEST);
	g_free(ctx);
	peer_free(&a);
	peer_free(&c);
}

/* every wire-visible field is the bare context: a scoped native client and
   an unscoped one (as rpe2e.pl is) exchange keys, messages and a REKEY */
static void test_scope_wire_bare(void)
{
	PEER a, b;
	E2E_GATE_RESULT g;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	unsigned char key[32], newkey[32];
	char *req, *rsp = NULL, *recip = NULL, *rsp2 = NULL, *recip2 = NULL, *error = NULL;
	char *c, *rk, *pk_b64;
	unsigned char *raw;
	gsize len;
	guint i;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable_scoped(&a, &net_a, "#test");
	enable(&b, "#test", "auto-accept");

	req = e2e_build_keyreq(a.kr, &a.id, A_TEST, BOB, &error);
	g_assert_nonnull(req);
	g_assert_null(strchr(req, '\x1f'));
	c = hs_channel(req, E2E_HS_KEYREQ);
	g_assert_cmpstr(c, ==, "#test");
	g_free(c);
	g_assert_cmpstr(path_str(a.kr, "pending", A_TEST "|" BOB, "channel"), ==, A_TEST);

	/* bob (unscoped) answers; the KEYRSP is wrapped for "#test" */
	deliver_keyreq_on(&b, &a, NULL, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_assert_true(deliver_keyrsp_on(&a, &b, &net_a, rsp));
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|" A_TEST, "status"), ==, "trusted");
	g_assert_null(path_get(a.kr, "incoming", BOB "|#test", NULL));

	/* bob's reciprocal KEYREQ names #test; alice's KEYRSP to it names #test
	   too and opens with the bare wrap info */
	deliver_keyreq_on(&a, &b, &net_a, recip, ALICE, &rsp2, &recip2);
	g_assert_null(rsp2);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	g_assert_cmpint(e2e_accept(a.kr, &a.id, BOB, A_TEST, NULL, ALICE, &rsp2, &recip2), ==,
	                E2E_ACCEPT_SENT);
	g_assert_nonnull(rsp2);
	g_assert_null(strchr(rsp2, '\x1f'));
	c = hs_channel(rsp2, E2E_HS_KEYRSP);
	g_assert_cmpstr(c, ==, "#test");
	g_free(c);
	g_assert_true(deliver_keyrsp_on(&b, &a, NULL, rsp2));
	g_assert_cmpstr(path_str(a.kr, "outgoing_recipients", A_TEST "|" BOB, "channel"), ==, A_TEST);

	/* each wire of a message decrypts with the bare context as AAD */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "scoped hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	raw = e2e_b64_decode(path_str(a.kr, "outgoing", A_TEST, "sk"), &len);
	g_assert_cmpuint(len, ==, 32);
	memcpy(key, raw, 32);
	g_free(raw);
	in_params(&in, &b, "alice", ALICE, "#test", NULL);
	for (i = 0; i < g.wires->len; i++) {
		E2E_WIRE *w = e2e_wire_parse(g_ptr_array_index(g.wires, i));
		char *pt;

		g_assert_null(strchr(g_ptr_array_index(g.wires, i), '\x1f'));
		g_assert_nonnull(w);
		pt = e2e_wire_decrypt(w, key, "#test", &len);
		g_assert_nonnull(pt);
		g_assert_cmpmem(pt, len, "scoped hello", strlen("scoped hello"));
		g_free(pt);
		e2e_wire_free(w);
		e2e_decrypt_incoming(b.kr, &b.id, &in, g_ptr_array_index(g.wires, i), &r);
		g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
		g_assert_cmpstr(r.plain, ==, "scoped hello");
		e2e_in_result_clear(&r);
	}
	e2e_gate_result_clear(&g);

	/* a REKEY for the scoped context names #test and bob takes it */
	memset(newkey, 0x5a, 32);
	rk = e2e_build_rekey(&a.id, A_TEST, b.id.pk, newkey);
	g_assert_nonnull(rk);
	g_assert_null(strchr(rk, '\x1f'));
	c = hs_channel(rk, E2E_HS_REKEY);
	g_assert_cmpstr(c, ==, "#test");
	g_free(c);
	g_assert_true(deliver_rekey(&b, &a, rk));
	pk_b64 = e2e_b64_encode(newkey, 32);
	g_assert_cmpstr(path_str(b.kr, "incoming", ALICE "|#test", "sk"), ==, pk_b64);
	g_free(pk_b64);

	g_free(rk);
	g_free(req);
	g_free(rsp);
	g_free(recip);
	g_free(rsp2);
	g_free(recip2);
	peer_free(&a);
	peer_free(&b);
}

/* rpe2e.pl's message, read under a session stored for network A */
static void test_scope_rpe2e_vector(void)
{
	PEER a;
	char *plain;

	peer_init(&a, "alice", ALICE, 0x41);
	set_trusted_session(a.kr, BOB "|" A_TEST, LEGACY_BOB_SK);
	g_assert_cmpint(vec_decrypt(&a, &net_a, 0, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, vec_wire[0].plain);
	g_free(plain);
	/* the long one, in chunks */
	g_assert_cmpint(vec_decrypt(&a, &net_a, 1, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, vec_wire[1].plain);
	g_free(plain);
	/* on network B there is no session for #test */
	g_assert_cmpint(vec_decrypt(&a, &net_b, 0, &plain), ==, E2E_IN_DROP);
	g_free(plain);
	peer_free(&a);
}

/* an incoming KEYREQ waits under the scoped context */
static void test_scope_inbound_keyreq(void)
{
	PEER a;
	char *rsp = NULL, *recip = NULL;

	peer_init(&a, "alice", ALICE, 0x41);
	enable_scoped(&a, &net_a, "#test");
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_a, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(recip);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	g_assert_cmpstr(path_str(a.kr, "pending_inbound", BOB "|" A_TEST, "channel"), ==, A_TEST);
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|#test", NULL));
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|" A_TEST, "status"), ==, "pending");
	g_assert_null(path_get(a.kr, "incoming", BOB "|#test", NULL));
	/* E2E is off for #test on network B: ignored there */
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_b, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|" B_TEST, NULL));
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, 1);
	peer_free(&a);
}

/* the DM sender rule compares the bare c= with "@" + the sender */
static void test_scope_dm_sender_rule(void)
{
	PEER a, b;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable_scoped(&a, &net_a, "@" BOB);
	enable_scoped(&a, &net_a, "@eve@x");
	req = e2e_build_keyreq(b.kr, &b.id, "@" BOB, ALICE, &error);
	g_assert_nonnull(req);
	deliver_keyreq_on(&a, &b, &net_a, req, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|E2E\x1f@" BOB, NULL));
	g_assert_cmpstr(path_str(a.kr, "pending_inbound", BOB "|E2E\x1f@" BOB, "channel"), ==,
	                "E2E\x1f@" BOB);
	g_free(req);
	/* bob asking for the DM key of someone else */
	req = e2e_build_keyreq(b.kr, &b.id, "@eve@x", ALICE, &error);
	g_assert_nonnull(req);
	deliver_keyreq_on(&a, &b, &net_a, req, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|E2E\x1f@eve@x", NULL));
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|@eve@x", NULL));
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, 1);
	g_free(req);
	/* auto-accept: our key goes back, and our own request for the
	   direction to us is for "@<own>" on this network */
	e2e_json_set_string(path_get(a.kr, "channels", "E2E\x1f@" BOB, NULL), "mode", "auto-accept");
	e2e_json_remove(e2e_json_get(b.kr, "pending"), "@" BOB "|" ALICE);
	req = e2e_build_keyreq(b.kr, &b.id, "@" BOB, ALICE, &error);
	deliver_keyreq_on(&a, &b, &net_a, req, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_free(error);
	error = hs_channel(recip, E2E_HS_KEYREQ);
	g_assert_cmpstr(error, ==, "@" ALICE);
	g_assert_nonnull(path_get(a.kr, "pending", "E2E\x1f@" ALICE "|" BOB, NULL));
	g_assert_null(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	g_free(error);
	g_free(rsp);
	g_free(recip);
	g_free(req);
	peer_free(&a);
	peer_free(&b);
}

/* a DM REKEY needs E2E on for its sender on this network */
static void test_scope_rekey_dm_config(void)
{
	PEER a, b;
	unsigned char key[32];
	char *rk;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	/* alice holds bob's DM key (the direction to her) on network A */
	enable_scoped(&b, &net_a, "@" ALICE);
	enable_scoped(&a, &net_a, "@" BOB);
	e2e_json_set_string(path_get(a.kr, "channels", "E2E\x1f@" BOB, NULL), "mode", "auto-accept");
	e2e_json_set_string(path_get(b.kr, "channels", "E2E\x1f@" ALICE, NULL), "mode", "auto-accept");
	{
		char *req, *rsp = NULL, *recip = NULL, *error = NULL;

		req = e2e_build_keyreq(a.kr, &a.id, "E2E\x1f@" ALICE, BOB, &error);
		g_assert_nonnull(req);
		deliver_keyreq_on(&b, &a, &net_a, req, BOB, &rsp, &recip);
		g_assert_nonnull(rsp);
		g_assert_true(deliver_keyrsp_on(&a, &b, &net_a, rsp));
		g_free(req);
		g_free(rsp);
		g_free(recip);
	}
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|E2E\x1f@" ALICE, "status"), ==, "trusted");
	memset(key, 0x6b, 32);
	rk = e2e_build_rekey(&b.id, "E2E\x1f@" ALICE, a.id.pk, key);
	/* E2E with bob is on only in an old, unscoped config: on several
	   networks that is not this one's - refused */
	e2e_json_remove(e2e_json_get(a.kr, "channels"), "E2E\x1f@" BOB);
	enable(&a, "@" BOB, "normal");
	g_assert_false(deliver_rekey_on(&a, &b, &net_a, rk));
	/* on the only network the old config counts where it is: taken,
	   and the old config is not taken over by a peer's REKEY */
	g_assert_true(deliver_rekey_on(&a, &b, &net_one, rk));
	g_assert_null(path_get(a.kr, "channels", "E2E\x1f@" BOB, NULL));
	g_assert_true(e2e_ctx_enabled(a.kr, "@" BOB));
	g_free(rk);
	peer_free(&a);
	peer_free(&b);
}


static void legacy_bob_real(PEER *a);

/* a handle may hold '|' and 0x1F; the network of a
   waiting request is its row's, not whatever follows the first '|' */
static void test_scope_prune_confused_handle(void)
{
	PEER a;
	char *rsp = NULL, *recip = NULL, *k;
	guint on_a = 0, on_b = 0, i;
	GPtrArray *keys;

	peer_init(&a, "alice", ALICE, 0x41);
	enable_scoped(&a, &net_a, "#test");
	enable_scoped(&a, &net_b, "#test");
	/* 64 real requests on B */
	for (i = 0; i < E2E_MAX_PENDING_INBOUND; i++) {
		char *h = g_strdup_printf("b%u@198.51.100.1", i);

		fake_now++;
		keyreq_from(&a, &net_b, h);
		g_free(h);
	}
	/* 20 on A from handles that read like B's when split at the first '|' */
	for (i = 0; i < 20; i++) {
		char *h = g_strdup_printf("e%u|E2E2\x1fz@203.0.113.9", i);

		fake_now++;
		keyreq_from(&a, &net_a, h);
		g_free(h);
	}
	/* one more real request on B: only B's oldest goes */
	fake_now++;
	keyreq_from(&a, &net_b, "newest@198.51.100.1");
	keys = e2e_json_keys(e2e_json_get(a.kr, "pending_inbound"));
	for (i = 0; i < keys->len; i++) {
		const char *c = path_str(a.kr, "pending_inbound", g_ptr_array_index(keys, i), "channel");

		on_a += g_strcmp0(c, A_TEST) == 0;
		on_b += g_strcmp0(c, B_TEST) == 0;
	}
	g_ptr_array_unref(keys);
	g_assert_cmpuint(on_a, ==, 20);
	g_assert_cmpuint(on_b, ==, E2E_MAX_PENDING_INBOUND);
	g_assert_null(path_get(a.kr, "pending_inbound", "b0@198.51.100.1|" B_TEST, NULL));
	g_assert_nonnull(path_get(a.kr, "pending_inbound", "b1@198.51.100.1|" B_TEST, NULL));
	g_assert_nonnull(path_get(a.kr, "pending_inbound", "newest@198.51.100.1|" B_TEST, NULL));
	k = g_strdup_printf("e0|E2E2\x1fz@203.0.113.9|" A_TEST);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", k, NULL));
	g_free(k);
	/* and A's cap holds for them: 64 at most */
	for (i = 20; i < E2E_MAX_PENDING_INBOUND + 10; i++) {
		char *h = g_strdup_printf("e%u|E2E2\x1fz@203.0.113.9", i);

		fake_now++;
		keyreq_from(&a, &net_a, h);
		g_free(h);
	}
	on_a = on_b = 0;
	keys = e2e_json_keys(e2e_json_get(a.kr, "pending_inbound"));
	for (i = 0; i < keys->len; i++) {
		const char *c = path_str(a.kr, "pending_inbound", g_ptr_array_index(keys, i), "channel");

		on_a += g_strcmp0(c, A_TEST) == 0;
		on_b += g_strcmp0(c, B_TEST) == 0;
	}
	g_ptr_array_unref(keys);
	g_assert_cmpuint(on_a, ==, E2E_MAX_PENDING_INBOUND);
	g_assert_cmpuint(on_b, ==, E2E_MAX_PENDING_INBOUND);
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
}

/* old rows without a "channel" member: the row's handle tells where the
   context starts */
static void test_scope_prune_handle_member(void)
{
	PEER a;
	int i;

	peer_init(&a, "alice", ALICE, 0x41);
	enable_scoped(&a, &net_a, "#test");
	for (i = 0; i < E2E_MAX_PENDING_INBOUND; i++) {
		E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
		char *h = g_strdup_printf("h%d|E2E\x1fq", i), *k = g_strconcat(h, "|" B_TEST, NULL);

		e2e_json_set_string(row, "handle", h);
		e2e_json_set_int(row, "received_at", 1);
		e2e_json_set(e2e_json_get(a.kr, "pending_inbound"), k, row);
		g_free(h);
		g_free(k);
	}
	keyreq_from(&a, &net_a, "real@198.51.100.1");
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, E2E_MAX_PENDING_INBOUND + 1);
	/* and rows with a "channel" member only: that names the network */
	for (i = 0; i < E2E_MAX_PENDING_INBOUND; i++) {
		E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
		char *k = g_strdup_printf("c%d|E2E\x1fq|" B_TEST, i);

		e2e_json_set_string(row, "channel", B_TEST);
		e2e_json_set_int(row, "received_at", 1);
		e2e_json_set(e2e_json_get(a.kr, "pending_inbound"), k, row);
		g_free(k);
	}
	keyreq_from(&a, &net_a, "real2@198.51.100.1");
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==,
	                 2 * E2E_MAX_PENDING_INBOUND + 2);
	peer_free(&a);
}

/* DM requests on one network with old "@<own>" rows: our reciprocal
   request waits where they are, also after /e2e accept */
static void test_legacy_dm_reciprocal_in_place(void)
{
	PEER a, b;
	E2E_JSON *row;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	/* old: E2E with bob (auto-accept), carol's DM session to us */
	enable(&a, "@" BOB, "auto-accept");
	set_trusted_session(a.kr, CAROL "|@" ALICE, LEGACY_BOB_SK);
	req = e2e_build_keyreq(b.kr, &b.id, "@" BOB, ALICE, &error);
	deliver_keyreq_on(&a, &b, &net_one, req, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_assert_nonnull(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	g_assert_null(path_get(a.kr, "pending", "E2E\x1f@" ALICE "|" BOB, NULL));
	g_assert_nonnull(path_get(a.kr, "incoming", CAROL "|@" ALICE, NULL));
	g_free(req);
	g_free(rsp);
	g_free(recip);
	/* /e2e accept in a query whose config is this network's, "@<own>"
	   still old: the reciprocal goes where "@<own>" is */
	e2e_json_remove(e2e_json_get(a.kr, "channels"), "@" BOB);
	enable(&a, "E2E\x1f@" BOB, "normal");
	e2e_json_remove(e2e_json_get(a.kr, "pending"), "@" ALICE "|" BOB);
	row = e2e_json_new(E2E_JSON_OBJECT);
	{
		char *pk = e2e_b64_encode(b.id.pk, 32);
		unsigned char eph_sk[32], eph[32];

		g_assert_true(e2e_x25519_keypair(eph_sk, eph));
		e2e_json_set_string(row, "pubkey", pk);
		g_free(pk);
		pk = e2e_b64_encode(eph, 32);
		e2e_json_set_string(row, "eph_x25519", pk);
		g_free(pk);
	}
	e2e_json_set_string(row, "handle", BOB);
	e2e_json_set_string(row, "channel", "E2E\x1f@" BOB);
	e2e_json_set(e2e_json_get(a.kr, "pending_inbound"), BOB "|E2E\x1f@" BOB, row);
	g_assert_cmpint(e2e_accept(a.kr, &a.id, BOB, "E2E\x1f@" BOB, "@" ALICE, ALICE, &rsp, &recip), ==,
	                E2E_ACCEPT_SENT);
	g_assert_nonnull(recip);
	g_assert_nonnull(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	g_assert_null(path_get(a.kr, "pending", "E2E\x1f@" ALICE "|" BOB, NULL));
	g_free(rsp);
	g_free(recip);
	/* bob asks again, the query's config this network's (auto-accept):
	   the reciprocal goes where "@<own>" is, too */
	e2e_json_remove(e2e_json_get(a.kr, "pending"), "@" ALICE "|" BOB);
	e2e_json_set_string(path_get(a.kr, "channels", "E2E\x1f@" BOB, NULL), "mode", "auto-accept");
	g_hash_table_remove_all(a.stamps);
	e2e_json_remove(e2e_json_get(b.kr, "pending"), "@" BOB "|" ALICE);
	req = e2e_build_keyreq(b.kr, &b.id, "@" BOB, ALICE, &error);
	deliver_keyreq_on(&a, &b, &net_one, req, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_assert_nonnull(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	g_assert_null(path_get(a.kr, "pending", "E2E\x1f@" ALICE "|" BOB, NULL));
	g_free(req);
	g_free(rsp);
	g_free(recip);
	/* several networks: the old "@<own>" cannot be used, ours goes under
	   this network's name - never under the blocked old one */
	e2e_json_remove(e2e_json_get(a.kr, "pending"), "@" ALICE "|" BOB);
	g_hash_table_remove_all(a.stamps);
	e2e_json_remove(e2e_json_get(b.kr, "pending"), "@" BOB "|" ALICE);
	req = e2e_build_keyreq(b.kr, &b.id, "@" BOB, ALICE, &error);
	deliver_keyreq_on(&a, &b, &net_a, req, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(recip);
	g_assert_nonnull(path_get(a.kr, "pending", "E2E\x1f@" ALICE "|" BOB, NULL));
	g_assert_null(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	g_free(req);
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
	peer_free(&b);
}


/* the global cap takes from the network holding the
   most requests, not the oldest of all - a flood on other networks does
   not push a network's real requests out */
static void test_scope_prune_global_fair(void)
{
	PEER a;
	int n, i;

	peer_init(&a, "alice", ALICE, 0x41);
	enable_scoped(&a, &net_b, "#test");
	for (i = 0; i < 10; i++) {
		char *h = g_strdup_printf("real%d@198.51.100.1", i);

		fake_now++;
		keyreq_from(&a, &net_b, h);
		g_free(h);
	}
	for (n = 0; n < 4; n++) {
		E2E_NET net = { NULL, E2E_CASEMAP_RFC1459, FALSE };

		net.label = g_strdup_printf("X%d", n);
		enable_scoped(&a, &net, "#test");
		for (i = 0; i < E2E_MAX_PENDING_INBOUND; i++) {
			char *h = g_strdup_printf("x%d.%d@203.0.113.9", n, i);

			fake_now++;
			keyreq_from(&a, &net, h);
			g_free(h);
		}
		g_free(net.label);
	}
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==,
	                 E2E_MAX_PENDING_INBOUND_ALL);
	for (i = 0; i < 10; i++) {
		char *k = g_strdup_printf("real%d@198.51.100.1|" B_TEST, i);

		g_assert_nonnull(path_get(a.kr, "pending_inbound", k, NULL));
		g_free(k);
	}
	/* the newest of a flooding network stay, its oldest went */
	g_assert_nonnull(path_get(a.kr, "pending_inbound", "x3.63@203.0.113.9|X3\x1f#test", NULL));
	g_assert_null(path_get(a.kr, "pending_inbound", "x0.0@203.0.113.9|X0\x1f#test", NULL));
	peer_free(&a);
}

/* an export and import keep every member of the config and key rows:
   the network that took a name over, the nick of a DM config */
static void test_legacy_import_keeps_members(void)
{
	E2E_JSON *kr = e2e_keyring_new(), *doc, *back, *row;
	PEER p = { .kr = kr };
	char *error = NULL, *ctx = NULL;

	legacy_fixture_fill(kr);
	enable(&p, "@bob@x.example.org", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@bob@x.example.org", NULL), "nick", "bob");
	e2e_json_set_string(path_get(kr, "channels", "@bob@x.example.org", NULL), "future", "kept");
	g_free(e2e_ctx_resolve(kr, &net_one, "#test", NULL));
	g_free(e2e_ctx_resolve(kr, &net_one, "@bob@x.example.org", NULL));
	doc = e2e_keyring_export(kr);
	back = e2e_keyring_import(doc, &error);
	g_assert_nonnull(back);
	g_assert_cmpstr(path_str(back, "channels", A_TEST, "adopted"), ==, "E2E");
	g_assert_cmpstr(path_str(back, "outgoing", A_TEST, "adopted"), ==, "E2E");
	g_assert_cmpstr(path_str(back, "outgoing", A_TEST, "sk"), ==, LEGACY_OUT_SK);
	row = path_get(back, "channels", "E2E\x1f@bob@x.example.org", NULL);
	g_assert_cmpstr(e2e_json_get_string(row, "nick"), ==, "bob");
	g_assert_cmpstr(e2e_json_get_string(row, "future"), ==, "kept");
	g_assert_cmpstr(e2e_json_get_string(row, "mode"), ==, "normal");
	g_assert_null(e2e_json_get(row, "channel"));
	g_assert_true(e2e_ctx_enabled(back, A_TEST));
	/* B stays refused after the round trip */
	g_assert_cmpint(e2e_ctx_lookup(back, &net_b, "#test", &ctx), ==, E2E_CTX_BLOCKED);
	g_free(ctx);
	{
		E2E_GATE_RESULT g;
		char *rt;

		e2e_gate_decide(back, TRUE, NULL, &net_b, "#test", "hello", no_resolve, NULL, &g);
		g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
		e2e_gate_result_clear(&g);
		/* and an import is what the export was */
		rt = e2e_keyring_snapshot(e2e_json_get(back, "channels"));
		g_assert_nonnull(rt);
		g_free(rt);
	}
	/* a repartee export (camelCase, no members of ours) still imports */
	e2e_json_free(back);
	row = g_ptr_array_index(e2e_json_get(doc, "outgoingSessions")->array, 0);
	e2e_json_remove(row, "created_at");
	e2e_json_set_int(row, "createdAt", 7);
	back = e2e_keyring_import(doc, &error);
	g_assert_nonnull(back);
	g_assert_cmpint(e2e_json_get_int(path_get(back, "outgoing",
	                                          e2e_json_get_string(row, "channel"), NULL),
	                                 "created_at", 0), ==, 7);
	g_assert_null(path_get(back, "outgoing", e2e_json_get_string(row, "channel"), "createdAt", NULL));
	e2e_json_free(back);
	e2e_json_free(doc);
	e2e_json_free(kr);
}

/* a DM of from to to (the gate of from, unscoped), and from's key for it */
static char *dm_wire(PEER *from, PEER *to, const char *text, char **sk)
{
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	E2E_GATE_RESULT g;
	char *wire, *ctx = g_strconcat("@", to->handle, NULL);

	g_hash_table_insert(nicks, (char *) to->nick, (char *) to->handle);
	enable(from, ctx, "normal");
	e2e_gate_decide(from->kr, TRUE, &from->id, NULL, to->nick, text, table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	wire = g_strdup(g_ptr_array_index(g.wires, 0));
	*sk = g_strdup(path_str(from->kr, "outgoing", ctx, "sk"));
	e2e_gate_result_clear(&g);
	g_hash_table_destroy(nicks);
	g_free(ctx);
	return wire;
}

static E2E_IN_ACTION dm_decrypt(PEER *to, PEER *from, const E2E_NET *net, const char *wire,
                                char **plain)
{
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	E2E_IN_ACTION action;

	in_params(&in, to, from->nick, from->handle, from->nick, net);
	e2e_decrypt_incoming(to->kr, &to->id, &in, wire, &r);
	action = r.action;
	*plain = g_strdup(r.plain);
	e2e_in_result_clear(&r);
	return action;
}

/* the user's own DM line to bob takes bob's old DM sessions to us
   ("@<own>") over with "@<bob>", not those of others */
static void test_legacy_dm_sessions_follow(void)
{
	PEER a, b, c;
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	E2E_GATE_RESULT g;
	E2E_JSON *seen;
	char *wb, *wc, *skb, *skc, *plain, *k;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	peer_init(&c, "carol", CAROL, -1);
	fake_now = 1791564129;
	wb = dm_wire(&b, &a, "dm from bob", &skb);
	wc = dm_wire(&c, &a, "dm from carol", &skc);
	/* alice's old keyring: E2E with bob and carol, their keys to her */
	enable(&a, "@" BOB, "normal");
	enable(&a, "@" CAROL, "normal");
	set_trusted_session(a.kr, BOB "|@" ALICE, skb);
	set_trusted_session(a.kr, CAROL "|@" ALICE, skc);
	e2e_json_set_string(path_get(a.kr, "incoming", BOB "|@" ALICE, NULL), "fp", "bbbb");
	seen = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_int(seen, "bm9uY2Vub25jZW5vbmNlbg==", 1);
	e2e_json_set(e2e_json_get(a.kr, "seen_rekeys"), "bbbb|@" ALICE, seen);
	/* our old request to bob under an older own address, and bob's
	   channel session (not a DM: stays) */
	seen = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(seen, "handle", BOB);
	e2e_json_set_string(seen, "channel", "@alice@old.example.org");
	e2e_json_set_int(seen, "created_at", fake_now);
	e2e_json_set(e2e_json_get(a.kr, "pending"), "@alice@old.example.org|" BOB, seen);
	set_trusted_session(a.kr, BOB "|#chan", skb);
	set_trusted_session(a.kr, CAROL "|@alice@old.example.org", skc);
	/* a row of this network for bob there already wins */
	set_trusted_session(a.kr, BOB "|@alice@older.example.org", skc);
	set_trusted_session(a.kr, BOB "|E2E\x1f@alice@older.example.org", skb);
	/* another handle ending in "|bob@..." is not bob */
	seen = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(seen, "handle", "eve|" BOB);
	e2e_json_set_int(seen, "created_at", fake_now);
	e2e_json_set(e2e_json_get(a.kr, "pending"), "@" ALICE "|eve|" BOB, seen);

	/* alice writes to bob (one network): "@bob" and bob's sessions to
	   her become E2E's */
	g_hash_table_insert(nicks, (char *) "bob", (char *) BOB);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "bob", "hi bob", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_true(g.save_needed);
	e2e_gate_result_clear(&g);
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|E2E\x1f@" ALICE, "sk"), ==, skb);
	g_assert_null(path_get(a.kr, "incoming", BOB "|@" ALICE, NULL));
	g_assert_nonnull(path_get(a.kr, "seen_rekeys", "bbbb|E2E\x1f@" ALICE, NULL));
	g_assert_cmpstr(path_str(a.kr, "pending", "E2E\x1f@alice@old.example.org|" BOB, "channel"), ==,
	                "E2E\x1f@alice@old.example.org");
	g_assert_null(path_get(a.kr, "pending", "@alice@old.example.org|" BOB, NULL));
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|#chan", NULL));
	g_assert_null(path_get(a.kr, "incoming", BOB "|E2E\x1f#chan", NULL));
	g_assert_nonnull(path_get(a.kr, "incoming", CAROL "|@alice@old.example.org", NULL));
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|E2E\x1f@alice@older.example.org", "sk"), ==, skb);
	g_assert_null(path_get(a.kr, "incoming", BOB "|@alice@older.example.org", NULL));
	g_assert_nonnull(path_get(a.kr, "pending", "@" ALICE "|eve|" BOB, NULL));
	/* carol's stay as they were */
	g_assert_cmpstr(path_str(a.kr, "incoming", CAROL "|@" ALICE, "sk"), ==, skc);
	g_assert_null(path_get(a.kr, "incoming", CAROL "|E2E\x1f@" ALICE, NULL));
	g_assert_true(e2e_ctx_enabled(a.kr, "@" CAROL));

	/* one network: both read */
	g_assert_cmpint(dm_decrypt(&a, &b, &net_one, wb, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, "dm from bob");
	g_free(plain);
	g_assert_cmpint(dm_decrypt(&a, &c, &net_one, wc, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, "dm from carol");
	g_free(plain);
	/* a second network: bob's DMs on E2E read, carol's old ones not */
	g_assert_cmpint(dm_decrypt(&a, &b, &net_a, wb, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, "dm from bob");
	g_free(plain);
	g_assert_cmpint(dm_decrypt(&a, &c, &net_a, wc, &plain), ==, E2E_IN_DROP);
	g_free(plain);
	g_assert_cmpint(dm_decrypt(&a, &b, &net_b, wb, &plain), ==, E2E_IN_DROP);
	g_free(plain);
	k = g_strconcat(CAROL "|@" ALICE, NULL);
	g_assert_nonnull(path_get(a.kr, "incoming", k, NULL));
	g_free(k);
	g_free(wb);
	g_free(wc);
	g_free(skb);
	g_free(skc);
	g_hash_table_destroy(nicks);
	peer_free(&a);
	peer_free(&b);
	peer_free(&c);
}

/* the mixed state on the only network: an old session or request of a
   peer under "@<own>" still counts while the network has rows of others
   there; and an old REKEY nonce stays used */
static void test_legacy_mixed_own_rows(void)
{
	PEER a, b;
	unsigned char key[32];
	char *rk, *pk, *ctx = NULL;
	E2E_JSON *seen;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	/* bob's old session to alice, and a row of this network for someone
	   else under "@<own>" */
	enable(&a, "@" BOB, "normal");
	enable(&b, "@" ALICE, "auto-accept");
	exchange(&a, &b, "@" ALICE);
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|@" ALICE, NULL));
	set_trusted_session(a.kr, CAROL "|E2E\x1f@" ALICE, LEGACY_BOB_SK);
	g_assert_cmpint(e2e_ctx_lookup(a.kr, &net_one, "@" ALICE, &ctx), ==, E2E_CTX_LEGACY);
	g_free(ctx);
	/* bob's REKEY replaces his old session */
	memset(key, 0x3c, 32);
	rk = e2e_build_rekey(&b.id, "@" ALICE, a.id.pk, key);
	g_assert_true(deliver_rekey_on(&a, &b, &net_one, rk));
	pk = e2e_b64_encode(key, 32);
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|E2E\x1f@" ALICE, "sk"), ==, pk);
	g_free(pk);
	/* replayed: refused, the nonce is used (under either name) */
	g_assert_false(deliver_rekey_on(&a, &b, &net_one, rk));
	g_free(rk);
	/* a REKEY whose nonce was used under the old name is refused too */
	memset(key, 0x3d, 32);
	rk = e2e_build_rekey(&b.id, "@" ALICE, a.id.pk, key);
	{
		char *body = unframe(rk);
		E2E_HANDSHAKE *hs = e2e_handshake_parse(body, E2E_HS_REKEY);
		char *n = e2e_b64_encode(hs->nonce, 16), *fp = e2e_fingerprint_hex(b.id.pk);
		char *sk = g_strconcat(fp, "|@" ALICE, NULL);

		e2e_json_remove(e2e_json_get(a.kr, "incoming"), BOB "|E2E\x1f@" ALICE);
		set_trusted_session(a.kr, BOB "|@" ALICE, LEGACY_BOB_SK);
		e2e_json_set_string(path_get(a.kr, "incoming", BOB "|@" ALICE, NULL), "fp", fp);
		seen = e2e_json_object_member(e2e_json_object_member(a.kr, "seen_rekeys"), sk);
		e2e_json_set_int(seen, n, 1);
		g_free(sk);
		g_free(fp);
		g_free(n);
		e2e_handshake_free(hs);
		g_free(body);
	}
	g_assert_false(deliver_rekey_on(&a, &b, &net_one, rk));
	g_free(rk);
	peer_free(&a);
	peer_free(&b);
}

/* an alt reading in the mixed state reads an old session too */
static void test_legacy_mixed_alt_reading(void)
{
	PEER a, b;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	char *alts[] = { (char *) "+#x", NULL };
	char *wire;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	fake_now = 1791564129;
	enable(&b, "+#x", "normal");
	wire = gate_wire(&b, "+#x", "plus x");
	set_trusted_session(a.kr, BOB "|+#x", path_str(b.kr, "outgoing", "+#x", "sk"));
	set_trusted_session(a.kr, CAROL "|E2E\x1f+#x", LEGACY_BOB_SK);
	in_params(&in, &a, "bob", BOB, "#x", &net_one);
	in.alt_ctxs = alts;
	e2e_decrypt_incoming(a.kr, &a.id, &in, wire, &r);
	g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
	g_assert_cmpstr(r.plain, ==, "plus x");
	e2e_in_result_clear(&r);
	g_free(wire);
	peer_free(&a);
	peer_free(&b);
}

/* our old request for bob's DM key is answered after "@<own>" got rows
   of this network for someone else */
static void test_legacy_mixed_keyrsp(void)
{
	PEER a, b;
	char *req, *rsp = NULL, *recip = NULL, *error = NULL;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "@" BOB, "normal");
	enable(&b, "@" ALICE, "auto-accept");
	req = e2e_build_keyreq(a.kr, &a.id, "@" ALICE, BOB, &error);
	g_assert_nonnull(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	set_trusted_session(a.kr, CAROL "|E2E\x1f@" ALICE, LEGACY_BOB_SK);
	set_trusted_session(a.kr, CAROL "|@" ALICE, LEGACY_BOB_SK);
	deliver_keyreq_on(&b, &a, NULL, req, BOB, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_true(deliver_keyrsp_on(&a, &b, &net_one, rsp));
	g_assert_null(path_get(a.kr, "pending", "@" ALICE "|" BOB, NULL));
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|E2E\x1f@" ALICE, "status"), ==, "trusted");
	g_free(req);
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
	peer_free(&b);
}

/* the pending notice looks the request up where the handler put it */
static void test_legacy_pending_notice_key(void)
{
	PEER a;
	char *rsp = NULL, *recip = NULL, *k;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	/* this network's config next to the old rows (mixed, one network) */
	enable(&a, A_TEST, "normal");
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	k = e2e_pending_inbound_key(a.kr, &net_one, BOB, "#test");
	g_assert_cmpstr(k, ==, BOB "|" A_TEST);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", k, NULL));
	g_free(k);
	/* old rows only: the bare name */
	e2e_json_remove(e2e_json_get(a.kr, "channels"), A_TEST);
	e2e_json_remove(e2e_json_get(a.kr, "pending_inbound"), BOB "|" A_TEST);
	e2e_json_remove(e2e_json_get(a.kr, "incoming"), BOB "|" A_TEST);
	k = e2e_pending_inbound_key(a.kr, &net_one, BOB, "#test");
	g_assert_cmpstr(k, ==, BOB "|#test");
	g_free(k);
	/* blocked: none */
	g_assert_null(e2e_pending_inbound_key(a.kr, &net_a, BOB, "#test"));
	peer_free(&a);
}

/* every network together stays bounded too */
static void test_scope_prune_global_cap(void)
{
	PEER a;
	int n;

	peer_init(&a, "alice", ALICE, 0x41);
	for (n = 0; n < 6; n++) {
		E2E_NET net = { NULL, E2E_CASEMAP_RFC1459, FALSE };
		int i;

		net.label = g_strdup_printf("N%d", n);
		enable_scoped(&a, &net, "#test");
		for (i = 0; i < E2E_MAX_PENDING_INBOUND; i++) {
			char *h = g_strdup_printf("u%d.%d@198.51.100.1", n, i);

			fake_now++;
			keyreq_from(&a, &net, h);
			g_free(h);
		}
		g_free(net.label);
	}
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==,
	                 E2E_MAX_PENDING_INBOUND_ALL);
	/* the newest stay */
	g_assert_nonnull(path_get(a.kr, "pending_inbound", "u5.63@198.51.100.1|N5\x1f#test", NULL));
	g_assert_null(path_get(a.kr, "pending_inbound", "u0.0@198.51.100.1|N0\x1f#test", NULL));
	peer_free(&a);
}

/* someone else's traffic never binds old rows to a
   network; once the user's own line has, another network is refused */
static void test_legacy_stranger_does_not_adopt(void)
{
	PEER a;
	E2E_GATE_RESULT g;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	char *rsp = NULL, *recip = NULL, *plain, *before, *after;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	legacy_bob_real(&a);
	/* erssi on one network (A): a stranger's signed KEYREQ and a wire for
	   #test - handled with the old rows in place, nothing taken over */
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|#test", NULL));
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	g_assert_cmpint(vec_decrypt(&a, &net_one, 0, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, vec_wire[0].plain);
	g_free(plain);
	g_assert_true(e2e_handle_keyrsp(a.kr, &net_one, BOB, "bob", vec_keyrsp));
	g_assert_true(e2e_handle_rekey(a.kr, &a.id, &net_one, BOB, "bob", vec_rekey));
	g_assert_nonnull(path_get(a.kr, "channels", "#test", NULL));
	g_assert_null(path_get(a.kr, "channels", A_TEST, NULL));
	g_assert_null(path_get(a.kr, "outgoing", A_TEST, NULL));
	g_assert_null(path_get(a.kr, "incoming", BOB "|" A_TEST, NULL));
	/* a second network comes: #test there is refused, never clear text */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "/e2e adopt"));
	e2e_gate_result_clear(&g);

	/* back on one network, the user's own line binds the rows to A */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, A_TEST);
	e2e_gate_result_clear(&g);
	g_assert_cmpstr(path_str(a.kr, "channels", A_TEST, "adopted"), ==, "E2E");
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "adopted"), ==, "E2E");
	/* network B: the name was encrypted on another network - refused,
	   nothing read there */
	before = e2e_keyring_snapshot(a.kr);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "/e2e adopt"));
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#TEST", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#pub,#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	fake_now = 1791564129;
	in_params(&in, &a, "bob", BOB, "#test", &net_b);
	e2e_decrypt_incoming(a.kr, &a.id, &in, vec_wire[0].wire[0], &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	g_assert_null(r.keyreq);
	e2e_in_result_clear(&r);
	after = e2e_keyring_snapshot(a.kr);
	g_assert_cmpstr(before, ==, after);
	g_free(before);
	g_free(after);
	/* A keeps encrypting, the rows stay A's */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	/* /e2e off on B (its own config, E2E off) settles it there */
	enable(&a, B_TEST, "normal");
	e2e_json_set_int(path_get(a.kr, "channels", B_TEST, NULL), "enabled", 0);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	/* and it rotates and stays marked */
	e2e_json_set_int(path_get(a.kr, "outgoing", A_TEST, NULL), "pending_rotation", 1);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "rotated", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "sk"), !=, LEGACY_OUT_SK);
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "adopted"), ==, "E2E");
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
}

/* a DM config taken over on A refuses the nick's DMs on B the same way */
static void test_legacy_adopted_dm_elsewhere(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_GATE_RESULT g;
	gboolean changed = FALSE;
	char *ctx;

	enable(&p, "@bob@old.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@bob@old.example.net", NULL), "nick", "bob");
	ctx = e2e_ctx_resolve(kr, &net_one, "@bob@old.example.net", &changed);
	g_assert_cmpstr(ctx, ==, "E2E\x1f@bob@old.example.net");
	g_assert_true(changed);
	g_free(ctx);
	/* the same address on B */
	e2e_gate_decide(kr, TRUE, NULL, &net_b, "bob", "dm", fixed_resolve, (char *) "bob@old.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	/* another address of the nick on B: still the old config's nick */
	e2e_gate_decide(kr, TRUE, NULL, &net_b, "bob", "dm", fixed_resolve, (char *) "bob@new.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	/* the case of a channel taken over on A, written otherwise on B */
	enable(&p, "#Secret", "normal");
	ctx = e2e_ctx_resolve(kr, &net_one, "#Secret", &changed);
	g_free(ctx);
	e2e_gate_decide(kr, TRUE, NULL, &net_b, "#secret", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	/* a channel set up fresh on A (nothing old) is A's only */
	enable(&p, "E2E\x1f#fresh", "normal");
	e2e_gate_decide(kr, TRUE, NULL, &net_b, "#fresh", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_json_free(kr);
}

/* the decryption paths of a BLOCKED context: an alt reading and our own
   copy read nothing with the old rows */
static void test_legacy_blocked_decrypt_paths(void)
{
	PEER a;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	char *alts[] = { (char *) "#test", NULL };
	unsigned char out[32];
	const char *err = NULL;
	GPtrArray *own;
	gsize len;
	unsigned char *raw;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	fake_now = 1791564129;
	/* "+#test": #fresh is no context, #test an old one */
	in_params(&in, &a, "bob", BOB, "#fresh", &net_a);
	in.alt_ctxs = alts;
	e2e_decrypt_incoming(a.kr, &a.id, &in, vec_wire[0].wire[0], &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	e2e_in_result_clear(&r);
	/* one network: read in place */
	in.net = &net_one;
	e2e_decrypt_incoming(a.kr, &a.id, &in, vec_wire[0].wire[0], &r);
	g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
	g_assert_false(r.save_needed);
	e2e_in_result_clear(&r);

	/* our own line relayed back, under the old outgoing key of #test */
	raw = e2e_b64_decode(LEGACY_OUT_SK, &len);
	memcpy(out, raw, 32);
	g_free(raw);
	own = e2e_encrypt_plain(out, "#test", "own old copy", fake_now, &err);
	g_assert_nonnull(own);
	in_params(&in, &a, "alice", ALICE, "#test", &net_a);
	in.is_own_line = TRUE;
	e2e_decrypt_incoming(a.kr, &a.id, &in, g_ptr_array_index(own, 0), &r);
	g_assert_cmpint(r.action, ==, E2E_IN_DROP);
	e2e_in_result_clear(&r);
	in.net = &net_one;
	e2e_decrypt_incoming(a.kr, &a.id, &in, g_ptr_array_index(own, 0), &r);
	g_assert_cmpint(r.action, ==, E2E_IN_PLAIN);
	g_assert_cmpstr(r.plain, ==, "own old copy");
	g_assert_false(r.save_needed);
	e2e_in_result_clear(&r);
	g_assert_nonnull(path_get(a.kr, "outgoing", "#test", NULL));
	g_ptr_array_unref(own);
	peer_free(&a);
}

/* the requests waiting for /e2e accept are bounded per network: a flood
   on one does not push another's out */
static void test_scope_prune_per_network(void)
{
	PEER a;
	char *rsp = NULL, *recip = NULL;
	int i;

	peer_init(&a, "alice", ALICE, 0x41);
	enable_scoped(&a, &net_a, "#test");
	for (i = 0; i < E2E_MAX_PENDING_INBOUND; i++) {
		E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
		char *k = g_strdup_printf("u%d@h|" B_TEST, i);

		e2e_json_set_int(row, "received_at", 1);
		e2e_json_set(e2e_json_get(a.kr, "pending_inbound"), k, row);
		g_free(k);
	}
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_a, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	g_assert_nonnull(path_get(a.kr, "pending_inbound", "u0@h|" B_TEST, NULL));
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, E2E_MAX_PENDING_INBOUND + 1);
	/* without a network all count together, as before */
	enable(&a, "#test", "normal");
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, NULL, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, E2E_MAX_PENDING_INBOUND);
	peer_free(&a);
}

/* no bare row of wire and no "channel" field naming it is left */
static void assert_no_bare(E2E_JSON *kr, const char *wire)
{
	static const char *const stores[] = {
		"channels", "outgoing", "incoming", "accepted", "pending", "outgoing_recipients",
		"pending_inbound", "seen_rekeys", NULL
	};
	char *head = g_strconcat(wire, "|", NULL), *tail = g_strconcat("|", wire, NULL);
	E2E_JSON *list = e2e_json_get(kr, "pending_trust_change");
	guint i, j;

	for (i = 0; stores[i] != NULL; i++) {
		E2E_JSON *store = e2e_json_get(kr, stores[i]);
		GPtrArray *keys = e2e_json_keys(store);

		for (j = 0; j < keys->len; j++) {
			const char *k = g_ptr_array_index(keys, j);

			g_assert_cmpstr(k, !=, wire);
			g_assert_false(g_str_has_suffix(k, tail) && strchr(k, '\x1f') == NULL);
			if (strcmp(stores[i], "pending") == 0 || strcmp(stores[i], "outgoing_recipients") == 0)
				g_assert_false(g_str_has_prefix(k, head) && strchr(k + strlen(head), '|') == NULL);
			g_assert_cmpstr(e2e_json_get_string(e2e_json_get(store, k), "channel"), !=, wire);
		}
		g_ptr_array_unref(keys);
	}
	for (i = 0; i < list->array->len; i++)
		g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, i), "channel"), !=, wire);
	g_free(head);
	g_free(tail);
}

/* one network: an old keyring is taken over the first time it is used */
static void test_legacy_single_network(void)
{
	PEER a;
	E2E_GATE_RESULT g;
	E2E_JSON *list;
	char *plain, *before, *after, *msg;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	/* deciding only (several targets, a raw line) changes nothing */
	before = e2e_keyring_snapshot(a.kr);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#pub,#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	msg = e2e_gate_multiline(a.kr, TRUE, &net_one, "PRIVMSG #pub :a\r\nPRIVMSG #test :b",
	                         strlen("PRIVMSG #pub :a\r\nPRIVMSG #test :b"), no_resolve, NULL);
	g_assert_nonnull(msg);
	g_free(msg);
	/* a line that is not encrypted takes nothing over either */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "\001VERSION\001", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	after = e2e_keyring_snapshot(a.kr);
	g_assert_cmpstr(before, ==, after);
	g_free(before);
	g_free(after);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, A_TEST);
	g_assert_true(g.save_needed);
	e2e_gate_result_clear(&g);
	assert_no_bare(a.kr, "#test");
	/* every row is there, scoped, the keys unchanged */
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "sk"), ==, LEGACY_OUT_SK);
	g_assert_true(e2e_ctx_enabled(a.kr, A_TEST));
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|" A_TEST, "sk"), ==, LEGACY_BOB_SK);
	g_assert_cmpstr(path_str(a.kr, "accepted", BOB "|" A_TEST, "fp"), ==, LEGACY_BOB_FP);
	g_assert_cmpstr(path_str(a.kr, "pending", A_TEST "|" CAROL, "channel"), ==, A_TEST);
	g_assert_cmpstr(path_str(a.kr, "outgoing_recipients", A_TEST "|" BOB, "channel"), ==, A_TEST);
	g_assert_cmpstr(path_str(a.kr, "pending_inbound", "dave@127.0.0.1|" A_TEST, "channel"), ==, A_TEST);
	g_assert_nonnull(path_get(a.kr, "seen_rekeys", LEGACY_BOB_FP "|" A_TEST, NULL));
	list = e2e_json_get(a.kr, "pending_trust_change");
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, 0), "channel"), ==, A_TEST);
	/* the other channels stay as they were */
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, 1), "channel"), ==, "#other");
	g_assert_true(e2e_ctx_enabled(a.kr, "#other"));
	g_assert_true(e2e_ctx_enabled(a.kr, "#test2"));
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|#other", NULL));
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|#test2", NULL));
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|#te", NULL));
	g_assert_nonnull(path_get(a.kr, "pending", "#te|st|" CAROL, NULL));
	g_assert_nonnull(path_get(a.kr, "outgoing_recipients", "#other|" BOB, NULL));
	g_assert_nonnull(path_get(a.kr, "seen_rekeys", LEGACY_BOB_FP "|#other", NULL));
	g_assert_null(path_get(a.kr, "channels", "E2E\x1f#other", NULL));
	/* rpe2e.pl's messages on #test read as before */
	g_assert_cmpint(vec_decrypt(&a, &net_one, 0, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, vec_wire[0].plain);
	g_free(plain);
	/* an incoming message for #other (old rows) is read with them where
	   they are: nothing is taken over by someone else's traffic */
	set_trusted_session(a.kr, BOB "|#other", LEGACY_BOB_SK);
	{
		E2E_IN_PARAMS in;
		E2E_IN_RESULT r;

		fake_now = 1791564129;
		in_params(&in, &a, "bob", BOB, "#other", &net_one);
		e2e_decrypt_incoming(a.kr, &a.id, &in, vec_wire[0].wire[0], &r);
		/* the AAD is "#test": not this channel's message */
		g_assert_cmpint(r.action, ==, E2E_IN_DROP);
		g_assert_false(r.save_needed);
		e2e_in_result_clear(&r);
	}
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|#other", NULL));
	g_assert_null(path_get(a.kr, "incoming", BOB "|E2E\x1f#other", NULL));
	peer_free(&a);
}

/* rpe2e.pl's bob (seed 0x42) as the fixture's bob: his real fingerprint
   on the old #test session, a peer record, and our old request to him
   that vec_keyrsp answers */
static void legacy_bob_real(PEER *a)
{
	unsigned char pk[32], eph[32];
	E2E_JSON *peer, *pend;
	char *fp, *s;

	unhex(vec_bob_pk_hex, pk, 32);
	fp = e2e_fingerprint_hex(pk);
	e2e_json_set_string(path_get(a->kr, "incoming", BOB "|#test", NULL), "fp", fp);
	peer = e2e_json_object_member(e2e_json_get(a->kr, "peers"), fp);
	s = e2e_b64_encode(pk, 32);
	e2e_json_set_string(peer, "pk", s);
	g_free(s);
	e2e_json_set_string(peer, "last_handle", BOB);
	e2e_json_set_string(peer, "last_nick", "bob");
	e2e_json_set_string(peer, "status", "trusted");
	unhex(vec_alice_eph_sk_hex, eph, 32);
	pend = e2e_json_new(E2E_JSON_OBJECT);
	s = e2e_b64_encode(eph, 32);
	e2e_json_set_string(pend, "eph_sk", s);
	g_free(s);
	e2e_json_set_string(pend, "handle", BOB);
	e2e_json_set_string(pend, "channel", "#test");
	e2e_json_set_int(pend, "created_at", fake_now);
	e2e_json_set(e2e_json_get(a->kr, "pending"), "#test|" BOB, pend);
	g_free(fp);
}

/* several networks: an old keyring's encrypted channel is refused, never
   sent in clear, and nothing of it is used or changed */
static void test_legacy_multi_network(void)
{
	PEER a, b;
	E2E_GATE_RESULT g;
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	char *before, *after, *plain, *rsp = NULL, *recip = NULL, *msg;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	/* old rows that rpe2e.pl's KEYRSP and REKEY (from bob) would match:
	   our request to bob, bob as a known peer with his real key */
	legacy_bob_real(&a);
	before = e2e_keyring_snapshot(a.kr);

	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "/e2e adopt"));
	g_assert_nonnull(strstr(g.message, "#test"));
	g_assert_nonnull(strstr(g.message, "binds it to E2E"));
	g_assert_null(strchr(g.message, '\x1f'));
	g_assert_null(g.wires);
	e2e_gate_result_clear(&g);
	/* the other readings and paths into the gate: refused as well */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "@#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#pub,#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#TEST", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	msg = e2e_gate_multiline(a.kr, TRUE, &net_a, "PRIVMSG #pub :a\r\nPRIVMSG #test :b",
	                         strlen("PRIVMSG #pub :a\r\nPRIVMSG #test :b"), no_resolve, NULL);
	g_assert_nonnull(msg);
	g_free(msg);
	/* a bot command goes out in clear text as on any encrypted channel,
	   with the warning */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "!op", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_BYPASS);
	g_assert_nonnull(g.message);
	e2e_gate_result_clear(&g);
	/* a channel without old rows is not affected */
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#fresh", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);

	/* bob's message on #test is not read with the old session */
	g_assert_cmpint(vec_decrypt(&a, &net_a, 0, &plain), ==, E2E_IN_DROP);
	g_free(plain);
	/* his KEYREQ is ignored */
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_a, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(recip);
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|#test", NULL));
	/* nor are a KEYRSP and a REKEY taken */
	g_assert_false(e2e_handle_keyrsp(a.kr, &net_a, BOB, "bob", vec_keyrsp));
	g_assert_false(e2e_handle_rekey(a.kr, &a.id, &net_a, BOB, "bob", vec_rekey));
	after = e2e_keyring_snapshot(a.kr);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	/* (the same keyring unscoped reads bob, takes his KEYRSP and REKEY:
	   the old rows are real) */
	g_assert_cmpint(vec_decrypt(&a, NULL, 0, &plain), ==, E2E_IN_PLAIN);
	g_free(plain);
	g_assert_true(e2e_handle_keyrsp(a.kr, NULL, BOB, "bob", vec_keyrsp));
	g_assert_true(e2e_handle_rekey(a.kr, &a.id, NULL, BOB, "bob", vec_rekey));

	/* an old DM config is refused the same way */
	peer_init(&b, "bob", BOB, -1);
	enable(&a, "@" BOB, "normal");
	g_hash_table_insert(nicks, (char *) "bob", (char *) BOB);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "bob", "dm secret", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "/e2e adopt"));
	e2e_gate_result_clear(&g);

	/* /e2e off on network A (a scoped config with E2E off): clear text
	   there, network B is still refused */
	e2e_json_set(e2e_json_get(a.kr, "channels"), A_TEST, e2e_json_new(E2E_JSON_OBJECT));
	e2e_json_set_int(path_get(a.kr, "channels", A_TEST, NULL), "enabled", 0);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	g_free(before);
	g_hash_table_destroy(nicks);
	peer_free(&a);
	peer_free(&b);
}

/* rows of this network exist: they are used, old ones are left alone */
static void test_legacy_scoped_wins(void)
{
	PEER a;
	E2E_GATE_RESULT g;
	E2E_JSON *out;
	char *ctx = NULL;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	enable(&a, A_TEST, "normal");
	out = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(out, "sk", "d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3c=");
	e2e_json_set_int(out, "created_at", fake_now);
	e2e_json_set_int(out, "pending_rotation", 0);
	e2e_json_set(e2e_json_get(a.kr, "outgoing"), A_TEST, out);
	/* several networks: the scoped rows are used, the old ones stay */
	g_assert_cmpint(e2e_ctx_lookup(a.kr, &net_a, "#test", &ctx), ==, E2E_CTX_SCOPED);
	g_assert_cmpstr(ctx, ==, A_TEST);
	g_free(ctx);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, A_TEST);
	e2e_gate_result_clear(&g);
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "sk"), ==, "d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3c=");
	g_assert_cmpstr(path_str(a.kr, "outgoing", "#test", "sk"), ==, LEGACY_OUT_SK);
	g_assert_nonnull(path_get(a.kr, "channels", "#test", NULL));
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|#test", NULL));
	/* the only network: the old rows are taken over next to the scoped
	   ones, which win (our key stays 0x77, bob's old session moves in) */
	g_assert_cmpint(e2e_ctx_lookup(a.kr, &net_one, "#test", &ctx), ==, E2E_CTX_LEGACY);
	g_assert_cmpstr(ctx, ==, "#test");
	g_free(ctx);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, A_TEST);
	g_assert_true(g.save_needed);
	e2e_gate_result_clear(&g);
	g_assert_cmpstr(path_str(a.kr, "outgoing", A_TEST, "sk"), ==, "d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3c=");
	g_assert_cmpstr(path_str(a.kr, "incoming", BOB "|" A_TEST, "sk"), ==, LEGACY_BOB_SK);
	assert_no_bare(a.kr, "#test");
	peer_free(&a);

	/* E2E turned off here wins over an old config that is on: clear text
	   on both kinds of network (nothing old is used for it) */
	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	enable(&a, A_TEST, "normal");
	e2e_json_set_int(path_get(a.kr, "channels", A_TEST, NULL), "enabled", 0);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_a, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	/* nor does a request get our key through the old config (even in
	   auto-accept mode) */
	e2e_json_set_string(path_get(a.kr, "channels", "#test", NULL), "mode", "auto-accept");
	{
		char *rsp = NULL, *recip = NULL;

		e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
		g_assert_null(rsp);
		g_assert_null(recip);
		g_assert_null(path_get(a.kr, "pending_inbound", BOB "|#test", NULL));
		g_assert_null(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	}
	/* and E2E on here wins over an old config that is off */
	e2e_json_set_int(path_get(a.kr, "channels", A_TEST, NULL), "enabled", 1);
	e2e_json_set_int(path_get(a.kr, "channels", "#test", NULL), "enabled", 0);
	e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, A_TEST);
	e2e_gate_result_clear(&g);
	g_assert_true(e2e_ctx_enabled(a.kr, A_TEST));
	g_assert_null(path_get(a.kr, "channels", "#test", NULL));
	peer_free(&a);
}

/* the loops over stores look at this network's rows (and old ones) only */
static void test_scope_loops_on_net(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_GATE_RESULT g;
	GPtrArray *t;
	char *other;

	/* the same channel in another case, on another network */
	enable(&p, "E2E2\x1f#Secret", "normal");
	e2e_gate_decide(kr, TRUE, NULL, &net_a, "#secret", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	/* on this network: refused, the name shown bare */
	enable(&p, "E2E\x1f#Secret", "normal");
	e2e_gate_decide(kr, TRUE, NULL, &net_a, "#secret", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(g.message, "write the name as #Secret"));
	g_assert_null(strchr(g.message, '\x1f'));
	e2e_gate_result_clear(&g);

	/* the DM configs of a nick: this network's only */
	enable(&p, "E2E2\x1f@bob@old.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "E2E2\x1f@bob@old.example.net", NULL), "nick", "bob");
	t = e2e_dm_configs_of(kr, &net_a, "bob");
	g_assert_cmpuint(t->len, ==, 0);
	g_ptr_array_unref(t);
	t = e2e_query_off_targets(kr, &net_a, "bob", NULL);
	g_assert_cmpuint(t->len, ==, 0);
	g_ptr_array_unref(t);
	g_assert_null(e2e_enabled_dm_elsewhere(kr, &net_a, "bob", "bob@new.example.net"));
	e2e_gate_decide(kr, TRUE, NULL, &net_a, "bob", "hello", fixed_resolve, (char *) "bob@new.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	t = e2e_dm_configs_of(kr, &net_b, "bob");
	g_assert_cmpuint(t->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(t, 0), ==, "bob@old.example.net");
	g_ptr_array_unref(t);
	/* on network B the same DM is refused */
	e2e_gate_decide(kr, TRUE, NULL, &net_b, "bob", "hello", fixed_resolve, (char *) "bob@new.example.net", &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_null(strchr(g.message, '\x1f'));
	e2e_gate_result_clear(&g);
	other = e2e_enabled_dm_elsewhere(kr, &net_b, "bob", "bob@new.example.net");
	g_assert_cmpstr(other, ==, "bob@old.example.net");
	g_free(other);
	t = e2e_query_off_targets(kr, &net_b, "bob", NULL);
	g_assert_cmpuint(t->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(t, 0), ==, "@bob@old.example.net");
	g_ptr_array_unref(t);
	/* an old unscoped config counts on every network */
	enable(&p, "@carol@c.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@carol@c.example.net", NULL), "nick", "carol");
	t = e2e_dm_configs_of(kr, &net_a, "carol");
	g_assert_cmpuint(t->len, ==, 1);
	g_ptr_array_unref(t);
	e2e_json_free(kr);
}

/* a name holding 0x1F cannot be scoped: never encrypted, and an old row
   under such a name is not read as another network's */
static void test_scope_separator_in_name(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_GATE_RESULT g;
	char *ctx = NULL;

	e2e_gate_decide(kr, TRUE, NULL, &net_a, "#a\x1f" "b", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	/* rpe2e.pl had E2E on for it: refused, not encrypted, not clear text */
	enable(&p, "#a\x1f" "b", "normal");
	g_assert_cmpint(e2e_ctx_lookup(kr, &net_a, "#a\x1f" "b", &ctx), ==, E2E_CTX_BLOCKED);
	g_assert_cmpstr(ctx, ==, "#a\x1f" "b");
	g_free(ctx);
	e2e_gate_decide(kr, TRUE, NULL, &net_one, "#a\x1f" "b", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	g_assert_null(path_get(kr, "outgoing", "#a\x1f" "b", NULL));
	e2e_gate_result_clear(&g);
	e2e_gate_decide(kr, TRUE, NULL, &net_a, "#A\x1f" "B", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_json_free(kr);
}


/* ---- regression scenarios ---- */

static E2E_NET net_two_only = { (char *) "E2E2", E2E_CASEMAP_RFC1459, TRUE };

/* requests waiting on each network label of pending_inbound */
static guint pending_on(PEER *a, const char *label)
{
	GPtrArray *keys = e2e_json_keys(e2e_json_get(a->kr, "pending_inbound"));
	guint i, n = 0;

	for (i = 0; i < keys->len; i++) {
		const char *c = path_str(a->kr, "pending_inbound", g_ptr_array_index(keys, i), "channel");
		char *l = c != NULL ? e2e_ctx_network(c) : NULL;

		n += g_strcmp0(l, label) == 0;
		g_free(l);
	}
	g_ptr_array_unref(keys);
	return n;
}

static void flood(PEER *a, const char *label, int count, const char *fmt)
{
	E2E_NET net = { (char *) label, E2E_CASEMAP_RFC1459, FALSE };
	int i;

	enable_scoped(a, &net, "#test");
	for (i = 0; i < count; i++) {
		char *h = g_strdup_printf(fmt, label, i);

		fake_now++;
		keyreq_from(a, &net, h);
		g_free(h);
	}
}

/* floods on every other network, also with handles that read like
   B's, never push B's real requests out */
static void test_regress_flood_around_b(void)
{
	PEER a;
	int i;

	peer_init(&a, "alice", ALICE, 0x41);
	flood(&a, "E2E2", 10, "real.%s.%d@198.51.100.1");
	flood(&a, "E2E", E2E_MAX_PENDING_INBOUND, "e%s%d|E2E2\x1fz@203.0.113.9");
	flood(&a, "C", E2E_MAX_PENDING_INBOUND + 20, "c.%s.%d@203.0.113.9");
	flood(&a, "D", E2E_MAX_PENDING_INBOUND, "d.%s.%d@203.0.113.9");
	flood(&a, "F", E2E_MAX_PENDING_INBOUND, "f.%s.%d@203.0.113.9");
	g_assert_cmpuint(pending_on(&a, "E2E2"), ==, 10);
	for (i = 0; i < 10; i++) {
		char *k = g_strdup_printf("real.E2E2.%d@198.51.100.1|" B_TEST, i);

		g_assert_nonnull(path_get(a.kr, "pending_inbound", k, NULL));
		g_free(k);
	}
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==,
	                 E2E_MAX_PENDING_INBOUND_ALL);
	g_assert_cmpuint(pending_on(&a, "C"), <=, E2E_MAX_PENDING_INBOUND);
	peer_free(&a);
}

/* the global cap shares out evenly among the fullest networks, and a
   smaller one is not touched while others hold more */
static void test_regress_fair_share(void)
{
	static const char *const big[] = { "X", "Y", "W", "V" };
	PEER a;
	guint lo = G_MAXUINT, hi = 0;
	int i;

	peer_init(&a, "alice", ALICE, 0x41);
	flood(&a, "X", E2E_MAX_PENDING_INBOUND, "x.%s.%d@203.0.113.9");
	flood(&a, "Y", E2E_MAX_PENDING_INBOUND, "y.%s.%d@203.0.113.9");
	flood(&a, "Z", 40, "z.%s.%d@198.51.100.1");
	flood(&a, "E2E2", 10, "b.%s.%d@198.51.100.1");
	flood(&a, "W", E2E_MAX_PENDING_INBOUND + 36, "w.%s.%d@203.0.113.9");
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==, 242);
	flood(&a, "V", E2E_MAX_PENDING_INBOUND, "v.%s.%d@203.0.113.9");
	g_assert_cmpuint(e2e_json_size(e2e_json_get(a.kr, "pending_inbound")), ==,
	                 E2E_MAX_PENDING_INBOUND_ALL);
	g_assert_cmpuint(pending_on(&a, "E2E2"), ==, 10);
	g_assert_cmpuint(pending_on(&a, "Z"), ==, 40);
	for (i = 0; i < 4; i++) {
		guint n = pending_on(&a, big[i]);

		lo = MIN(lo, n);
		hi = MAX(hi, n);
	}
	g_assert_cmpuint(hi - lo, <=, 1);
	g_assert_cmpuint(lo, >=, 40);
	peer_free(&a);
}

/* export and import give back the config and key rows member for
   member, and B stays refused */
static void test_regress_export_import(void)
{
	E2E_JSON *kr = e2e_keyring_new(), *doc, *back;
	PEER p = { .kr = kr };
	char *error = NULL, *s1, *s2;
	E2E_GATE_RESULT g;

	legacy_fixture_fill(kr);
	enable(&p, "@bob@x.example.org", "normal");
	e2e_json_set_string(path_get(kr, "channels", "@bob@x.example.org", NULL), "nick", "bob");
	e2e_json_set_string(path_get(kr, "channels", "#test", NULL), "x-other-client", "1");
	e2e_json_set_string(path_get(kr, "outgoing", "#test", NULL), "x-other-client", "2");
	g_free(e2e_ctx_resolve(kr, &net_one, "#test", NULL));
	g_free(e2e_ctx_resolve(kr, &net_one, "@bob@x.example.org", NULL));
	doc = e2e_keyring_export(kr);
	back = e2e_keyring_import(doc, &error);
	g_assert_nonnull(back);
	s1 = e2e_keyring_snapshot(e2e_json_get(kr, "channels"));
	s2 = e2e_keyring_snapshot(e2e_json_get(back, "channels"));
	g_assert_cmpstr(s1, ==, s2);
	g_free(s1);
	g_free(s2);
	s1 = e2e_keyring_snapshot(e2e_json_get(kr, "outgoing"));
	s2 = e2e_keyring_snapshot(e2e_json_get(back, "outgoing"));
	g_assert_cmpstr(s1, ==, s2);
	g_free(s1);
	g_free(s2);
	e2e_gate_decide(back, TRUE, NULL, &net_b, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(back, TRUE, NULL, &net_two_only, "#test", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_json_free(back);
	e2e_json_free(doc);
	e2e_json_free(kr);
}

/* a keyring of old DM rows: alice with bob and carol, their keys to her */
static void old_dm_keyring(PEER *a, PEER *b, PEER *c, char **wb, char **wc)
{
	char *skb, *skc;

	fake_now = 1791564129;
	*wb = dm_wire(b, a, "dm from bob", &skb);
	*wc = dm_wire(c, a, "dm from carol", &skc);
	enable(a, "@" BOB, "normal");
	enable(a, "@" CAROL, "normal");
	set_trusted_session(a->kr, BOB "|@" ALICE, skb);
	set_trusted_session(a->kr, CAROL "|@" ALICE, skc);
	{
		char *fp = e2e_fingerprint_hex(b->id.pk);
		E2E_JSON *peer;

		e2e_json_set_string(path_get(a->kr, "incoming", BOB "|@" ALICE, NULL), "fp", fp);
		peer = e2e_json_object_member(e2e_json_get(a->kr, "peers"), fp);
		e2e_json_set_string(peer, "last_handle", BOB);
		e2e_json_set_string(peer, "last_nick", "bob");
		e2e_json_set_string(peer, "status", "trusted");
		g_free(fp);
		fp = e2e_fingerprint_hex(c->id.pk);
		e2e_json_set_string(path_get(a->kr, "incoming", CAROL "|@" ALICE, NULL), "fp", fp);
		peer = e2e_json_object_member(e2e_json_get(a->kr, "peers"), fp);
		e2e_json_set_string(peer, "last_handle", CAROL);
		e2e_json_set_string(peer, "last_nick", "carol");
		e2e_json_set_string(peer, "status", "trusted");
		g_free(fp);
	}
	g_free(skb);
	g_free(skc);
}

static E2E_GATE_ACTION dm_gate(PEER *a, const E2E_NET *net, const char *nick, const char *handle)
{
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	E2E_GATE_RESULT g;
	E2E_GATE_ACTION action;

	g_hash_table_insert(nicks, (char *) nick, (char *) handle);
	e2e_gate_decide(a->kr, TRUE, &a->id, net, nick, "a dm", table_resolve, nicks, &g);
	action = g.action;
	e2e_gate_result_clear(&g);
	g_hash_table_destroy(nicks);
	return action;
}

/* after the user's DM line to bob, a second network: bob on E2E is
   read and written to, everything old is refused, nothing in clear */
static void test_regress_second_network(void)
{
	PEER a, b, c;
	char *wb, *wc, *plain;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	peer_init(&c, "carol", CAROL, -1);
	old_dm_keyring(&a, &b, &c, &wb, &wc);
	g_assert_cmpint(dm_gate(&a, &net_one, "bob", BOB), ==, E2E_GATE_CIPHER);
	/* the second network comes */
	g_assert_cmpint(dm_decrypt(&a, &b, &net_a, wb, &plain), ==, E2E_IN_PLAIN);
	g_free(plain);
	g_assert_cmpint(dm_gate(&a, &net_a, "bob", BOB), ==, E2E_GATE_CIPHER);
	g_assert_cmpint(dm_gate(&a, &net_b, "bob", BOB), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(dm_gate(&a, &net_b, "bob", "bob@new.example.org"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(dm_decrypt(&a, &b, &net_b, wb, &plain), ==, E2E_IN_DROP);
	g_free(plain);
	g_assert_cmpint(dm_gate(&a, &net_a, "carol", CAROL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(dm_gate(&a, &net_b, "carol", CAROL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(dm_decrypt(&a, &c, &net_a, wc, &plain), ==, E2E_IN_DROP);
	g_free(plain);
	/* someone without E2E: clear text as always */
	g_assert_cmpint(dm_gate(&a, &net_b, "dave", "dave@198.51.100.7"), ==, E2E_GATE_PASS);
	g_free(wb);
	g_free(wc);
	peer_free(&a);
	peer_free(&b);
	peer_free(&c);
}

/* no key of channels, outgoing, incoming, accepted or seen_rekeys holds
   0x1F */
static void assert_nothing_scoped(E2E_JSON *kr)
{
	static const char *const stores[] = {
		"channels", "outgoing", "incoming", "accepted", "seen_rekeys", NULL
	};
	int i;
	guint j;

	for (i = 0; stores[i] != NULL; i++) {
		GPtrArray *keys = e2e_json_keys(e2e_json_get(kr, stores[i]));

		for (j = 0; j < keys->len; j++)
			g_assert_null(strchr(g_ptr_array_index(keys, j), '\x1f'));
		g_ptr_array_unref(keys);
	}
}

/* on the only network, everything that comes in - wires, DMs, KEYREQ,
   KEYRSP, REKEY - reads and answers with old rows where they are and
   binds nothing to the network */
static void test_regress_inbound_never_adopts(void)
{
	PEER a, b, c;
	char *wb, *wc, *plain, *rsp = NULL, *recip = NULL;

	peer_init(&a, "alice", ALICE, 0x41);
	peer_init(&b, "bob", BOB, -1);
	peer_init(&c, "carol", CAROL, -1);
	legacy_fixture_fill(a.kr);
	legacy_bob_real(&a);
	old_dm_keyring(&a, &b, &c, &wb, &wc);
	g_assert_cmpint(vec_decrypt(&a, &net_one, 0, &plain), ==, E2E_IN_PLAIN);
	g_free(plain);
	g_assert_cmpint(dm_decrypt(&a, &b, &net_one, wb, &plain), ==, E2E_IN_PLAIN);
	g_free(plain);
	g_assert_cmpint(dm_decrypt(&a, &c, &net_one, wc, &plain), ==, E2E_IN_PLAIN);
	g_free(plain);
	g_assert_true(e2e_handle_keyrsp(a.kr, &net_one, BOB, "bob", vec_keyrsp));
	g_assert_true(e2e_handle_rekey(a.kr, &a.id, &net_one, BOB, "bob", vec_rekey));
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	assert_nothing_scoped(a.kr);
	g_assert_null(path_get(a.kr, "channels", A_TEST, "adopted"));
	g_free(wb);
	g_free(wc);
	g_free(recip);
	peer_free(&a);
	peer_free(&b);
	peer_free(&c);
}

/* N3c: a REKEY taken on one side of the takeover is a replay on the other
   - in a DM and on #test, on this network and on another */
static void test_regress_replay_across_adoption(void)
{
	PEER a, b, c;
	unsigned char key[32];
	char *wb, *wc, *r1, *r2;

	peer_init(&a, "alice", ALICE, -1);
	peer_init(&b, "bob", BOB, -1);
	peer_init(&c, "carol", CAROL, -1);
	old_dm_keyring(&a, &b, &c, &wb, &wc);
	enable(&b, "@" ALICE, "normal");
	enable(&c, "@" ALICE, "normal");
	/* before the takeover, in place: bob's REKEY taken... */
	memset(key, 0x51, 32);
	r1 = e2e_build_rekey(&b.id, "@" ALICE, a.id.pk, key);
	g_assert_true(deliver_rekey_on(&a, &b, &net_one, r1));
	/* ...the user's line takes bob over: the replay is still refused */
	g_assert_cmpint(dm_gate(&a, &net_one, "bob", BOB), ==, E2E_GATE_CIPHER);
	g_assert_false(deliver_rekey_on(&a, &b, &net_one, r1));
	g_assert_false(deliver_rekey_on(&a, &b, &net_a, r1));
	/* a new REKEY of bob's, taken through his scoped session, leaves the
	   old rows alone */
	g_free(r1);
	memset(key, 0x53, 32);
	r1 = e2e_build_rekey(&b.id, "@" ALICE, a.id.pk, key);
	g_assert_true(deliver_rekey_on(&a, &b, &net_one, r1));
	{
		char *fp = e2e_fingerprint_hex(b.id.pk), *sk = g_strconcat(fp, "|@" ALICE, NULL);

		g_assert_null(path_get(a.kr, "seen_rekeys", sk, NULL));
		g_free(sk);
		g_free(fp);
	}
	/* carol (still old) after bob's takeover: her REKEY is taken on E2E
	   (the mixed state) and is a replay when E2E2 is the only network */
	memset(key, 0x52, 32);
	r2 = e2e_build_rekey(&c.id, "@" ALICE, a.id.pk, key);
	g_assert_true(deliver_rekey_on(&a, &c, &net_one, r2));
	g_assert_false(deliver_rekey_on(&a, &c, &net_one, r2));
	g_assert_false(deliver_rekey_on(&a, &c, &net_two_only, r2));
	g_free(r1);
	g_free(r2);
	g_free(wb);
	g_free(wc);
	peer_free(&a);
	peer_free(&b);
	peer_free(&c);

	/* #test: rpe2e.pl's REKEY taken in place, then the user's line takes
	   #test over: replayed, refused here and on the other network */
	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	legacy_bob_real(&a);
	g_assert_true(e2e_handle_rekey(a.kr, &a.id, &net_one, BOB, "bob", vec_rekey));
	{
		E2E_GATE_RESULT g;

		e2e_gate_decide(a.kr, TRUE, &a.id, &net_one, "#test", "hello", no_resolve, NULL, &g);
		g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
		e2e_gate_result_clear(&g);
	}
	g_assert_false(e2e_handle_rekey(a.kr, &a.id, &net_one, BOB, "bob", vec_rekey));
	g_assert_false(e2e_handle_rekey(a.kr, &a.id, &net_two_only, BOB, "bob", vec_rekey));
	g_assert_false(e2e_handle_rekey(a.kr, &a.id, &net_b, BOB, "bob", vec_rekey));
	peer_free(&a);
}

/* a stranger's signed KEYREQ and wire for #test on the only network,
   then a second network - #test there is never clear text */
static void test_regress_cross_network_stranger(void)
{
	static const char *const targets[] = { "#test", "#TEST", "@#test", "#pub,#test", NULL };
	PEER a;
	E2E_GATE_RESULT g;
	char *rsp = NULL, *recip = NULL, *plain;
	int i;

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_cmpint(vec_decrypt(&a, &net_one, 0, &plain), ==, E2E_IN_PLAIN);
	g_free(plain);
	for (i = 0; targets[i] != NULL; i++) {
		e2e_gate_decide(a.kr, TRUE, &a.id, &net_b, targets[i], "hello", no_resolve, NULL, &g);
		g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
		e2e_gate_result_clear(&g);
	}
	/* the second network alone (the first one gone): never clear text
	   either (encrypted: the user's line binds the old rows there) */
	for (i = 0; targets[i] != NULL; i++) {
		e2e_gate_decide(a.kr, TRUE, &a.id, &net_two_only, targets[i], "hello", no_resolve, NULL, &g);
		g_assert_cmpint(g.action, !=, E2E_GATE_PASS);
		e2e_gate_result_clear(&g);
	}
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
}

/* item 2: a KEYREQ in the mixed state finds the old config, session and
   acceptance row by row (on the only network only) */
static void test_legacy_mixed_keyreq(void)
{
	PEER a;
	E2E_JSON *our_req;
	char *rsp = NULL, *recip = NULL, *fp;
	unsigned char pk[32];

	peer_init(&a, "alice", ALICE, 0x41);
	legacy_fixture_fill(a.kr);
	legacy_bob_real(&a);
	unhex(vec_bob_pk_hex, pk, 32);
	fp = e2e_fingerprint_hex(pk);
	e2e_json_set_string(path_get(a.kr, "accepted", BOB "|#test", NULL), "fp", fp);
	g_free(fp);
	/* a row of this network for someone else on #test: mixed */
	set_trusted_session(a.kr, CAROL "|" A_TEST, LEGACY_BOB_SK);
	/* several networks: this network has no config, nothing answered */
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_a, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	/* the only network: bob was accepted on #test - answered, not pending
	   (our old request to him is set aside for now) */
	our_req = e2e_json_copy(path_get(a.kr, "pending", "#test|" BOB, NULL));
	e2e_json_remove(e2e_json_get(a.kr, "pending"), "#test|" BOB);
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	g_assert_null(path_get(a.kr, "pending_inbound", BOB "|#test", NULL));
	g_assert_null(recip);	/* his session is held (the old one) */
	/* accepted already (the old row): no second acceptance written */
	g_assert_null(path_get(a.kr, "accepted", BOB "|" A_TEST, NULL));
	g_free(rsp);
	g_free(recip);
	/* not accepted, quiet: someone whose (old) key we hold still waits
	   for /e2e accept, he is not dropped as a stranger */
	e2e_json_remove(e2e_json_get(a.kr, "accepted"), BOB "|#test");
	e2e_json_set_string(path_get(a.kr, "channels", "#test", NULL), "mode", "quiet");
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_null(rsp);
	g_assert_nonnull(path_get(a.kr, "pending_inbound", BOB "|" A_TEST, NULL));
	/* our old request to him waits: no second one in reply to his */
	e2e_json_set(e2e_json_get(a.kr, "pending"), "#test|" BOB, our_req);
	e2e_json_remove(e2e_json_get(a.kr, "pending_inbound"), BOB "|" A_TEST);
	e2e_json_set_string(path_get(a.kr, "channels", "#test", NULL), "mode", "auto-accept");
	e2e_json_set_string(path_get(a.kr, "incoming", BOB "|#test", NULL), "status", "pending");
	g_hash_table_remove_all(a.stamps);
	e2e_handle_keyreq(a.kr, &a.id, a.stamps, &net_one, BOB, "bob", vec_keyreq, ALICE, &rsp, &recip);
	g_assert_nonnull(rsp);
	g_assert_nonnull(path_get(a.kr, "pending", "#test|" BOB, NULL));
	g_assert_null(recip);
	g_free(rsp);
	g_free(recip);
	peer_free(&a);
}

/* ---- CASEMAPPING: channel names compared as the server compares them ---- */

#define ZABA_LOWER "#\xc5\xbc" "aba"	/* #Å¼aba */
#define ZABA_UPPER "#\xc5\xbb" "aba"	/* #Å»aba */
#define VEC_KEY_22 "IiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiI="	/* 0x22 x 32 */

static E2E_NET cm_unicode = { (char *) "E2E", E2E_CASEMAP_UNICODE, FALSE };
static E2E_NET cm_ascii = { (char *) "E2E", E2E_CASEMAP_ASCII, FALSE };
static E2E_NET cm_rfc1459 = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };

/* the gate's decision on a line to target; *message (if not NULL) its
   message */
static E2E_GATE_ACTION cm_gate(E2E_JSON *kr, const E2E_NET *net, const char *target,
                               char **message)
{
	E2E_GATE_RESULT g;
	E2E_GATE_ACTION a;

	e2e_gate_decide(kr, TRUE, NULL, net, target, "hello", no_resolve, NULL, &g);
	a = g.action;
	if (message != NULL) {
		*message = g.message;
		g.message = NULL;
	}
	e2e_gate_result_clear(&g);
	return a;
}

/* a server with Unicode CASEMAPPING takes #Å»aba for #Å¼aba: not in clear
   text; an ascii one does not */
static void test_casemap_gate_unicode_refuses(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	char *msg = NULL;

	enable(&p, "E2E\x1f" ZABA_LOWER, "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, ZABA_UPPER, &msg), ==, E2E_GATE_REFUSE);
	g_assert_nonnull(msg);
	g_assert_nonnull(strstr(msg, ZABA_LOWER));
	g_assert_null(strchr(msg, '\x1f'));
	g_free(msg);
	/* +#Å»aba (a STATUSMSG reading as well) too */
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "+" ZABA_UPPER, NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, ZABA_UPPER, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, ZABA_UPPER, NULL), ==, E2E_GATE_PASS);
	/* rpe2e.pl loaded: its guard decides with the same gate */
	{
		char *m = NULL, *t = NULL;
		const char *line = "PRIVMSG " ZABA_UPPER " :hello";

		g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
		                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
		g_assert_cmpstr(t, ==, ZABA_UPPER);
		g_free(m);
		g_free(t);
		m = t = NULL;
		g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_ascii, line, strlen(line),
		                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
		g_assert_null(m);
		g_free(t);
	}
	/* the name itself is encrypted */
	g_assert_cmpint(cm_gate(kr, &cm_unicode, ZABA_LOWER, NULL), ==, E2E_GATE_CIPHER);
	e2e_json_free(kr);
}

/* ascii: [ and { are two channels; rfc1459: one */
static void test_casemap_ascii_brackets_pass(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_NET strict = { (char *) "E2E", E2E_CASEMAP_STRICT_RFC1459, FALSE };

	enable(&p, "E2E\x1f#a[b", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "#a{b", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#a{b", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &strict, "#a{b", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#a{b", NULL), ==, E2E_GATE_REFUSE);
	/* ~ and ^: rfc1459 only */
	enable(&p, "E2E\x1f#x~", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#x^", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &strict, "#x^", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "#X~", NULL), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);
}

/* another network's config is not this network's channel */
static void test_casemap_other_network_ignored(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "E2E2\x1f" ZABA_LOWER, "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, ZABA_UPPER, NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* a target that is not UTF-8 on a Unicode CASEMAPPING cannot be
   compared as the server compares it: refused while E2E is on for a
   channel of this network; nor can a config of such a name */
static void test_casemap_not_utf8_refused(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_NET one = { (char *) "E2E", E2E_CASEMAP_UNICODE, TRUE };
	char *msg = NULL;

	/* nothing encrypted on this network: nothing to be mistaken for */
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#a\xff", NULL), ==, E2E_GATE_PASS);
	enable(&p, "E2E2\x1f#a", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#a\xff", NULL), ==, E2E_GATE_PASS);

	enable(&p, "E2E\x1f" ZABA_LOWER, "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#a\xff", &msg), ==, E2E_GATE_REFUSE);
	g_assert_nonnull(msg);
	g_assert_nonnull(strstr(msg, "not valid UTF-8"));
	g_assert_null(strchr(msg, '\x1f'));
	g_free(msg);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "+#a\xc5", NULL), ==, E2E_GATE_REFUSE);
	/* the exact maps compare bytes: decided */
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#a\xff", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "#a\xff", NULL), ==, E2E_GATE_PASS);
	/* a valid name is decided */
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#plain", NULL), ==, E2E_GATE_PASS);
	/* an old row (the only network) counts as well */
	e2e_json_remove(e2e_json_get(kr, "channels"), "E2E\x1f" ZABA_LOWER);
	enable(&p, ZABA_LOWER, "normal");
	g_assert_cmpint(cm_gate(kr, &one, "#a\xff", NULL), ==, E2E_GATE_REFUSE);
	/* off: nothing to be mistaken for */
	e2e_json_set_int(path_get(kr, "channels", ZABA_LOWER, NULL), "enabled", 0);
	g_assert_cmpint(cm_gate(kr, &one, "#a\xff", NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);

	/* a config whose name is not UTF-8: any other name may be it */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "E2E\x1f#\xc4" "x", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#\xc3\xa4x", &msg), ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(msg, "not valid UTF-8"));
	g_free(msg);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#plain", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#\xc4" "x", NULL), ==, E2E_GATE_CIPHER);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#plain", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#\xc4" "X", NULL), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);
}

/* rpe2e.pl's message for #Å¼aba, read from a wire whose target is
   written otherwise */
static E2E_IN_ACTION cm_decrypt(PEER *to, const E2E_NET *net, const char *target, char **plain)
{
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	E2E_IN_ACTION action;

	fake_now = 1791564129;
	in_params(&in, to, "bob", BOB, target, net);
	e2e_decrypt_incoming(to->kr, &to->id, &in, vec_wire[3].wire[0], &r);
	action = r.action;
	*plain = r.plain;
	r.plain = NULL;
	g_assert_null(r.keyreq);
	e2e_in_result_clear(&r);
	return action;
}

static void test_casemap_decrypt_folded_target(void)
{
	PEER a;
	char *plain;
	E2E_NET one = { (char *) "E2E", E2E_CASEMAP_UNICODE, TRUE };

	g_assert_cmpstr(vec_wire[3].ctx, ==, ZABA_LOWER);
	peer_init(&a, "alice", ALICE, 0x41);
	set_trusted_session(a.kr, BOB "|E2E\x1f" ZABA_LOWER, VEC_KEY_22);
	g_assert_cmpint(cm_decrypt(&a, &cm_unicode, ZABA_UPPER, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, "hello");
	g_free(plain);
	g_assert_cmpint(cm_decrypt(&a, &cm_unicode, ZABA_LOWER, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, "hello");
	g_free(plain);
	g_assert_cmpint(cm_decrypt(&a, &cm_ascii, ZABA_UPPER, &plain), ==, E2E_IN_DROP);
	g_assert_null(plain);
	/* another network's session: never */
	g_assert_cmpint(cm_decrypt(&a, &(E2E_NET) { (char *) "E2E2", E2E_CASEMAP_UNICODE, FALSE },
	                           ZABA_UPPER, &plain), ==, E2E_IN_DROP);
	g_assert_null(plain);
	/* another sender's session: never */
	e2e_json_remove(e2e_json_get(a.kr, "incoming"), BOB "|E2E\x1f" ZABA_LOWER);
	set_trusted_session(a.kr, "carol@127.0.0.1|E2E\x1f" ZABA_LOWER, VEC_KEY_22);
	g_assert_cmpint(cm_decrypt(&a, &cm_unicode, ZABA_UPPER, &plain), ==, E2E_IN_DROP);
	g_assert_null(plain);
	/* a handle that only starts like the sender's */
	set_trusted_session(a.kr, BOB "x|E2E\x1f" ZABA_LOWER, VEC_KEY_22);
	g_assert_cmpint(cm_decrypt(&a, &cm_unicode, ZABA_UPPER, &plain), ==, E2E_IN_DROP);
	g_assert_null(plain);
	/* an old row: used in place on the only network, BLOCKED on several */
	set_trusted_session(a.kr, BOB "|" ZABA_LOWER, VEC_KEY_22);
	g_assert_cmpint(cm_decrypt(&a, &cm_unicode, ZABA_UPPER, &plain), ==, E2E_IN_DROP);
	g_assert_null(plain);
	g_assert_cmpint(cm_decrypt(&a, &one, ZABA_UPPER, &plain), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(plain, ==, "hello");
	g_free(plain);
	/* incoming traffic never takes old rows over */
	g_assert_nonnull(path_get(a.kr, "incoming", BOB "|" ZABA_LOWER, NULL));
	g_assert_null(path_get(a.kr, "incoming", BOB "|E2E\x1f" ZABA_LOWER, NULL));
	peer_free(&a);
}

/* the single code point c folds to with GLib, or c */
static gunichar casefold_char(gunichar c)
{
	char buf[8], *f;
	gunichar out = c;

	buf[g_unichar_to_utf8(c, buf)] = '\0';
	f = g_utf8_casefold(buf, -1);
	if (*f != '\0' && *g_utf8_next_char(f) == '\0')
		out = g_utf8_get_char(f);
	g_free(f);
	return out;
}

/* the fold of tests/e2e/e2eircd.py --casemapping <map>: the server */
static char *server_fold(E2E_CASEMAP map, const char *s)
{
	char *nfkc, *out, *p;

	if (map == E2E_CASEMAP_UNICODE) {
		GString *closed;

		/* unicodedata.normalize("NFKC", s).casefold(); Python folds the
		   Cherokee small letters to the capitals and keeps those, GLib
		   folds each pair into the other: closed to the capital, the
		   smaller code point, as Python has it */
		nfkc = g_utf8_normalize(s, -1, G_NORMALIZE_NFKC);
		out = g_utf8_casefold(nfkc, -1);
		g_free(nfkc);
		closed = g_string_new(NULL);
		for (p = out; *p != '\0'; p = g_utf8_next_char(p)) {
			gunichar c = g_utf8_get_char(p), f = casefold_char(c);

			if (f != c && casefold_char(f) == c)
				c = MIN(c, f);
			g_string_append_unichar(closed, c);
		}
		g_free(out);
		return g_string_free(closed, FALSE);
	}
	out = g_strdup(s);
	for (p = out; *p != '\0'; p++) {
		if (*p >= 'A' && *p <= 'Z')
			*p += 'a' - 'A';
		else if (map == E2E_CASEMAP_ASCII)
			continue;
		else if (*p == '[' || *p == ']' || *p == '\\')
			*p = *p == '[' ? '{' : *p == ']' ? '}' : '|';
		else if (*p == '~' && map == E2E_CASEMAP_RFC1459)
			*p = '^';
	}
	return out;
}

/* a code point of the pools below; none whose NFKC holds a space, a
   comma or a control character (no other target, no other line) */
static gunichar cm_pool_char(GRand *rand);

static gunichar cm_random_char(GRand *rand)
{
	for (;;) {
		gunichar c = cm_pool_char(rand);
		char buf[8], *nfkc, *p;
		gboolean ok = TRUE;

		buf[g_unichar_to_utf8(c, buf)] = '\0';
		nfkc = g_utf8_normalize(buf, -1, G_NORMALIZE_NFKC);
		for (p = nfkc; p != NULL && *p != '\0'; p++)
			if ((unsigned char) *p <= 0x20 || *p == ',' || *p == 0x7f)
				ok = FALSE;
		g_free(nfkc);
		if (ok)
			return c;
	}
}

static gunichar cm_pool_char(GRand *rand)
{
	static const char ascii[] = "AZaz09[]\\~{}|^-Kk";
	static const gunichar special[] = {
		0xc5, 0xe5, 0x212b, 0x212a, 0xdf, 0x1e9e, 0x130, 0x131, 0x3a3, 0x3c3, 0x3c2,
		0x17b, 0x17c, 0x141, 0x142, 0xfb00, 0xff21, 0xff41, 0x2160, 0x24b6, 0x1d400,
		0x301, 0x307, 0x345, 0x3b9, 0x17f, 0xb5, 0x3bc, 0x13a0, 0xab70,
	};

	switch (g_rand_int_range(rand, 0, 4)) {
	case 0:
		return ascii[g_rand_int_range(rand, 0, sizeof(ascii) - 1)];
	case 1:
		return special[g_rand_int_range(rand, 0, G_N_ELEMENTS(special))];
	case 2:
		return g_rand_int_range(rand, 0xc0, 0x250);
	default:
		return g_rand_int_range(rand, 0x370, 0x500);
	}
}

/* s another way: other case per letter, rfc1459 specials swapped,
   another normalisation form */
static char *cm_variant(GRand *rand, const char *s)
{
	static const GNormalizeMode modes[] = {
		G_NORMALIZE_NFD, G_NORMALIZE_NFC, G_NORMALIZE_NFKD, G_NORMALIZE_NFKC,
	};
	GString *out = g_string_new(NULL);
	const char *p;
	int mode;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p), v = c;
		const char *swap;

		/* the expansions of full case folding */
		if ((c == 0xdf || c == 0x1e9e) && g_rand_boolean(rand)) {
			g_string_append(out, g_rand_boolean(rand) ? "ss" : "SS");
			continue;
		}
		switch (g_rand_int_range(rand, 0, 5)) {
		case 0:
			if (c < 0x80 && (swap = strchr("[]\\~{}|^", c)) != NULL)
				v = "{}|^[]\\~"[swap - "[]\\~{}|^"];
			break;
		case 1:
			v = g_unichar_toupper(c);
			break;
		case 2:
			v = g_unichar_tolower(c);
			break;
		case 3:
			v = g_unichar_totitle(c);
			break;
		}
		g_string_append_unichar(out, v);
	}
	mode = g_rand_int_range(rand, 0, G_N_ELEMENTS(modes) + 1);
	if (mode < (int) G_N_ELEMENTS(modes)) {
		char *normal = g_utf8_normalize(out->str, -1, modes[mode]);

		g_string_assign(out, normal);
		g_free(normal);
	}
	return g_string_free(out, FALSE);
}

#define CM_SEED 0x434d4150u
#define CM_NAMES 1500
#define CM_VARIANTS 6

/* the superset rule: for every enabled channel, a name the server (the
   fold of e2eircd.py) takes for the same is never let out in clear text
   - by the gate, nor by rpe2e.pl's guard. On the exact maps a name the
   server takes for another channel is (nothing refused more) */
static void test_casemap_superset(void)
{
	static const E2E_CASEMAP maps[] = {
		E2E_CASEMAP_ASCII, E2E_CASEMAP_RFC1459, E2E_CASEMAP_STRICT_RFC1459, E2E_CASEMAP_UNICODE,
	};
	GRand *rand = g_rand_new_with_seed(CM_SEED);
	GString *name = g_string_new(NULL);
	guint m, i, v, equal = 0, apart = 0;

	for (m = 0; m < G_N_ELEMENTS(maps); m++) {
		E2E_NET net = { (char *) "E2E", maps[m], FALSE };

		for (i = 0; i < CM_NAMES; i++) {
			E2E_JSON *kr = e2e_keyring_new();
			PEER p = { .kr = kr };
			char *ctx, *want;
			int n;

			g_string_assign(name, "#");
			for (n = g_rand_int_range(rand, 1, 6); n > 0; n--)
				g_string_append_unichar(name, cm_random_char(rand));
			ctx = g_strconcat("E2E\x1f", name->str, NULL);
			enable(&p, ctx, "normal");
			want = server_fold(maps[m], name->str);
			for (v = 0; v < CM_VARIANTS; v++) {
				char *variant = cm_variant(rand, name->str), *vfold, *line, *msg, *tgt;
				E2E_GATE_ACTION a, ga;

				vfold = server_fold(maps[m], variant);
				a = cm_gate(kr, &net, variant, NULL);
				line = g_strconcat("PRIVMSG ", variant, " :hello", NULL);
				ga = e2e_script_guard(kr, TRUE, FALSE, FALSE, &net, line, strlen(line),
				                      no_resolve, NULL, &msg, &tgt);
				g_free(msg);
				g_free(tgt);
				if (strcmp(vfold, want) == 0) {
					equal++;
					if (a == E2E_GATE_PASS || ga == E2E_GATE_PASS)
						g_error("map %d: %s is %s to the server, yet let out in clear text",
						        maps[m], variant, name->str);
				} else if (maps[m] != E2E_CASEMAP_UNICODE) {
					apart++;
					if (a != E2E_GATE_PASS)
						g_error("map %d: %s is another channel than %s, yet refused",
						        maps[m], variant, name->str);
				}
				g_free(line);
				g_free(vfold);
				g_free(variant);
			}
			g_free(want);
			g_free(ctx);
			e2e_json_free(kr);
		}
	}
	/* the test is not empty */
	g_test_message("%u names the server takes for the enabled one, %u for another", equal, apart);
	g_assert_cmpuint(equal, >, CM_NAMES);
	g_assert_cmpuint(apart, >, CM_NAMES / 10);
	g_string_free(name, TRUE);
	g_rand_free(rand);
}

/* a DM to a nick the server takes for the nick of a query with E2E on,
   whose address is not known now: refused */
static void test_casemap_dm_nick(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_GATE_RESULT g;

	enable(&p, "E2E\x1f@bob@old.example.net", "normal");
	e2e_json_set_string(path_get(kr, "channels", "E2E\x1f@bob@old.example.net", NULL), "nick", "bob[");
	e2e_gate_decide(kr, TRUE, NULL, &cm_rfc1459, "BOB{", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_gate_decide(kr, TRUE, NULL, &cm_ascii, "BOB{", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_json_set_string(path_get(kr, "channels", "E2E\x1f@bob@old.example.net", NULL), "nick", "\xc5\xbc" "bob");
	e2e_gate_decide(kr, TRUE, NULL, &cm_unicode, "\xc5\xbb" "BOB", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	e2e_json_free(kr);
}

/* ---- CASEMAPPING ---- */

static E2E_NET cm_unknown = { (char *) "E2E", E2E_CASEMAP_UNKNOWN, FALSE };

/* bob's message for session_ctx (key 0x55), delivered by the server
   with target; *via the name the result is marked with */
static E2E_IN_ACTION cm_moved(const E2E_NET *net, const char *session_ctx, const char *target,
                              char **plain, char **via)
{
	PEER a;
	unsigned char key[32];
	const char *error = NULL;
	GPtrArray *wires;
	E2E_IN_PARAMS in;
	E2E_IN_RESULT r;
	E2E_IN_ACTION action;
	char *sk, *row;

	memset(key, 0x55, sizeof(key));
	fake_now = 1791564129;
	peer_init(&a, "alice", ALICE, 0x41);
	sk = e2e_b64_encode(key, 32);
	row = g_strconcat(BOB "|E2E\x1f", session_ctx, NULL);
	set_trusted_session(a.kr, row, sk);
	wires = e2e_encrypt_plain(key, session_ctx, "moved line", fake_now, &error);
	g_assert_nonnull(wires);
	in_params(&in, &a, "bob", BOB, target, net);
	e2e_decrypt_incoming(a.kr, &a.id, &in, g_ptr_array_index(wires, 0), &r);
	action = r.action;
	*plain = r.plain;
	*via = r.via;
	r.plain = r.via = NULL;
	e2e_in_result_clear(&r);
	g_ptr_array_unref(wires);
	g_free(sk);
	g_free(row);
	peer_free(&a);
	return action;
}

/* the superset fold also opens bob's #a[b line delivered as
   #a{b - a channel a Unicode server keeps apart. The AEAD does not
   reject it (the AAD is the session's name): the line is marked with
   the channel whose key opened it. */
static void test_casemap_decrypt_marked(void)
{
	static const struct { const char *session, *target; } moved[] = {
		{ "#a[b", "#a{b" },
		{ "#i", "#\xc4\xb1" },                  /* #Ä± */
		{ "#ab", "#a\xe2\x80\x8b" "b" },        /* ZWSP */
	};
	char *plain, *via;
	guint i;

	for (i = 0; i < G_N_ELEMENTS(moved); i++) {
		g_assert_cmpint(cm_moved(&cm_unicode, moved[i].session, moved[i].target, &plain, &via),
		                ==, E2E_IN_PLAIN);
		g_assert_cmpstr(plain, ==, "moved line");
		g_assert_cmpstr(via, ==, moved[i].session);
		g_free(plain);
		g_free(via);
		g_assert_cmpint(cm_moved(&cm_unknown, moved[i].session, moved[i].target, &plain, &via),
		                ==, E2E_IN_PLAIN);
		g_assert_cmpstr(via, ==, moved[i].session);
		g_free(plain);
		g_free(via);
	}
	/* one channel to every Unicode server: no mark */
	g_assert_cmpint(cm_moved(&cm_unicode, ZABA_LOWER, ZABA_UPPER, &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_null(via);
	g_free(plain);
	g_assert_cmpint(cm_moved(&cm_unicode, ZABA_LOWER, "#\xc5\xbb" "ABA", &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_null(via);
	g_free(plain);
	/* names a per code point server (towlower, Go) or a PRECIS
	   one keeps apart: marked */
	{
		static const struct { const char *session, *target; } apart[] = {
			{ "#\xc5\xbc" "aba", "#z\xcc\x87" "aba" },  /* NFC / NFD */
			{ "#ss", "#\xc3\x9f" },                      /* ß */
			{ "#ff", "#\xef\xac\x80" },                  /* ﬀ */
		};

		for (i = 0; i < G_N_ELEMENTS(apart); i++) {
			g_assert_cmpint(cm_moved(&cm_unicode, apart[i].session, apart[i].target, &plain, &via),
			                ==, E2E_IN_PLAIN);
			g_assert_cmpstr(via, ==, apart[i].session);
			g_free(plain);
			g_free(via);
		}
	}
	/* a server comparing in upper case keeps the Kelvin sign
	   apart from k, and Final_Sigma makes #ΑΣ #ας: marked */
	g_assert_cmpint(cm_moved(&cm_unicode, "#k", "#\xe2\x84\xaa", &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(via, ==, "#k");
	g_free(plain);
	g_free(via);
	g_assert_cmpint(cm_moved(&cm_unicode, "#\xce\xb1\xcf\x83", "#\xce\x91\xce\xa3", &plain, &via), ==,
	                E2E_IN_PLAIN);
	g_assert_cmpstr(via, ==, "#\xce\xb1\xcf\x83");
	g_free(plain);
	g_free(via);
	/* an unknown CASEMAPPING (cp1250, cp1251) may keep any
	   non-ASCII spelling apart: marked */
	g_assert_cmpint(cm_moved(&cm_unknown, ZABA_LOWER, ZABA_UPPER, &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(via, ==, ZABA_LOWER);
	g_free(plain);
	g_free(via);
	g_assert_cmpint(cm_moved(&cm_unknown, "#\xd0\xb8", "#\xd0\x98", &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(via, ==, "#\xd0\xb8");
	g_free(plain);
	g_free(via);
	g_assert_cmpint(cm_moved(&cm_unknown, "#abc", "#ABC", &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_null(via);
	g_free(plain);
	/* the exact maps: the server's own rule, no mark */
	g_assert_cmpint(cm_moved(&cm_rfc1459, "#a[b", "#A{B", &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_null(via);
	g_free(plain);
	/* ... but their Latin-1 bytes are not every server's */
	g_assert_cmpint(cm_moved(&cm_rfc1459, "#\xe9", "#\xc9", &plain, &via), ==, E2E_IN_PLAIN);
	g_assert_cmpstr(via, ==, "#\xe9");
	g_free(plain);
	g_free(via);
	/* not opened at all on ascii */
	g_assert_cmpint(cm_moved(&cm_ascii, "#a[b", "#a{b", &plain, &via), ==, E2E_IN_DROP);
	g_assert_null(plain);
	g_assert_null(via);

	/* what the display shows */
	plain = e2e_plain_with_via("moved line", "#a[b");
	g_assert_cmpstr(plain, ==, "moved line\017 (E2E key of #a[b)");
	g_free(plain);
	plain = e2e_plain_with_via("\001ACTION waves\001", "#a[b");
	g_assert_cmpstr(plain, ==, "\001ACTION waves\017 (E2E key of #a[b)\001");
	g_free(plain);
	plain = e2e_plain_with_via("x", "#a\003" "4b");
	g_assert_cmpstr(plain, ==, "x\017 (E2E key of #a?4b)");
	g_free(plain);
	/* formatting left on by the text (black on black) is reset
	   before the mark */
	plain = e2e_plain_with_via("x\00301,01", "#a[b");
	g_assert_cmpstr(plain, ==, "x\00301,01\017 (E2E key of #a[b)");
	g_free(plain);
}

/* servers of the ircu family fold Latin-1 bytes */
static void test_casemap_latin1_bytes(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "E2E\x1f#\xe9", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#\xc9", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "#\xc9", NULL), ==, E2E_GATE_REFUSE);
	enable(&p, "E2E\x1f#\xc3\xa9", "normal");	/* #Ã© */
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#\xe3\xa9", NULL), ==, E2E_GATE_REFUSE);
	/* Ã and Ã· are no letters */
	enable(&p, "E2E\x1f#\xd7", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "#\xf7", NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* an unknown CASEMAPPING may be a byte code page */
static void test_casemap_unknown_codepage(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	/* cp1251 takes #Ð¸ (D0 B8 as UTF-8) for #Ð¨ (D0 A8) byte by byte */
	enable(&p, "E2E\x1f#\xd0\xb8", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unknown, "#\xd0\xa8", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#\xd0\xa8", NULL), ==, E2E_GATE_PASS);
	/* koi8-r: #Ð³ (D0 B3) / #Ð£ (D0 A3) */
	enable(&p, "E2E\x1f#\xd0\xb3", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unknown, "#\xd0\xa3", NULL), ==, E2E_GATE_REFUSE);
	/* a byte code page keeps ASCII names apart from the others */
	g_assert_cmpint(cm_gate(kr, &cm_unknown, "#plain", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_unknown, "#PLAIN", NULL), ==, E2E_GATE_PASS);
	/* the name itself is encrypted */
	g_assert_cmpint(cm_gate(kr, &cm_unknown, "#\xd0\xb8", NULL), ==, E2E_GATE_CIPHER);
	e2e_json_free(kr);
}

/* before 005, the widest map */
static void test_casemap_before_005(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_NET pre = { (char *) "E2E", e2e_casemap_of_server(NULL, FALSE), FALSE };
	E2E_NET post = { (char *) "E2E", e2e_casemap_of_server(NULL, TRUE), FALSE };

	enable(&p, "E2E\x1f" ZABA_LOWER, "normal");
	g_assert_cmpint(cm_gate(kr, &pre, ZABA_UPPER, NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &post, ZABA_UPPER, NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* only channel configs count for the undecided-name refusal */
static void test_casemap_undecided_channels_only(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "E2E\x1f@bob@h\xff", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#plain", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#a\xff", NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* /e2e off in a query names its nick exactly */
static void test_casemap_off_exact(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	GPtrArray *t;

	enable(&p, "E2E\x1f@a@h.example.org", "normal");
	e2e_json_set_string(path_get(kr, "channels", "E2E\x1f@a@h.example.org", NULL), "nick", "a[");
	t = e2e_query_off_targets(kr, &cm_unicode, "a{", NULL);
	g_assert_cmpuint(t->len, ==, 0);
	g_ptr_array_unref(t);
	t = e2e_query_off_targets(kr, &cm_unicode, "a[", NULL);
	g_assert_cmpuint(t->len, ==, 1);
	g_ptr_array_unref(t);
	/* still on for the nick to the server: refusals and "still on" */
	t = e2e_dm_configs_of(kr, &cm_unicode, "a{");
	g_assert_cmpuint(t->len, ==, 1);
	g_ptr_array_unref(t);
	e2e_json_free(kr);
}

/* an unassigned code point is not decided */
static void test_casemap_unassigned(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "E2E\x1f#ab", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#a\xe2\x81\xa5" "b", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "#cd", NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* with rpe2e.pl loaded, a name the server takes for the
   script's own (bare) entry but which the script, comparing exactly,
   does not know */
static void test_casemap_guard_bare_rows(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	const char *line;
	char *m, *t;

	enable(&p, ZABA_LOWER, "normal");
	line = "PRIVMSG " ZABA_UPPER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(m, ZABA_LOWER));
	g_assert_cmpstr(t, ==, ZABA_UPPER);
	g_free(m);
	g_free(t);
	/* the exact name is the script's */
	line = "PRIVMSG " ZABA_LOWER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
	g_assert_null(m);
	g_free(t);
	/* another channel to an rfc1459 server */
	line = "PRIVMSG " ZABA_UPPER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_rfc1459, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
	g_free(m);
	g_free(t);
	/* ASCII case too: rpe2e.pl does not fold it */
	enable(&p, "#multi", "normal");
	line = "PRIVMSG #MULTI :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_ascii, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	/* the escape hatches the script keeps: non-ACTION CTCP, bot commands */
	line = "PRIVMSG #MULTI :\001VERSION\001";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_ascii, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
	g_free(m);
	g_free(t);
	line = "PRIVMSG #MULTI :!seen x";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_ascii, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), !=, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	/* rpe2e.pl does not split "#x,#y": a piece the server takes
	   for a channel E2E is on for there, the same name too */
	line = "PRIVMSG #x," ZABA_LOWER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	line = "PRIVMSG #x," ZABA_UPPER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	line = "PRIVMSG #x," ZABA_LOWER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_rfc1459, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	line = "PRIVMSG #multi,#x :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_ascii, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	line = "PRIVMSG #x,#y :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
	g_free(m);
	g_free(t);
	/* the script encrypts from its own row whatever the module's
	   config of the name says - one that is off does not hide it */
	enable(&p, "E2E\x1f" ZABA_LOWER, "normal");
	e2e_json_set_int(path_get(kr, "channels", "E2E\x1f" ZABA_LOWER, NULL), "enabled", 0);
	line = "PRIVMSG " ZABA_UPPER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
	g_free(m);
	g_free(t);
	line = "PRIVMSG " ZABA_LOWER " :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_unicode, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
	g_free(m);
	g_free(t);
	/* off: nothing */
	e2e_json_set_int(path_get(kr, "channels", "#multi", NULL), "enabled", 0);
	line = "PRIVMSG #MULTI :hello";
	g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_ascii, line, strlen(line),
	                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_PASS);
	g_free(m);
	g_free(t);
	e2e_json_free(kr);
}

/* IRCnet: "!chan" is short for the "!XXXXXchan" with that ID */
static void test_casemap_ircnet_short_name(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "E2E\x1f!AB12Csecret", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!secret", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!SECRET", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!other", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!AB12Csecret", NULL), ==, E2E_GATE_CIPHER);
	e2e_json_free(kr);
	/* an ID written in lower case, and the other way round: a
	   short name with E2E on, the full name as the target */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "E2E\x1f!ab12cchan", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!chan", NULL), ==, E2E_GATE_REFUSE);
	enable(&p, "E2E\x1f!short", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!AB12Cshort", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!ab12cSHORT", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "!AB12Cother", NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* a list of targets with a query piece E2E is on for */
static char *cm_resolve_bob(const char *nick, const E2E_JSON *kr, void *data)
{
	return g_ascii_strcasecmp(nick, "bob") == 0 ? g_strdup(BOB) : NULL;
}

static void cm_set_peer(E2E_JSON *kr, const char *fp, const char *nick, const char *handle)
{
	E2E_JSON *peer = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_string(peer, "last_nick", nick);
	e2e_json_set_string(peer, "last_handle", handle);
	e2e_json_set_string(peer, "status", "trusted");
	e2e_json_set(e2e_json_get(kr, "peers"), fp, peer);
}

static E2E_GATE_ACTION cm_gate_r(E2E_JSON *kr, const E2E_NET *net, const char *target,
                                 const char *body, E2E_RESOLVE_FUNC resolve)
{
	E2E_GATE_RESULT g;
	E2E_GATE_ACTION a;

	e2e_gate_decide(kr, TRUE, NULL, net, target, body, resolve, NULL, &g);
	a = g.action;
	e2e_gate_result_clear(&g);
	return a;
}

static E2E_GATE_ACTION cm_guard(E2E_JSON *kr, const E2E_NET *net, const char *line,
                                E2E_RESOLVE_FUNC resolve)
{
	char *m = NULL, *t = NULL;
	E2E_GATE_ACTION a = e2e_script_guard(kr, TRUE, FALSE, FALSE, net, line, strlen(line), resolve,
	                                     NULL, &m, &t);

	if (a == E2E_GATE_REFUSE) {
		g_assert_nonnull(m);
		g_assert_null(strchr(m, '\x1f'));
	}
	g_free(m);
	g_free(t);
	return a;
}

static void test_casemap_list_dm_pieces(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	/* the module active: its gate decides every piece (as before) */
	enable(&p, "E2E\x1f@" BOB, "normal");
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "bob,carol", "hi", cm_resolve_bob), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "#x,bob", "hi", cm_resolve_bob), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "#x,bob", "\001ACTION waves\001", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "carol,dave", "hi", cm_resolve_bob), ==, E2E_GATE_PASS);
	/* the address not known now: the query's nick, or the peer last seen
	   as that nick, folded by the map */
	e2e_json_set_string(path_get(kr, "channels", "E2E\x1f@" BOB, NULL), "nick", "bob[");
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "carol,BOB{", "hi", no_resolve), ==, E2E_GATE_REFUSE);
	e2e_json_remove(path_get(kr, "channels", "E2E\x1f@" BOB, NULL), "nick");
	cm_set_peer(kr, "f00d", "bob[", BOB);
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "carol,BOB{", "hi", no_resolve), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);

	/* rpe2e.pl loaded: it looks "bob,carol" up as one name and sends it
	   in clear; its own (bare) query rows count */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "@" BOB, "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", cm_resolve_bob), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,bob :hi", cm_resolve_bob), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,bob :\001ACTION waves\001", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,dave :hi", cm_resolve_bob), ==, E2E_GATE_PASS);
	/* the script's own: one target, and the CTCP escape hatch */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob :hi", cm_resolve_bob), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :\001VERSION\001", cm_resolve_bob), ==,
	                E2E_GATE_PASS);
	/* not resolved: the query's nick or the peer's last nick, folded */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,BOB{ :hi", no_resolve), ==, E2E_GATE_PASS);
	cm_set_peer(kr, "f00d", "bob[", BOB);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,BOB{ :hi", no_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_ascii, "PRIVMSG carol,BOB{ :hi", no_resolve), ==, E2E_GATE_PASS);
	e2e_json_remove(e2e_json_get(kr, "peers"), "f00d");
	e2e_json_set_string(path_get(kr, "channels", "@" BOB, NULL), "nick", "bob[");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,BOB{ :hi", no_resolve), ==, E2E_GATE_REFUSE);
	/* the module's config of that query off does not hide the script's */
	enable(&p, "E2E\x1f@" BOB, "normal");
	e2e_json_set_int(path_get(kr, "channels", "E2E\x1f@" BOB, NULL), "enabled", 0);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", cm_resolve_bob), ==, E2E_GATE_REFUSE);
	/* the script's row off, the module's on: refused as well */
	e2e_json_set_int(path_get(kr, "channels", "@" BOB, NULL), "enabled", 0);
	e2e_json_set_int(path_get(kr, "channels", "E2E\x1f@" BOB, NULL), "enabled", 1);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", cm_resolve_bob), ==, E2E_GATE_REFUSE);
	/* both off: clear text, as asked */
	e2e_json_set_int(path_get(kr, "channels", "E2E\x1f@" BOB, NULL), "enabled", 0);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", cm_resolve_bob), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* a bot command bypasses E2E on a channel only - DMs never bypass,
   also when a channel piece of the list makes the line a bot command */
static void test_casemap_list_dm_bypass(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	/* the module active: the DM piece refuses the list (as before) */
	enable(&p, "E2E\x1f@" BOB, "normal");
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "#x,bob", "!help secret", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "bob,#x", ".net secret", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "#x,carol", "!help", cm_resolve_bob), ==, E2E_GATE_BYPASS);
	e2e_json_free(kr);

	/* rpe2e.pl loaded, E2E on for bob in its own (bare) row */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "@" BOB, "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,bob :!help secret", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,#x :.net secret", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,BOB :!help secret", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	/* not resolved: the query's nick */
	e2e_json_set_string(path_get(kr, "channels", "@" BOB, NULL), "nick", "bob");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,bob :!help secret", no_resolve), ==,
	                E2E_GATE_REFUSE);
	/* a list of channels and a nick without E2E: the bot command goes
	   out, as the module would send it */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,carol :!help", cm_resolve_bob), !=,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x :!help", cm_resolve_bob), !=, E2E_GATE_REFUSE);
	/* channels: a bot command bypasses E2E in the script and in the
	   module alike, whatever the case of the name */
	enable(&p, "#\xc5\xbc" "aba", "normal");
	g_assert_cmpint(cm_guard(kr, &cm_unicode, "PRIVMSG #\xc5\xbb" "aba :!help", cm_resolve_bob), !=,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_unicode, "PRIVMSG #x,#\xc5\xbb" "aba :!help", cm_resolve_bob), !=,
	                E2E_GATE_REFUSE);
	/* not a bot command: refused as before */
	g_assert_cmpint(cm_guard(kr, &cm_unicode, "PRIVMSG #x,#\xc5\xbb" "aba :help", cm_resolve_bob), ==,
	                E2E_GATE_REFUSE);
	e2e_json_free(kr);
}

/* an old bare-nick row (no '@') with E2E on counts for a nick in a
   list, as nick_enabled() does for the module */
static void test_casemap_list_bare_nick(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "bob", "normal");
	/* the module: the address not known now, the old row refuses */
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "bob,carol", "hi", no_resolve), ==, E2E_GATE_REFUSE);
	/* rpe2e.pl loaded */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", no_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,BOB :hi", no_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG #x,bob :!help", no_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,dave :hi", no_resolve), ==, E2E_GATE_PASS);
	/* one target: the script's own */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob :hi", no_resolve), ==, E2E_GATE_PASS);
	/* this network's scoped bare-nick row as well, another network's not */
	e2e_json_set_int(path_get(kr, "channels", "bob", NULL), "enabled", 0);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", no_resolve), ==, E2E_GATE_PASS);
	enable(&p, "E2E2\x1f" "bob", "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", no_resolve), ==, E2E_GATE_PASS);
	enable(&p, "E2E\x1f" "bob", "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob,carol :hi", no_resolve), ==, E2E_GATE_REFUSE);
	/* a channel row is not a nick */
	e2e_json_free(kr);
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "#bob", "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol,dave :hi", no_resolve), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

#define ZBIK_LOWER "\xc5\xbc" "bik"	/* żbik */
#define ZBIK_UPPER "\xc5\xbb" "BIK"	/* ŻBIK */

/* a single DM target with its address unknown now: an old bare-nick row
   with E2E on counts for every nick the server takes for it (its
   CASEMAPPING, as for channels), not only for the name as it is stored */
static void test_casemap_single_bare_nick(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	char *msg = NULL;

	enable(&p, "bob", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "bob", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "BOB", &msg), ==, E2E_GATE_REFUSE);
	g_assert_cmpstr(msg, ==, E2E_REFUSE_NO_HANDLE);
	g_free(msg);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "BOB", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "BOB", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "carol", NULL), ==, E2E_GATE_PASS);
	/* a nick in a list: as before */
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "carol,BOB", NULL), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);

	/* rfc1459: [ ] \ ~ are { } | ^ */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "bob[", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "BOB{", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "bob{", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "BOB{", NULL), ==, E2E_GATE_REFUSE);
	/* ascii keeps them apart */
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "bob{", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "BOB{", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_ascii, "BOB[", NULL), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);

	/* UNICODE (and before the server named its CASEMAPPING) */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, ZBIK_LOWER, "normal");
	g_assert_cmpint(cm_gate(kr, &cm_unicode, ZBIK_UPPER, NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, ZBIK_LOWER, NULL), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);

	/* this network's scoped bare-nick row, folded too; another network's
	   not; one turned off is no row */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "E2E2\x1f" "bob", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "BOB", NULL), ==, E2E_GATE_PASS);
	enable(&p, "bob", "normal");
	e2e_json_set_int(path_get(kr, "channels", "bob", NULL), "enabled", 0);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "BOB", NULL), ==, E2E_GATE_PASS);
	enable(&p, "E2E\x1f" "bob", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "BOB", NULL), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "bob", NULL), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);

	/* a channel row is no nick, not even one the fold joins to a nick
	   (U+FF03 FULLWIDTH NUMBER SIGN is '#' after NFKC) */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "#bob", "normal");
	g_assert_cmpint(cm_gate(kr, &cm_rfc1459, "BOB", NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_gate(kr, &cm_unicode, "\xef\xbc\x83" "bob", NULL), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* the keyring's cache only (what a NULL kr asks for: the live handle,
   which there is none of here) */
static char *cache_resolve(const char *nick, const E2E_JSON *kr, void *data)
{
	const char *c = kr != NULL ? e2e_find_handle_by_nick(kr, nick) : NULL;

	return c != NULL && *c != '\0' ? g_strdup(c) : NULL;
}

/* the live handle BOB (an open query) */
static char *live_resolve_bob(const char *nick, const E2E_JSON *kr, void *data)
{
	return g_strdup(BOB);
}

static E2E_GATE_ACTION guard_msg(E2E_JSON *kr, const E2E_NET *net, const char *line,
                                 E2E_RESOLVE_FUNC resolve, char **msg)
{
	char *t = NULL, *m = NULL;
	E2E_GATE_ACTION a = e2e_script_guard(kr, TRUE, FALSE, FALSE, net, line, strlen(line), resolve,
	                                     NULL, &m, &t);

	g_free(t);
	if (msg != NULL)
		*msg = m;
	else
		g_free(m);
	return a;
}

/* an empty address is none */
static char *empty_resolve(const char *nick, const E2E_JSON *kr, void *data)
{
	return g_strdup("");
}

/* rpe2e.pl loaded, a single DM target: the script looks an old bare-nick
   row up exactly and a peer by lc(last_nick); a nick the server takes for
   one with E2E on there that the script does not find gets clear text
   from it - refused. What the script finds itself stays its own. */
static void test_casemap_script_single_nick(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };

	enable(&p, "bob", "normal");
	/* the script's own refusal */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob :hi", no_resolve), ==, E2E_GATE_PASS);
	{
		const char *line = "PRIVMSG BOB :hi";
		char *m = NULL, *t = NULL;

		g_assert_cmpint(e2e_script_guard(kr, TRUE, FALSE, FALSE, &cm_rfc1459, line, strlen(line),
		                                 no_resolve, NULL, &m, &t), ==, E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(m, "does not find it under that name"));
		g_assert_nonnull(strstr(m, "for bob in"));
		g_assert_nonnull(strstr(m, "write the nick as bob"));
		g_assert_cmpstr(t, ==, "BOB");
		g_free(m);
		g_free(t);
	}
	g_assert_cmpint(cm_guard(kr, &cm_ascii, "PRIVMSG BOB :hi", no_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG carol :hi", no_resolve), ==, E2E_GATE_PASS);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG BOB :hi", empty_resolve), ==, E2E_GATE_REFUSE);
	/* resolved: the script goes by the address, as the module does */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG BOB :hi", cm_resolve_bob), ==, E2E_GATE_PASS);
	/* non-ACTION CTCP stays the escape hatch */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG BOB :\x01VERSION\x01", no_resolve), ==,
	                E2E_GATE_PASS);
	e2e_json_free(kr);

	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "bob[", "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG BOB{ :hi", no_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_ascii, "PRIVMSG bob{ :hi", no_resolve), ==, E2E_GATE_PASS);
	e2e_json_free(kr);

	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, ZBIK_LOWER, "normal");
	g_assert_cmpint(cm_guard(kr, &cm_unicode, "PRIVMSG " ZBIK_UPPER " :hi", no_resolve), ==,
	                E2E_GATE_REFUSE);
	e2e_json_free(kr);

	/* an old "@<handle>" config of a peer last seen as bob{: the script
	   does not find bob[ (lc() is ASCII only) */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "@" BOB, "normal");
	cm_set_peer(kr, "00112233445566778899aabbccddeeff", "bob{", BOB);
	{
		char *msg = NULL;

		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob[ :hi", no_resolve, &msg), ==,
		                E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(msg, "for @" BOB " in"));
		g_assert_nonnull(strstr(msg, "write the nick as bob{"));
		g_free(msg);
	}
	g_assert_cmpint(cm_guard(kr, &cm_ascii, "PRIVMSG bob[ :hi", no_resolve), ==, E2E_GATE_PASS);
	/* a query known by its "nick" only: no name the script finds */
	e2e_json_remove(e2e_json_get(kr, "peers"), "00112233445566778899aabbccddeeff");
	e2e_json_set_string(path_get(kr, "channels", "@" BOB, NULL), "nick", "bob{");
	{
		char *msg = NULL;

		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob[ :hi", no_resolve, &msg), ==,
		                E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(msg, "cannot find it now"));
		g_free(msg);
	}
	e2e_json_free(kr);
}

/* rpe2e.pl compares a non-ASCII nick with the keyring's last_nick by
   Perl's lc() on a string it decoded from JSON, not on the bytes irssi
   gives it: it may not find the peer, and sends in clear text. A
   non-ASCII nick resolved only through the keyring's cache counts as not
   resolved for the script; a live address is the script's own. */
static void test_casemap_script_nonascii_cache(void)
{
	static const char *const nicks[] = {
		"\xc5\xbc" "bik",			/* żbik */
		"\xd0\xb1\xd0\xbe\xd0\xb1",		/* боб */
		"k\xc3\xb3tek",				/* kótek */
		"\xe4\xb8\xad" "bob",			/* 中bob */
		NULL
	};
	E2E_NET u = { (char *) "E2E", E2E_CASEMAP_UNICODE, TRUE };
	int i;

	for (i = 0; nicks[i] != NULL; i++) {
		E2E_JSON *kr = e2e_keyring_new();
		PEER p = { .kr = kr };
		char line[64], *msg = NULL;
		E2E_GATE_RESULT g;

		enable(&p, "@" BOB, "normal");
		cm_set_peer(kr, "00112233445566778899aabbccddeeff", nicks[i], BOB);
		g_snprintf(line, sizeof(line), "PRIVMSG %s :secret", nicks[i]);
		/* the module itself encrypts (on a copy: it takes the old rows
		   over) */
		{
			E2E_JSON *copy = e2e_json_copy(kr);

			e2e_gate_decide(copy, TRUE, NULL, &u, nicks[i], "secret", cache_resolve, NULL, &g);
			g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
			e2e_gate_result_clear(&g);
			e2e_json_free(copy);
		}
		g_assert_cmpint(guard_msg(kr, &u, line, cache_resolve, &msg), ==, E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(msg, "cannot find it now"));
		g_assert_nonnull(strstr(msg, "open a query with"));
		g_free(msg);
		/* a live address: the script finds it as well */
		g_assert_cmpint(guard_msg(kr, &u, line, live_resolve_bob, NULL), ==, E2E_GATE_PASS);
		e2e_json_free(kr);
	}
	/* the remedy names a spelling only when the script finds that one:
	   ASCII, and not the very name it did not find */
	{
		E2E_JSON *kr = e2e_keyring_new();
		PEER p = { .kr = kr };
		char *msg = NULL;

		enable(&p, "@" BOB, "normal");
		cm_set_peer(kr, "00112233445566778899aabbccddeeff", "\xc5\xbb" "BIK", BOB);
		g_assert_cmpint(guard_msg(kr, &u, "PRIVMSG \xc5\xbc" "bik :x", cache_resolve, &msg), ==,
		                E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(msg, "cannot find it now"));
		g_free(msg);
		e2e_json_set_string(path_get(kr, "peers", "00112233445566778899aabbccddeeff", NULL), "last_nick",
		                    "bob");
		g_assert_cmpint(guard_msg(kr, &u, "PRIVMSG bob :x", no_resolve, &msg), ==, E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(msg, "cannot find it now"));
		g_free(msg);
		e2e_json_free(kr);
	}
	/* ASCII: the script's lc() finds it */
	{
		E2E_JSON *kr = e2e_keyring_new();
		PEER p = { .kr = kr };

		enable(&p, "@" BOB, "normal");
		cm_set_peer(kr, "00112233445566778899aabbccddeeff", "bob", BOB);
		g_assert_cmpint(cm_guard(kr, &u, "PRIVMSG bob :secret", cache_resolve), ==, E2E_GATE_PASS);
		g_assert_cmpint(cm_guard(kr, &u, "PRIVMSG BOB :secret", cache_resolve), ==, E2E_GATE_PASS);
		e2e_json_free(kr);
	}
}

/* an old context of a peer the server takes for the nick (its
   CASEMAPPING), with rows of no known network (several networks): BLOCKED
   - refused as it is for the nick the keyring's cache names exactly */
static void test_casemap_blocked_folded_peer(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	E2E_NET ascii_a = { (char *) "E2E", E2E_CASEMAP_ASCII, FALSE };
	char *msg = NULL;

	enable(&p, "@" BOB, "normal");
	e2e_json_set_int(path_get(kr, "channels", "@" BOB, NULL), "enabled", 0);
	e2e_json_set(e2e_json_get(kr, "outgoing"), "@" BOB, e2e_json_new(E2E_JSON_OBJECT));
	cm_set_peer(kr, "00112233445566778899aabbccddeeff", "bob{", BOB);
	g_assert_cmpint(cm_gate_r(kr, &net_a, "bob{", "hi", cache_resolve), ==, E2E_GATE_REFUSE);
	{
		E2E_GATE_RESULT g;

		e2e_gate_decide(kr, TRUE, NULL, &net_a, "bob[", "hi", cache_resolve, NULL, &g);
		g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
		msg = g_strdup_printf(E2E_REFUSE_LEGACY, "bob[", "E2E");
		g_assert_cmpstr(g.message, ==, msg);
		g_free(msg);
		e2e_gate_result_clear(&g);
	}
	g_assert_cmpint(cm_gate_r(kr, &net_a, "BOB[", "hi", cache_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &net_a, "carol", "hi", cache_resolve), ==, E2E_GATE_PASS);
	/* ascii keeps bob[ apart */
	g_assert_cmpint(cm_gate_r(kr, &ascii_a, "bob[", "hi", cache_resolve), ==, E2E_GATE_PASS);
	/* the only network: the old rows are this one's, nothing blocked */
	g_assert_cmpint(cm_gate_r(kr, &net_one, "bob[", "hi", cache_resolve), ==, E2E_GATE_PASS);
	e2e_json_free(kr);

	/* a context another network took over */
	kr = e2e_keyring_new();
	p.kr = kr;
	enable(&p, "E2E2\x1f@" BOB, "normal");
	e2e_json_set_int(path_get(kr, "channels", "E2E2\x1f@" BOB, NULL), "enabled", 0);
	e2e_json_set_string(path_get(kr, "channels", "E2E2\x1f@" BOB, NULL), E2E_ADOPTED_MEMBER, "E2E2");
	cm_set_peer(kr, "00112233445566778899aabbccddeeff", "bob{", BOB);
	g_assert_cmpint(cm_gate_r(kr, &net_a, "bob{", "hi", cache_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &net_a, "bob[", "hi", cache_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_gate_r(kr, &net_a, "carol", "hi", cache_resolve), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* rpe2e.pl loaded: the nick resolves to another address than the one of
   a query E2E is on for whose peer the server takes for that nick - the
   native gate refuses (REFUSE_DM_MOVED), so does the guard */
static void test_casemap_script_dm_moved(void)
{
	E2E_JSON *kr = e2e_keyring_new();
	PEER p = { .kr = kr };
	char *msg = NULL;

	enable(&p, "@" BOB, "normal");
	cm_set_peer(kr, "00112233445566778899aabbccddeeff", "bob{", BOB);
	cm_set_peer(kr, "ffeeddccbbaa99887766554433221100", "BOB[", "other@host");
	g_assert_cmpint(cm_gate_r(kr, &cm_rfc1459, "bob[", "hi", cache_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob[ :hi", cache_resolve, &msg), ==,
	                E2E_GATE_REFUSE);
	g_assert_nonnull(strstr(msg, "bob[ is now other@host"));
	g_free(msg);
	/* ascii: bob{ is someone else */
	g_assert_cmpint(cm_guard(kr, &cm_ascii, "PRIVMSG bob[ :hi", cache_resolve), ==, E2E_GATE_PASS);
	/* bob{ itself resolves to the query's address: the script encrypts */
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob{ :hi", cache_resolve), ==, E2E_GATE_PASS);
	/* the same nick, exactly, seen since at another address */
	e2e_json_set_string(path_get(kr, "peers", "ffeeddccbbaa99887766554433221100", NULL), "last_nick",
	                    "bob{");
	e2e_json_set_int(path_get(kr, "peers", "ffeeddccbbaa99887766554433221100", NULL), "last_seen", 5);
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob{ :hi", cache_resolve), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(cm_guard(kr, &cm_ascii, "PRIVMSG bob{ :hi", cache_resolve), ==, E2E_GATE_REFUSE);
	/* E2E on for the new address as well: the script encrypts there */
	enable(&p, "@other@host", "normal");
	g_assert_cmpint(cm_guard(kr, &cm_rfc1459, "PRIVMSG bob{ :hi", cache_resolve), ==, E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* a peer as rpe2e.pl may store it: last_seen a number, null or missing */
static void tie_peer(E2E_JSON *kr, const char *fp, const char *nick, const char *handle,
                     const char *seen)
{
	E2E_JSON *peer = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_string(peer, "last_nick", nick);
	e2e_json_set_string(peer, "last_handle", handle);
	if (seen != NULL && strcmp(seen, "null") == 0)
		e2e_json_set(peer, "last_seen", e2e_json_new(E2E_JSON_NULL));
	else if (seen != NULL)
		e2e_json_set_int(peer, "last_seen", g_ascii_strtoll(seen, NULL, 10));
	e2e_json_set(e2e_json_get(kr, "peers"), fp, peer);
}

/* the newest last_seen wins, a tie goes to the smallest
   fingerprint key (byte order), null or missing is 0 - whatever order
   the peers were stored in */
static void test_cache_tie_order(void)
{
	static const char *const seens[] = { NULL, "null", "0" };
	int order, i, j;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			for (order = 0; order < 2; order++) {
				E2E_JSON *kr = e2e_keyring_new();

				if (order == 0) {
					tie_peer(kr, "a9", "bob", BOB, seens[i]);
					tie_peer(kr, "f2", "BOB", "h2@x", seens[j]);
				} else {
					tie_peer(kr, "f2", "BOB", "h2@x", seens[j]);
					tie_peer(kr, "a9", "bob", BOB, seens[i]);
				}
				g_assert_cmpstr(e2e_find_handle_by_nick(kr, "bob"), ==, BOB);
				/* newer wins over the key */
				e2e_json_set_int(path_get(kr, "peers", "f2", NULL), "last_seen", 1);
				g_assert_cmpstr(e2e_find_handle_by_nick(kr, "bob"), ==, "h2@x");
				e2e_json_free(kr);
			}
		}
	}
}

/* rpe2e.pl loaded: two addresses tie for the nick in the keyring's cache.
   An older script picks one by Perl's hash order, which changes from
   process to process: the module's pick may not be the script's, so a
   cache-only address with a tie counts as not resolved. */
static void test_casemap_script_cache_tie(void)
{
	int order;

	for (order = 0; order < 2; order++) {
		E2E_JSON *kr = e2e_keyring_new();
		PEER p = { .kr = kr };
		char *msg = NULL;

		enable(&p, "@" BOB, "normal");
		if (order == 0) {
			tie_peer(kr, "a9", "bob", BOB, "null");
			tie_peer(kr, "f2", "bob", "h2@x", "null");
		} else {
			tie_peer(kr, "a9", "bob", "h2@x", NULL);
			tie_peer(kr, "f2", "bob", BOB, "0");
		}
		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob :x", cache_resolve, &msg), ==,
		                E2E_GATE_REFUSE);
		g_assert_nonnull(strstr(msg, "cannot find it now"));
		g_free(msg);
		/* a live address is the script's as well */
		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob :x", live_resolve_bob, NULL), ==,
		                E2E_GATE_PASS);
		/* the tie broken by last_seen: the script's pick is the module's */
		e2e_json_set_int(path_get(kr, "peers", order == 0 ? "a9" : "f2", NULL), "last_seen", 7);
		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob :x", cache_resolve, NULL), ==,
		                E2E_GATE_PASS);
		e2e_json_free(kr);
	}
	/* two peers of one address: no doubt which */
	{
		E2E_JSON *kr = e2e_keyring_new();
		PEER p = { .kr = kr };

		enable(&p, "@" BOB, "normal");
		tie_peer(kr, "a9", "bob", BOB, NULL);
		tie_peer(kr, "f2", "BOB", BOB, NULL);
		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob :x", cache_resolve, NULL), ==,
		                E2E_GATE_PASS);
		/* a tie below the newest does not count, whichever comes
		   first (the newest is the last key here) */
		tie_peer(kr, "c3", "bob", "h3@x", NULL);
		tie_peer(kr, "c4", "bob", "h4@x", NULL);
		e2e_json_set_int(path_get(kr, "peers", "f2", NULL), "last_seen", 3);
		g_assert_cmpint(guard_msg(kr, &cm_rfc1459, "PRIVMSG bob :x", cache_resolve, NULL), ==,
		                E2E_GATE_PASS);
		e2e_json_free(kr);
	}
}

/* the dry gate decides without touching the keyring: no key is made, no
   rotation done, no old row taken over (the /unload e2e check of queued
   lines) */
static void test_gate_dry(void)
{
	E2E_GATE_RESULT g;
	PEER a;
	char *before, *after;

	peer_init(&a, "alice", ALICE, -1);
	/* an old context on the only network, no key of ours yet */
	enable(&a, "#old", "normal");
	/* a context of this network whose key is due to rotate */
	enable(&a, "E2E\x1f#new", "normal");
	e2e_json_set(e2e_json_object_member(a.kr, "outgoing"), "E2E\x1f#new", e2e_json_new(E2E_JSON_OBJECT));
	e2e_json_set_string(path_get(a.kr, "outgoing", "E2E\x1f#new", NULL), "sk", LEGACY_BOB_SK);
	e2e_json_set_int(path_get(a.kr, "outgoing", "E2E\x1f#new", NULL), "created_at", 1);
	e2e_json_set_int(path_get(a.kr, "outgoing", "E2E\x1f#new", NULL), "pending_rotation", 1);
	before = e2e_keyring_snapshot(a.kr);

	e2e_gate_decide_dry(a.kr, TRUE, &net_one, "#old", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_cmpstr(g.ctx, ==, "E2E\x1f#old");
	g_assert_null(g.wires);
	g_assert_false(g.save_needed);
	e2e_gate_result_clear(&g);
	e2e_gate_decide_dry(a.kr, TRUE, &net_one, "#new", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_null(g.wires);
	g_assert_null(g.notices);
	e2e_gate_result_clear(&g);
	e2e_gate_decide_dry(a.kr, TRUE, &net_one, "#new", "!seen x", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_BYPASS);
	e2e_gate_result_clear(&g);
	e2e_gate_decide_dry(a.kr, TRUE, &net_one, "#plain", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	e2e_gate_decide_dry(a.kr, FALSE, &net_one, "#plain", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	after = e2e_keyring_snapshot(a.kr);
	g_assert_cmpstr(before, ==, after);
	g_free(after);

	/* the real gate would have changed it (the check above can fail) */
	e2e_gate_decide(a.kr, TRUE, NULL, &net_one, "#old", "hello", no_resolve, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	e2e_gate_result_clear(&g);
	after = e2e_keyring_snapshot(a.kr);
	g_assert_cmpstr(before, !=, after);
	g_free(after);
	g_free(before);
	peer_free(&a);
}

/* the attack: y's KEYREQ for x's query, stored as
   rpe2e.pl stored it, must never get y our key for x */
static void test_accept_foreign_dm(void)
{
	PEER me, y, x;
	E2E_JSON *row;
	E2E_HANDSHAKE *req;
	unsigned char key[32];
	gboolean rot, gen;
	char *req_ctcp, *body, *b, *rsp = NULL, *recip = NULL, *error = NULL, *before, *after;
	guint n = 99;

	peer_init(&me, "me", "me@m", -1);
	peer_init(&y, "y", "y@hy", -1);
	peer_init(&x, "x", "x@hx", -1);
	enable(&me, "@x@hx", "normal");
	g_assert_true(e2e_outgoing_key(me.kr, "@x@hx", key, &rot, &gen));
	enable(&y, "@x@hx", "normal");
	req_ctcp = e2e_build_keyreq(y.kr, &y.id, "@x@hx", "me@m", &error);
	g_assert_nonnull(req_ctcp);
	body = unframe(req_ctcp);

	/* the native module never stores it */
	deliver_keyreq_on(&me, &y, &net_one, req_ctcp, "me@m", &rsp, &recip);
	g_assert_null(rsp);
	g_assert_null(path_get(me.kr, "pending_inbound", "y@hy|@x@hx", NULL));

	/* stored the way rpe2e.pl did */
	req = e2e_handshake_parse(body, E2E_HS_KEYREQ);
	row = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(row, "handle", "y@hy");
	e2e_json_set_string(row, "channel", "@x@hx");
	e2e_json_set_string(row, "sender_handle", "y@hy");
	e2e_json_set_string(row, "sender_nick", "x");
	b = e2e_b64_encode(req->pub, 32);
	e2e_json_set_string(row, "pubkey", b);
	g_free(b);
	b = e2e_b64_encode(req->eph, 32);
	e2e_json_set_string(row, "eph_x25519", b);
	g_free(b);
	e2e_json_set_int(row, "received_at", 1);
	e2e_json_set(e2e_json_object_member(me.kr, "pending_inbound"), "y@hy|@x@hx", row);
	e2e_handshake_free(req);

	/* never picked, by no argument nor by the nick it came under */
	g_assert_null(e2e_pending_handle(me.kr, "@x@hx", NULL, E2E_CASEMAP_RFC1459, &n));
	g_assert_cmpuint(n, ==, 0);
	g_assert_null(e2e_pending_handle(me.kr, "@x@hx", "X", E2E_CASEMAP_RFC1459, &n));
	/* and refused when named directly */
	before = e2e_keyring_snapshot(me.kr);
	g_assert_cmpint(e2e_accept(me.kr, &me.id, "y@hy", "@x@hx", "@me@m", "me@m", &rsp, &recip), ==,
	                E2E_ACCEPT_NOT_PEER);
	g_assert_null(rsp);
	g_assert_null(recip);
	/* the session path too: y's key under x's query is not trusted */
	set_trusted_session(me.kr, "y@hy|@x@hx", LEGACY_BOB_SK);
	g_free(before);
	before = e2e_keyring_snapshot(me.kr);
	g_assert_cmpint(e2e_accept(me.kr, &me.id, "y@hy", "@x@hx", "@me@m", "me@m", &rsp, &recip), ==,
	                E2E_ACCEPT_NOT_PEER);
	/* an old bare-nick context is no address of anybody */
	g_assert_cmpint(e2e_accept(me.kr, &me.id, "y@hy", "E2E\x1fy", NULL, "me@m", &rsp, &recip), ==,
	                E2E_ACCEPT_NOT_PEER);
	after = e2e_keyring_snapshot(me.kr);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	g_free(before);

	/* x's own request is answered (the refusal above can fail) */
	g_free(req_ctcp);
	enable(&x, "@x@hx", "normal");
	req_ctcp = e2e_build_keyreq(x.kr, &x.id, "@x@hx", "me@m", &error);
	deliver_keyreq_on(&me, &x, &net_one, req_ctcp, "me@m", &rsp, &recip);
	g_assert_nonnull(path_get(me.kr, "pending_inbound", "x@hx|@x@hx", NULL));
	g_assert_cmpstr(e2e_pending_handle(me.kr, "@x@hx", NULL, E2E_CASEMAP_RFC1459, &n), ==, "x@hx");
	g_assert_cmpint(e2e_accept(me.kr, &me.id, "x@hx", "@x@hx", "@me@m", "me@m", &rsp, &recip), ==,
	                E2E_ACCEPT_SENT);
	g_assert_nonnull(rsp);
	g_free(rsp);
	g_free(recip);
	g_free(req_ctcp);
	g_free(body);
	g_free(error);
	peer_free(&me);
	peer_free(&y);
	peer_free(&x);
}

static char *put_peer(E2E_JSON *kr, const PEER *p)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
	char *fp = e2e_fingerprint_hex(p->id.pk), *b = e2e_b64_encode(p->id.pk, 32);

	e2e_json_set_string(row, "pk", b);
	e2e_json_set_string(row, "last_handle", p->handle);
	e2e_json_set_string(row, "last_nick", p->nick);
	e2e_json_set_string(row, "status", "trusted");
	e2e_json_set_int(row, "last_seen", 1);
	e2e_json_set(e2e_json_object_member(kr, "peers"), fp, row);
	g_free(b);
	return fp;
}

/* a recipient of our key for ctx, accepted there (as /e2e accept left it) */
static void put_accepted_recipient(E2E_JSON *kr, const char *handle, const char *ctx, const char *fp)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
	char *k = g_strconcat(handle, "|", ctx, NULL);

	e2e_json_set_string(row, "fp", fp);
	e2e_json_set_string(row, "by", "accept");
	e2e_json_set_int(row, "accepted_at", 1);
	e2e_json_set(e2e_json_object_member(kr, "accepted"), k, row);
	g_free(k);
	row = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(row, "channel", ctx);
	e2e_json_set_string(row, "handle", handle);
	e2e_json_set_string(row, "fingerprint", fp);
	e2e_json_set_int(row, "first_sent_at", 1);
	k = g_strconcat(ctx, "|", handle, NULL);
	e2e_json_set(e2e_json_object_member(kr, "outgoing_recipients"), k, row);
	g_free(k);
}

/* an "accepted" y|@x an earlier bug already wrote gets nothing
   more: no REKEY of x's query, no KEYRSP; told as damaged; dropped by
   /e2e decline (our key for x then rotates) */
static void test_damaged_dm_rows(void)
{
#define DCTX "E2E\x1f@x@hx"
	PEER me, x, y;
	E2E_GATE_RESULT g;
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	unsigned char key[32], eph[32];
	gboolean rot, gen, rotated, damaged_told = FALSE;
	char *fpx, *fpy, *rsp;
	guint i;

	peer_init(&me, "me", "me@m", -1);
	peer_init(&x, "x", "x@hx", -1);
	peer_init(&y, "y", "y@hy", -1);
	g_hash_table_insert(nicks, (char *) "x", (char *) "x@hx");
	enable(&me, DCTX, "normal");
	g_assert_true(e2e_outgoing_key(me.kr, DCTX, key, &rot, &gen));
	e2e_json_set_int(path_get(me.kr, "outgoing", DCTX, NULL), "pending_rotation", 1);
	fpx = put_peer(me.kr, &x);
	fpy = put_peer(me.kr, &y);
	put_accepted_recipient(me.kr, "x@hx", DCTX, fpx);
	put_accepted_recipient(me.kr, "y@hy", DCTX, fpy);
	set_trusted_session(me.kr, "y@hy|" DCTX, LEGACY_BOB_SK);

	g_assert_true(e2e_dm_row_foreign("y@hy", DCTX));
	g_assert_true(e2e_dm_row_foreign("y@hy", "@x@hx"));
	g_assert_true(e2e_dm_row_foreign("y@hy", "E2E\x1f" "x"));
	g_assert_false(e2e_dm_row_foreign("x@hx", DCTX));
	g_assert_false(e2e_dm_row_foreign("y@hy", "E2E\x1f#test"));
	g_assert_false(e2e_peer_accepted(me.kr, "y@hy", DCTX, fpy));
	g_assert_true(e2e_peer_accepted(me.kr, "x@hx", DCTX, fpx));
	/* /e2e list -all: y's only acceptance is the damaged row - y is not
	   shown as accepted ([trusted]) for it; x is */
	g_assert_false(e2e_fingerprint_accepted(me.kr, fpy));
	g_assert_true(e2e_fingerprint_accepted(me.kr, fpx));
	/* accepted only in a channel: an acceptance (channels are never
	   damaged rows) */
	put_accepted_recipient(me.kr, "z@hz", "E2E\x1f#chan", "ab" "ab" "ab" "ab");
	g_assert_true(e2e_fingerprint_accepted(me.kr, "ab" "ab" "ab" "ab"));
	e2e_forget_acceptance(me.kr, "z@hz", "E2E\x1f#chan");
	e2e_json_remove(e2e_json_get(me.kr, "outgoing_recipients"), "E2E\x1f#chan|z@hz");
	g_assert_false(e2e_fingerprint_accepted(me.kr, "ab" "ab" "ab" "ab"));

	/* the rotation reaches x only, and says why not y */
	e2e_gate_decide(me.kr, TRUE, &me.id, &net_a, "x", "hello", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_nonnull(g.notices);
	g_assert_cmpuint(g.notices->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(g.notices, 0), ==, "x");
	for (i = 0; g.warnings != NULL && i < g.warnings->len; i++)
		if (strstr(g_ptr_array_index(g.warnings, i), "damaged") != NULL &&
		    strstr(g_ptr_array_index(g.warnings, i), "y@hy") != NULL &&
		    strstr(g_ptr_array_index(g.warnings, i), "/e2e decline") != NULL)
			damaged_told = TRUE;
	g_assert_true(damaged_told);
	e2e_gate_result_clear(&g);
	/* told once: no longer a recipient */
	g_assert_null(path_get(me.kr, "outgoing_recipients", DCTX "|y@hy", NULL));
	g_assert_nonnull(path_get(me.kr, "outgoing_recipients", DCTX "|x@hx", NULL));
	e2e_json_set_int(path_get(me.kr, "outgoing", DCTX, NULL), "pending_rotation", 1);
	e2e_gate_decide(me.kr, TRUE, &me.id, &net_a, "x", "again", table_resolve, nicks, &g);
	for (i = 0; g.warnings != NULL && i < g.warnings->len; i++)
		g_assert_null(strstr(g_ptr_array_index(g.warnings, i), "y@hy"));
	g_assert_cmpuint(g.notices->len, ==, 2);
	e2e_gate_result_clear(&g);

	/* never a KEYRSP to y for x's query */
	memset(eph, 0x42, sizeof(eph));
	rsp = e2e_build_keyrsp_for_req(me.kr, &me.id, DCTX, "y@hy", y.id.pk, eph);
	g_assert_null(rsp);
	g_assert_null(path_get(me.kr, "outgoing_recipients", DCTX "|y@hy", NULL));

	/* /e2e decline: y's damaged rows go, x's stay, our key rotates */
	e2e_json_set_int(path_get(me.kr, "outgoing", DCTX, NULL), "pending_rotation", 0);
	put_accepted_recipient(me.kr, "y@hy", DCTX, fpy);
	g_assert_cmpuint(e2e_drop_damaged_dm(me.kr, DCTX, "z@hz", &rotated), ==, 0);
	g_assert_false(rotated);
	g_assert_cmpuint(e2e_drop_damaged_dm(me.kr, DCTX, "y@hy", &rotated), ==, 3);
	g_assert_true(rotated);
	g_assert_null(path_get(me.kr, "accepted", "y@hy|" DCTX, NULL));
	g_assert_null(path_get(me.kr, "incoming", "y@hy|" DCTX, NULL));
	g_assert_null(path_get(me.kr, "outgoing_recipients", DCTX "|y@hy", NULL));
	g_assert_nonnull(path_get(me.kr, "accepted", "x@hx|" DCTX, NULL));
	g_assert_nonnull(path_get(me.kr, "outgoing_recipients", DCTX "|x@hx", NULL));
	g_assert_cmpint(e2e_json_get_int(path_get(me.kr, "outgoing", DCTX, NULL), "pending_rotation", 0), ==, 1);
	/* NULL: every damaged row of ctx */
	put_accepted_recipient(me.kr, "y@hy", DCTX, fpy);
	put_accepted_recipient(me.kr, "w@hw", DCTX, fpy);
	g_assert_cmpuint(e2e_drop_damaged_dm(me.kr, DCTX, NULL, &rotated), ==, 4);
	g_assert_nonnull(path_get(me.kr, "accepted", "x@hx|" DCTX, NULL));

	g_hash_table_destroy(nicks);
	g_free(fpx);
	g_free(fpy);
	peer_free(&me);
	peer_free(&x);
	peer_free(&y);
#undef DCTX
}

/* y may hold our CURRENT key for x already - the next message to
   x rotates it without /e2e decline, the new key reaches x only; told
   once */
static void test_damaged_dm_auto_rotation(void)
{
#define DCTX "E2E\x1f@x@hx"
	PEER me, x, y;
	E2E_GATE_RESULT g;
	GHashTable *nicks = g_hash_table_new(g_str_hash, g_str_equal);
	unsigned char key[32];
	gboolean rot, gen, told = FALSE;
	char *fpx, *fpy, *sk0;
	GPtrArray *found;
	guint i;

	peer_init(&me, "me", "me@m", -1);
	peer_init(&x, "x", "x@hx", -1);
	peer_init(&y, "y", "y@hy", -1);
	g_hash_table_insert(nicks, (char *) "x", (char *) "x@hx");
	enable(&me, DCTX, "normal");
	g_assert_true(e2e_outgoing_key(me.kr, DCTX, key, &rot, &gen));
	sk0 = g_strdup(path_str(me.kr, "outgoing", DCTX, "sk"));
	fpx = put_peer(me.kr, &x);
	fpy = put_peer(me.kr, &y);
	put_accepted_recipient(me.kr, "x@hx", DCTX, fpx);
	put_accepted_recipient(me.kr, "y@hy", DCTX, fpy);

	/* no /e2e decline, no rotation due: the next message rotates */
	e2e_gate_decide(me.kr, TRUE, &me.id, &net_a, "x", "hello", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_true(g.save_needed);
	g_assert_cmpstr(path_str(me.kr, "outgoing", DCTX, "sk"), !=, sk0);
	g_assert_nonnull(g.notices);
	g_assert_cmpuint(g.notices->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(g.notices, 0), ==, "x");
	for (i = 0; g.warnings != NULL && i < g.warnings->len; i++)
		if (strstr(g_ptr_array_index(g.warnings, i), "y@hy") != NULL &&
		    strstr(g_ptr_array_index(g.warnings, i), "rotates") != NULL)
			told = TRUE;
	g_assert_true(told);
	e2e_gate_result_clear(&g);
	g_assert_nonnull(path_get(me.kr, "accepted", "y@hy|" DCTX, E2E_DAMAGED_MEMBER, NULL));
	g_assert_null(path_get(me.kr, "outgoing_recipients", DCTX "|y@hy", NULL));

	/* once: the next message neither rotates nor tells again */
	g_free(sk0);
	sk0 = g_strdup(path_str(me.kr, "outgoing", DCTX, "sk"));
	e2e_gate_decide(me.kr, TRUE, &me.id, &net_a, "x", "again", table_resolve, nicks, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_CIPHER);
	g_assert_null(g.notices);
	g_assert_null(g.warnings);
	g_assert_cmpstr(path_str(me.kr, "outgoing", DCTX, "sk"), ==, sk0);
	e2e_gate_result_clear(&g);
	found = e2e_rotate_for_damaged(me.kr, NULL);
	g_assert_cmpuint(found->len, ==, 0);
	g_ptr_array_unref(found);

	/* a trusted session of another address under a query with a config
	   (not one under "@<own>", which has none) is found too, by the
	   scan of every context */
	set_trusted_session(me.kr, "z@hz|" DCTX, LEGACY_BOB_SK);
	set_trusted_session(me.kr, "x@hx|E2E\x1f@me@m", LEGACY_BOB_SK);
	found = e2e_rotate_for_damaged(me.kr, NULL);
	g_assert_cmpuint(found->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(found, 0), ==, "z@hz");
	g_assert_cmpstr(g_ptr_array_index(found, 1), ==, DCTX);
	g_ptr_array_unref(found);
	g_assert_true(e2e_json_truthy(path_get(me.kr, "outgoing", DCTX, "pending_rotation", NULL)));
	/* a channel's rows are no query's */
	found = e2e_rotate_for_damaged(me.kr, "E2E\x1f#test");
	g_assert_cmpuint(found->len, ==, 0);
	g_ptr_array_unref(found);

	g_hash_table_destroy(nicks);
	g_free(sk0);
	g_free(fpx);
	g_free(fpy);
	peer_free(&me);
	peer_free(&x);
	peer_free(&y);
#undef DCTX
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	e2e_now = fixed_now;

	g_test_add_func("/e2e/proto/identity", test_identity);
	g_test_add_func("/e2e/proto/handshake-normal", test_handshake_normal_mode);
	g_test_add_func("/e2e/proto/handshake-modes", test_handshake_modes);
	g_test_add_func("/e2e/proto/trust-changes", test_trust_changes);
	g_test_add_func("/e2e/proto/rpe2e-messages", test_rpe2e_messages);
	g_test_add_func("/e2e/proto/gate", test_gate);
	g_test_add_func("/e2e/proto/gate-dry", test_gate_dry);
	g_test_add_func("/e2e/proto/accept-foreign-dm", test_accept_foreign_dm);
	g_test_add_func("/e2e/proto/damaged-dm-rows", test_damaged_dm_rows);
	g_test_add_func("/e2e/proto/damaged-dm-auto-rotation", test_damaged_dm_auto_rotation);
	g_test_add_func("/e2e/proto/incoming", test_incoming);
	g_test_add_func("/e2e/proto/stamps-and-glob", test_stamps_and_glob);
	g_test_add_func("/e2e/proto/export-import", test_export_import);
	g_test_add_func("/e2e/hardening/keyrsp-is-no-acceptance", test_hardening_keyrsp_is_no_acceptance);
	g_test_add_func("/e2e/hardening/rekey-needs-session", test_hardening_rekey_needs_session);
	g_test_add_func("/e2e/hardening/multiline-buffer", test_hardening_multiline_buffer);
	g_test_add_func("/e2e/hardening/dm-handle-changed", test_hardening_dm_handle_changed);
	g_test_add_func("/e2e/hardening/no-key-no-trust", test_hardening_no_key_no_trust);
	g_test_add_func("/e2e/hardening/control-characters", test_hardening_control_characters);
	g_test_add_func("/e2e/hardening/keyrsp-binding", test_hardening_keyrsp_binding);
	g_test_add_func("/e2e/hardening/rekey-replay", test_hardening_rekey_replay);
	g_test_add_func("/e2e/hardening/limits", test_hardening_limits);
	g_test_add_func("/e2e/hardening/robustness", test_hardening_robustness);
	g_test_add_func("/e2e/hardening/unresolved-nick", test_hardening_unresolved_nick);
	g_test_add_func("/e2e/hardening/rekey-replay-after-many", test_hardening_rekey_replay_after_many);
	g_test_add_func("/e2e/hardening/growth-from-wires", test_hardening_growth_from_wires);
	g_test_add_func("/e2e/hardening/rekey-to-accepted-only", test_hardening_rekey_to_accepted_only);
	g_test_add_func("/e2e/hardening/queued-buffer-privmsg", test_hardening_queued_buffer_privmsg);
	g_test_add_func("/e2e/hardening/query-off-this-address", test_hardening_query_off_this_address);
	g_test_add_func("/e2e/scope/two-networks-separate", test_scope_two_networks);
	g_test_add_func("/e2e/scope/wire-fields-bare", test_scope_wire_bare);
	g_test_add_func("/e2e/scope/rpe2e-vector-scoped", test_scope_rpe2e_vector);
	g_test_add_func("/e2e/scope/inbound-keyreq-stored-scoped", test_scope_inbound_keyreq);
	g_test_add_func("/e2e/scope/dm-sender-rule-bare", test_scope_dm_sender_rule);
	g_test_add_func("/e2e/scope/loops-on-net", test_scope_loops_on_net);
	g_test_add_func("/e2e/scope/rekey-dm-config", test_scope_rekey_dm_config);
	g_test_add_func("/e2e/scope/prune-per-network", test_scope_prune_per_network);
	g_test_add_func("/e2e/scope/prune-confused-handle", test_scope_prune_confused_handle);
	g_test_add_func("/e2e/scope/prune-global-cap", test_scope_prune_global_cap);
	g_test_add_func("/e2e/scope/prune-handle-member", test_scope_prune_handle_member);
	g_test_add_func("/e2e/scope/prune-global-fair", test_scope_prune_global_fair);
	g_test_add_func("/e2e/legacy/import-keeps-members", test_legacy_import_keeps_members);
	g_test_add_func("/e2e/legacy/dm-sessions-follow", test_legacy_dm_sessions_follow);
	g_test_add_func("/e2e/legacy/mixed-own-rows", test_legacy_mixed_own_rows);
	g_test_add_func("/e2e/legacy/mixed-keyrsp", test_legacy_mixed_keyrsp);
	g_test_add_func("/e2e/legacy/mixed-alt-reading", test_legacy_mixed_alt_reading);
	g_test_add_func("/e2e/legacy/pending-notice-key", test_legacy_pending_notice_key);
	g_test_add_func("/e2e/legacy/mixed-keyreq", test_legacy_mixed_keyreq);
	g_test_add_func("/e2e/regress/flood-around-b", test_regress_flood_around_b);
	g_test_add_func("/e2e/regress/fair-share", test_regress_fair_share);
	g_test_add_func("/e2e/regress/export-import", test_regress_export_import);
	g_test_add_func("/e2e/regress/second-network", test_regress_second_network);
	g_test_add_func("/e2e/regress/inbound-never-adopts", test_regress_inbound_never_adopts);
	g_test_add_func("/e2e/regress/replay-across-adoption", test_regress_replay_across_adoption);
	g_test_add_func("/e2e/regress/cross-network-stranger", test_regress_cross_network_stranger);
	g_test_add_func("/e2e/legacy/dm-reciprocal-in-place", test_legacy_dm_reciprocal_in_place);
	g_test_add_func("/e2e/legacy/stranger-does-not-adopt", test_legacy_stranger_does_not_adopt);
	g_test_add_func("/e2e/legacy/adopted-dm-elsewhere", test_legacy_adopted_dm_elsewhere);
	g_test_add_func("/e2e/legacy/blocked-decrypt-paths", test_legacy_blocked_decrypt_paths);
	g_test_add_func("/e2e/scope/separator-in-name", test_scope_separator_in_name);
	g_test_add_func("/e2e/legacy/single-network-adopts", test_legacy_single_network);
	g_test_add_func("/e2e/legacy/multi-network-refuses", test_legacy_multi_network);
	g_test_add_func("/e2e/legacy/scoped-wins", test_legacy_scoped_wins);
	g_test_add_func("/e2e/casemap/gate-unicode-refuses", test_casemap_gate_unicode_refuses);
	g_test_add_func("/e2e/casemap/ascii-brackets-pass", test_casemap_ascii_brackets_pass);
	g_test_add_func("/e2e/casemap/other-network-ignored", test_casemap_other_network_ignored);
	g_test_add_func("/e2e/casemap/not-utf8-refused", test_casemap_not_utf8_refused);
	g_test_add_func("/e2e/casemap/decrypt-folded-target", test_casemap_decrypt_folded_target);
	g_test_add_func("/e2e/casemap/superset", test_casemap_superset);
	g_test_add_func("/e2e/casemap/dm-nick", test_casemap_dm_nick);
	g_test_add_func("/e2e/casemap/decrypt-marked", test_casemap_decrypt_marked);
	g_test_add_func("/e2e/casemap/latin1-bytes", test_casemap_latin1_bytes);
	g_test_add_func("/e2e/casemap/unknown-codepage", test_casemap_unknown_codepage);
	g_test_add_func("/e2e/casemap/before-005", test_casemap_before_005);
	g_test_add_func("/e2e/casemap/undecided-channels-only", test_casemap_undecided_channels_only);
	g_test_add_func("/e2e/casemap/off-exact", test_casemap_off_exact);
	g_test_add_func("/e2e/casemap/unassigned", test_casemap_unassigned);
	g_test_add_func("/e2e/casemap/guard-bare-rows", test_casemap_guard_bare_rows);
	g_test_add_func("/e2e/casemap/ircnet-short-name", test_casemap_ircnet_short_name);
	g_test_add_func("/e2e/casemap/list-dm-pieces", test_casemap_list_dm_pieces);
	g_test_add_func("/e2e/casemap/list-dm-bypass", test_casemap_list_dm_bypass);
	g_test_add_func("/e2e/casemap/list-bare-nick", test_casemap_list_bare_nick);
	g_test_add_func("/e2e/casemap/single-bare-nick", test_casemap_single_bare_nick);
	g_test_add_func("/e2e/casemap/script-single-nick", test_casemap_script_single_nick);
	g_test_add_func("/e2e/casemap/script-nonascii-cache", test_casemap_script_nonascii_cache);
	g_test_add_func("/e2e/casemap/blocked-folded-peer", test_casemap_blocked_folded_peer);
	g_test_add_func("/e2e/casemap/script-dm-moved", test_casemap_script_dm_moved);
	g_test_add_func("/e2e/proto/cache-tie-order", test_cache_tie_order);
	g_test_add_func("/e2e/casemap/script-cache-tie", test_casemap_script_cache_tie);

	return g_test_run();
}
