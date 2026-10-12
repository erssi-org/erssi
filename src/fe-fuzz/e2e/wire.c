/*
 wire.c : fuzz what an IRC peer can send the e2e module

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The input is one line as erssi hands it over: the text of a PRIVMSG
 (+RPE2E01 ...), the body of a NOTICE CTCP (RPEE2E KEYREQ ...), or an
 outgoing line the gate looks at (PRIVMSG <target> :<text>). Besides not
 crashing, every parser must be consistent with its encoder: whatever
 parses encodes to something that parses to the same thing, and the
 strict base64 decoder only accepts canonical text. The input, and the
 target of a PRIVMSG line, is also folded under every CASEMAPPING map: a
 fold folds to itself (the exact maps always, the Unicode ones for valid
 UTF-8 - e2e-scope.h), and the Unicode fold of valid UTF-8 is valid UTF-8.
*/

#include <irssi/src/fe-fuzz/e2e/e2e-fuzz.h>

#include <irssi/src/e2e/e2e-crypto.h>
#include <irssi/src/e2e/e2e-scope.h>
#include <irssi/src/e2e/e2e-wire.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* rpe2e.pl's "hello" on "#żaba" under the key 0x22 x 32 */
static const char known_wire[] =
	"+RPE2E01 396b958c840d504a 1791564129 1/1 "
	"aoIDyQCW/9kDelxEU66u/tzMJCeVXlPc:Az7K12qPCo84Lrb3+OGd5+vRzSpS";
/* rpe2e.pl's KEYREQ for #test */
static const char known_keyreq[] =
	"RPEE2E KEYREQ v=1 c=#test p=IVL40Zt5HSRFMkLhXy6rbLfP-ntqXtMAl5YOBpiB2xI "
	"e=dyENFcLDQDPLOFf273NGoG4fNqqFMlJYWm3TBA06Kkk n=COJ6gbQTz82kjdu7MtoJ6g "
	"s=Aa6n-YKBtfjjkGw0OI73ic9lz0wcQuuHPrNwnNnhnwnuMH6eETdRgJZL6gwIFsm4TQB2s-QBcn8szI2cG-_qCQ";

static unsigned char key[32];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	unsigned char k[32];
	E2E_WIRE *w;
	E2E_HANDSHAKE *hs;
	char *pt;
	size_t len;

	(void) argc;
	(void) argv;
	/* the target fuzzes the real code: check once that it is */
	memset(k, 0x22, sizeof(k));
	w = e2e_wire_parse(known_wire);
	pt = w != NULL ? e2e_wire_decrypt(w, k, "#\xc5\xbc" "aba", &len) : NULL;
	if (pt == NULL || strcmp(pt, "hello") != 0) {
		fprintf(stderr, "the known wire message does not decrypt\n");
		abort();
	}
	g_free(pt);
	e2e_wire_free(w);
	hs = e2e_handshake_parse(known_keyreq, E2E_HS_KEYREQ);
	if (hs == NULL || !e2e_handshake_verify(hs)) {
		fprintf(stderr, "the known KEYREQ does not verify\n");
		abort();
	}
	e2e_handshake_free(hs);
	return 0;
}

static void check_wire(const char *text)
{
	E2E_WIRE *w, *again;
	char *enc, *pt;
	size_t len;

	w = e2e_wire_parse(text);
	if (w == NULL)
		return;
	enc = e2e_wire_encode(w);
	again = e2e_wire_parse(enc);
	if (again == NULL || memcmp(again->msgid, w->msgid, 8) != 0 || again->ts != w->ts ||
	    again->part != w->part || again->total != w->total ||
	    memcmp(again->nonce, w->nonce, 24) != 0 || again->ctlen != w->ctlen ||
	    (w->ctlen > 0 && memcmp(again->ct, w->ct, w->ctlen) != 0)) {
		fprintf(stderr, "wire re-encoding differs: %s\n", enc);
		abort();
	}
	/* a forged message must not open under some key */
	pt = e2e_wire_decrypt(w, key, "#fuzz", &len);
	if (pt != NULL && strlen(pt) > len)
		abort();
	g_free(pt);
	e2e_wire_free(again);
	g_free(enc);
	e2e_wire_free(w);
}

static void check_handshake(const char *text)
{
	E2E_HS_TYPE t;

	e2e_handshake_type(text);
	for (t = E2E_HS_KEYREQ; t <= E2E_HS_REKEY; t++) {
		E2E_HANDSHAKE *hs = e2e_handshake_parse(text, t), *again;
		char *enc;

		if (hs == NULL)
			continue;
		if (e2e_handshake_type(text) != t)
			abort();
		e2e_handshake_verify(hs);
		enc = e2e_handshake_encode(hs);
		again = e2e_handshake_parse(enc, t);
		if (again == NULL || strcmp(again->channel, hs->channel) != 0 ||
		    memcmp(again->pub, hs->pub, 32) != 0 || memcmp(again->eph, hs->eph, 32) != 0 ||
		    memcmp(again->nonce, hs->nonce, 16) != 0 || memcmp(again->sig, hs->sig, 64) != 0 ||
		    memcmp(again->wrap_nonce, hs->wrap_nonce, 24) != 0 ||
		    again->wrap_ctlen != hs->wrap_ctlen ||
		    (hs->wrap_ctlen > 0 && memcmp(again->wrap_ct, hs->wrap_ct, hs->wrap_ctlen) != 0)) {
			fprintf(stderr, "handshake re-encoding differs: %s\n", enc);
			abort();
		}
		e2e_handshake_free(again);
		g_free(enc);
		e2e_handshake_free(hs);
	}
}

static const E2E_CASEMAP casemaps[] = {
	E2E_CASEMAP_RFC1459, E2E_CASEMAP_STRICT_RFC1459, E2E_CASEMAP_ASCII,
	E2E_CASEMAP_UNICODE, E2E_CASEMAP_UNKNOWN,
};

/* the CASEMAPPING folds of a name (e2e-scope.h) */
static void check_casefold(const char *name)
{
	gboolean valid = g_utf8_validate(name, -1, NULL);
	gsize i;

	for (i = 0; i < G_N_ELEMENTS(casemaps); i++) {
		E2E_CASEMAP map = casemaps[i];
		gboolean unicode = map == E2E_CASEMAP_UNICODE || map == E2E_CASEMAP_UNKNOWN;
		gboolean exact = FALSE;
		char *fold, *plain, *again, *narrow;

		fold = e2e_casefold_ex(name, map, &exact);
		plain = e2e_casefold(name, map);
		if (fold == NULL || plain == NULL || strcmp(fold, plain) != 0)
			abort();
		/* the exact maps decide every name */
		if (!unicode && !exact)
			abort();
		/* idempotent, except for invalid UTF-8 under the Unicode maps,
		   which the Latin-1 byte fold can make valid */
		if (!unicode || valid) {
			again = e2e_casefold(fold, map);
			if (strcmp(again, fold) != 0) {
				fprintf(stderr, "fold %d is not idempotent: \"%s\" -> \"%s\" -> \"%s\"\n",
				        (int) map, name, fold, again);
				abort();
			}
			g_free(again);
		}
		if (unicode && valid && !g_utf8_validate(fold, -1, NULL))
			abort();
		/* no narrow fold of invalid UTF-8 under UNICODE; the other
		   maps always have one */
		narrow = e2e_casefold_narrow(name, map);
		if (map == E2E_CASEMAP_UNICODE ? !valid && narrow != NULL : narrow == NULL)
			abort();
		g_free(narrow);
		g_free(plain);
		g_free(fold);
	}
}

static void check_line(const char *text)
{
	char *tags, *target, *body, **readings;
	int i;

	if (!e2e_parse_privmsg_line(text, &tags, &target, &body))
		return;
	readings = e2e_channel_readings(target);
	for (i = 0; readings[i] != NULL; i++)
		if (!e2e_is_channel(readings[i]))
			abort();
	check_casefold(target);
	e2e_is_bot_command(body);
	e2e_is_ctcp(body);
	e2e_is_action(body);
	g_strfreev(readings);
	g_free(tags);
	g_free(target);
	g_free(body);
}

/* strict base64: only canonical text decodes */
static void check_base64(const char *text)
{
	unsigned char *raw;
	char *enc;
	size_t len;

	raw = e2e_b64_decode(text, &len);
	if (raw != NULL) {
		enc = e2e_b64_encode(raw, len);
		if (strcmp(enc, text) != 0)
			abort();
		g_free(enc);
		g_free(raw);
	}
	raw = e2e_b64url_decode(text, &len);
	if (raw != NULL) {
		enc = e2e_b64url_encode(raw, len);
		if (strcmp(enc, text) != 0)
			abort();
		g_free(enc);
		g_free(raw);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *text = g_strndup((const char *) data, size);
	char *clean;
	GPtrArray *chunks;
	const char *error;

	check_wire(text);
	check_handshake(text);
	check_line(text);
	check_base64(text);
	check_casefold(text);

	/* what decrypted text goes through before it is shown */
	clean = e2e_utf8_clean((const char *) data, size);
	if (!g_utf8_validate(clean, -1, NULL))
		abort();
	g_free(clean);

	/* chunks never cut a UTF-8 character of valid input */
	chunks = e2e_split_plaintext(text, 1 + size % 200, &error);
	if (chunks != NULL) {
		GString *joined = g_string_new(NULL);
		guint i;

		for (i = 0; i < chunks->len; i++) {
			const char *c = g_ptr_array_index(chunks, i);

			if (g_utf8_validate(text, -1, NULL) && !g_utf8_validate(c, -1, NULL))
				abort();
			g_string_append(joined, c);
		}
		if (strcmp(joined->str, text) != 0)
			abort();
		g_string_free(joined, TRUE);
		g_ptr_array_unref(chunks);
	}
	g_free(text);
	return 0;
}
