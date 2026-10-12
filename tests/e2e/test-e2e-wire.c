/*
 test-e2e-wire.c : RPE2E wire format, chunking and CTCP handshake messages

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The messages in rpe2e-vectors.h were produced by rpe2e.pl (libsodium), so
 decrypting and verifying them here shows the native code reads what the
 script and repartee send. The AAD is built here by hand from the RPE2E
 v1.0 layout as an independent oracle.
*/

#include <glib.h>
#include <string.h>

#include <irssi/src/e2e/e2e-crypto.h>
#include <irssi/src/e2e/e2e-wire.h>

#include "rpe2e-vectors.h"

static void unhex(const char *hex, unsigned char *out, gsize len)
{
	g_assert_true(e2e_hex_decode(hex, out, len));
}

/* RPE2E v1.0 AAD, written out independently of e2e_aad() */
static GByteArray *aad_oracle(const char *ctx, const unsigned char msgid[8], gint64 ts,
                              int part, int total)
{
	GByteArray *a = g_byte_array_new();
	guint8 b[8];
	int i;

	g_byte_array_append(a, (const guint8 *) "RPE2E01", 7);
	b[0] = strlen(ctx) >> 8;
	b[1] = strlen(ctx) & 0xff;
	g_byte_array_append(a, b, 2);
	g_byte_array_append(a, (const guint8 *) ctx, strlen(ctx));
	b[0] = 0;
	b[1] = 8;
	g_byte_array_append(a, b, 2);
	g_byte_array_append(a, msgid, 8);
	g_byte_array_append(a, b, 2);
	for (i = 0; i < 8; i++)
		b[i] = (guint64) ts >> (56 - 8 * i);
	g_byte_array_append(a, b, 8);
	b[0] = 0;
	b[1] = 1;
	b[2] = part;
	g_byte_array_append(a, b, 3);
	b[2] = total;
	g_byte_array_append(a, b, 3);
	return a;
}

static void test_base64(void)
{
	static const struct { const char *raw, *std, *url; } v[] = {
		{ "", "", "" },
		{ "f", "Zg==", "Zg" },
		{ "fo", "Zm8=", "Zm8" },
		{ "foo", "Zm9v", "Zm9v" },
		{ "\xfb\xff\xbf", "+/+/", "-_-_" },
	};
	gsize i, len;
	unsigned char *out;
	char *enc;

	for (i = 0; i < G_N_ELEMENTS(v); i++) {
		enc = e2e_b64_encode((const unsigned char *) v[i].raw, strlen(v[i].raw));
		g_assert_cmpstr(enc, ==, v[i].std);
		g_free(enc);
		enc = e2e_b64url_encode((const unsigned char *) v[i].raw, strlen(v[i].raw));
		g_assert_cmpstr(enc, ==, v[i].url);
		g_free(enc);
		out = e2e_b64_decode(v[i].std, &len);
		g_assert_nonnull(out);
		g_assert_cmpmem(out, len, v[i].raw, strlen(v[i].raw));
		g_free(out);
		out = e2e_b64url_decode(v[i].url, &len);
		g_assert_nonnull(out);
		g_assert_cmpmem(out, len, v[i].raw, strlen(v[i].raw));
		g_free(out);
	}
	/* strict, like repartee's base64 engines */
	g_assert_null(e2e_b64_decode("Zg", &len));	/* padding required */
	g_assert_null(e2e_b64_decode("Zh==", &len));	/* non-zero trailing bits */
	g_assert_null(e2e_b64_decode("Zm9v\n", &len));
	g_assert_null(e2e_b64_decode("Zm-v", &len));	/* url alphabet */
	g_assert_null(e2e_b64_decode("Z===", &len));
	g_assert_null(e2e_b64url_decode("Zg==", &len));	/* no padding */
	g_assert_null(e2e_b64url_decode("Z", &len));
	g_assert_null(e2e_b64url_decode("Zm+v", &len));	/* std alphabet */
}

static void test_wire_vectors(void)
{
	gsize i, j;

	for (i = 0; i < G_N_ELEMENTS(vec_wire); i++) {
		unsigned char key[32];
		GString *joined = g_string_new(NULL);

		unhex(vec_wire[i].key_hex, key, 32);
		for (j = 0; vec_wire[i].wire[j] != NULL; j++) {
			E2E_WIRE *w = e2e_wire_parse(vec_wire[i].wire[j]);
			GByteArray *aad, *oracle;
			char *pt, *again;
			gsize ptlen;

			g_assert_nonnull(w);
			g_assert_cmpint(w->part, ==, j + 1);
			/* the AAD the native code builds is the RPE2E one */
			aad = e2e_aad(vec_wire[i].ctx, w->msgid, w->ts, w->part, w->total);
			oracle = aad_oracle(vec_wire[i].ctx, w->msgid, w->ts, w->part, w->total);
			g_assert_cmpmem(aad->data, aad->len, oracle->data, oracle->len);
			g_byte_array_unref(aad);
			g_byte_array_unref(oracle);

			pt = e2e_wire_decrypt(w, key, vec_wire[i].ctx, &ptlen);
			g_assert_nonnull(pt);
			g_string_append_len(joined, pt, ptlen);
			g_free(pt);
			/* another context (AAD) or key does not open it */
			g_assert_null(e2e_wire_decrypt(w, key, "#other", &ptlen));
			key[0] ^= 1;
			g_assert_null(e2e_wire_decrypt(w, key, vec_wire[i].ctx, &ptlen));
			key[0] ^= 1;

			/* encoding the parsed message gives the same line back */
			again = e2e_wire_encode(w);
			g_assert_cmpstr(again, ==, vec_wire[i].wire[j]);
			g_free(again);
			e2e_wire_free(w);
		}
		g_assert_cmpstr(joined->str, ==, vec_wire[i].plain);
		g_string_free(joined, TRUE);
	}
}

static void test_wire_parse_rejects(void)
{
	static const char *const bad[] = {
		"",
		"RPE2E01 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01",
		/* msgid not 16 hex digits */
		"+RPE2E01 00112233445566 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 00112233445566zz 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		/* timestamp */
		"+RPE2E01 0011223344556677 -1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1x 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		/* part / total */
		"+RPE2E01 0011223344556677 1 0/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1 2/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1 1/0 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1 1/17 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1 1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		/* nonce must be 24 bytes, a colon, base64 */
		"+RPE2E01 0011223344556677 1 1/1 AAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",
		"+RPE2E01 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:",
		"+RPE2E01 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AA*A",
		"+RPE2E01 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAA:AAAA",
		/* trailing fields */
		"+RPE2E01 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA== x",
		"+RPE2E01x 0011223344556677 1 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==",
		/* +RPE2E01 with a secret, as typed by a user: not a wire */
		"+RPE2E01 secret",
	};
	E2E_WIRE *w;
	gsize i;

	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		w = e2e_wire_parse(bad[i]);
		if (w != NULL)
			g_error("parsed: %s", bad[i]);
	}
	/* the accepted forms: tabs and trailing white space like rpe2e.pl's \s */
	w = e2e_wire_parse("+RPE2E01\t0011223344556677  1 16/16 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==  ");
	g_assert_nonnull(w);
	g_assert_cmpint(w->ts, ==, 1);
	g_assert_cmpint(w->part, ==, 16);
	g_assert_cmpuint(w->ctlen, ==, 16);
	e2e_wire_free(w);
	/* a timestamp past 64 bits is still a wire message (dropped later as
	   out of tolerance, as rpe2e.pl does), not plain text to show */
	w = e2e_wire_parse("+RPE2E01 0011223344556677 99999999999999999999999 1/1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:AAAAAAAAAAAAAAAAAAAAAA==");
	g_assert_nonnull(w);
	g_assert_cmpint(w->ts, ==, G_MAXINT64);
	e2e_wire_free(w);
}

static void test_roundtrip(void)
{
	unsigned char key[32];
	const char *error = NULL;
	GPtrArray *lines;
	GString *joined = g_string_new(NULL);
	char *text, *pt;
	gsize i, len;

	memset(key, 0x5a, sizeof(key));
	text = g_strnfill(500, 'x');
	lines = e2e_encrypt_plain(key, "#test", text, 1791564129, &error);
	g_assert_nonnull(lines);
	g_assert_cmpuint(lines->len, ==, 3);
	for (i = 0; i < lines->len; i++) {
		E2E_WIRE *w = e2e_wire_parse(g_ptr_array_index(lines, i));

		g_assert_nonnull(w);
		g_assert_cmpint(w->total, ==, 3);
		g_assert_cmpint(w->ts, ==, 1791564129);
		pt = e2e_wire_decrypt(w, key, "#test", &len);
		g_assert_nonnull(pt);
		g_string_append_len(joined, pt, len);
		g_free(pt);
		e2e_wire_free(w);
	}
	g_assert_cmpstr(joined->str, ==, text);
	g_ptr_array_unref(lines);
	g_string_free(joined, TRUE);
	g_free(text);
}

static GPtrArray *split(const char *text, gsize budget)
{
	const char *error = NULL;

	return e2e_split_plaintext(text, budget, &error);
}

static void test_chunker(void)
{
	GPtrArray *c;
	GString *s;
	char *a;
	int i;

	a = g_strnfill(180, 'a');
	c = split(a, 180);
	g_assert_cmpuint(c->len, ==, 1);
	g_ptr_array_unref(c);
	g_free(a);

	a = g_strnfill(181, 'a');
	c = split(a, 180);
	g_assert_cmpuint(c->len, ==, 2);
	g_assert_cmpuint(strlen(g_ptr_array_index(c, 1)), ==, 1);
	g_ptr_array_unref(c);
	g_free(a);

	/* "ż" (2 bytes) is never split: 179 x 'a' + "ż" = 179 + 2 */
	s = g_string_new(NULL);
	for (i = 0; i < 179; i++)
		g_string_append_c(s, 'a');
	g_string_append(s, "\xc5\xbc");
	c = split(s->str, 180);
	g_assert_cmpuint(c->len, ==, 2);
	g_assert_cmpuint(strlen(g_ptr_array_index(c, 0)), ==, 179);
	g_assert_cmpstr(g_ptr_array_index(c, 1), ==, "\xc5\xbc");
	g_ptr_array_unref(c);
	g_string_free(s, TRUE);

	/* the byte budget counts UTF-8 bytes */
	s = g_string_new(NULL);
	for (i = 0; i < 90; i++)
		g_string_append(s, "\xc5\xbc");
	c = split(s->str, 180);
	g_assert_cmpuint(c->len, ==, 1);
	g_ptr_array_unref(c);
	g_string_free(s, TRUE);

	/* MAX_CHUNKS: 16 x 180 bytes fit, one more byte does not */
	a = g_strnfill(16 * 180, 'a');
	c = split(a, 180);
	g_assert_cmpuint(c->len, ==, 16);
	g_ptr_array_unref(c);
	g_free(a);
	a = g_strnfill(16 * 180 + 1, 'a');
	g_assert_null(split(a, 180));
	g_free(a);

	g_assert_null(split("", 180));
	g_assert_null(split("\xc5\xbc", 1));
}

static void test_ctcp_action(void)
{
	unsigned char key[32];
	const char *error = NULL;
	GPtrArray *lines;
	GString *body = g_string_new(NULL), *joined = g_string_new(NULL);
	char *frame, *pt;
	gsize i, len;

	memset(key, 1, sizeof(key));
	/* a short ACTION is one chunk with the whole frame */
	lines = e2e_encrypt_ctcp(key, "#test", "\001ACTION waves\001", 1, &error);
	g_assert_cmpuint(lines->len, ==, 1);
	g_ptr_array_unref(lines);

	/* a long one is split into independent ACTION frames, never inside a
	   UTF-8 character */
	for (i = 0; i < 120; i++)
		g_string_append(body, "\xc5\xbc");
	frame = g_strdup_printf("\001ACTION %s\001", body->str);
	lines = e2e_encrypt_ctcp(key, "#test", frame, 1, &error);
	g_assert_nonnull(lines);
	g_assert_cmpuint(lines->len, ==, 2);
	for (i = 0; i < lines->len; i++) {
		E2E_WIRE *w = e2e_wire_parse(g_ptr_array_index(lines, i));

		g_assert_cmpint(w->total, ==, 1);
		pt = e2e_wire_decrypt(w, key, "#test", &len);
		g_assert_true(len > 9 && strncmp(pt, "\001ACTION ", 8) == 0 && pt[len - 1] == 1);
		g_assert_true(g_utf8_validate(pt + 8, len - 9, NULL));
		g_string_append_len(joined, pt + 8, len - 9);
		g_free(pt);
		e2e_wire_free(w);
	}
	g_assert_cmpstr(joined->str, ==, body->str);
	g_ptr_array_unref(lines);
	g_free(frame);
	/* any other long CTCP cannot be split */
	frame = g_strdup_printf("\001VERSION %s\001", body->str);
	g_assert_null(e2e_encrypt_ctcp(key, "#test", frame, 1, &error));
	g_free(frame);
	g_string_free(body, TRUE);
	g_string_free(joined, TRUE);
}

static void test_handshake_vectors(void)
{
	E2E_HANDSHAKE *req, *rsp, *rk;
	unsigned char bob_pk[32], seed[32], pk[32], sk[64], eph_sk[32], x[32], shared[32];
	unsigned char wrap[32], key[32], *raw;
	char *info, *again;
	gsize len;

	unhex(vec_bob_pk_hex, bob_pk, 32);

	/* KEYREQ from rpe2e.pl: verifies, and its ephemeral key matches the
	   secret rpe2e.pl kept in the keyring */
	g_assert_cmpint(e2e_handshake_type(vec_keyreq), ==, E2E_HS_KEYREQ);
	req = e2e_handshake_parse(vec_keyreq, E2E_HS_KEYREQ);
	g_assert_nonnull(req);
	g_assert_cmpstr(req->channel, ==, "#test");
	g_assert_cmpmem(req->pub, 32, bob_pk, 32);
	g_assert_true(e2e_handshake_verify(req));
	raw = e2e_b64_decode(vec_keyreq_eph_sk_b64, &len);
	g_assert_cmpuint(len, ==, 32);
	g_assert_true(e2e_x25519_public(x, raw));
	g_assert_cmpmem(x, 32, req->eph, 32);
	g_free(raw);
	again = e2e_handshake_encode(req);
	g_assert_cmpstr(again, ==, vec_keyreq);
	g_free(again);
	req->channel[1] = 'b';	/* the channel is signed */
	g_assert_false(e2e_handshake_verify(req));
	e2e_handshake_free(req);

	/* KEYRSP from rpe2e.pl answering alice: unwraps to bob's #test key */
	g_assert_cmpint(e2e_handshake_type(vec_keyrsp), ==, E2E_HS_KEYRSP);
	rsp = e2e_handshake_parse(vec_keyrsp, E2E_HS_KEYRSP);
	g_assert_nonnull(rsp);
	g_assert_true(e2e_handshake_verify(rsp));
	unhex(vec_alice_eph_sk_hex, eph_sk, 32);
	g_assert_true(e2e_x25519(shared, eph_sk, rsp->eph));
	info = g_strdup("RPE2E01-WRAP:#test");
	g_assert_true(e2e_wrap_key(wrap, shared, info, strlen(info)));
	g_assert_cmpuint(rsp->wrap_ctlen, ==, 48);
	g_assert_true(e2e_xchacha_decrypt(key, rsp->wrap_ct, rsp->wrap_ctlen,
	                                  (unsigned char *) info, strlen(info),
	                                  rsp->wrap_nonce, wrap));
	g_free(info);
	memset(x, 0x11, 32);
	g_assert_cmpmem(key, 32, x, 32);
	again = e2e_handshake_encode(rsp);
	g_assert_cmpstr(again, ==, vec_keyrsp);
	g_free(again);
	e2e_handshake_free(rsp);

	/* REKEY from rpe2e.pl to alice: alice opens it with the X25519 form
	   of her Ed25519 identity */
	g_assert_cmpint(e2e_handshake_type(vec_rekey), ==, E2E_HS_REKEY);
	rk = e2e_handshake_parse(vec_rekey, E2E_HS_REKEY);
	g_assert_nonnull(rk);
	g_assert_true(e2e_handshake_verify(rk));
	unhex(vec_alice_seed_hex, seed, 32);
	g_assert_true(e2e_ed25519_seed_keypair(seed, pk, sk));
	g_assert_true(e2e_ed25519_sk_to_x25519(x, sk));
	g_assert_true(e2e_x25519(shared, x, rk->eph));
	info = g_strdup("RPE2E01-REKEY:#test");
	g_assert_true(e2e_wrap_key(wrap, shared, info, strlen(info)));
	g_assert_true(e2e_xchacha_decrypt(key, rk->wrap_ct, rk->wrap_ctlen,
	                                  (unsigned char *) info, strlen(info),
	                                  rk->wrap_nonce, wrap));
	g_free(info);
	unhex(vec_rekey_key_hex, x, 32);
	g_assert_cmpmem(key, 32, x, 32);
	e2e_handshake_free(rk);

	/* alice's KEYREQ (made with rpe2e.pl's helpers) verifies too */
	req = e2e_handshake_parse(vec_alice_keyreq, E2E_HS_KEYREQ);
	g_assert_nonnull(req);
	g_assert_true(e2e_handshake_verify(req));
	g_assert_cmpmem(req->pub, 32, pk, 32);
	e2e_handshake_free(req);
}

static void test_handshake_build(void)
{
	E2E_HANDSHAKE hs, *back;
	unsigned char pk[32], sk[64];
	char *enc;

	g_assert_true(e2e_ed25519_keypair(pk, sk));
	memset(&hs, 0, sizeof(hs));
	hs.type = E2E_HS_KEYRSP;
	hs.channel = (char *) "@alice@127.0.0.1";
	memcpy(hs.pub, pk, 32);
	memset(hs.eph, 3, 32);
	memset(hs.nonce, 4, 16);
	memset(hs.wrap_nonce, 5, 24);
	hs.wrap_ct = (unsigned char *) g_strnfill(48, 6);
	hs.wrap_ctlen = 48;
	g_assert_true(e2e_handshake_sign(&hs, sk));
	enc = e2e_handshake_encode(&hs);
	g_assert_true(g_str_has_prefix(enc, "RPEE2E KEYRSP v=1 c=@alice@127.0.0.1 p="));
	back = e2e_handshake_parse(enc, E2E_HS_KEYRSP);
	g_assert_nonnull(back);
	g_assert_true(e2e_handshake_verify(back));
	g_assert_cmpmem(back->wrap_ct, back->wrap_ctlen, hs.wrap_ct, 48);
	/* a KEYRSP is not a REKEY */
	g_assert_null(e2e_handshake_parse(enc, E2E_HS_REKEY));
	e2e_handshake_free(back);
	g_free(enc);
	g_free(hs.wrap_ct);
}

static void test_handshake_rejects(void)
{
	char *p, *s, *e, *n, *ok;
	unsigned char b[64];

	memset(b, 7, sizeof(b));
	p = e2e_b64url_encode(b, 32);
	e = e2e_b64url_encode(b, 32);
	n = e2e_b64url_encode(b, 16);
	s = e2e_b64url_encode(b, 64);

	ok = g_strdup_printf("RPEE2E KEYREQ v=1 c=#a p=%s e=%s n=%s s=%s", p, e, n, s);
	{
		E2E_HANDSHAKE *hs = e2e_handshake_parse(ok, E2E_HS_KEYREQ);

		g_assert_nonnull(hs);
		e2e_handshake_free(hs);
	}
	g_free(ok);
#define REJECT(fmt, ...) G_STMT_START { \
	char *bad_ = g_strdup_printf(fmt, __VA_ARGS__); \
	if (e2e_handshake_parse(bad_, E2E_HS_KEYREQ) != NULL) \
		g_error("parsed: %s", bad_); \
	g_free(bad_); \
} G_STMT_END
	/* a duplicated field could move a signed request to another channel */
	REJECT("RPEE2E KEYREQ v=1 c=#a c=#b p=%s e=%s n=%s s=%s", p, e, n, s);
	REJECT("RPEE2E KEYREQ v=2 c=#a p=%s e=%s n=%s s=%s", p, e, n, s);
	REJECT("RPEE2E KEYREQ c=#a p=%s e=%s n=%s s=%s", p, e, n, s);
	REJECT("RPEE2E KEYREQ v=1 c= p=%s e=%s n=%s s=%s", p, e, n, s);
	REJECT("RPEE2E KEYREQ v=1 p=%s e=%s n=%s s=%s x", p, e, n, s);
	REJECT("RPEE2E KEYREQ v=1 c=#a p=%s e=%s n=%s s=%s", n, e, n, s);
	REJECT("RPEE2E KEYREQ v=1 c=#a p=%s e=%s n=%s s=%s", p, e, p, s);
	REJECT("RPEE2E KEYREQ v=1 c=#a p=%s= e=%s n=%s s=%s", p, e, n, s);
	REJECT("RPEE2E KEYRSP v=1 c=#a p=%s e=%s n=%s s=%s", p, e, n, s);
	REJECT("XPEE2E KEYREQ v=1 c=#a p=%s e=%s n=%s s=%s", p, e, n, s);
	REJECT("RPEE2E KEYREQ v=1 c=#a p=%s e=%s %s", p, e, n);
#undef REJECT
	g_assert_cmpint(e2e_handshake_type("RPEE2E KEYREQ"), ==, E2E_HS_NONE);
	g_assert_cmpint(e2e_handshake_type("RPEE2E  KEYRSP x"), ==, E2E_HS_KEYRSP);
	g_assert_cmpint(e2e_handshake_type("RPEE2E REKEYX x"), ==, E2E_HS_NONE);
	g_free(p);
	g_free(e);
	g_free(n);
	g_free(s);
}

static void assert_readings(const char *target, const char *const *expected)
{
	char **r = e2e_channel_readings(target);
	guint i;

	g_assert_cmpuint(g_strv_length(r), ==, g_strv_length((char **) expected));
	for (i = 0; expected[i] != NULL; i++)
		g_assert_cmpstr(r[i], ==, expected[i]);
	g_strfreev(r);
}

static void test_readings(void)
{
	const char *const chan[] = { "#chan", NULL };
	const char *const voiced[] = { "#chan", "+#chan", NULL };
	const char *const two[] = { "#c", "+#c", NULL };
	const char *const amp[] = { "&chan", NULL };
	const char *const plus[] = { "+", NULL };
	const char *const none[] = { NULL };

	assert_readings("#chan", chan);
	/* STATUSMSG: a message to the ops of #chan is a message to #chan */
	assert_readings("@#chan", chan);
	/* + and & are both status and channel prefixes: every reading */
	assert_readings("+#chan", voiced);
	assert_readings("@+#c", two);
	assert_readings("&chan", amp);
	assert_readings("+", plus);
	assert_readings("alice", none);
	assert_readings("@", none);
	assert_readings("", none);
}

static void test_privmsg_line(void)
{
	char *tags, *target, *body;

	g_assert_true(e2e_parse_privmsg_line("PRIVMSG #c :hi there", &tags, &target, &body));
	g_assert_cmpstr(tags, ==, "");
	g_assert_cmpstr(target, ==, "#c");
	g_assert_cmpstr(body, ==, "hi there");
	g_free(tags); g_free(target); g_free(body);

	/* /quote in any case, IRCv3 tags kept for the ciphertext */
	g_assert_true(e2e_parse_privmsg_line("privmsg #c :secret", &tags, &target, &body));
	g_assert_cmpstr(body, ==, "secret");
	g_free(tags); g_free(target); g_free(body);
	g_assert_true(e2e_parse_privmsg_line("@+draft/reply=abc PrivMsg #c :secret", &tags, &target, &body));
	g_assert_cmpstr(tags, ==, "@+draft/reply=abc ");
	g_assert_cmpstr(target, ==, "#c");
	g_free(tags); g_free(target); g_free(body);
	/* without the colon, and an empty text */
	g_assert_true(e2e_parse_privmsg_line("PRIVMSG bob hello", &tags, &target, &body));
	g_assert_cmpstr(body, ==, "hello");
	g_free(tags); g_free(target); g_free(body);
	g_assert_true(e2e_parse_privmsg_line("PRIVMSG bob :", &tags, &target, &body));
	g_assert_cmpstr(body, ==, "");
	g_free(tags); g_free(target); g_free(body);

	g_assert_false(e2e_parse_privmsg_line("NOTICE #c :x", &tags, &target, &body));
	g_assert_false(e2e_parse_privmsg_line("PRIVMSG #c", &tags, &target, &body));
	g_assert_false(e2e_parse_privmsg_line("PRIVMSGX #c :x", &tags, &target, &body));
	/* what servers accept as well: leading spaces, our own prefix */
	g_assert_true(e2e_parse_privmsg_line("  :me PRIVMSG #c :x", &tags, &target, &body));
	g_assert_cmpstr(tags, ==, "");
	g_assert_cmpstr(target, ==, "#c");
	g_assert_cmpstr(body, ==, "x");
	g_free(tags); g_free(target); g_free(body);
	g_assert_true(e2e_parse_privmsg_line("@a=b :me!u@h PRIVMSG #c :x", &tags, &target, &body));
	g_assert_cmpstr(tags, ==, "@a=b ");
	g_assert_cmpstr(target, ==, "#c");
	g_free(tags); g_free(target); g_free(body);
	g_assert_false(e2e_parse_privmsg_line(":me", &tags, &target, &body));
}

static void test_bot_command(void)
{
	g_assert_true(e2e_is_bot_command(".op me"));
	g_assert_true(e2e_is_bot_command("!seen bob"));
	g_assert_true(e2e_is_bot_command("!Z"));
	/* ordinary chat stays encrypted */
	g_assert_false(e2e_is_bot_command("...so what"));
	g_assert_false(e2e_is_bot_command("!!"));
	g_assert_false(e2e_is_bot_command(". "));
	g_assert_false(e2e_is_bot_command("!?"));
	g_assert_false(e2e_is_bot_command("!\xc5\xbc"));
	g_assert_false(e2e_is_bot_command("hi"));
	g_assert_false(e2e_is_bot_command(".op\nme"));
}

static void test_utf8_clean(void)
{
	char *s;

	s = e2e_utf8_clean("za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87", 10);
	g_assert_cmpstr(s, ==, "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87");
	g_free(s);
	s = e2e_utf8_clean("a\xc5" "b\xff", 4);
	g_assert_cmpstr(s, ==, "a\xef\xbf\xbd" "b\xef\xbf\xbd");
	g_free(s);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/e2e/wire/base64", test_base64);
	g_test_add_func("/e2e/wire/rpe2e-vectors", test_wire_vectors);
	g_test_add_func("/e2e/wire/parse-rejects", test_wire_parse_rejects);
	g_test_add_func("/e2e/wire/roundtrip", test_roundtrip);
	g_test_add_func("/e2e/wire/chunker", test_chunker);
	g_test_add_func("/e2e/wire/ctcp-action", test_ctcp_action);
	g_test_add_func("/e2e/wire/handshake-vectors", test_handshake_vectors);
	g_test_add_func("/e2e/wire/handshake-build", test_handshake_build);
	g_test_add_func("/e2e/wire/handshake-rejects", test_handshake_rejects);
	g_test_add_func("/e2e/wire/readings", test_readings);
	g_test_add_func("/e2e/wire/privmsg-line", test_privmsg_line);
	g_test_add_func("/e2e/wire/bot-command", test_bot_command);
	g_test_add_func("/e2e/wire/utf8-clean", test_utf8_clean);

	return g_test_run();
}
