/*
 e2e-wire.c : RPE2E v1.0 wire format, chunking and CTCP handshake messages

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 A port of the wire handling of rpe2e.pl 0.2.0 (repartee's
 scripts/irssi/rpe2e.pl; MIT, Copyright (c) 2026 outragedevs,
 contrib/rpe2e/LICENSE) and repartee's src/e2e/{wire,chunker,handshake}.rs. Where the
 two differ in what they accept, the stricter reading is used (base64
 and hex must be canonical, as in repartee): every peer sends canonical
 encodings, so this only refuses forged or damaged lines.

 "White space" is Perl's \s on bytes: space, \t, \n, \v, \f, \r.
*/

#include "e2e-wire.h"
#include "e2e-crypto.h"

#include <string.h>

static gboolean is_ws(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* ---- base64 / hex ---- */

static const char b64_std[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char b64_url[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static char *b64_encode(const unsigned char *data, size_t len, const char *alphabet,
                        gboolean pad)
{
	GString *out = g_string_sized_new((len + 2) / 3 * 4 + 1);
	size_t i;

	for (i = 0; i + 2 < len; i += 3) {
		guint32 v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];

		g_string_append_c(out, alphabet[(v >> 18) & 63]);
		g_string_append_c(out, alphabet[(v >> 12) & 63]);
		g_string_append_c(out, alphabet[(v >> 6) & 63]);
		g_string_append_c(out, alphabet[v & 63]);
	}
	if (len - i == 1) {
		guint32 v = data[i] << 16;

		g_string_append_c(out, alphabet[(v >> 18) & 63]);
		g_string_append_c(out, alphabet[(v >> 12) & 63]);
		if (pad)
			g_string_append(out, "==");
	} else if (len - i == 2) {
		guint32 v = (data[i] << 16) | (data[i + 1] << 8);

		g_string_append_c(out, alphabet[(v >> 18) & 63]);
		g_string_append_c(out, alphabet[(v >> 12) & 63]);
		g_string_append_c(out, alphabet[(v >> 6) & 63]);
		if (pad)
			g_string_append_c(out, '=');
	}
	return g_string_free(out, FALSE);
}

static int b64_value(char c, const char *alphabet)
{
	const char *p;

	if (c == '\0')
		return -1;
	p = strchr(alphabet, c);
	return p == NULL ? -1 : (int) (p - alphabet);
}

/* strict: only the alphabet, padding exactly where (and only if) the
   variant needs it, and zero bits after the last byte - what the base64
   crate's STANDARD and URL_SAFE_NO_PAD engines accept */
static unsigned char *b64_decode(const char *text, size_t textlen, const char *alphabet,
                                 gboolean pad, size_t *len)
{
	unsigned char *out;
	size_t i, n, full, rest, o = 0;
	guint32 v;
	int k, c;

	/* n: the significant characters ("xx==" -> 2 in the last group) */
	n = textlen;
	if (pad) {
		if (n % 4 != 0)
			return NULL;
		if (n > 0 && text[n - 1] == '=')
			n--;
		if (n > 0 && n == textlen - 1 && text[n - 1] == '=')
			n--;
	}
	rest = n % 4;
	if (rest == 1)
		return NULL;
	full = n - rest;

	out = g_malloc(full / 4 * 3 + 3);
	for (i = 0; i < full; i += 4) {
		v = 0;
		for (k = 0; k < 4; k++) {
			c = b64_value(text[i + k], alphabet);
			if (c < 0)
				goto fail;
			v = (v << 6) | c;
		}
		out[o++] = v >> 16;
		out[o++] = (v >> 8) & 0xff;
		out[o++] = v & 0xff;
	}
	if (rest > 0) {
		v = 0;
		for (k = 0; k < (int) rest; k++) {
			c = b64_value(text[full + k], alphabet);
			if (c < 0)
				goto fail;
			v = (v << 6) | c;
		}
		if (rest == 2) {
			if (v & 0xf)
				goto fail;
			out[o++] = v >> 4;
		} else {
			if (v & 0x3)
				goto fail;
			out[o++] = v >> 10;
			out[o++] = (v >> 2) & 0xff;
		}
	}
	*len = o;
	return out;
fail:
	g_free(out);
	return NULL;
}

char *e2e_b64_encode(const unsigned char *data, size_t len)
{
	return b64_encode(data, len, b64_std, TRUE);
}

char *e2e_b64url_encode(const unsigned char *data, size_t len)
{
	return b64_encode(data, len, b64_url, FALSE);
}

unsigned char *e2e_b64_decode(const char *text, size_t *len)
{
	return b64_decode(text, strlen(text), b64_std, TRUE, len);
}

unsigned char *e2e_b64url_decode(const char *text, size_t *len)
{
	return b64_decode(text, strlen(text), b64_url, FALSE, len);
}

char *e2e_hex_encode(const unsigned char *data, size_t len)
{
	static const char digits[] = "0123456789abcdef";
	char *out = g_malloc(len * 2 + 1);
	size_t i;

	for (i = 0; i < len; i++) {
		out[2 * i] = digits[data[i] >> 4];
		out[2 * i + 1] = digits[data[i] & 15];
	}
	out[len * 2] = '\0';
	return out;
}

static gboolean hex_decode_n(const char *hex, size_t hexlen, unsigned char *out, size_t len)
{
	size_t i;
	int hi, lo;

	if (hexlen != len * 2)
		return FALSE;
	for (i = 0; i < len; i++) {
		hi = g_ascii_xdigit_value(hex[2 * i]);
		lo = g_ascii_xdigit_value(hex[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return FALSE;
		out[i] = (hi << 4) | lo;
	}
	return TRUE;
}

gboolean e2e_hex_decode(const char *hex, unsigned char *out, size_t len)
{
	return hex_decode_n(hex, strlen(hex), out, len);
}

/* ---- the encrypted message ---- */

/* \d+ as a number; values past G_MAXINT64 saturate (rpe2e.pl compares
   them as numbers too: a huge timestamp is then just out of tolerance) */
static gboolean parse_digits(const char **pp, gint64 *value)
{
	const char *p = *pp;
	gint64 v = 0;

	if (!g_ascii_isdigit(*p))
		return FALSE;
	for (; g_ascii_isdigit(*p); p++) {
		int d = *p - '0';

		v = v > (G_MAXINT64 - d) / 10 ? G_MAXINT64 : v * 10 + d;
	}
	*pp = p;
	*value = v;
	return TRUE;
}

static gboolean skip_ws1(const char **pp)
{
	const char *p = *pp;

	if (!is_ws(*p))
		return FALSE;
	while (is_ws(*p))
		p++;
	*pp = p;
	return TRUE;
}

/* rpe2e.pl: m{^\+RPE2E01\s+(\S+)\s+(\d+)\s+(\d+)/(\d+)\s+(\S+):(\S+)\s*$} */
E2E_WIRE *e2e_wire_parse(const char *line)
{
	E2E_WIRE *w;
	const char *p, *tok, *colon;
	unsigned char *nonce;
	size_t toklen, len;
	gint64 part, total;

	if (line == NULL || strncmp(line, E2E_WIRE_PREFIX, strlen(E2E_WIRE_PREFIX)) != 0)
		return NULL;
	p = line + strlen(E2E_WIRE_PREFIX);
	if (!skip_ws1(&p))
		return NULL;

	w = g_new0(E2E_WIRE, 1);
	tok = p;
	while (*p != '\0' && !is_ws(*p))
		p++;
	if (!hex_decode_n(tok, p - tok, w->msgid, sizeof(w->msgid)) || !skip_ws1(&p))
		goto fail;
	if (!parse_digits(&p, &w->ts) || !skip_ws1(&p))
		goto fail;
	if (!parse_digits(&p, &part) || *p++ != '/' || !parse_digits(&p, &total) ||
	    !skip_ws1(&p))
		goto fail;
	if (total < 1 || total > E2E_MAX_CHUNKS || part < 1 || part > total)
		goto fail;
	w->part = (int) part;
	w->total = (int) total;

	tok = p;
	while (*p != '\0' && !is_ws(*p))
		p++;
	toklen = p - tok;
	while (is_ws(*p))
		p++;
	if (*p != '\0')
		goto fail;
	colon = memchr(tok, ':', toklen);
	if (colon == NULL || colon == tok || colon == tok + toklen - 1)
		goto fail;
	nonce = b64_decode(tok, colon - tok, b64_std, TRUE, &len);
	if (nonce == NULL || len != sizeof(w->nonce)) {
		g_free(nonce);
		goto fail;
	}
	memcpy(w->nonce, nonce, sizeof(w->nonce));
	g_free(nonce);
	w->ct = b64_decode(colon + 1, tok + toklen - colon - 1, b64_std, TRUE, &w->ctlen);
	if (w->ct == NULL)
		goto fail;
	return w;
fail:
	e2e_wire_free(w);
	return NULL;
}

void e2e_wire_free(E2E_WIRE *wire)
{
	if (wire == NULL)
		return;
	g_free(wire->ct);
	g_free(wire);
}

char *e2e_wire_encode(const E2E_WIRE *wire)
{
	char *msgid, *nonce, *ct, *line;

	msgid = e2e_hex_encode(wire->msgid, sizeof(wire->msgid));
	nonce = e2e_b64_encode(wire->nonce, sizeof(wire->nonce));
	ct = e2e_b64_encode(wire->ct, wire->ctlen);
	line = g_strdup_printf("%s %s %" G_GINT64_FORMAT " %d/%d %s:%s", E2E_WIRE_PREFIX,
	                       msgid, wire->ts, wire->part, wire->total, nonce, ct);
	g_free(msgid);
	g_free(nonce);
	g_free(ct);
	return line;
}

/* PROTO || be16(len) ctx || be16(8) msgid || be16(8) be64(ts) ||
   be16(1) part || be16(1) total. The context name goes in as the bytes
   erssi has (UTF-8), once. */
GByteArray *e2e_aad(const char *ctx, const unsigned char msgid[8], gint64 ts,
                    int part, int total)
{
	GByteArray *aad = g_byte_array_new();
	size_t ctxlen = strlen(ctx);
	guint8 b[8];
	int i;

	g_byte_array_append(aad, (const guint8 *) E2E_PROTO, strlen(E2E_PROTO));
	b[0] = (ctxlen >> 8) & 0xff;
	b[1] = ctxlen & 0xff;
	g_byte_array_append(aad, b, 2);
	g_byte_array_append(aad, (const guint8 *) ctx, ctxlen);
	b[0] = 0;
	b[1] = 8;
	g_byte_array_append(aad, b, 2);
	g_byte_array_append(aad, msgid, 8);
	g_byte_array_append(aad, b, 2);
	for (i = 0; i < 8; i++)
		b[i] = ((guint64) ts >> (56 - 8 * i)) & 0xff;
	g_byte_array_append(aad, b, 8);
	b[0] = 0;
	b[1] = 1;
	b[2] = part & 0xff;
	g_byte_array_append(aad, b, 3);
	b[2] = total & 0xff;
	g_byte_array_append(aad, b, 3);
	return aad;
}

/* ---- chunking and encryption ---- */

GPtrArray *e2e_split_plaintext(const char *text, size_t budget, const char **error)
{
	GPtrArray *chunks;
	size_t len, i, j;

	if (text == NULL || *text == '\0') {
		*error = "empty plaintext";
		return NULL;
	}
	len = strlen(text);
	chunks = g_ptr_array_new_with_free_func(g_free);
	for (i = 0; i < len; i = j) {
		j = MIN(i + budget, len);
		/* back to the start of a UTF-8 character */
		while (j > i && j < len && ((unsigned char) text[j] & 0xc0) == 0x80)
			j--;
		if (j == i) {
			*error = "cannot split: UTF-8 codepoint too large";
			g_ptr_array_unref(chunks);
			return NULL;
		}
		g_ptr_array_add(chunks, g_strndup(text + i, j - i));
		if (chunks->len > E2E_MAX_CHUNKS) {
			*error = "chunk overflow";
			g_ptr_array_unref(chunks);
			return NULL;
		}
	}
	return chunks;
}

static GPtrArray *encrypt_chunks(const unsigned char key[32], const char *ctx,
                                 GPtrArray *chunks, gint64 ts, const char **error)
{
	GPtrArray *out;
	E2E_WIRE w;
	guint i;

	memset(&w, 0, sizeof(w));
	if (!e2e_random_bytes(w.msgid, sizeof(w.msgid))) {
		*error = "no random bytes";
		return NULL;
	}
	w.ts = ts;
	w.total = chunks->len;
	out = g_ptr_array_new_with_free_func(g_free);
	for (i = 0; i < chunks->len; i++) {
		const char *chunk = g_ptr_array_index(chunks, i);
		size_t len = strlen(chunk);
		GByteArray *aad;
		gboolean ok;

		w.part = i + 1;
		w.ctlen = len + E2E_TAG_LEN;
		w.ct = g_malloc(w.ctlen);
		aad = e2e_aad(ctx, w.msgid, w.ts, w.part, w.total);
		ok = e2e_random_bytes(w.nonce, sizeof(w.nonce)) &&
		     e2e_xchacha_encrypt(w.ct, (const unsigned char *) chunk, len,
		                         aad->data, aad->len, w.nonce, key);
		g_byte_array_unref(aad);
		if (ok)
			g_ptr_array_add(out, e2e_wire_encode(&w));
		g_free(w.ct);
		if (!ok) {
			*error = "encryption failed";
			g_ptr_array_unref(out);
			return NULL;
		}
	}
	return out;
}

GPtrArray *e2e_encrypt_plain(const unsigned char key[32], const char *ctx,
                             const char *plain, gint64 ts, const char **error)
{
	GPtrArray *chunks, *out;

	chunks = e2e_split_plaintext(plain, E2E_MAX_PT_PER_CHUNK, error);
	if (chunks == NULL)
		return NULL;
	out = encrypt_chunks(key, ctx, chunks, ts, error);
	g_ptr_array_unref(chunks);
	return out;
}

#define ACTION_PREFIX "\001ACTION "

/* repartee E2eManager::encrypt_outgoing_ctcp: a frame that fits one chunk
   goes as it is; a longer ACTION becomes independent "\001ACTION piece\001"
   frames (the CTCP envelope is never cut across chunks), each its own
   message. */
GPtrArray *e2e_encrypt_ctcp(const unsigned char key[32], const char *ctx,
                            const char *frame, gint64 ts, const char **error)
{
	GPtrArray *pieces, *out, *one;
	size_t len = strlen(frame), prefix = strlen(ACTION_PREFIX);
	char *body;
	guint i, j;

	if (len <= E2E_MAX_PT_PER_CHUNK)
		return e2e_encrypt_plain(key, ctx, frame, ts, error);
	if (!e2e_is_action(frame)) {
		*error = "CTCP frame exceeds one encrypted chunk and cannot be split";
		return NULL;
	}
	body = g_strndup(frame + prefix, len - prefix - 1);
	pieces = e2e_split_plaintext(body, E2E_MAX_PT_PER_CHUNK - prefix - 1, error);
	g_free(body);
	if (pieces == NULL)
		return NULL;
	out = g_ptr_array_new_with_free_func(g_free);
	for (i = 0; i < pieces->len; i++) {
		char *piece = g_strconcat(ACTION_PREFIX, (char *) g_ptr_array_index(pieces, i),
		                          "\001", NULL);

		one = e2e_encrypt_plain(key, ctx, piece, ts, error);
		g_free(piece);
		if (one == NULL) {
			g_ptr_array_unref(out);
			out = NULL;
			break;
		}
		for (j = 0; j < one->len; j++)
			g_ptr_array_add(out, g_strdup(g_ptr_array_index(one, j)));
		g_ptr_array_unref(one);
	}
	g_ptr_array_unref(pieces);
	return out;
}

char *e2e_wire_decrypt(const E2E_WIRE *wire, const unsigned char key[32],
                       const char *ctx, size_t *len)
{
	GByteArray *aad;
	char *pt;
	gboolean ok;

	if (wire->ctlen < E2E_TAG_LEN)
		return NULL;
	pt = g_malloc(wire->ctlen - E2E_TAG_LEN + 1);
	aad = e2e_aad(ctx, wire->msgid, wire->ts, wire->part, wire->total);
	ok = e2e_xchacha_decrypt((unsigned char *) pt, wire->ct, wire->ctlen,
	                         aad->data, aad->len, wire->nonce, key);
	g_byte_array_unref(aad);
	if (!ok) {
		g_free(pt);
		return NULL;
	}
	*len = wire->ctlen - E2E_TAG_LEN;
	pt[*len] = '\0';
	return pt;
}

char *e2e_utf8_clean(const char *bytes, size_t len)
{
	GString *out = g_string_sized_new(len + 1);
	const char *p = bytes, *end = bytes + len;

	while (p < end) {
		gunichar c = g_utf8_get_char_validated(p, end - p);

		if (c == (gunichar) -1 || c == (gunichar) -2) {
			g_string_append(out, "\xef\xbf\xbd");
			p++;
		} else if (c == 0) {
			/* a NUL ends a C string anyway */
			break;
		} else {
			const char *next = g_utf8_next_char(p);

			g_string_append_len(out, p, next - p);
			p = next;
		}
	}
	return g_string_free(out, FALSE);
}

/* ---- the handshake ---- */

static const char *hs_name(E2E_HS_TYPE type)
{
	switch (type) {
	case E2E_HS_KEYREQ:
		return "KEYREQ";
	case E2E_HS_KEYRSP:
		return "KEYRSP";
	case E2E_HS_REKEY:
		return "REKEY";
	default:
		return NULL;
	}
}

/* rpe2e.pl: /^RPEE2E\s+KEYREQ\s/ and the like */
E2E_HS_TYPE e2e_handshake_type(const char *body)
{
	const char *p = body;
	size_t taglen = strlen(E2E_CTCP_TAG);
	E2E_HS_TYPE t;

	if (body == NULL || strncmp(p, E2E_CTCP_TAG, taglen) != 0)
		return E2E_HS_NONE;
	p += taglen;
	if (!skip_ws1(&p))
		return E2E_HS_NONE;
	for (t = E2E_HS_KEYREQ; t <= E2E_HS_REKEY; t++) {
		size_t n = strlen(hs_name(t));

		if (strncmp(p, hs_name(t), n) == 0 && is_ws(p[n]))
			return t;
	}
	return E2E_HS_NONE;
}

/* split /\s+/ as Perl does: a leading empty field is kept, trailing ones
   are dropped */
static char **split_ws(const char *body)
{
	GPtrArray *parts = g_ptr_array_new();
	const char *p = body, *start;

	if (is_ws(*p))
		g_ptr_array_add(parts, g_strdup(""));
	while (*p != '\0') {
		while (is_ws(*p))
			p++;
		if (*p == '\0')
			break;
		start = p;
		while (*p != '\0' && !is_ws(*p))
			p++;
		g_ptr_array_add(parts, g_strndup(start, p - start));
	}
	g_ptr_array_add(parts, NULL);
	return (char **) g_ptr_array_free(parts, FALSE);
}

static gboolean b64u_fixed(GHashTable *kv, const char *key, unsigned char *out, size_t len)
{
	const char *value = g_hash_table_lookup(kv, key);
	unsigned char *raw;
	size_t rawlen;
	gboolean ok;

	if (value == NULL)
		return FALSE;
	raw = e2e_b64url_decode(value, &rawlen);
	ok = raw != NULL && rawlen == len;
	if (ok)
		memcpy(out, raw, len);
	g_free(raw);
	return ok;
}

E2E_HANDSHAKE *e2e_handshake_parse(const char *body, E2E_HS_TYPE type)
{
	E2E_HANDSHAKE *hs = NULL;
	GHashTable *kv;
	char **parts;
	const char *value;
	guint n, i;

	if (body == NULL || hs_name(type) == NULL)
		return NULL;
	parts = split_ws(body);
	n = g_strv_length(parts);
	kv = g_hash_table_new(g_str_hash, g_str_equal);
	if (n < (type == E2E_HS_KEYREQ ? 7u : 9u) ||
	    strcmp(parts[0], E2E_CTCP_TAG) != 0 || strcmp(parts[1], hs_name(type)) != 0)
		goto out;
	for (i = 2; i < n; i++) {
		char *eq = strchr(parts[i], '=');

		if (eq == NULL)
			continue;
		*eq = '\0';
		/* a repeated field could re-target a signed message */
		if (g_hash_table_contains(kv, parts[i]))
			goto out;
		g_hash_table_insert(kv, parts[i], eq + 1);
	}
	value = g_hash_table_lookup(kv, "v");
	if (g_strcmp0(value, "1") != 0)
		goto out;
	value = g_hash_table_lookup(kv, "c");
	if (value == NULL || *value == '\0')
		goto out;

	hs = g_new0(E2E_HANDSHAKE, 1);
	hs->type = type;
	hs->channel = g_strdup(value);
	if (!b64u_fixed(kv, "p", hs->pub, 32) || !b64u_fixed(kv, "e", hs->eph, 32) ||
	    !b64u_fixed(kv, "n", hs->nonce, 16) || !b64u_fixed(kv, "s", hs->sig, 64))
		goto fail;
	if (type != E2E_HS_KEYREQ) {
		value = g_hash_table_lookup(kv, "w");
		if (!b64u_fixed(kv, "wn", hs->wrap_nonce, 24) || value == NULL)
			goto fail;
		hs->wrap_ct = e2e_b64url_decode(value, &hs->wrap_ctlen);
		if (hs->wrap_ct == NULL)
			goto fail;
	}
	goto out;
fail:
	e2e_handshake_free(hs);
	hs = NULL;
out:
	g_hash_table_destroy(kv);
	g_strfreev(parts);
	return hs;
}

/* "KEYREQ:" ctx ":" pub ":" eph ":" nonce, KEYRSP / REKEY with
   ":" wrap_nonce ":" wrap before the nonce - raw bytes */
static GByteArray *hs_payload(const E2E_HANDSHAKE *hs)
{
	GByteArray *p = g_byte_array_new();
	const char *name = hs_name(hs->type);

	g_byte_array_append(p, (const guint8 *) name, strlen(name));
	g_byte_array_append(p, (const guint8 *) ":", 1);
	g_byte_array_append(p, (const guint8 *) hs->channel, strlen(hs->channel));
	g_byte_array_append(p, (const guint8 *) ":", 1);
	g_byte_array_append(p, hs->pub, 32);
	g_byte_array_append(p, (const guint8 *) ":", 1);
	g_byte_array_append(p, hs->eph, 32);
	g_byte_array_append(p, (const guint8 *) ":", 1);
	if (hs->type != E2E_HS_KEYREQ) {
		g_byte_array_append(p, hs->wrap_nonce, 24);
		g_byte_array_append(p, (const guint8 *) ":", 1);
		if (hs->wrap_ctlen > 0)
			g_byte_array_append(p, hs->wrap_ct, hs->wrap_ctlen);
		g_byte_array_append(p, (const guint8 *) ":", 1);
	}
	g_byte_array_append(p, hs->nonce, 16);
	return p;
}

gboolean e2e_handshake_sign(E2E_HANDSHAKE *hs, const unsigned char sk[64])
{
	GByteArray *payload = hs_payload(hs);
	gboolean ok = e2e_ed25519_sign(hs->sig, payload->data, payload->len, sk);

	g_byte_array_unref(payload);
	return ok;
}

gboolean e2e_handshake_verify(const E2E_HANDSHAKE *hs)
{
	GByteArray *payload = hs_payload(hs);
	gboolean ok = e2e_ed25519_verify(hs->sig, payload->data, payload->len, hs->pub);

	g_byte_array_unref(payload);
	return ok;
}

char *e2e_handshake_encode(const E2E_HANDSHAKE *hs)
{
	GString *out = g_string_new(NULL);
	char *p, *e, *n, *s, *wn = NULL, *w = NULL;

	p = e2e_b64url_encode(hs->pub, 32);
	e = e2e_b64url_encode(hs->eph, 32);
	n = e2e_b64url_encode(hs->nonce, 16);
	s = e2e_b64url_encode(hs->sig, 64);
	g_string_printf(out, "%s %s v=1 c=%s p=%s e=%s", E2E_CTCP_TAG, hs_name(hs->type),
	                hs->channel, p, e);
	if (hs->type != E2E_HS_KEYREQ) {
		wn = e2e_b64url_encode(hs->wrap_nonce, 24);
		w = e2e_b64url_encode(hs->wrap_ct, hs->wrap_ctlen);
		g_string_append_printf(out, " wn=%s w=%s", wn, w);
	}
	g_string_append_printf(out, " n=%s s=%s", n, s);
	g_free(p);
	g_free(e);
	g_free(n);
	g_free(s);
	g_free(wn);
	g_free(w);
	return g_string_free(out, FALSE);
}

void e2e_handshake_free(E2E_HANDSHAKE *hs)
{
	if (hs == NULL)
		return;
	g_free(hs->channel);
	g_free(hs->wrap_ct);
	g_free(hs);
}

/* ---- what the outbound gate needs to know about a line ---- */

gboolean e2e_is_channel(const char *name)
{
	return name != NULL && strchr("#&!+", *name) != NULL && *name != '\0';
}

/* `@#chan` has one reading (#chan); `+#chan` two (#chan, +#chan), since
   + and & are status AND channel prefixes and ISUPPORT is not consulted -
   rpe2e.pl's _channel_readings */
char **e2e_channel_readings(const char *target)
{
	GPtrArray *readings = g_ptr_array_new();
	size_t run = 0;
	int j;

	if (target == NULL)
		target = "";
	while (target[run] != '\0' && strchr("@%+&~", target[run]) != NULL)
		run++;
	for (j = (int) run; j >= 0; j--) {
		const char *rest = target + j;

		if (*rest != '\0' && e2e_is_channel(rest))
			g_ptr_array_add(readings, g_strdup(rest));
	}
	g_ptr_array_add(readings, NULL);
	return (char **) g_ptr_array_free(readings, FALSE);
}

gboolean e2e_is_bot_command(const char *body)
{
	return (body[0] == '.' || body[0] == '!') && g_ascii_isalpha(body[1]) &&
	       strchr(body, '\n') == NULL;
}

gboolean e2e_is_ctcp(const char *body)
{
	size_t len = strlen(body);

	return len >= 2 && body[0] == '\001' && body[len - 1] == '\001';
}

gboolean e2e_is_action(const char *body)
{
	return strncmp(body, ACTION_PREFIX, strlen(ACTION_PREFIX)) == 0 &&
	       body[strlen(body) - 1] == '\001';
}

/* rpe2e.pl: /^(\@\S+\s+)?PRIVMSG\s+(\S+)\s+:?(.*)$/si - and what servers
   take as well, so /quote cannot slip past: leading spaces and a source
   prefix (":me PRIVMSG ...") */
gboolean e2e_parse_privmsg_line(const char *line, char **tags, char **target, char **body)
{
	const char *p = line, *tags_start, *tags_end, *t;
	size_t tlen;

	while (*p == ' ')
		p++;
	tags_start = tags_end = p;
	if (*p == '@') {
		p++;
		if (*p == '\0' || is_ws(*p))
			return FALSE;
		while (*p != '\0' && !is_ws(*p))
			p++;
		if (!skip_ws1(&p))
			return FALSE;
		tags_end = p;
	}
	if (*p == ':') {
		while (*p != '\0' && !is_ws(*p))
			p++;
		if (!skip_ws1(&p))
			return FALSE;
	}
	if (g_ascii_strncasecmp(p, "PRIVMSG", 7) != 0)
		return FALSE;
	p += 7;
	if (!skip_ws1(&p))
		return FALSE;
	t = p;
	while (*p != '\0' && !is_ws(*p))
		p++;
	tlen = p - t;
	if (tlen == 0 || !skip_ws1(&p))
		return FALSE;
	if (*p == ':')
		p++;
	*tags = g_strndup(tags_start, tags_end - tags_start);
	*target = g_strndup(t, tlen);
	*body = g_strdup(p);
	return TRUE;
}
