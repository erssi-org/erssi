/*
 e2e-proto.c : RPE2E key exchange, trust, outbound gate and decryption

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 A port of rpe2e.pl 0.2.0 from repartee (MIT, Copyright (c) 2026
 outragedevs; contrib/rpe2e/LICENSE): the functions keep the names of the
 script's subs (handle_keyreq, _build_keyrsp_for_req, _gate_decide,
 _decrypt_wire_message, ...) so the two can be compared side by side. The wire format is unchanged (the same
 messages, signatures, AAD, wrap info and chunking as rpe2e.pl and
 repartee); some LOCAL decisions are stricter. Differences:
  - the identity is passed in instead of re-read from disk by each sub,
  - a line to several targets ("#a,#b") is refused when one of them is
    encrypted (rpe2e.pl looked the whole "#a,#b" up as one name and sent
    it in clear text),
  - a stored key that does not decode is an error, not garbage key bytes.
 Stricter local rules (beyond rpe2e.pl):
  - our key (KEYRSP) goes only to a peer the user ACCEPTED for that
    context: /e2e accept, an autotrust rule or auto-accept mode, stored as
    "accepted" { "<handle>|<ctx>": { fp, accepted_at, by } } and bound to
    the fingerprint. rpe2e.pl and repartee answered whenever they held a
    trusted session FROM the peer - which our automatic KEYREQ to anyone
    who sends a wire, or a REKEY, gave any channel member. In normal mode
    each side now accepts the other once; in quiet mode a request from a
    peer whose key we hold waits in pending_inbound as well (a stranger's
    is still dropped). Keyrings of rpe2e.pl / repartee have no "accepted"
    (rpe2e.pl keeps the member, an export does not carry it): a peer that
    asks for our key again needs one /e2e accept,
  - a DM KEYREQ must name its sender (c = "@" + sender handle, the
    rpe2e-dm-addendum); one naming anybody else is ignored,
  - a KEYRSP is taken only for a request we sent to that handle (rpe2e.pl
    also took one for a request sent without a handle, from anyone), and
    only an answer that opens uses the request up (a signed junk KEYRSP
    deleted it),
  - a REKEY only replaces a trusted session (with a real key) of the same
    handle, context and fingerprint while E2E is on there (a DM: the
    peer's "@<handle>" config); it never creates one (rpe2e.pl took one
    from any peer it had seen, even one waiting for /e2e accept). A REKEY
    is refused when its signed nonce was taken before from that peer for
    that context ("seen_rekeys", as repartee's e2e_seen_rekeys; rpe2e.pl
    had no replay check). A REKEY carries no timestamp or counter, so the
    nonces are never forgotten; after E2E_MAX_SEEN_REKEYS (1024) REKEYs of
    one peer in one context the further ones are refused and that peer's
    next message makes a normal KEYREQ/KEYRSP exchange,
  - REKEYs of a rotation go only to recipients accepted for the context;
    others (from a keyring of rpe2e.pl or repartee) are dropped from the
    recipients with a warning,
  - the all-zero key of a waiting request is never trusted or used:
    /e2e accept and /e2e unrevoke refuse a session without a real key,
  - CR, LF and NUL in a decrypted message become spaces,
  - a KEYRSP hands out the current key while a rotation is due (rpe2e.pl
    rotated there without sending the REKEYs); the next message rotates,
  - a stored time from the future or out of range counts as expired,
  - what strangers make us store is bounded, the oldest go first:
    pending_inbound 64; our own KEYREQs waiting for an answer
    ("pending", one per wire from a new handle) 256; peers nobody
    accepted 256 - with their sessions, so a peer who only answered our
    automatic KEYREQ is dropped again (accepted, revoked peers and
    recipients of our key always stay); pending_trust_change 128,
  - a DM to a nick with E2E on for another ident@host of it (a new host,
    or someone else took the nick), or for an address of it while the
    current one is not known, is refused (rpe2e.pl sent clear text),
  - an outgoing buffer of several IRC lines (CR, LF or NUL inside, e.g. a
    script's raw line) is refused whole when one of its PRIVMSGs would be
    encrypted or refused (e2e_gate_multiline; rpe2e.pl looked at the
    first line only),
 and in e2e-core.c / e2e-commands.c / e2e-keyring.c:
  - only the exact lines of ciphertext the gate sends pass it unexamined;
    a ready-made "+RPE2E01 ..." line from the user, a script or a proxy
    client to an encrypted context is encrypted again (rpe2e.pl passed any
    well-formed wire, and everything else while it sent ciphertext),
  - a line of ciphertext longer than the server takes is refused, not
    cut,
  - /upgrade sends the flood queue through the gate (irssi wrote it raw),
  - after a corrupt keyring is moved aside the state is LOST until
    /e2e reset or /e2e import: the gate refuses (bot commands excepted),
    nothing is saved (rpe2e.pl started an empty keyring: clear text
    everywhere),
  - handshake NOTICEs go through the flood queue (a REKEY right before
    its message excepted), KEYRSP and REKEY are rate-limited like KEYREQ
    (once per 10 s per kind, sender and context),
  - only a Perl script named rpe2e* switches the module off, not any
    script binding /e2e,
  - /e2e handshake needs the peer's handle (it is bound into the request),
  - /e2e on in a query stores its nick in the config ("nick"); /e2e off
    there turns off the query's address only (the live one, else the one
    config of that query) and names the others; /e2e off <ident@host>
    turns off one explicitly,
  - /e2e list shows a session we read but whose peer was never accepted
    as "key held, not accepted", not "trusted".
 Networks (e2e-scope.h): the keyring keeps each context per
 network, "<label>\x1f<wire>", while every wire-visible field - c=, the
 AAD, the HKDF info of a wrap, what is signed - stays the bare name, so
 rpe2e.pl, repartee and rpe2e.py see no difference. Old unscoped rows are
 used in place while erssi uses one network and taken over (marked as
 that network's) only by the user's own encrypted line - incoming
 traffic never moves them; then the name is BLOCKED on other networks.
 On several networks old rows are BLOCKED:
 the gate refuses the context (E2E_REFUSE_LEGACY), a wire, KEYREQ, KEYRSP
 or REKEY for it is treated as E2E off (rpe2e.pl knows one network per
 keyring only). A name holding 0x1F cannot be scoped: never encrypted,
 and refused where the old rules would not have sent it in clear.
 CASEMAPPING: channel names - and the nicks of DM configs -
 are compared as the server's CASEMAPPING compares them (e2e_casefold):
 ascii, rfc1459 and strict-rfc1459 exactly, rfc1459 when the server
 sends none; rfc7613, rfc8265, utf8 and unknown values with a superset
 fold (NFKC + case folding + the rfc1459 specials); the exact maps also
 fold Latin-1 bytes C0-DE as the ircu family does. Over-folding refuses
 more on the way out: the gate refuses a name the server may take for a
 channel with E2E on, and one it cannot decide (not UTF-8 or unassigned
 on a Unicode CASEMAPPING, non-ASCII on an unknown one, anything before
 the server's 005) while E2E is on for a channel of that network. On the
 way in, a wire for a channel written otherwise is also tried with the
 sender's session of the same channel by the fold (AAD: that session's
 name), on this network only. The AEAD does not reject what the fold
 joins: a server that keeps #a[b and #a{b apart can deliver the
 sender's #a[b line as #a{b, so a line opened under a name that is not
 the target's to every server of the CASEMAPPING is shown with
 "(E2E key of <name>)".
 Not addressed (known limitations, /help e2e):
 NOTICEs, non-ACTION CTCPs, bot commands and IRCv3 tags stay clear text;
 nicks in /e2e accept etc. resolve through the keyring's last_nick; a
 KEYREQ is not bound to the handle that sends it (the signed payload has
 no handle - a protocol limit), so someone who forwards a peer's request
 before the peer's first contact pins that fingerprint to their own handle
 and the peer's own request is then blocked as handle_changed until
 /e2e reverify (a denial of the first contact, no key goes anywhere);
 rpe2e.pl is recognised by the name of the Perl script (rpe2e*), and a
 script of that name is trusted to be it.
*/

#include "e2e-proto.h"
#include "e2e-crypto.h"
#include "e2e-keyring.h"
#include "e2e-wire.h"

#include <string.h>
#include <time.h>

#define WRAP_INFO  "RPE2E01-WRAP:"
#define REKEY_INFO "RPE2E01-REKEY:"

#define BOT_BYPASS_WARN  "bot-style message to %s sent in CLEARTEXT — channel lines starting with '.' or '!' and a letter bypass encryption for bots"
#define REFUSE_AMBIGUOUS "ambiguous target %s — E2E is enabled on both %s (STATUSMSG subset vs distinct channel); refusing to guess a context"
#define REFUSE_CASE      "E2E is enabled on %s, the same channel as %s for the server — message NOT sent; write the name as %s"
#define REFUSE_MULTI     "message to several targets (%s) includes an encrypted conversation — message NOT sent; send it to each target separately"
#define REFUSE_DM_MOVED  "E2E is on for %s at %s, but %s is now %s — message NOT sent. If it is the same person: /e2e on in the query encrypts to the new address (a new key exchange); /e2e off %s turns E2E off for the old one"
#define REFUSE_DM_UNKNOWN "E2E is on for %s at %s, but the address of %s is not known now (no query or common channel) — message NOT sent; /e2e off %s turns E2E off for it"
#define REFUSE_RAW_LINES "a raw line holding several IRC lines (CR/LF) includes a PRIVMSG that the E2E gate does not let out as it is (%s) — NOTHING of it was sent"
#define REFUSE_UNDECIDED "how the server's CASEMAPPING compares %s cannot be decided (not valid UTF-8, an unassigned character, or a CASEMAPPING erssi does not know): %s may be the same channel as %s, where E2E is on - message NOT sent"
#define REFUSE_UNSCOPED  "E2E cannot be kept per network for %s (its name holds the control character 0x1F, or the network has no name) and may be on for it — message NOT sent"

static gint64 default_now(void)
{
	return (gint64) time(NULL);
}

gint64 (*e2e_now)(void) = default_now;

static char *pipe_key(const char *a, const char *b)
{
	return g_strconcat(a, "|", b, NULL);
}

/* a base64 member that must decode to exactly len bytes */
static gboolean b64_member(const E2E_JSON *obj, const char *key, unsigned char *out, size_t len)
{
	const char *s = e2e_json_get_string(obj, key);
	unsigned char *raw;
	size_t rawlen;
	gboolean ok;

	if (s == NULL)
		return FALSE;
	raw = e2e_b64_decode(s, &rawlen);
	ok = raw != NULL && rawlen == len;
	if (ok)
		memcpy(out, raw, len);
	if (raw != NULL)
		e2e_wipe(raw, rawlen);
	g_free(raw);
	return ok;
}

static void set_b64(E2E_JSON *obj, const char *key, const unsigned char *data, size_t len)
{
	char *s = e2e_b64_encode(data, len);

	e2e_json_set_string(obj, key, s);
	e2e_wipe(s, strlen(s));
	g_free(s);
}

static const char *status_of(const E2E_JSON *row)
{
	const char *s = e2e_json_get_string(row, "status");

	return s != NULL ? s : "";
}

gboolean e2e_session_has_key(const E2E_JSON *row)
{
	static const unsigned char zero[32] = { 0 };
	unsigned char key[32];
	gboolean ok;

	if (row == NULL || !b64_member(row, "sk", key, 32))
		return FALSE;
	/* the placeholder of a request waiting for /e2e accept: anyone can
	   encrypt under it */
	ok = !e2e_mem_equal(key, zero, 32);
	e2e_wipe(key, sizeof(key));
	return ok;
}

/* a session we decrypt with: trusted, and with a real key */
static gboolean is_trusted(const E2E_JSON *row)
{
	return row != NULL && strcmp(status_of(row), "trusted") == 0 && e2e_session_has_key(row);
}

/* created_at of a row still within ttl seconds; one from the future (an
   imported keyring, a clock set back) is not, and nothing overflows */
static gboolean is_fresh(gint64 created_at, gint64 ttl)
{
	gint64 now = e2e_now();

	return created_at <= now && created_at > now - ttl;
}

gboolean e2e_dm_row_foreign(const char *handle, const char *ctx)
{
	const char *wire;

	if (handle == NULL || ctx == NULL)
		return FALSE;
	/* handles are compared exactly everywhere (the keys of the rows) */
	wire = e2e_ctx_wire(ctx);
	return !e2e_is_channel(wire) && (*wire != '@' || strcmp(wire + 1, handle) != 0);
}

guint e2e_drop_damaged_dm(E2E_JSON *kr, const char *ctx, const char *handle, gboolean *rotated)
{
	static const char *const stores[] = { "pending_inbound", "accepted", "incoming", NULL };
	E2E_JSON *recipients = e2e_json_get(kr, "outgoing_recipients"), *out;
	GPtrArray *keys, *doomed;
	gboolean keyed = FALSE;
	guint i, j, n = 0;
	size_t cl;

	if (rotated != NULL)
		*rotated = FALSE;
	if (ctx == NULL || e2e_is_channel(e2e_ctx_wire(ctx)))
		return 0;
	cl = strlen(ctx);
	for (i = 0; stores[i] != NULL; i++) {
		E2E_JSON *store = e2e_json_get(kr, stores[i]);

		keys = e2e_json_keys(store);
		doomed = g_ptr_array_new_with_free_func(g_free);
		for (j = 0; j < keys->len; j++) {
			const char *k = g_ptr_array_index(keys, j);
			size_t kl = strlen(k);
			char *h;

			if (kl <= cl + 1 || strcmp(k + kl - cl, ctx) != 0 || k[kl - cl - 1] != '|')
				continue;
			h = g_strndup(k, kl - cl - 1);
			if (e2e_dm_row_foreign(h, ctx) && (handle == NULL || strcmp(h, handle) == 0)) {
				g_ptr_array_add(doomed, g_strdup(k));
				keyed |= strcmp(stores[i], "accepted") == 0;
			}
			g_free(h);
		}
		for (j = 0; j < doomed->len; j++)
			n += e2e_json_remove(store, g_ptr_array_index(doomed, j));
		g_ptr_array_unref(doomed);
		g_ptr_array_unref(keys);
	}
	keys = e2e_json_keys(recipients);
	doomed = g_ptr_array_new_with_free_func(g_free);
	for (j = 0; j < keys->len; j++) {
		E2E_JSON *row = e2e_json_get(recipients, g_ptr_array_index(keys, j));
		const char *h = e2e_json_get_string(row, "handle");

		if (g_strcmp0(e2e_json_get_string(row, "channel"), ctx) == 0 && h != NULL &&
		    e2e_dm_row_foreign(h, ctx) && (handle == NULL || strcmp(h, handle) == 0)) {
			g_ptr_array_add(doomed, g_strdup(g_ptr_array_index(keys, j)));
			keyed = TRUE;
		}
	}
	for (j = 0; j < doomed->len; j++)
		n += e2e_json_remove(recipients, g_ptr_array_index(doomed, j));
	g_ptr_array_unref(doomed);
	g_ptr_array_unref(keys);
	/* it may hold our key for ctx: a new one goes to the rightful peer */
	out = e2e_json_get(e2e_json_get(kr, "outgoing"), ctx);
	if (keyed && out != NULL)
		e2e_json_set_int(out, "pending_rotation", 1);
	if (rotated != NULL)
		*rotated = keyed && out != NULL;
	return n;
}

/* a key "<handle>|<c>" of store: handle and c (a handle holds no '|') */
static gboolean split_pipe(const char *key, char **handle, char **c)
{
	const char *bar = strchr(key, '|');

	if (bar == NULL || bar == key || bar[1] == '\0')
		return FALSE;
	*handle = g_strndup(key, bar - key);
	*c = g_strdup(bar + 1);
	return TRUE;
}

static void due_rotation(E2E_JSON *kr, const char *ctx)
{
	E2E_JSON *out = e2e_json_get(e2e_json_get(kr, "outgoing"), ctx);

	if (out != NULL)
		e2e_json_set_int(out, "pending_rotation", 1);
}

GPtrArray *e2e_rotate_for_damaged(E2E_JSON *kr, const char *ctx)
{
	static const char *const stores[] = { "accepted", "incoming", NULL };
	GPtrArray *found = g_ptr_array_new_with_free_func(g_free), *keys, *doomed;
	E2E_JSON *channels = e2e_json_get(kr, "channels"), *recipients;
	guint i, j;

	for (i = 0; stores[i] != NULL; i++) {
		E2E_JSON *store = e2e_json_get(kr, stores[i]);

		keys = e2e_json_keys(store);
		for (j = 0; j < keys->len; j++) {
			E2E_JSON *row = e2e_json_get(store, g_ptr_array_index(keys, j));
			char *h, *c;

			if (!split_pipe(g_ptr_array_index(keys, j), &h, &c))
				continue;
			/* a session: only a trusted one under a peer's query (one
			   under "@<own>" has no config) */
			if ((ctx == NULL || strcmp(c, ctx) == 0) && e2e_dm_row_foreign(h, c) &&
			    e2e_json_get(row, E2E_DAMAGED_MEMBER) == NULL &&
			    (strcmp(stores[i], "accepted") == 0 ||
			     (is_trusted(row) && e2e_json_get(channels, c) != NULL))) {
				e2e_json_set_int(row, E2E_DAMAGED_MEMBER, e2e_now());
				due_rotation(kr, c);
				g_ptr_array_add(found, h);
				g_ptr_array_add(found, c);
			} else {
				g_free(h);
				g_free(c);
			}
		}
		g_ptr_array_unref(keys);
	}
	/* a recipient of our key there: no recipient any more */
	recipients = e2e_json_get(kr, "outgoing_recipients");
	keys = e2e_json_keys(recipients);
	doomed = g_ptr_array_new_with_free_func(g_free);
	for (j = 0; j < keys->len; j++) {
		E2E_JSON *row = e2e_json_get(recipients, g_ptr_array_index(keys, j));
		const char *h = e2e_json_get_string(row, "handle"), *c = e2e_json_get_string(row, "channel");

		if (h != NULL && c != NULL && (ctx == NULL || strcmp(c, ctx) == 0) &&
		    e2e_dm_row_foreign(h, c)) {
			gboolean told = FALSE;

			for (i = 0; i + 1 < found->len && !told; i += 2)
				told = strcmp(g_ptr_array_index(found, i), h) == 0 &&
				       strcmp(g_ptr_array_index(found, i + 1), c) == 0;
			if (!told) {
				g_ptr_array_add(found, g_strdup(h));
				g_ptr_array_add(found, g_strdup(c));
			}
			due_rotation(kr, c);
			g_ptr_array_add(doomed, g_strdup(g_ptr_array_index(keys, j)));
		}
	}
	for (j = 0; j < doomed->len; j++)
		e2e_json_remove(recipients, g_ptr_array_index(doomed, j));
	g_ptr_array_unref(doomed);
	g_ptr_array_unref(keys);
	return found;
}

gboolean e2e_peer_accepted(const E2E_JSON *kr, const char *handle, const char *ctx,
                           const char *fp_hex)
{
	const char *fp;
	char *k;

	/* never someone accepted for another person's query */
	if (e2e_dm_row_foreign(handle, ctx))
		return FALSE;
	k = pipe_key(handle, ctx);
	fp = e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "accepted"), k), "fp");

	g_free(k);
	return fp != NULL && fp_hex != NULL && strcmp(fp, fp_hex) == 0;
}

/* "accepted": { "<handle>|<ctx>": { fp, accepted_at, by } } - this
   client's own member, rpe2e.pl keeps it untouched */
static void mark_accepted(E2E_JSON *kr, const char *handle, const char *ctx,
                          const char *fp_hex, const char *by)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
	char *k = pipe_key(handle, ctx);

	e2e_json_set_string(row, "fp", fp_hex);
	e2e_json_set_int(row, "accepted_at", e2e_now());
	e2e_json_set_string(row, "by", by);
	e2e_json_set(e2e_json_object_member(kr, "accepted"), k, row);
	g_free(k);
}

void e2e_forget_acceptance(E2E_JSON *kr, const char *handle, const char *ctx)
{
	char *k = pipe_key(handle, ctx);

	e2e_json_remove(e2e_json_get(kr, "accepted"), k);
	g_free(k);
}

/* the oldest member of an object by an integer field of it (NULL: the
   member is the integer) */
static char *oldest_member(const E2E_JSON *obj, const char *field)
{
	GPtrArray *keys = e2e_json_keys(obj);
	char *oldest = NULL;
	gint64 best = 0;
	guint i;

	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);
		gint64 t = field != NULL ? e2e_json_get_int(e2e_json_get(obj, k), field, 0)
		                         : e2e_json_get_int(obj, k, 0);

		if (oldest == NULL || t < best) {
			g_free(oldest);
			oldest = g_strdup(k);
			best = t;
		}
	}
	g_ptr_array_unref(keys);
	return oldest;
}

/* the waiting request key / row is on net (always without one). The
   handle is the sender's ident@host, which may hold '|' and 0x1F: the
   context is the row's "channel" member (we wrote it), else what follows
   the row's "handle" member, else what follows the first '|' */
static const char *pending_row_ctx(const char *key, const E2E_JSON *row)
{
	const char *channel = e2e_json_get_string(row, "channel");
	const char *handle = e2e_json_get_string(row, "handle");
	const char *bar;
	size_t hl;

	if (channel != NULL)
		return channel;
	if (handle != NULL && strncmp(key, handle, hl = strlen(handle)) == 0 && key[hl] == '|')
		return key + hl + 1;
	bar = strchr(key, '|');
	return bar != NULL ? bar + 1 : key;
}

static gboolean pending_row_on_net(const char *key, const E2E_JSON *row, const E2E_NET *net)
{
	return net == NULL || e2e_ctx_on_net(pending_row_ctx(key, row), net);
}

/* the oldest waiting request of the network holding the most of them
   (old unscoped ones count as one network), so a flood on some networks
   only pushes out their own */
static char *oldest_of_fullest(const E2E_JSON *pending)
{
	GHashTable *count = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray *keys = e2e_json_keys(pending);
	E2E_JSON *mine = e2e_json_new(E2E_JSON_OBJECT);
	char *fullest = NULL, *k;
	guint i, best = 0;

	for (i = 0; i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i);
		char *label = e2e_ctx_network(pending_row_ctx(key, e2e_json_get(pending, key)));
		guint n;

		if (label == NULL)
			label = g_strdup("");
		n = GPOINTER_TO_UINT(g_hash_table_lookup(count, label)) + 1;
		/* the first to reach the highest count wins ties (keys are sorted) */
		if (n > best) {
			best = n;
			g_free(fullest);
			fullest = g_strdup(label);
		}
		g_hash_table_replace(count, label, GUINT_TO_POINTER(n));
	}
	for (i = 0; fullest != NULL && i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i);
		E2E_JSON *r = e2e_json_get(pending, key);
		char *label = e2e_ctx_network(pending_row_ctx(key, r));

		if (g_strcmp0(label != NULL ? label : "", fullest) == 0)
			e2e_json_set_int(mine, key, e2e_json_get_int(r, "received_at", 0));
		g_free(label);
	}
	k = oldest_member(mine, NULL);
	e2e_json_free(mine);
	g_ptr_array_unref(keys);
	g_hash_table_destroy(count);
	g_free(fullest);
	return k;
}

/* requests waiting for /e2e accept: at most E2E_MAX_PENDING_INBOUND on
   net (another network's do not push them out) and
   E2E_MAX_PENDING_INBOUND_ALL in all (then the network holding the most
   gives one up); the oldest go first, with the keyless session row
   stored next to them */
static void prune_pending_inbound(E2E_JSON *kr, const E2E_NET *net)
{
	E2E_JSON *pending = e2e_json_get(kr, "pending_inbound");

	for (;;) {
		GPtrArray *keys = e2e_json_keys(pending);
		E2E_JSON *mine = e2e_json_new(E2E_JSON_OBJECT), *row;
		char *k = NULL;
		guint i;

		for (i = 0; i < keys->len; i++) {
			const char *key = g_ptr_array_index(keys, i);
			E2E_JSON *r = e2e_json_get(pending, key);

			if (pending_row_on_net(key, r, net))
				e2e_json_set_int(mine, key, e2e_json_get_int(r, "received_at", 0));
		}
		g_ptr_array_unref(keys);
		if (e2e_json_size(mine) > E2E_MAX_PENDING_INBOUND)
			k = oldest_member(mine, NULL);
		else if (e2e_json_size(pending) > E2E_MAX_PENDING_INBOUND_ALL)
			k = oldest_of_fullest(pending);
		e2e_json_free(mine);
		if (k == NULL)
			break;
		e2e_json_remove(pending, k);
		row = e2e_json_get(e2e_json_get(kr, "incoming"), k);
		if (row != NULL && !e2e_session_has_key(row))
			e2e_json_remove(e2e_json_get(kr, "incoming"), k);
		g_free(k);
	}
}

gboolean e2e_fingerprint_accepted(const E2E_JSON *kr, const char *fp_hex)
{
	E2E_JSON *accepted = e2e_json_get(kr, "accepted");
	GPtrArray *keys = e2e_json_keys(accepted);
	gboolean found = FALSE;
	guint i;

	for (i = 0; i < keys->len && !found; i++) {
		const char *key = g_ptr_array_index(keys, i);
		char *h, *c;

		/* a damaged row (accepted for another person's query)
		   is no acceptance of anybody */
		if (split_pipe(key, &h, &c)) {
			gboolean foreign = e2e_dm_row_foreign(h, c);

			g_free(h);
			g_free(c);
			if (foreign)
				continue;
		}
		found = g_strcmp0(e2e_json_get_string(e2e_json_get(accepted, key), "fp"), fp_hex) == 0;
	}
	g_ptr_array_unref(keys);
	return found;
}

/* the rows of store whose member field is value */
static void remove_rows_with(E2E_JSON *store, const char *field, const char *value)
{
	GPtrArray *keys = e2e_json_keys(store), *doomed = g_ptr_array_new_with_free_func(g_free);
	guint i;

	for (i = 0; i < keys->len; i++)
		if (g_strcmp0(e2e_json_get_string(e2e_json_get(store, g_ptr_array_index(keys, i)), field),
		              value) == 0)
			g_ptr_array_add(doomed, g_strdup(g_ptr_array_index(keys, i)));
	g_ptr_array_unref(keys);
	for (i = 0; i < doomed->len; i++)
		e2e_json_remove(store, g_ptr_array_index(doomed, i));
	g_ptr_array_unref(doomed);
}

/* the members of store whose name starts with prefix */
static void remove_prefixed(E2E_JSON *store, const char *prefix)
{
	GPtrArray *keys = e2e_json_keys(store), *doomed = g_ptr_array_new_with_free_func(g_free);
	guint i;

	for (i = 0; i < keys->len; i++)
		if (g_str_has_prefix(g_ptr_array_index(keys, i), prefix))
			g_ptr_array_add(doomed, g_strdup(g_ptr_array_index(keys, i)));
	g_ptr_array_unref(keys);
	for (i = 0; i < doomed->len; i++)
		e2e_json_remove(store, g_ptr_array_index(doomed, i));
	g_ptr_array_unref(doomed);
}

/* Peers nobody accepted - only seen asking, or holding a session only
   because they answered our automatic KEYREQ (anyone who sends a wire):
   beyond E2E_MAX_UNACCEPTED_PEERS the least recently seen go, with their
   sessions and used REKEY nonces (their next wire asks again). Accepted
   peers, revoked ones (the TOFU block) and recipients of our key stay. */
static void prune_unaccepted_peers(E2E_JSON *kr, const char *keep)
{
	E2E_JSON *peers = e2e_json_get(kr, "peers"), *recipients = e2e_json_get(kr, "outgoing_recipients");
	GPtrArray *keys;
	GHashTable *kept = g_hash_table_new(g_str_hash, g_str_equal);
	E2E_JSON *candidates, *accepted = e2e_json_get(kr, "accepted");
	guint i;

	if (e2e_json_size(peers) <= E2E_MAX_UNACCEPTED_PEERS)
		goto done;
	keys = e2e_json_keys(accepted);
	for (i = 0; i < keys->len; i++) {
		const char *fp = e2e_json_get_string(e2e_json_get(accepted, g_ptr_array_index(keys, i)), "fp");

		if (fp != NULL)
			g_hash_table_add(kept, (char *) fp);
	}
	g_ptr_array_unref(keys);
	keys = e2e_json_keys(recipients);
	for (i = 0; i < keys->len; i++) {
		const char *fp = e2e_json_get_string(e2e_json_get(recipients, g_ptr_array_index(keys, i)),
		                                     "fingerprint");

		if (fp != NULL)
			g_hash_table_add(kept, (char *) fp);
	}
	g_ptr_array_unref(keys);

	candidates = e2e_json_new(E2E_JSON_OBJECT);
	keys = e2e_json_keys(peers);
	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);
		E2E_JSON *peer = e2e_json_get(peers, k);

		if (strcmp(k, keep) != 0 && strcmp(status_of(peer), "revoked") != 0 &&
		    !g_hash_table_contains(kept, k))
			e2e_json_set_int(e2e_json_object_member(candidates, k), "last_seen",
			                 e2e_json_get_int(peer, "last_seen", 0));
	}
	g_ptr_array_unref(keys);
	/* + 1: the one just seen */
	while (e2e_json_size(candidates) + 1 > E2E_MAX_UNACCEPTED_PEERS) {
		char *k = oldest_member(candidates, "last_seen"), *prefix;

		if (k == NULL)
			break;
		e2e_json_remove(candidates, k);
		e2e_json_remove(peers, k);
		remove_rows_with(e2e_json_get(kr, "incoming"), "fp", k);
		prefix = g_strconcat(k, "|", NULL);
		remove_prefixed(e2e_json_get(kr, "seen_rekeys"), prefix);
		g_free(prefix);
		g_free(k);
	}
	e2e_json_free(candidates);
done:
	g_hash_table_destroy(kept);
}

char *e2e_fingerprint_hex(const unsigned char pk[32])
{
	unsigned char fp[16];

	/* SHA-256 does not fail; if it ever did, an all-zero fingerprint
	   matches no peer instead of a NULL key */
	if (!e2e_fingerprint(pk, fp))
		memset(fp, 0, sizeof(fp));
	return e2e_hex_encode(fp, sizeof(fp));
}

/* ---- identity ---- */

gboolean e2e_identity_get(E2E_JSON *kr, E2E_IDENTITY *id, gboolean create,
                          gboolean *created, char **error)
{
	E2E_JSON *ident = e2e_json_get(kr, "identity");
	unsigned char pk[32], sk[64];
	char *hex;

	*created = FALSE;
	memset(id, 0, sizeof(*id));
	if (ident == NULL || ident->type == E2E_JSON_NULL ||
	    (ident->type == E2E_JSON_OBJECT && e2e_json_size(ident) == 0)) {
		if (!create) {
			*error = g_strdup("no identity yet");
			return FALSE;
		}
		if (!e2e_ed25519_keypair(id->pk, id->sk) || !e2e_fingerprint(id->pk, id->fp)) {
			*error = g_strdup("cannot create an identity");
			e2e_identity_wipe(id);
			return FALSE;
		}
		hex = e2e_hex_encode(id->fp, 16);
		g_strlcpy(id->fp_hex, hex, sizeof(id->fp_hex));
		g_free(hex);
		ident = e2e_json_new(E2E_JSON_OBJECT);
		set_b64(ident, "pk", id->pk, 32);
		set_b64(ident, "sk", id->sk, 64);
		e2e_json_set_string(ident, "fp", id->fp_hex);
		e2e_json_set_int(ident, "created_at", e2e_now());
		e2e_json_set(kr, "identity", ident);
		*created = TRUE;
		return TRUE;
	}

	/* an identity that exists is used as it is or not at all: replacing it
	   would show every peer a new key */
	if (!b64_member(ident, "pk", id->pk, 32) || !b64_member(ident, "sk", id->sk, 64) ||
	    e2e_json_get_string(ident, "fp") == NULL ||
	    !e2e_hex_decode(e2e_json_get_string(ident, "fp"), id->fp, 16)) {
		*error = g_strdup("the identity in the keyring is damaged (pk, sk or fp)");
		e2e_identity_wipe(id);
		return FALSE;
	}
	if (!e2e_ed25519_seed_keypair(id->sk, pk, sk) || !e2e_mem_equal(pk, id->pk, 32) ||
	    !e2e_mem_equal(sk + 32, id->sk + 32, 32)) {
		*error = g_strdup("the identity in the keyring is damaged (the keys do not match)");
		e2e_wipe(sk, sizeof(sk));
		e2e_identity_wipe(id);
		return FALSE;
	}
	e2e_wipe(sk, sizeof(sk));
	if (!e2e_fingerprint(id->pk, pk) || !e2e_mem_equal(pk, id->fp, 16)) {
		*error = g_strdup("the identity in the keyring is damaged (the fingerprint does not match)");
		e2e_identity_wipe(id);
		return FALSE;
	}
	hex = e2e_hex_encode(id->fp, 16);
	g_strlcpy(id->fp_hex, hex, sizeof(id->fp_hex));
	g_free(hex);
	return TRUE;
}

void e2e_identity_wipe(E2E_IDENTITY *id)
{
	e2e_wipe(id, sizeof(*id));
}

/* ---- rate limiting ---- */

GHashTable *e2e_stamps_new(void)
{
	return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

gboolean e2e_stamp_allow(GHashTable *stamps, const char *key, int interval)
{
	gint64 now = e2e_now(), *stamp;
	GHashTableIter iter;
	gpointer value;

	if (g_hash_table_size(stamps) > 256) {
		g_hash_table_iter_init(&iter, stamps);
		while (g_hash_table_iter_next(&iter, NULL, &value))
			if (now - *(gint64 *) value >= interval)
				g_hash_table_iter_remove(&iter);
	}
	stamp = g_hash_table_lookup(stamps, key);
	if (stamp != NULL && now - *stamp < interval)
		return FALSE;
	stamp = g_new(gint64, 1);
	*stamp = now;
	g_hash_table_replace(stamps, g_strdup(key), stamp);
	return TRUE;
}

/* ---- lookups ---- */

gboolean e2e_ctx_enabled(const E2E_JSON *kr, const char *ctx)
{
	return e2e_json_truthy(e2e_json_get(e2e_json_get(e2e_json_get(kr, "channels"), ctx),
	                                    "enabled"));
}

/* the keyring's cache of nick: the last_handle of the peer last seen as
   nick (ASCII case only), the newest last_seen first (null or missing:
   0), on a tie the smallest fingerprint key in byte order - the order
   e2e_json_keys() gives, the same in rpe2e.pl. *tie (if not
   NULL): peers of another last_handle share that newest last_seen. */
static const char *find_handle_by_nick(const E2E_JSON *kr, const char *nick, gboolean *tie)
{
	E2E_JSON *peers = e2e_json_get(kr, "peers");
	GPtrArray *keys = e2e_json_keys(peers);
	const char *best = NULL;
	gint64 best_seen = 0;
	gboolean found = FALSE, tied = FALSE;
	guint i;

	for (i = 0; i < keys->len; i++) {
		E2E_JSON *peer = e2e_json_get(peers, g_ptr_array_index(keys, i));
		const char *last = e2e_json_get_string(peer, "last_nick");
		const char *handle = e2e_json_get_string(peer, "last_handle");
		gint64 seen;

		if (last == NULL || g_ascii_strcasecmp(last, nick) != 0)
			continue;
		seen = e2e_json_get_int(peer, "last_seen", 0);
		if (!found || seen > best_seen) {
			found = TRUE;
			tied = FALSE;
			best_seen = seen;
			best = handle;
		} else if (seen == best_seen && g_strcmp0(handle, best) != 0) {
			tied = TRUE;
		}
	}
	g_ptr_array_unref(keys);
	if (tie != NULL)
		*tie = tied;
	return best;
}

const char *e2e_find_handle_by_nick(const E2E_JSON *kr, const char *nick)
{
	return find_handle_by_nick(kr, nick, NULL);
}

const char *e2e_find_peer_by_handle(const E2E_JSON *kr, const char *handle, E2E_JSON **peer)
{
	E2E_JSON *peers = e2e_json_get(kr, "peers");
	GPtrArray *keys = e2e_json_keys(peers);
	const char *found = NULL;
	guint i;

	if (peer != NULL)
		*peer = NULL;
	for (i = 0; i < keys->len && found == NULL; i++) {
		E2E_JSON *p = e2e_json_get(peers, g_ptr_array_index(keys, i));

		const char *last = e2e_json_get_string(p, "last_handle");

		/* ($peer->{last_handle} // '') eq $handle */
		if (g_strcmp0(last != NULL ? last : "", handle) == 0) {
			found = g_ptr_array_index(keys, i);
			if (peer != NULL)
				*peer = p;
		}
	}
	g_ptr_array_unref(keys);
	return found;
}

/* iterative * / ? matching with one backtrack point: linear in practice,
   no regex, no blow-up on "a*a*a*...". The pattern is ASCII-lowercased
   like Perl's lc. */
gboolean e2e_glob_match(const char *pattern, const char *text)
{
	const char *p = pattern, *t = text, *star = NULL, *mark = NULL;

	while (*t != '\0') {
		if (*p == '*') {
			star = p++;
			mark = t;
		} else if (*p != '\0' && (*p == '?' || g_ascii_tolower(*p) == g_ascii_tolower(*t))) {
			p++;
			t++;
		} else if (star != NULL) {
			p = star + 1;
			t = ++mark;
		} else {
			return FALSE;
		}
	}
	while (*p == '*')
		p++;
	return *p == '\0';
}

gboolean e2e_autotrust_matches(const E2E_JSON *kr, const char *handle, const char *ctx)
{
	E2E_JSON *rules = e2e_json_get(kr, "autotrust");
	guint i;

	if (rules == NULL || rules->type != E2E_JSON_ARRAY)
		return FALSE;
	for (i = 0; i < rules->array->len; i++) {
		E2E_JSON *rule = g_ptr_array_index(rules->array, i);
		const char *scope = e2e_json_get_string(rule, "scope");
		const char *pattern = e2e_json_get_string(rule, "handle_pattern");

		/* "global"; a rule of this network ("<label>\x1f<wire>") for
		   exactly ctx; an old rule (a bare name, rpe2e.pl's) for that
		   name on every network */
		if (scope == NULL || ctx == NULL)
			continue;
		if (strcmp(scope, "global") != 0 && strcmp(scope, ctx) != 0 &&
		    (strchr(scope, E2E_SCOPE_SEP) != NULL || strcmp(scope, e2e_ctx_wire(ctx)) != 0))
			continue;
		if (e2e_glob_match(pattern != NULL ? pattern : "", handle))
			return TRUE;
	}
	return FALSE;
}

gboolean e2e_outgoing_key(E2E_JSON *kr, const char *ctx, unsigned char key[32],
                          gboolean *rotated, gboolean *generated)
{
	E2E_JSON *outgoing = e2e_json_get(kr, "outgoing"), *row, *fresh;
	gboolean had_pending;

	*rotated = FALSE;
	*generated = FALSE;
	row = e2e_json_get(outgoing, ctx);
	if (row != NULL && !e2e_json_truthy(e2e_json_get(row, "pending_rotation")))
		return b64_member(row, "sk", key, 32);
	had_pending = row != NULL;
	if (!e2e_random_bytes(key, 32))
		return FALSE;
	fresh = e2e_json_new(E2E_JSON_OBJECT);
	set_b64(fresh, "sk", key, 32);
	e2e_json_set_int(fresh, "created_at", e2e_now());
	e2e_json_set_int(fresh, "pending_rotation", 0);
	/* the network that took the name over stays marked */
	if (e2e_json_get_string(row, E2E_ADOPTED_MEMBER) != NULL)
		e2e_json_set_string(fresh, E2E_ADOPTED_MEMBER, e2e_json_get_string(row, E2E_ADOPTED_MEMBER));
	e2e_json_set(outgoing, ctx, fresh);
	*rotated = had_pending;
	*generated = TRUE;
	return TRUE;
}

/* the key handed out in a KEYRSP: the current one even while a rotation
   is due - the rotation stays due and happens, with the REKEYs to every
   recipient (this one too), at the next message */
static gboolean current_outgoing_key(E2E_JSON *kr, const char *ctx, unsigned char key[32])
{
	E2E_JSON *row = e2e_json_get(e2e_json_get(kr, "outgoing"), ctx);
	gboolean rotated, generated;

	if (row != NULL)
		return b64_member(row, "sk", key, 32);
	return e2e_outgoing_key(kr, ctx, key, &rotated, &generated);
}

/* ---- trust bookkeeping ---- */

typedef enum {
	CHANGE_NEW,
	CHANGE_KNOWN,
	CHANGE_REVOKED,
	CHANGE_HANDLE,
	CHANGE_FINGERPRINT
} PEER_CHANGE;

/* _classify_peer_change; *old_fp: the fingerprint the handle had before */
static PEER_CHANGE classify_peer_change(const E2E_JSON *kr, const char *fp_hex,
                                        const char *handle, const char **old_fp)
{
	E2E_JSON *peer = e2e_json_get(e2e_json_get(kr, "peers"), fp_hex);

	*old_fp = NULL;
	if (peer != NULL) {
		if (strcmp(status_of(peer), "revoked") == 0)
			return CHANGE_REVOKED;
		if (g_strcmp0(e2e_json_get_string(peer, "last_handle"), handle) != 0 &&
		    !(e2e_json_get_string(peer, "last_handle") == NULL && *handle == '\0'))
			return CHANGE_HANDLE;
		return CHANGE_KNOWN;
	}
	*old_fp = e2e_find_peer_by_handle(kr, handle, NULL);
	return *old_fp != NULL ? CHANGE_FINGERPRINT : CHANGE_NEW;
}

static void record_pending_trust_change(E2E_JSON *kr, const char *handle, const char *ctx,
                                        const char *change, const unsigned char *new_pub,
                                        const char *old_fp, const char *new_fp)
{
	E2E_JSON *list = e2e_json_get(kr, "pending_trust_change"), *row;
	guint i;

	for (i = 0; i < list->array->len;) {
		row = g_ptr_array_index(list->array, i);
		if (g_strcmp0(e2e_json_get_string(row, "handle"), handle) == 0 &&
		    g_strcmp0(e2e_json_get_string(row, "channel"), ctx) == 0)
			g_ptr_array_remove_index(list->array, i);
		else
			i++;
	}
	/* bounded: anyone can make these with a forwarded request */
	while (list->array->len >= E2E_MAX_TRUST_CHANGES)
		g_ptr_array_remove_index(list->array, 0);
	row = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(row, "handle", handle);
	e2e_json_set_string(row, "channel", ctx);
	e2e_json_set_string(row, "change", change);
	if (new_pub != NULL)
		set_b64(row, "new_pubkey", new_pub, 32);
	else
		e2e_json_set(row, "new_pubkey", e2e_json_new(E2E_JSON_NULL));
	e2e_json_set_string(row, "old_fp", old_fp);
	e2e_json_set_string(row, "new_fp", new_fp);
	e2e_json_set_int(row, "recorded_at", e2e_now());
	e2e_json_array_add(list, row);
}

/* the three blocking outcomes of _classify_peer_change, recorded; TRUE when
   the message may go on */
static gboolean check_peer(E2E_JSON *kr, const char *fp_hex, const unsigned char pub[32],
                           const char *handle, const char *ctx, PEER_CHANGE *change)
{
	const char *old_fp;
	char *old;

	*change = classify_peer_change(kr, fp_hex, handle, &old_fp);
	switch (*change) {
	case CHANGE_REVOKED:
		record_pending_trust_change(kr, handle, ctx, "revoked", NULL, fp_hex, fp_hex);
		return FALSE;
	case CHANGE_HANDLE:
		record_pending_trust_change(kr, handle, ctx, "handle_changed", NULL, fp_hex, fp_hex);
		return FALSE;
	case CHANGE_FINGERPRINT:
		old = g_strdup(old_fp);	/* lives in kr, which the record changes */
		record_pending_trust_change(kr, handle, ctx, "fingerprint_changed", pub, old, fp_hex);
		g_free(old);
		return FALSE;
	default:
		return TRUE;
	}
}

/* $kr->{peers}{$fp} = { %$peer, pk, last_handle, [last_nick], first_seen //
   now, last_seen, status }; status NULL keeps the old one (or "pending") */
static void update_peer(E2E_JSON *kr, const char *fp_hex, const unsigned char pub[32],
                        const char *handle, const char *nick, const char *status)
{
	E2E_JSON *peer = e2e_json_object_member(e2e_json_get(kr, "peers"), fp_hex);
	E2E_JSON *first = e2e_json_get(peer, "first_seen");
	gint64 now = e2e_now();

	set_b64(peer, "pk", pub, 32);
	e2e_json_set_string(peer, "last_handle", handle);
	if (nick != NULL)
		e2e_json_set_string(peer, "last_nick", nick);
	if (first == NULL || first->type == E2E_JSON_NULL)
		e2e_json_set_int(peer, "first_seen", now);
	e2e_json_set_int(peer, "last_seen", now);
	if (status == NULL)
		status = e2e_json_get_string(peer, "status") != NULL ? NULL : "pending";
	if (status != NULL)
		e2e_json_set_string(peer, "status", status);
	prune_unaccepted_peers(kr, fp_hex);
}

static void set_session(E2E_JSON *kr, const char *handle, const char *ctx,
                        const char *fp_hex, const unsigned char key[32], const char *status)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);
	char *k = pipe_key(handle, ctx);

	e2e_json_set_string(row, "fp", fp_hex);
	set_b64(row, "sk", key, 32);
	e2e_json_set_string(row, "status", status);
	e2e_json_set_int(row, "created_at", e2e_now());
	e2e_json_set(e2e_json_get(kr, "incoming"), k, row);
	g_free(k);
}

/* ---- key exchange ---- */

static char *frame(const E2E_HANDSHAKE *hs)
{
	char *body = e2e_handshake_encode(hs), *out;

	out = g_strconcat("\001", body, "\001", NULL);
	g_free(body);
	return out;
}

static char *pending_key(const char *ctx, const char *handle)
{
	return handle != NULL && *handle != '\0' ? pipe_key(ctx, handle) : g_strdup(ctx);
}

char *e2e_build_keyreq(E2E_JSON *kr, const E2E_IDENTITY *id, const char *ctx,
                       const char *handle, char **error)
{
	E2E_JSON *pending = e2e_json_get(kr, "pending"), *row;
	E2E_HANDSHAKE hs;
	unsigned char eph_sk[32];
	char *key, *out = NULL;

	key = pending_key(ctx, handle);
	row = e2e_json_get(pending, key);
	if (row != NULL) {
		if (is_fresh(e2e_json_get_int(row, "created_at", 0), E2E_PENDING_KEYREQ_TTL)) {
			char *where = e2e_ctx_display(ctx), *shown = pending_key(where, handle);

			*error = g_strdup_printf("key exchange already pending for %s", shown);
			g_free(shown);
			g_free(where);
			g_free(key);
			return NULL;
		}
		e2e_json_remove(pending, key);
	}
	/* the wire names the bare context; the keyring keeps it scoped */
	memset(&hs, 0, sizeof(hs));
	hs.type = E2E_HS_KEYREQ;
	hs.channel = (char *) e2e_ctx_wire(ctx);
	memcpy(hs.pub, id->pk, 32);
	if (!e2e_x25519_keypair(eph_sk, hs.eph) || !e2e_random_bytes(hs.nonce, 16) ||
	    !e2e_handshake_sign(&hs, id->sk)) {
		*error = g_strdup("cannot create a key request");
		goto out;
	}
	row = e2e_json_new(E2E_JSON_OBJECT);
	set_b64(row, "eph_sk", eph_sk, 32);
	e2e_json_set_string(row, "handle", handle);
	e2e_json_set_string(row, "channel", ctx);
	e2e_json_set_int(row, "created_at", e2e_now());
	e2e_json_set(pending, key, row);
	/* one per wire from a new handle: bounded, the oldest go */
	while (e2e_json_size(pending) > E2E_MAX_PENDING_OUT) {
		char *old = oldest_member(pending, "created_at");

		if (old == NULL || strcmp(old, key) == 0) {
			g_free(old);
			break;
		}
		e2e_json_remove(pending, old);
		g_free(old);
	}
	out = frame(&hs);
out:
	e2e_wipe(eph_sk, sizeof(eph_sk));
	g_free(key);
	return out;
}

char *e2e_build_keyrsp_for_req(E2E_JSON *kr, const E2E_IDENTITY *id, const char *ctx,
                               const char *sender_handle, const unsigned char req_pub[32],
                               const unsigned char req_eph[32])
{
	E2E_HANDSHAKE hs;
	E2E_JSON *rec;
	unsigned char eph_sk[32], shared[32], wrap[32], our[32];
	gboolean ok;
	char *info, *fp_hex, *key, *out = NULL;

	/* our key for a query goes to that query's own address only */
	if (e2e_dm_row_foreign(sender_handle, ctx))
		return NULL;
	memset(&hs, 0, sizeof(hs));
	hs.type = E2E_HS_KEYRSP;
	hs.channel = (char *) e2e_ctx_wire(ctx);
	memcpy(hs.pub, id->pk, 32);
	info = g_strconcat(WRAP_INFO, e2e_ctx_wire(ctx), NULL);
	hs.wrap_ctlen = 32 + E2E_TAG_LEN;
	hs.wrap_ct = g_malloc(hs.wrap_ctlen);
	ok = e2e_x25519_keypair(eph_sk, hs.eph) &&
	     e2e_x25519(shared, eph_sk, req_eph) &&
	     e2e_wrap_key(wrap, shared, info, strlen(info)) &&
	     current_outgoing_key(kr, ctx, our) &&
	     e2e_random_bytes(hs.wrap_nonce, 24) &&
	     e2e_xchacha_encrypt(hs.wrap_ct, our, 32, (const unsigned char *) info, strlen(info),
	                         hs.wrap_nonce, wrap) &&
	     e2e_random_bytes(hs.nonce, 16) &&
	     e2e_handshake_sign(&hs, id->sk);
	e2e_wipe(eph_sk, sizeof(eph_sk));
	e2e_wipe(shared, sizeof(shared));
	e2e_wipe(wrap, sizeof(wrap));
	e2e_wipe(our, sizeof(our));
	g_free(info);
	if (!ok)
		goto out;

	fp_hex = e2e_fingerprint_hex(req_pub);
	update_peer(kr, fp_hex, req_pub, sender_handle, NULL, "trusted");
	rec = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(rec, "channel", ctx);
	e2e_json_set_string(rec, "handle", sender_handle);
	e2e_json_set_string(rec, "fingerprint", fp_hex);
	e2e_json_set_int(rec, "first_sent_at", e2e_now());
	key = pipe_key(ctx, sender_handle);
	e2e_json_set(e2e_json_get(kr, "outgoing_recipients"), key, rec);
	g_free(key);
	g_free(fp_hex);
	out = frame(&hs);
out:
	g_free(hs.wrap_ct);
	return out;
}

char *e2e_build_rekey(const E2E_IDENTITY *id, const char *ctx,
                      const unsigned char peer_pk[32], const unsigned char new_key[32])
{
	E2E_HANDSHAKE hs;
	unsigned char eph_sk[32], peer_x[32], shared[32], wrap[32];
	char *info, *out = NULL;
	gboolean ok;

	memset(&hs, 0, sizeof(hs));
	hs.type = E2E_HS_REKEY;
	hs.channel = (char *) e2e_ctx_wire(ctx);
	memcpy(hs.pub, id->pk, 32);
	info = g_strconcat(REKEY_INFO, e2e_ctx_wire(ctx), NULL);
	hs.wrap_ctlen = 32 + E2E_TAG_LEN;
	hs.wrap_ct = g_malloc(hs.wrap_ctlen);
	/* the peer's only long-term key is its Ed25519 identity */
	ok = e2e_x25519_keypair(eph_sk, hs.eph) &&
	     e2e_ed25519_pk_to_x25519(peer_x, peer_pk) &&
	     e2e_x25519(shared, eph_sk, peer_x) &&
	     e2e_wrap_key(wrap, shared, info, strlen(info)) &&
	     e2e_random_bytes(hs.wrap_nonce, 24) &&
	     e2e_xchacha_encrypt(hs.wrap_ct, new_key, 32, (const unsigned char *) info,
	                         strlen(info), hs.wrap_nonce, wrap) &&
	     e2e_random_bytes(hs.nonce, 16) &&
	     e2e_handshake_sign(&hs, id->sk);
	e2e_wipe(eph_sk, sizeof(eph_sk));
	e2e_wipe(shared, sizeof(shared));
	e2e_wipe(wrap, sizeof(wrap));
	g_free(info);
	if (ok)
		out = frame(&hs);
	g_free(hs.wrap_ct);
	return out;
}

/* A reciprocal establishes the peer -> us direction, keyed by OUR handle
   for a DM; NULL while that is unknown (the peer's first message heals it
   later with an automatic KEYREQ) */
static char *reciprocal_ctx(const char *ctx, const char *own_handle)
{
	char *own, *out;

	if (e2e_is_channel(e2e_ctx_wire(ctx)))
		return g_strdup(ctx);
	if (own_handle == NULL || *own_handle == '\0')
		return NULL;
	/* on the network of ctx */
	own = g_strconcat("@", own_handle, NULL);
	out = e2e_ctx_rescope(ctx, own);
	g_free(own);
	return out;
}

/* the old (bare) twin of ctx, a context of net, whose rows still count
   row by row: on the only network, while this network has rows of its own
   there as well (e2e_ctx_find gave ctx) - e2e_adopt_legacy would keep
   each old row nobody of this network has. NULL otherwise. */
static char *legacy_twin(const E2E_JSON *kr, const E2E_NET *net, const char *ctx)
{
	char *found = NULL;

	if (net == NULL || ctx == NULL || strchr(ctx, E2E_SCOPE_SEP) == NULL)
		return NULL;
	if (e2e_ctx_lookup(kr, net, e2e_ctx_wire(ctx), &found) == E2E_CTX_LEGACY)
		return found;
	g_free(found);
	return NULL;
}

/* store's row "<a>|<b>" of ctx, else of its old twin (*used: that key) */
static E2E_JSON *row_or_twin(const E2E_JSON *kr, const E2E_NET *net, const char *store,
                             const char *handle, const char *ctx, gboolean handle_first,
                             char **used)
{
	E2E_JSON *rows = e2e_json_get(kr, store), *row;
	char *twin, *k;

	k = handle_first ? pipe_key(handle, ctx) : pipe_key(ctx, handle);
	row = e2e_json_get(rows, k);
	if (row == NULL && (twin = legacy_twin(kr, net, ctx)) != NULL) {
		g_free(k);
		k = handle_first ? pipe_key(handle, twin) : pipe_key(twin, handle);
		row = e2e_json_get(rows, k);
		g_free(twin);
	}
	if (used != NULL)
		*used = k;
	else
		g_free(k);
	return row;
}

/* store's row ctx (channels, outgoing), else its old twin's */
static E2E_JSON *ctx_row_or_twin(const E2E_JSON *kr, const E2E_NET *net, const char *store,
                                 const char *ctx)
{
	E2E_JSON *row = e2e_json_get(e2e_json_get(kr, store), ctx);
	char *twin;

	if (row == NULL && (twin = legacy_twin(kr, net, ctx)) != NULL) {
		row = e2e_json_get(e2e_json_get(kr, store), twin);
		g_free(twin);
	}
	return row;
}

/* handle was accepted for ctx, or (mixed state) for its old twin */
static gboolean accepted_or_twin(const E2E_JSON *kr, const E2E_NET *net, const char *handle,
                                 const char *ctx, const char *fp_hex)
{
	char *twin;
	gboolean ok = e2e_peer_accepted(kr, handle, ctx, fp_hex);

	if (!ok && (twin = legacy_twin(kr, net, ctx)) != NULL) {
		ok = e2e_peer_accepted(kr, handle, twin, fp_hex);
		g_free(twin);
	}
	return ok;
}

/* "@<handle>" on net: the config of a DM with handle, where it is (an old
   one is not taken over); NULL: E2E cannot be on there - an old row on
   several networks */
static char *dm_config_ctx(const E2E_JSON *kr, const E2E_NET *net, const char *handle)
{
	char *wire = g_strconcat("@", handle, NULL);
	char *ctx = e2e_ctx_find(kr, net, wire);

	g_free(wire);
	return ctx;
}

/* the reciprocal context on net, where its rows are: "@<own>" may still
   be old (e2e_ctx_find); when it cannot be used here, this network's */
static char *reciprocal_ctx_on(const E2E_JSON *kr, const E2E_NET *net, const char *ctx,
                               const char *own_handle)
{
	char *rctx = reciprocal_ctx(ctx, own_handle), *found;

	if (rctx == NULL || net == NULL || e2e_is_channel(e2e_ctx_wire(rctx)))
		return rctx;
	found = e2e_ctx_find(kr, net, e2e_ctx_wire(rctx));
	if (found == NULL)
		return rctx;
	g_free(rctx);
	return found;
}

static char *maybe_build_reciprocal_keyreq(E2E_JSON *kr, const E2E_IDENTITY *id,
                                           GHashTable *stamps, const E2E_NET *net,
                                           const char *ctx, const char *sender,
                                           const char *own_handle)
{
	char *rctx, *k, *pk, *out = NULL, *error = NULL;
	gboolean pending, trusted;

	rctx = reciprocal_ctx_on(kr, net, ctx, own_handle);
	if (rctx == NULL)
		return NULL;
	k = pipe_key(sender, rctx);
	pk = pending_key(rctx, sender);
	trusted = is_trusted(row_or_twin(kr, net, "incoming", sender, rctx, TRUE, NULL));
	pending = row_or_twin(kr, net, "pending", sender, rctx, FALSE, NULL) != NULL;
	if (!pending && !trusted && e2e_stamp_allow(stamps, sender, E2E_KEYREQ_MIN_INTERVAL))
		out = e2e_build_keyreq(kr, id, rctx, sender, &error);
	g_free(error);
	g_free(k);
	g_free(pk);
	g_free(rctx);
	return out;
}

/* own_ctx: where "@<own>" is for a DM (the caller found it), else NULL */
static char *reciprocal_on_accept(E2E_JSON *kr, const E2E_IDENTITY *id, const char *ctx,
                                  const char *own_ctx, const char *sender_handle,
                                  const char *own_handle)
{
	char *rctx, *k, *pk, *out = NULL, *error = NULL;

	rctx = own_ctx != NULL && !e2e_is_channel(e2e_ctx_wire(ctx)) ? g_strdup(own_ctx)
	                                                            : reciprocal_ctx(ctx, own_handle);
	if (rctx == NULL)
		return NULL;
	k = pipe_key(sender_handle, rctx);
	if (!is_trusted(e2e_json_get(e2e_json_get(kr, "incoming"), k))) {
		pk = pending_key(rctx, sender_handle);
		e2e_json_remove(e2e_json_get(kr, "pending"), pk);
		g_free(pk);
		out = e2e_build_keyreq(kr, id, rctx, sender_handle, &error);
	}
	g_free(error);
	g_free(k);
	g_free(rctx);
	return out;
}

char *e2e_build_reciprocal_keyreq_on_accept(E2E_JSON *kr, const E2E_IDENTITY *id,
                                            const char *ctx, const char *sender_handle,
                                            const char *own_handle)
{
	return reciprocal_on_accept(kr, id, ctx, NULL, sender_handle, own_handle);
}

void e2e_handle_keyreq(E2E_JSON *kr, const E2E_IDENTITY *id, GHashTable *outgoing_stamps,
                       const E2E_NET *net, const char *sender_handle, const char *nick,
                       const char *body, const char *own_handle, char **rsp, char **reciprocal)
{
	E2E_HANDSHAKE *req;
	E2E_JSON *cfg, *sess, *row;
	PEER_CHANGE change;
	const char *mode;
	gboolean autotrust, trusted;
	char *fp_hex = NULL, *k = NULL, *dm, *ctx = NULL;

	*rsp = NULL;
	*reciprocal = NULL;
	req = e2e_handshake_parse(body, E2E_HS_KEYREQ);
	if (req == NULL)
		return;
	/* the signature and the sender rule are about the wire: the bare c= */
	if (!e2e_handshake_verify(req))
		goto out;
	/* a DM request asks for the direction to its sender: "@" + the
	   sender's own handle (rpe2e-dm-addendum). One naming anybody else
	   would get our DM key to that person. */
	if (!e2e_is_channel(req->channel)) {
		dm = g_strconcat("@", sender_handle, NULL);
		trusted = strcmp(dm, req->channel) == 0;
		g_free(dm);
		if (!trusted)
			goto out;
	}
	/* the context on this network, an old one where it is (someone
	   else's request never takes it over); one of unknown network is off */
	ctx = e2e_ctx_find(kr, net, req->channel);
	if (ctx == NULL)
		goto out;
	/* requests for a context where E2E is off are ignored (the config,
	   session and acceptance row by row in the mixed state of the only
	   network, as e2e_adopt_legacy would keep them) */
	cfg = ctx_row_or_twin(kr, net, "channels", ctx);
	if (!e2e_json_truthy(e2e_json_get(cfg, "enabled")))
		goto out;
	fp_hex = e2e_fingerprint_hex(req->pub);
	if (!check_peer(kr, fp_hex, req->pub, sender_handle, ctx, &change))
		goto out;
	update_peer(kr, fp_hex, req->pub, sender_handle, nick, NULL);

	autotrust = e2e_autotrust_matches(kr, sender_handle, ctx);
	mode = e2e_json_get_string(cfg, "mode");
	if (mode == NULL)
		mode = "normal";
	if (autotrust)
		mode = "auto-accept";
	k = pipe_key(sender_handle, ctx);
	sess = row_or_twin(kr, net, "incoming", sender_handle, ctx, TRUE, NULL);
	trusted = is_trusted(sess);
	/* Our key goes only to someone the user accepted (/e2e accept,
	   autotrust, auto-accept mode). Holding a trusted session FROM them
	   is not that: we got it because we asked, e.g. by the automatic
	   KEYREQ to anyone who sends a wire (rpe2e.pl and repartee answer
	   then, see the header). */
	if (strcmp(mode, "auto-accept") != 0 &&
	    !accepted_or_twin(kr, net, sender_handle, ctx, fp_hex)) {
		/* quiet: a stranger's request is dropped silently */
		if (strcmp(mode, "quiet") == 0 && !trusted)
			goto out;
		/* waits for /e2e accept; a session with their key is kept */
		if (!trusted) {
			unsigned char zero[32] = { 0 };

			set_session(kr, sender_handle, ctx, fp_hex, zero, "pending");
		}
		row = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set_string(row, "handle", sender_handle);
		e2e_json_set_string(row, "channel", ctx);
		e2e_json_set_string(row, "sender_handle", sender_handle);
		e2e_json_set_string(row, "sender_nick", nick);
		set_b64(row, "pubkey", req->pub, 32);
		set_b64(row, "eph_x25519", req->eph, 32);
		set_b64(row, "nonce", req->nonce, 16);
		set_b64(row, "sig", req->sig, 64);
		e2e_json_set_int(row, "received_at", e2e_now());
		e2e_json_set(e2e_json_get(kr, "pending_inbound"), k, row);
		prune_pending_inbound(kr, net);
		goto out;
	}
	*rsp = e2e_build_keyrsp_for_req(kr, id, ctx, sender_handle, req->pub, req->eph);
	if (*rsp != NULL && !accepted_or_twin(kr, net, sender_handle, ctx, fp_hex))
		mark_accepted(kr, sender_handle, ctx, fp_hex,
		              autotrust ? "autotrust" : "auto-accept");
	*reciprocal = maybe_build_reciprocal_keyreq(kr, id, outgoing_stamps, net, ctx,
	                                            sender_handle, own_handle);
out:
	g_free(ctx);
	g_free(k);
	g_free(fp_hex);
	e2e_handshake_free(req);
}

/* the session key from a KEYRSP / REKEY wrap */
static gboolean unwrap(const E2E_HANDSHAKE *hs, const unsigned char x_sk[32],
                       const char *prefix, unsigned char key[32])
{
	unsigned char shared[32], wrap[32];
	char *info;
	gboolean ok;

	if (hs->wrap_ctlen != 32 + E2E_TAG_LEN)
		return FALSE;
	info = g_strconcat(prefix, hs->channel, NULL);
	ok = e2e_x25519(shared, x_sk, hs->eph) &&
	     e2e_wrap_key(wrap, shared, info, strlen(info)) &&
	     e2e_xchacha_decrypt(key, hs->wrap_ct, hs->wrap_ctlen,
	                         (const unsigned char *) info, strlen(info), hs->wrap_nonce, wrap);
	/* an all-zero session key is the placeholder of a waiting request,
	   never a key to install */
	if (ok) {
		static const unsigned char zero[32] = { 0 };

		ok = !e2e_mem_equal(key, zero, 32);
	}
	e2e_wipe(shared, sizeof(shared));
	e2e_wipe(wrap, sizeof(wrap));
	g_free(info);
	return ok;
}

gboolean e2e_handle_keyrsp(E2E_JSON *kr, const E2E_NET *net, const char *sender_handle,
                           const char *nick, const char *body)
{
	E2E_HANDSHAKE *rsp;
	E2E_JSON *pending, *row;
	PEER_CHANGE change;
	unsigned char eph_sk[32], key[32];
	char *pk, *fp_hex = NULL, *ctx = NULL;
	gboolean have_eph, ok = FALSE;

	rsp = e2e_handshake_parse(body, E2E_HS_KEYRSP);
	if (rsp == NULL)
		return FALSE;
	if (!e2e_handshake_verify(rsp))
		goto out;
	ctx = e2e_ctx_find(kr, net, rsp->channel);
	if (ctx == NULL)
		goto out;
	/* only an answer to a request of ours to this sender. rpe2e.pl also
	   took one to a request sent without a known handle - from anyone. */
	pending = e2e_json_get(kr, "pending");
	row = row_or_twin(kr, net, "pending", sender_handle, ctx, FALSE, &pk);
	if (row == NULL) {
		g_free(pk);
		goto out;
	}
	have_eph = b64_member(row, "eph_sk", eph_sk, 32);
	if (!have_eph) {
		e2e_json_remove(pending, pk);
		g_free(pk);
		goto out;
	}
	ok = unwrap(rsp, eph_sk, WRAP_INFO, key);
	e2e_wipe(eph_sk, sizeof(eph_sk));
	/* used up only by an answer that opens: a signed but junk KEYRSP
	   must not cancel the real one */
	if (ok)
		e2e_json_remove(pending, pk);
	g_free(pk);
	if (!ok)
		goto out;
	ok = FALSE;
	fp_hex = e2e_fingerprint_hex(rsp->pub);
	if (!check_peer(kr, fp_hex, rsp->pub, sender_handle, ctx, &change))
		goto out;
	update_peer(kr, fp_hex, rsp->pub, sender_handle, nick, "trusted");
	set_session(kr, sender_handle, ctx, fp_hex, key, "trusted");
	ok = TRUE;
out:
	e2e_wipe(key, sizeof(key));
	g_free(ctx);
	g_free(fp_hex);
	e2e_handshake_free(rsp);
	return ok;
}

/* a REKEY nonce seen before: a replay (repartee's e2e_seen_rekeys). The
   nonce is used up even when the unwrap fails - a sender never reuses
   one. "seen_rekeys": { "<fp>|<ctx>": { <nonce b64>: seen_at } }. A REKEY
   has no timestamp or counter, so nothing is ever forgotten: with
   E2E_MAX_SEEN_REKEYS of a peer and context remembered, its further
   REKEYs are refused - its next message then cannot be read and asks for
   the key with a KEYREQ, whose answer cannot be replayed. */
static gboolean consume_rekey_nonce(E2E_JSON *kr, const char *fp_hex, const char *ctx,
                                    const unsigned char nonce[16])
{
	E2E_JSON *seen;
	char *k, *n;
	gboolean fresh;

	k = pipe_key(fp_hex, ctx);
	seen = e2e_json_object_member(e2e_json_object_member(kr, "seen_rekeys"), k);
	g_free(k);
	n = e2e_b64_encode(nonce, 16);
	fresh = e2e_json_get(seen, n) == NULL && e2e_json_size(seen) < E2E_MAX_SEEN_REKEYS;
	if (fresh)
		e2e_json_set_int(seen, n, e2e_now());
	g_free(n);
	return fresh;
}

gboolean e2e_handle_rekey(E2E_JSON *kr, const E2E_IDENTITY *id, const E2E_NET *net,
                          const char *sender_handle, const char *nick, const char *body)
{
	E2E_HANDSHAKE *rk;
	E2E_JSON *sess;
	PEER_CHANGE change;
	unsigned char x_sk[32], key[32];
	const char *old_fp;
	char *fp_hex = NULL, *cfg_ctx, *k, *ctx = NULL, *twin, *s_nonce, *used_key = NULL;
	gboolean ok = FALSE, on, from_twin;

	rk = e2e_handshake_parse(body, E2E_HS_REKEY);
	if (rk == NULL)
		return FALSE;
	if (!e2e_handshake_verify(rk))
		goto out;
	fp_hex = e2e_fingerprint_hex(rk->pub);
	ctx = e2e_ctx_find(kr, net, rk->channel);
	if (ctx == NULL)
		goto out;
	/* E2E has to be on: the context, for a DM the peer's config ("@" +
	   their handle on this network; the REKEY names the direction to us,
	   "@<own>") */
	cfg_ctx = e2e_is_channel(rk->channel) ? g_strdup(ctx)
	                                      : dm_config_ctx(kr, net, sender_handle);
	on = cfg_ctx != NULL && e2e_ctx_enabled(kr, cfg_ctx);
	g_free(cfg_ctx);
	if (!on)
		goto out;
	/* it replaces a trusted session of this handle and context with this
	   key; it never creates one (rpe2e.pl took it from anyone it had seen,
	   even a peer still waiting for /e2e accept) */
	sess = row_or_twin(kr, net, "incoming", sender_handle, ctx, TRUE, &used_key);
	k = pipe_key(sender_handle, ctx);
	from_twin = strcmp(used_key, k) != 0;
	g_free(k);
	g_free(used_key);
	if (!is_trusted(sess) || g_strcmp0(e2e_json_get_string(sess, "fp"), fp_hex) != 0)
		goto out;
	if (classify_peer_change(kr, fp_hex, sender_handle, &old_fp) == CHANGE_NEW)
		goto out;
	if (!check_peer(kr, fp_hex, rk->pub, sender_handle, ctx, &change))
		goto out;
	/* a nonce used under the old name is used */
	twin = legacy_twin(kr, net, ctx);
	if (twin != NULL) {
		gboolean used;

		k = pipe_key(fp_hex, twin);
		s_nonce = e2e_b64_encode(rk->nonce, 16);
		used = e2e_json_get(e2e_json_get(e2e_json_get(kr, "seen_rekeys"), k), s_nonce) != NULL;
		g_free(s_nonce);
		g_free(k);
		g_free(twin);
		if (used)
			goto out;
	}
	if (!consume_rekey_nonce(kr, fp_hex, ctx, rk->nonce))
		goto out;
	/* taken through the old session: its rows still count where they are
	   (another network alone later) - the nonce is used for them too */
	if (from_twin && (twin = legacy_twin(kr, net, ctx)) != NULL) {
		E2E_JSON *seen;

		k = pipe_key(fp_hex, twin);
		seen = e2e_json_object_member(e2e_json_object_member(kr, "seen_rekeys"), k);
		s_nonce = e2e_b64_encode(rk->nonce, 16);
		e2e_json_set_int(seen, s_nonce, e2e_now());
		g_free(s_nonce);
		g_free(k);
		g_free(twin);
	}
	if (!e2e_ed25519_sk_to_x25519(x_sk, id->sk))
		goto out;
	ok = unwrap(rk, x_sk, REKEY_INFO, key);
	e2e_wipe(x_sk, sizeof(x_sk));
	if (!ok)
		goto out;
	update_peer(kr, fp_hex, rk->pub, sender_handle, nick, "trusted");
	set_session(kr, sender_handle, ctx, fp_hex, key, "trusted");
out:
	e2e_wipe(key, sizeof(key));
	g_free(ctx);
	g_free(fp_hex);
	e2e_handshake_free(rk);
	return ok;
}

gboolean e2e_handshake_allow(GHashTable *stamps, E2E_HS_TYPE type, const char *sender,
                             const char *ctx)
{
	static const char *const names[] = { "", "KEYREQ", "KEYRSP", "REKEY" };
	char *key;
	gboolean ok;

	key = g_strconcat(names[type <= E2E_HS_REKEY ? type : 0], "|", sender, "|", ctx, NULL);
	ok = e2e_stamp_allow(stamps, key, E2E_KEYREQ_INBOUND_MIN_INTERVAL);
	g_free(key);
	return ok;
}

/* ---- trust decisions of the user ---- */

E2E_ACCEPT_RESULT e2e_accept(E2E_JSON *kr, const E2E_IDENTITY *id, const char *handle,
                             const char *ctx, const char *own_ctx, const char *own_handle,
                             char **rsp, char **reciprocal)
{
	const char *ctxs[2] = { ctx, own_ctx };
	E2E_JSON *pending, *rows[2];
	unsigned char pub[32], eph[32];
	char *k, *fp_hex;
	const char *wire = e2e_ctx_wire(ctx);
	int i, keyed = 0;

	*rsp = *reciprocal = NULL;
	/* a query's key goes to that query's peer only: a request (or a
	   session) of another address under it - rpe2e.pl stored such
	   requests - would get them our key for someone else */
	if (!e2e_is_channel(wire) && (*wire != '@' || strcmp(wire + 1, handle) != 0))
		return E2E_ACCEPT_NOT_PEER;
	k = pipe_key(handle, ctx);
	pending = e2e_json_get(e2e_json_get(kr, "pending_inbound"), k);
	if (pending != NULL) {
		if (!b64_member(pending, "pubkey", pub, 32) ||
		    !b64_member(pending, "eph_x25519", eph, 32)) {
			g_free(k);
			return E2E_ACCEPT_DAMAGED;
		}
		*rsp = e2e_build_keyrsp_for_req(kr, id, ctx, handle, pub, eph);
		if (*rsp == NULL) {
			g_free(k);
			return E2E_ACCEPT_FAILED;
		}
		e2e_json_remove(e2e_json_get(kr, "pending_inbound"), k);
		g_free(k);
		fp_hex = e2e_fingerprint_hex(pub);
		mark_accepted(kr, handle, ctx, fp_hex, "accept");
		g_free(fp_hex);
		*reciprocal = reciprocal_on_accept(kr, id, ctx, own_ctx, handle, own_handle);
		return E2E_ACCEPT_SENT;
	}
	g_free(k);

	/* no request waiting: the session(s) of this peer here - for a query
	   the one under "@<own>" holds the key */
	for (i = 0; i < 2; i++) {
		rows[i] = NULL;
		if (ctxs[i] == NULL || (i == 1 && strcmp(ctxs[1], ctx) == 0))
			continue;
		k = pipe_key(handle, ctxs[i]);
		rows[i] = e2e_json_get(e2e_json_get(kr, "incoming"), k);
		g_free(k);
	}
	if (rows[0] == NULL && rows[1] == NULL)
		return E2E_ACCEPT_NOTHING;
	for (i = 0; i < 2; i++)
		keyed += e2e_session_has_key(rows[i]);
	/* trusted without a key would show anything encrypted under the
	   placeholder as genuine (rpe2e.pl did that) */
	if (keyed == 0)
		return E2E_ACCEPT_NO_KEY;
	for (i = 0; i < 2; i++) {
		const char *fp = e2e_json_get_string(rows[i], "fp");

		if (!e2e_session_has_key(rows[i]))
			continue;
		e2e_json_set_string(rows[i], "status", "trusted");
		if (fp != NULL)
			mark_accepted(kr, handle, ctx, fp, "accept");
	}
	return E2E_ACCEPT_TRUSTED;
}

int e2e_unrevoke(E2E_JSON *kr, const char *handle, const char *ctx, const char *own_ctx)
{
	const char *ctxs[2] = { ctx, own_ctx };
	int i, n = 0;

	for (i = 0; i < 2; i++) {
		E2E_JSON *row;
		char *k;

		if (ctxs[i] == NULL || (i == 1 && strcmp(ctxs[1], ctx) == 0))
			continue;
		k = pipe_key(handle, ctxs[i]);
		row = e2e_json_get(e2e_json_get(kr, "incoming"), k);
		g_free(k);
		if (e2e_session_has_key(row)) {
			e2e_json_set_string(row, "status", "trusted");
			n++;
		}
	}
	return n;
}

gboolean e2e_session_accepted(const E2E_JSON *kr, const char *handle, const char *ctx,
                              const char *fp)
{
	char *peer = g_strconcat("@", handle, NULL);
	char *actx = e2e_is_channel(e2e_ctx_wire(ctx)) ? g_strdup(ctx) : e2e_ctx_rescope(ctx, peer);
	gboolean ok = actx != NULL && e2e_peer_accepted(kr, handle, actx, fp);

	g_free(peer);
	g_free(actx);
	return ok;
}

/* The config key ctx counts on net, and the bare name it stands for
   there (*wire): this network's, or an old unscoped one that no config of
   this network hides (that one wins, e2e_adopt_legacy). Without a network
   every key counts as it is, as before the scope. */
static gboolean config_on_net(const E2E_JSON *kr, const char *ctx, const E2E_NET *net,
                              const char **wire)
{
	char *scoped;
	gboolean hidden;

	*wire = ctx;
	if (net == NULL)
		return TRUE;
	*wire = e2e_ctx_wire(ctx);
	if (!e2e_ctx_on_net(ctx, net)) {
		/* another network's: only one that took old rows of the name
		   over - they may have been this network's (e2e_ctx_lookup) */
		if (e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "channels"), ctx),
		                        E2E_ADOPTED_MEMBER) == NULL)
			return FALSE;
	} else if (strchr(ctx, E2E_SCOPE_SEP) != NULL) {
		return TRUE;
	}
	/* one of this network's own configs hides it */
	scoped = e2e_ctx_scoped(net, *wire);
	hidden = scoped != NULL && e2e_json_get(e2e_json_get(kr, "channels"), scoped) != NULL;
	g_free(scoped);
	return !hidden;
}

/* the CASEMAPPING of net (no network: rfc1459, as before the scope) */
static E2E_CASEMAP net_casemap(const E2E_NET *net)
{
	return net != NULL ? net->casemap : E2E_CASEMAP_RFC1459;
}

/* a nick folded once by the CASEMAPPING of net (the superset fold;
   without a network, rfc1459), to compare with many; narrow: only what
   every server of that CASEMAPPING takes for one (e2e_casefold_narrow) */
typedef struct {
	const E2E_NET *net;
	const char *nick;
	char *fold;		/* NULL: matches nothing */
	gboolean narrow;
} NICK_FOLD;

static char *fold_nick(const E2E_NET *net, const char *nick, gboolean narrow)
{
	return narrow ? e2e_casefold_narrow(nick, net_casemap(net)) : e2e_casefold(nick, net_casemap(net));
}

static void nick_fold_init(NICK_FOLD *f, const E2E_NET *net, const char *nick, gboolean narrow)
{
	f->net = net;
	f->nick = nick;
	f->narrow = narrow;
	f->fold = nick != NULL ? fold_nick(net, nick, narrow) : NULL;
}

static void nick_fold_clear(NICK_FOLD *f)
{
	g_free(f->fold);
	f->fold = NULL;
}

/* other may be the same nick as f's to the server */
static gboolean nick_fold_same(const NICK_FOLD *f, const char *other)
{
	char *fo;
	gboolean same;

	if (f->fold == NULL || other == NULL)
		return FALSE;
	/* one name folds one way */
	if (strcmp(other, f->nick) == 0)
		return TRUE;
	fo = fold_nick(f->net, other, f->narrow);
	same = fo != NULL && strcmp(fo, f->fold) == 0;
	g_free(fo);
	return same;
}

/* the peers by their last_handle: handle -> GPtrArray of their last_nick
   (strings of kr), in the order of the peers' keys; one look per peer
   instead of one per peer and config */
static GHashTable *peers_by_handle(const E2E_JSON *kr)
{
	E2E_JSON *peers = e2e_json_get(kr, "peers");
	GPtrArray *keys = e2e_json_keys(peers);
	GHashTable *idx = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
	                                        (GDestroyNotify) g_ptr_array_unref);
	guint i;

	for (i = 0; i < keys->len; i++) {
		E2E_JSON *peer = e2e_json_get(peers, g_ptr_array_index(keys, i));
		const char *handle = e2e_json_get_string(peer, "last_handle");
		const char *nick = e2e_json_get_string(peer, "last_nick");
		GPtrArray *nicks;

		if (handle == NULL || nick == NULL)
			continue;
		nicks = g_hash_table_lookup(idx, handle);
		if (nicks == NULL) {
			nicks = g_ptr_array_new();
			g_hash_table_insert(idx, (gpointer) handle, nicks);
		}
		g_ptr_array_add(nicks, (gpointer) nick);
	}
	g_ptr_array_unref(keys);
	return idx;
}

/* the last_nick of a peer last seen under handle that f matches, or NULL;
   *idx is built on the first call */
static const char *peer_nick_at(const E2E_JSON *kr, GHashTable **idx, const char *handle,
                                const NICK_FOLD *f)
{
	GPtrArray *nicks;
	guint i;

	if (handle == NULL || f->fold == NULL)
		return NULL;
	if (*idx == NULL)
		*idx = peers_by_handle(kr);
	nicks = g_hash_table_lookup(*idx, handle);
	for (i = 0; nicks != NULL && i < nicks->len; i++)
		if (nick_fold_same(f, g_ptr_array_index(nicks, i)))
			return g_ptr_array_index(nicks, i);
	return NULL;
}

/* the handles of the "@<handle>" configs with E2E on that belong to nick:
   its "nick" member, or a peer last seen as nick under that handle (nicks
   compared by the CASEMAPPING of net) */
static GPtrArray *dm_configs(const E2E_JSON *kr, const E2E_NET *net, const char *nick,
                             gboolean narrow)
{
	E2E_JSON *channels = e2e_json_get(kr, "channels");
	GPtrArray *keys = e2e_json_keys(channels);
	GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
	GHashTable *idx = NULL;
	NICK_FOLD f;
	guint i;

	nick_fold_init(&f, net, nick, narrow);
	for (i = 0; i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i), *ctx;

		if (!e2e_ctx_enabled(kr, key) || !config_on_net(kr, key, net, &ctx) || *ctx != '@')
			continue;
		if (nick_fold_same(&f, e2e_json_get_string(e2e_json_get(channels, key), "nick")) ||
		    peer_nick_at(kr, &idx, ctx + 1, &f) != NULL)
			g_ptr_array_add(out, g_strdup(ctx + 1));
	}
	nick_fold_clear(&f);
	if (idx != NULL)
		g_hash_table_destroy(idx);
	g_ptr_array_unref(keys);
	return out;
}

GPtrArray *e2e_dm_configs_of(const E2E_JSON *kr, const E2E_NET *net, const char *nick)
{
	return dm_configs(kr, net, nick, FALSE);
}

GPtrArray *e2e_query_off_targets(const E2E_JSON *kr, const E2E_NET *net, const char *nick,
                                 const char *live)
{
	GPtrArray *out = g_ptr_array_new_with_free_func(g_free), *mine;

	/* the address of this query; without one, the config of this query
	   when there is exactly one - never every address of the nick */
	if (live != NULL && *live != '\0') {
		g_ptr_array_add(out, g_strconcat("@", live, NULL));
		return out;
	}
	/* the nick of this query, not every nick the fold may take for it:
	   what turns E2E off is never wider than the server's rule */
	mine = dm_configs(kr, net, nick, TRUE);
	if (mine->len == 1)
		g_ptr_array_add(out, g_strconcat("@", (char *) g_ptr_array_index(mine, 0), NULL));
	g_ptr_array_unref(mine);
	return out;
}

gboolean e2e_buffer_has_privmsg(const char *buf, gsize len)
{
	gsize start, end;
	gboolean found = FALSE;

	for (start = 0; start < len && !found; start = end + 1) {
		char *line, *tags, *target, *body;

		for (end = start; end < len && buf[end] != '\r' && buf[end] != '\n' && buf[end] != '\0'; end++)
			;
		line = g_strndup(buf + start, end - start);
		found = e2e_parse_privmsg_line(line, &tags, &target, &body);
		if (found) {
			g_free(tags);
			g_free(target);
			e2e_wipe(body, strlen(body));
			g_free(body);
		}
		g_free(line);
	}
	return found;
}

/* ---- the outbound gate ---- */

static void add_warning(E2E_GATE_RESULT *res, char *text)
{
	if (res->warnings == NULL)
		res->warnings = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(res->warnings, text);
}

/* _distribute_rekey: the new key to everyone who got the old one, by
   NOTICE; best effort (a peer that misses it re-handshakes on its next
   undecryptable message) */
static void distribute_rekey(E2E_JSON *kr, const E2E_IDENTITY *id, const char *ctx,
                             const unsigned char key[32], E2E_GATE_RESULT *res)
{
	E2E_JSON *recipients = e2e_json_get(kr, "outgoing_recipients");
	GPtrArray *keys = e2e_json_keys(recipients), *dropped = g_ptr_array_new_with_free_func(g_free);
	guint i;

	for (i = 0; i < keys->len; i++) {
		E2E_JSON *row = e2e_json_get(recipients, g_ptr_array_index(keys, i)), *peer;
		unsigned char pk[32];
		const char *nick, *fp, *handle;
		char *wire;

		if (g_strcmp0(e2e_json_get_string(row, "channel"), ctx) != 0)
			continue;
		fp = e2e_json_get_string(row, "fingerprint");
		handle = e2e_json_get_string(row, "handle");
		/* someone else than the query's own address: told
		   once, then no recipient any more; gets nothing */
		if (handle != NULL && e2e_dm_row_foreign(handle, ctx)) {
			char *where = e2e_ctx_display(ctx), *who = e2e_text_display(handle);

			add_warning(res, g_strdup_printf("rekey to %s skipped: a damaged entry - accepted for the query %s of another address (a request rpe2e.pl kept); it gets no key - /e2e decline %s in that query removes it",
			                                 who, where, who));
			g_free(where);
			g_free(who);
			g_ptr_array_add(dropped, g_strdup(g_ptr_array_index(keys, i)));
			continue;
		}
		/* the new key only to someone accepted here (rpe2e.pl and repartee
		   handed the old one to whoever held a session): the others are
		   no recipients any more */
		if (handle == NULL || !e2e_peer_accepted(kr, handle, ctx, fp)) {
			char *where = e2e_ctx_display(ctx);

			add_warning(res, g_strdup_printf("rekey to %s skipped: never accepted on %s (a keyring of rpe2e.pl or repartee?) — they no longer get your key; /e2e accept them when they ask",
			                                 handle != NULL ? handle : "?", where));
			g_free(where);
			g_ptr_array_add(dropped, g_strdup(g_ptr_array_index(keys, i)));
			continue;
		}
		peer = fp != NULL ? e2e_json_get(e2e_json_get(kr, "peers"), fp) : NULL;
		if (peer == NULL || !b64_member(peer, "pk", pk, 32))
			continue;
		nick = e2e_json_get_string(peer, "last_nick");
		if (nick == NULL || *nick == '\0')
			continue;
		if (id == NULL) {
			add_warning(res, g_strdup_printf("rekey to %s skipped: no identity of ours to sign it",
			                                 nick));
			continue;
		}
		wire = e2e_build_rekey(id, ctx, pk, key);
		if (wire == NULL) {
			add_warning(res, g_strdup_printf("rekey to %s skipped: cannot convert the peer's Ed25519 key for REKEY",
			                                 nick));
			continue;
		}
		if (res->notices == NULL)
			res->notices = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(res->notices, g_strdup(nick));
		g_ptr_array_add(res->notices, wire);
	}
	g_ptr_array_unref(keys);
	for (i = 0; i < dropped->len; i++)
		e2e_json_remove(recipients, g_ptr_array_index(dropped, i));
	g_ptr_array_unref(dropped);
}

/* IRCnet: "!chan" names the "!XXXXXchan" with that channel ID (five
   letters or digits, in any case) - longname such a name whose short
   form folds to shortfold (a folded "!chan") */
static gboolean ircnet_short_name(const char *longname, const char *shortfold, E2E_CASEMAP map)
{
	char *shortname, *folded;
	gboolean same;
	int i;

	if (longname[0] != '!' || strlen(longname) < 7 || shortfold[0] != '!')
		return FALSE;
	for (i = 1; i <= 5; i++) {
		if (!g_ascii_isalnum(longname[i]))
			return FALSE;
	}
	shortname = g_strconcat("!", longname + 6, NULL);
	folded = e2e_casefold(shortname, map);
	same = strcmp(folded, shortfold) == 0;
	g_free(folded);
	g_free(shortname);
	return same;
}

typedef enum {
	OTHER_NET,		/* the configs of this network (the gate) */
	OTHER_SCRIPT,		/* rpe2e.pl's own rows, not those named like a reading */
	OTHER_SCRIPT_ALL	/* rpe2e.pl's own rows, a reading's too */
} OTHER_ROWS;

/* "#Secret" when E2E is on for "#secret" on this network: the server
   takes both for the same channel (its CASEMAPPING, e2e_casefold), so
   the line must not go out in clear text. The bare name of the config
   found (also an IRCnet "!XXXXXchan" for a "!chan"). When none is found,
   *undecided: a reading or the name of a channel
   config with E2E on cannot be folded as the server folds it (not UTF-8
   or an unassigned code point on a Unicode CASEMAPPING, a non-ASCII name
   on an unknown one) - *bad that name, *other the config's (a reading
   may be any channel to the server). rows: which configs (OTHER_ROWS);
   rpe2e.pl compares names exactly, and does not split "#a,#b" */
static char *enabled_other_case(const E2E_JSON *kr, const E2E_NET *net, char **readings,
                                OTHER_ROWS rows, gboolean *undecided, const char **bad,
                                const char **other)
{
	E2E_JSON *channels = e2e_json_get(kr, "channels");
	GPtrArray *keys = e2e_json_keys(channels);
	E2E_CASEMAP map = net_casemap(net);
	char *found = NULL;
	guint i, j;

	*undecided = FALSE;
	*bad = *other = NULL;
	for (j = 0; readings[j] != NULL && found == NULL; j++) {
		gboolean want_exact, have_exact;
		char *want = e2e_casefold_ex(readings[j], map, &want_exact);

		for (i = 0; i < keys->len && found == NULL; i++) {
			const char *key = g_ptr_array_index(keys, i), *wire;
			char *have;

			if (rows == OTHER_NET) {
				if (!config_on_net(kr, key, net, &wire))
					continue;
			} else {
				/* rpe2e.pl's own rows: it encrypts from them whatever
				   the module's config of the name says */
				if (strchr(key, E2E_SCOPE_SEP) != NULL)
					continue;
				wire = key;
				/* a reading itself is the script's to encrypt */
				if (rows == OTHER_SCRIPT &&
				    g_strv_contains((const char *const *) readings, wire))
					continue;
			}
			/* channels only: a DM config is no channel name */
			if (!e2e_ctx_enabled(kr, key) || !e2e_is_channel(wire))
				continue;
			have = e2e_casefold_ex(wire, map, &have_exact);
			/* an unknown CASEMAPPING decides no non-ASCII name, but a
			   byte code page keeps ASCII names apart from the others:
			   for an ASCII reading only what the superset fold cannot
			   decide counts */
			if (!have_exact && want_exact && map == E2E_CASEMAP_UNKNOWN)
				g_free(e2e_casefold_ex(wire, E2E_CASEMAP_UNICODE, &have_exact));
			if (strcmp(have, want) == 0 || ircnet_short_name(wire, want, map) ||
			    ircnet_short_name(readings[j], have, map)) {
				found = g_strdup(wire);
			} else if ((!want_exact || !have_exact) && !*undecided) {
				*undecided = TRUE;
				*bad = want_exact ? wire : readings[j];
				*other = wire;
			}
			g_free(have);
		}
		g_free(want);
	}
	g_ptr_array_unref(keys);
	if (found != NULL)
		*undecided = FALSE;
	return found;
}

/* The "@<handle>" with E2E on that belongs to nick, other than the live
   handle: a config of a query with that nick (its "nick" member) or of a
   peer last seen under that nick. NULL when there is none. */
char *e2e_enabled_dm_elsewhere(const E2E_JSON *kr, const E2E_NET *net, const char *nick,
                               const char *live)
{
	GPtrArray *mine = e2e_dm_configs_of(kr, net, nick);
	char *found = NULL;
	guint i;

	for (i = 0; i < mine->len && found == NULL; i++)
		if (live == NULL || strcmp(g_ptr_array_index(mine, i), live) != 0)
			found = g_strdup(g_ptr_array_index(mine, i));
	g_ptr_array_unref(mine);
	return found;
}

/* the name of a target cannot be kept per network: a reading holds 0x1F
   (an old row of that name would read as another network's), or the
   network has no label */
static gboolean unscopable(const E2E_NET *net, char **readings)
{
	int i;

	if (net == NULL)
		return FALSE;
	if (net->label == NULL)
		return TRUE;
	for (i = 0; readings != NULL && readings[i] != NULL; i++)
		if (strchr(readings[i], E2E_SCOPE_SEP) != NULL)
			return TRUE;
	return FALSE;
}

/* E2E is on for ctx (what e2e_ctx_lookup gave for wire): for an old
   context taken over on the only network, the scoped config wins when
   there is one (e2e_adopt_legacy) */
static gboolean lookup_enabled(const E2E_JSON *kr, const E2E_NET *net, E2E_CTX_KIND kind,
                               const char *ctx, const char *wire)
{
	char *scoped;
	gboolean on;

	if (kind != E2E_CTX_LEGACY)
		return e2e_ctx_enabled(kr, ctx);
	scoped = e2e_ctx_scoped(net, wire);
	on = e2e_ctx_enabled(kr, scoped != NULL &&
	                         e2e_json_get(e2e_json_get(kr, "channels"), scoped) != NULL ? scoped : ctx);
	g_free(scoped);
	return on;
}

/* the bare name of a config key that may be an old bare-nick row of a
   nick on net, unscoped or of this network, or NULL; a "@<handle>"
   config is none (a channel is ruled out by the caller: is_nick_wire) */
static const char *bare_nick_row(const char *key, const E2E_NET *net)
{
	const char *wire = e2e_ctx_wire(key);

	if (*wire == '\0' || *wire == '@')
		return NULL;
	if (strchr(key, E2E_SCOPE_SEP) != NULL &&
	    (net == NULL || net->label == NULL || !e2e_ctx_on_net(key, net)))
		return NULL;
	return wire;
}

/* wire names no channel */
static gboolean is_nick_wire(const char *wire)
{
	char **readings = e2e_channel_readings(wire);
	gboolean nick = readings[0] == NULL;

	g_strfreev(readings);
	return nick;
}

/* an old bare-nick config of a DM target with E2E on, unscoped or of this
   network: the name as it is, or any the server of net may take for the
   same nick (its CASEMAPPING, as channels: only more refusals) */
static gboolean nick_enabled(const E2E_JSON *kr, const E2E_NET *net, const char *nick)
{
	char *scoped = e2e_ctx_scoped(net, nick);
	gboolean on = e2e_ctx_enabled(kr, nick) || (scoped != NULL && e2e_ctx_enabled(kr, scoped));
	GPtrArray *keys;
	NICK_FOLD f;
	guint i;

	g_free(scoped);
	if (on)
		return TRUE;
	nick_fold_init(&f, net, nick, FALSE);
	keys = e2e_json_keys(e2e_json_get(kr, "channels"));
	for (i = 0; i < keys->len && !on; i++) {
		const char *key = g_ptr_array_index(keys, i), *wire;

		/* the cheap tests first, the fold last but one */
		on = e2e_ctx_enabled(kr, key) && (wire = bare_nick_row(key, net)) != NULL &&
		     nick_fold_same(&f, wire) && is_nick_wire(wire);
	}
	g_ptr_array_unref(keys);
	nick_fold_clear(&f);
	return on;
}

/* the handle of a peer last seen as a nick the server of net may take for
   nick (its CASEMAPPING) whose "@<handle>" is BLOCKED on net (old rows on
   several networks, or another network's), or NULL: what the gate
   refuses when the keyring's cache names that peer exactly, also for the
   other spellings */
static const char *folded_peer_blocked(const E2E_JSON *kr, const E2E_NET *net, const char *nick)
{
	E2E_JSON *peers = e2e_json_get(kr, "peers");
	GPtrArray *keys = e2e_json_keys(peers);
	const char *found = NULL;
	NICK_FOLD f;
	guint i;

	nick_fold_init(&f, net, nick, FALSE);
	for (i = 0; i < keys->len && found == NULL; i++) {
		E2E_JSON *peer = e2e_json_get(peers, g_ptr_array_index(keys, i));
		const char *handle = e2e_json_get_string(peer, "last_handle");
		char *wire, *ctx = NULL;

		if (handle == NULL || *handle == '\0' ||
		    !nick_fold_same(&f, e2e_json_get_string(peer, "last_nick")))
			continue;
		wire = g_strconcat("@", handle, NULL);
		if (e2e_ctx_lookup(kr, net, wire, &ctx) == E2E_CTX_BLOCKED)
			found = handle;
		g_free(ctx);
		g_free(wire);
	}
	nick_fold_clear(&f);
	g_ptr_array_unref(keys);
	return found;
}

/* _gate_decide for one target; dry: decide only (no key is generated,
   nothing in kr changes) */
static void gate_single(E2E_JSON *kr, gboolean kr_ok, const E2E_IDENTITY *id,
                        const E2E_NET *net, const char *target, const char *body,
                        E2E_RESOLVE_FUNC resolve, void *resolve_data, gboolean dry,
                        E2E_GATE_RESULT *res)
{
	char **readings, **ctxs = NULL;
	E2E_CTX_KIND *kinds = NULL, kind = E2E_CTX_NEW;
	GPtrArray *on = NULL, *damaged = NULL;
	const char *error = NULL, *blocked = NULL, *wire = NULL;
	gboolean is_channel, is_action, rotated, generated, adopted = FALSE;
	unsigned char key[32];
	char *ctx = NULL, *handle, *dm_wire = NULL;
	guint i, n = 0;

	readings = e2e_channel_readings(target);
	is_channel = readings[0] != NULL;
	is_action = e2e_is_action(body);
	/* non-ACTION CTCP (VERSION, PING, ...) is a deliberate clear-text
	   escape hatch, as in repartee */
	if (e2e_is_ctcp(body) && !is_action) {
		res->action = E2E_GATE_PASS;
		goto out;
	}

	/* nothing can be stored for such a name on this network: decided as
	   before the scope, and never encrypted */
	if (unscopable(net, is_channel ? readings : NULL)) {
		gate_single(kr, kr_ok, id, NULL, target, body, resolve, resolve_data, TRUE, res);
		if (res->action == E2E_GATE_CIPHER ||
		    (res->action == E2E_GATE_REFUSE && kr_ok)) {
			e2e_gate_result_clear(res);
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup_printf(REFUSE_UNSCOPED, target);
		}
		goto out;
	}

	/* every reading on this network; an old one of unknown network
	   (BLOCKED) is never sent in clear text nor encrypted */
	on = g_ptr_array_new();
	if (is_channel) {
		n = g_strv_length(readings);
		ctxs = g_new0(char *, n + 1);
		kinds = g_new0(E2E_CTX_KIND, n);
		for (i = 0; i < n; i++) {
			kinds[i] = e2e_ctx_lookup(kr, net, readings[i], &ctxs[i]);
			if (kinds[i] == E2E_CTX_BLOCKED) {
				if (blocked == NULL)
					blocked = readings[i];
			} else if (lookup_enabled(kr, net, kinds[i], ctxs[i], readings[i])) {
				g_ptr_array_add(on, readings[i]);
			}
		}
	}

	/* bot commands go out in clear text, channel only, warned about
	   whenever E2E cannot be ruled out */
	if (is_channel && e2e_is_bot_command(body)) {
		res->action = E2E_GATE_BYPASS;
		if (!kr_ok || on->len > 0 || blocked != NULL)
			res->message = g_strdup_printf(BOT_BYPASS_WARN, readings[0]);
		goto out;
	}
	/* an unreadable keyring may hold an enabled config: fail closed */
	if (!kr_ok) {
		res->action = E2E_GATE_REFUSE;
		res->message = g_strdup(E2E_REFUSE_KEYRING);
		goto out;
	}

	if (is_channel) {
		if (blocked != NULL) {
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup_printf(E2E_REFUSE_LEGACY, blocked, net->label);
			goto out;
		}
		if (on->len > 1) {
			char *both;

			g_ptr_array_add(on, NULL);
			both = g_strjoinv(" and ", (char **) on->pdata);
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup_printf(REFUSE_AMBIGUOUS, target, both);
			g_free(both);
			goto out;
		}
		/* encrypting under the one enabled reading is always safe */
		for (i = 0; on->len > 0 && readings[i] != g_ptr_array_index(on, 0); i++)
			;
		wire = readings[i];
		kind = kinds[i];
		ctx = ctxs[i];
		ctxs[i] = NULL;
	} else {
		handle = resolve(target, kr, resolve_data);
		if (handle == NULL || *handle == '\0') {
			char *other;

			g_free(handle);
			/* the address is not known now (no query, no common channel),
			   but E2E may be on for the nick at the one it had: an old
			   bare-nick row, or a "@<handle>" config of the nick */
			other = e2e_enabled_dm_elsewhere(kr, net, target, NULL);
			if (nick_enabled(kr, net, target)) {
				res->action = E2E_GATE_REFUSE;
				res->message = g_strdup(E2E_REFUSE_NO_HANDLE);
			} else if (other != NULL) {
				res->action = E2E_GATE_REFUSE;
				res->message = g_strdup_printf(REFUSE_DM_UNKNOWN, target, other, target, other);
			} else if (net != NULL && (blocked = folded_peer_blocked(kr, net, target)) != NULL) {
				/* as for the address the cache names exactly */
				res->action = E2E_GATE_REFUSE;
				res->message = strchr(blocked, E2E_SCOPE_SEP) == NULL ?
					g_strdup_printf(E2E_REFUSE_LEGACY, target, net->label) :
					g_strdup_printf(REFUSE_UNSCOPED, target);
			} else {
				res->action = E2E_GATE_PASS;
			}
			g_free(other);
			goto out;
		}
		dm_wire = g_strconcat("@", handle, NULL);
		g_free(handle);
		wire = dm_wire;
		kind = e2e_ctx_lookup(kr, net, wire, &ctx);
		if (kind == E2E_CTX_BLOCKED) {
			res->action = E2E_GATE_REFUSE;
			/* a handle with 0x1F: nothing can be stored for it */
			res->message = net != NULL && strchr(wire, E2E_SCOPE_SEP) == NULL ?
				g_strdup_printf(E2E_REFUSE_LEGACY, target, net->label) :
				g_strdup_printf(REFUSE_UNSCOPED, target);
			goto out;
		}
	}

	if (!lookup_enabled(kr, net, kind, ctx, wire)) {
		gboolean undecided = FALSE;
		const char *bad = NULL, *config = NULL;
		char *other = is_channel ? enabled_other_case(kr, net, readings, OTHER_NET, &undecided, &bad, &config)
		                         : e2e_enabled_dm_elsewhere(kr, net, target, wire + 1);

		if (other != NULL && is_channel) {
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup_printf(REFUSE_CASE, other, target, other);
		} else if (undecided) {
			/* may be the same channel, never "not encrypted" */
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup_printf(REFUSE_UNDECIDED, bad, target, config);
		} else if (other != NULL) {
			/* the nick has another ident@host now (a new host, or
			   someone else took the nick): never clear text where the
			   user expects E2E */
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup_printf(REFUSE_DM_MOVED, target, other, target, wire + 1, other);
		} else {
			res->action = E2E_GATE_PASS;
		}
		g_free(other);
		goto out;
	}
	if (dry) {
		/* what it would be: an old context is not taken over now */
		res->ctx = kind == E2E_CTX_LEGACY ? e2e_ctx_scoped(net, wire) : g_strdup(ctx);
		res->action = E2E_GATE_CIPHER;
		goto out;
	}
	/* the only network: the old rows become this network's; for a DM
	   also the peer's sessions to us ("<peer>|@<own>"), not those of
	   others */
	if (kind == E2E_CTX_LEGACY) {
		g_free(ctx);
		ctx = e2e_ctx_resolve(kr, net, wire, &adopted);
		if (ctx == NULL) {
			res->action = E2E_GATE_REFUSE;
			res->message = g_strdup(E2E_REFUSE_ENCRYPT);
			goto out;
		}
		if (!is_channel)
			e2e_adopt_legacy_peer_sessions(kr, net->label, wire + 1);
	}
	res->ctx = g_strdup(ctx);
	res->save_needed = adopted;
	/* another address accepted for this query may hold our key:
	   it rotates now, the new one reaches only the query's peer */
	damaged = e2e_rotate_for_damaged(kr, ctx);
	for (i = 0; i + 1 < damaged->len; i += 2) {
		char *who = e2e_text_display(g_ptr_array_index(damaged, i));
		char *where = e2e_ctx_display(g_ptr_array_index(damaged, i + 1));

		add_warning(res, g_strdup_printf("a damaged entry: %s was accepted for the query %s of another address (a request rpe2e.pl kept) and may hold your key - it rotates now, the new one reaches only that query's peer; /e2e decline %s in this query removes the entry",
		                                 who, where, who));
		g_free(who);
		g_free(where);
	}
	if (!e2e_outgoing_key(kr, ctx, key, &rotated, &generated)) {
		res->action = E2E_GATE_REFUSE;
		res->message = g_strdup(E2E_REFUSE_ENCRYPT);
		goto out;
	}
	if (rotated)
		distribute_rekey(kr, id, ctx, key, res);
	/* written only when a key was generated or rotated (or old rows were
	   taken over), not per message */
	res->save_needed = generated || adopted || damaged->len > 0;
	/* the AAD is the bare context */
	res->wires = is_action ? e2e_encrypt_ctcp(key, e2e_ctx_wire(ctx), body, e2e_now(), &error)
	                       : e2e_encrypt_plain(key, e2e_ctx_wire(ctx), body, e2e_now(), &error);
	e2e_wipe(key, sizeof(key));
	if (res->wires == NULL) {
		res->action = E2E_GATE_REFUSE;
		res->message = g_strdup(E2E_REFUSE_ENCRYPT);
		goto out;
	}
	res->action = E2E_GATE_CIPHER;
out:
	if (on != NULL)
		g_ptr_array_unref(on);
	if (damaged != NULL)
		g_ptr_array_unref(damaged);
	for (i = 0; ctxs != NULL && i < n; i++)
		g_free(ctxs[i]);
	g_free(ctxs);
	g_free(kinds);
	g_free(dm_wire);
	g_free(ctx);
	g_strfreev(readings);
}

static void gate_decide(E2E_JSON *kr, gboolean kr_ok, const E2E_IDENTITY *id,
                        const E2E_NET *net, const char *target, const char *body,
                        E2E_RESOLVE_FUNC resolve, void *resolve_data, gboolean dry,
                        E2E_GATE_RESULT *res)
{
	char **targets;
	int i;

	memset(res, 0, sizeof(*res));
	if (strchr(target, ',') == NULL) {
		gate_single(kr, kr_ok, id, net, target, body, resolve, resolve_data, dry, res);
		return;
	}
	/* "#a,#b": one line cannot be encrypted for several contexts. Clear
	   text only when no target is encrypted. */
	res->action = E2E_GATE_PASS;
	targets = g_strsplit(target, ",", -1);
	for (i = 0; targets[i] != NULL; i++) {
		E2E_GATE_RESULT one;

		if (*targets[i] == '\0')
			continue;
		memset(&one, 0, sizeof(one));
		gate_single(kr, kr_ok, id, net, targets[i], body, resolve, resolve_data, TRUE, &one);
		if (one.action == E2E_GATE_REFUSE || one.action == E2E_GATE_CIPHER) {
			g_free(res->message);
			res->action = E2E_GATE_REFUSE;
			res->message = one.action == E2E_GATE_REFUSE && !kr_ok ? g_strdup(one.message)
			                                                       : g_strdup_printf(REFUSE_MULTI, target);
			e2e_gate_result_clear(&one);
			break;
		}
		if (one.action == E2E_GATE_BYPASS && res->action == E2E_GATE_PASS) {
			res->action = E2E_GATE_BYPASS;
			res->message = one.message;
			one.message = NULL;
		}
		e2e_gate_result_clear(&one);
	}
	g_strfreev(targets);
}

void e2e_gate_decide(E2E_JSON *kr, gboolean kr_ok, const E2E_IDENTITY *id,
                     const E2E_NET *net, const char *target, const char *body,
                     E2E_RESOLVE_FUNC resolve, void *resolve_data, E2E_GATE_RESULT *res)
{
	gate_decide(kr, kr_ok, id, net, target, body, resolve, resolve_data, FALSE, res);
}

void e2e_gate_decide_dry(const E2E_JSON *kr, gboolean kr_ok, const E2E_NET *net,
                         const char *target, const char *body,
                         E2E_RESOLVE_FUNC resolve, void *resolve_data, E2E_GATE_RESULT *res)
{
	/* dry: gate_single and gate_decide never write to kr */
	gate_decide((E2E_JSON *) kr, kr_ok, NULL, net, target, body, resolve, resolve_data, TRUE, res);
}

void e2e_gate_result_clear(E2E_GATE_RESULT *res)
{
	if (res->wires != NULL)
		g_ptr_array_unref(res->wires);
	if (res->notices != NULL)
		g_ptr_array_unref(res->notices);
	if (res->warnings != NULL)
		g_ptr_array_unref(res->warnings);
	g_free(res->message);
	g_free(res->ctx);
	memset(res, 0, sizeof(*res));
}

gboolean e2e_gate_lines_fit(const E2E_GATE_RESULT *res, const char *tags, const char *target,
                            gsize max_len)
{
	/* "PRIVMSG <target> :<wire>"; IRCv3 tags have a budget of their own
	   (irc_send_cmd_full) */
	gsize fixed = strlen("PRIVMSG ") + strlen(target) + strlen(" :");
	guint i;

	for (i = 0; res->wires != NULL && i < res->wires->len; i++)
		if (fixed + strlen(g_ptr_array_index(res->wires, i)) > max_len)
			return FALSE;
	return TRUE;
}

char *e2e_gate_multiline(E2E_JSON *kr, gboolean kr_ok, const E2E_NET *net,
                         const char *buf, gsize len,
                         E2E_RESOLVE_FUNC resolve, void *resolve_data)
{
	char *message = NULL;
	gsize start, end;

	/* every piece a server may take for a line of its own: ircd 2.11
	   ends a line at CR as well as at LF */
	for (start = 0; start < len && message == NULL; start = end + 1) {
		char *line, *tags, *target, *body;
		E2E_GATE_RESULT res;

		for (end = start; end < len && buf[end] != '\r' && buf[end] != '\n' && buf[end] != '\0'; end++)
			;
		line = g_strndup(buf + start, end - start);
		if (*line != '\0' && e2e_parse_privmsg_line(line, &tags, &target, &body)) {
			gate_decide(kr, kr_ok, NULL, net, target, body, resolve, resolve_data, TRUE, &res);
			if (res.action == E2E_GATE_REFUSE)
				message = g_strdup_printf(REFUSE_RAW_LINES, res.message);
			else if (res.action == E2E_GATE_CIPHER)
				message = g_strdup_printf(REFUSE_RAW_LINES, target);
			e2e_gate_result_clear(&res);
			g_free(tags);
			g_free(target);
			e2e_wipe(body, strlen(body));
			g_free(body);
		}
		e2e_wipe(line, strlen(line));
		g_free(line);
	}
	return message;
}

/* ---- incoming messages ---- */

static gboolean try_decrypt(const E2E_WIRE *w, const E2E_JSON *row, const char *ctx,
                            E2E_IN_RESULT *res)
{
	unsigned char key[32];
	char *pt;
	size_t len, i;

	/* never the all-zero placeholder: anyone can encrypt under it */
	if (!e2e_session_has_key(row) || !b64_member(row, "sk", key, 32))
		return FALSE;
	/* the AAD is the bare context */
	pt = e2e_wire_decrypt(w, key, e2e_ctx_wire(ctx), &len);
	e2e_wipe(key, sizeof(key));
	if (pt == NULL)
		return FALSE;
	/* no line breaks or NUL into irssi, logs and scripts: a decrypted
	   message is one line (formatting codes stay) */
	for (i = 0; i < len; i++)
		if (pt[i] == '\r' || pt[i] == '\n' || pt[i] == '\0')
			pt[i] = ' ';
	res->plain = e2e_utf8_clean(pt, len);
	e2e_wipe(pt, len);
	g_free(pt);
	res->action = E2E_IN_PLAIN;
	return TRUE;
}

/* the storage context of wire on the network of p, an old one where it
   is (a message from others never takes old rows over); NULL for an old
   context of unknown network, which is treated as E2E off */
static char *in_ctx(const E2E_JSON *kr, const E2E_IN_PARAMS *p, const char *wire)
{
	return e2e_ctx_find(kr, p->net, wire);
}

/* in the incoming row key "<handle>|<ctx>" of handle (the row's "handle"
   member, else everything up to the first '|'), the ctx; NULL for a row
   of another handle */
static const char *incoming_ctx_of(const char *key, const E2E_JSON *row, const char *handle)
{
	const char *h = e2e_json_get_string(row, "handle");
	gsize len;

	if (h == NULL) {
		const char *bar = strchr(key, '|');

		len = bar != NULL ? (gsize) (bar - key) : strlen(key);
	} else {
		len = strlen(h);
		if (strncmp(key, h, len) != 0)
			return NULL;
	}
	if (key[len] != '|' || strlen(handle) != len || strncmp(key, handle, len) != 0)
		return NULL;
	return key + len + 1;
}

/* have (an e2e_casefold_narrow() result, or NULL) is name's */
static gboolean narrow_same(const char *have, const char *name, E2E_CASEMAP map)
{
	char *want = e2e_casefold_narrow(name, map);
	gboolean same = have != NULL && want != NULL && strcmp(have, want) == 0;

	g_free(want);
	return same;
}

/* wire is the target of p (or another reading of it) to every server of
   map: the same under e2e_casefold_narrow() */
static gboolean surely_same_channel(const char *wire, const E2E_IN_PARAMS *p, E2E_CASEMAP map)
{
	char *have = e2e_casefold_narrow(wire, map);
	gboolean same = narrow_same(have, p->target, map);
	int i;

	for (i = 0; !same && p->alt_ctxs != NULL && p->alt_ctxs[i] != NULL; i++)
		same = narrow_same(have, p->alt_ctxs[i], map);
	g_free(have);
	return same;
}

/* No session of the sender for the channel as the wire names it: the
   sender's sessions for a channel the server may take for the same one
   (its CASEMAPPING, the superset fold), each one more AEAD attempt with
   the AAD of the name it was set up for - the name the sender encrypts
   under. Each name is looked up on this network as the wire's own name
   would be (in_ctx: an old row of unknown network is not used, another
   network's never). The AEAD does NOT reject a line of the sender's
   that the server moved from such a channel to the target (the AAD is
   that channel's name): where the two names are not one channel to
   every server of the CASEMAPPING, res->via names the session's channel
   for the display. TRUE when one opened it. */
static gboolean try_decrypt_folded(const E2E_JSON *kr, const E2E_IN_PARAMS *p, const E2E_WIRE *w,
                                   E2E_IN_RESULT *res)
{
	E2E_JSON *incoming = e2e_json_get(kr, "incoming");
	GPtrArray *keys = e2e_json_keys(incoming), *wants = g_ptr_array_new_with_free_func(g_free);
	GHashTable *tried = g_hash_table_new(g_str_hash, g_str_equal);
	E2E_CASEMAP map = net_casemap(p->net);
	gboolean done = FALSE;
	guint i, j;

	g_hash_table_add(tried, (char *) p->target);
	g_ptr_array_add(wants, e2e_casefold(p->target, map));
	for (i = 0; p->alt_ctxs != NULL && p->alt_ctxs[i] != NULL; i++) {
		g_hash_table_add(tried, p->alt_ctxs[i]);
		g_ptr_array_add(wants, e2e_casefold(p->alt_ctxs[i], map));
	}
	for (i = 0; i < keys->len && !done; i++) {
		const char *key = g_ptr_array_index(keys, i), *x, *wire;
		E2E_JSON *row;
		char *have, *ctx;
		gboolean same = FALSE;

		x = incoming_ctx_of(key, e2e_json_get(incoming, key), p->handle);
		if (x == NULL || (p->net != NULL && !e2e_ctx_on_net(x, p->net)))
			continue;
		wire = e2e_ctx_wire(x);
		if (!e2e_is_channel(wire) || g_hash_table_contains(tried, wire))
			continue;
		g_hash_table_add(tried, (char *) wire);
		have = e2e_casefold(wire, map);
		for (j = 0; j < wants->len && !same; j++)
			same = strcmp(have, g_ptr_array_index(wants, j)) == 0;
		g_free(have);
		if (!same)
			continue;
		/* this network's rows of the name, or old ones used in place */
		ctx = in_ctx(kr, p, wire);
		if (ctx == NULL || (p->net != NULL && !e2e_ctx_on_net(ctx, p->net))) {
			g_free(ctx);
			continue;
		}
		row = row_or_twin(kr, p->net, "incoming", p->handle, ctx, TRUE, NULL);
		if (is_trusted(row) && try_decrypt(w, row, ctx, res)) {
			g_free(res->ctx);
			res->ctx = ctx;
			done = TRUE;
			/* a name only the superset fold takes for the target's:
			   the server may keep them apart and have moved the line
			   - say whose key opened it */
			if (!surely_same_channel(wire, p, map))
				res->via = g_strdup(wire);
		} else {
			g_free(ctx);
		}
	}
	g_hash_table_destroy(tried);
	g_ptr_array_unref(wants);
	g_ptr_array_unref(keys);
	return done;
}

/* _decrypt_wire_message */
void e2e_decrypt_incoming(E2E_JSON *kr, const E2E_IDENTITY *id, const E2E_IN_PARAMS *p,
                          const char *text, E2E_IN_RESULT *res)
{
	E2E_WIRE *w;
	E2E_JSON *row;
	gboolean is_dm = !e2e_is_channel(p->target);
	char *wire = NULL, *ctx;
	int i;

	memset(res, 0, sizeof(*res));
	w = e2e_wire_parse(text);
	if (w == NULL) {
		res->action = E2E_IN_NOT_WIRE;
		return;
	}
	res->action = E2E_IN_DROP;
	if (p->is_own_line) {
		res->ctx = g_strdup("(own copy)");
	} else if (is_dm) {
		/* a DM is keyed by OUR handle (we are its recipient); never fall
		   back to the sender's, that would negotiate the wrong direction */
		if (p->own_handle == NULL || *p->own_handle == '\0') {
			res->action = E2E_IN_WAIT_OWN;
			goto out;
		}
		wire = g_strconcat("@", p->own_handle, NULL);
		res->ctx = g_strdup(wire);
	} else {
		wire = g_strdup(p->target);
		res->ctx = g_strdup(wire);
	}

	if (ABS(e2e_now() - w->ts) > E2E_TS_TOLERANCE) {
		res->debug = g_strdup_printf("drop ciphertext from %s (%s): timestamp skew",
		                             p->nick, p->handle);
		goto out;
	}

	if (p->is_own_line) {
		/* our own line from a bouncer or playback: open it with our
		   outgoing key; never a handle source, never a KEYREQ target */
		GPtrArray *ctxs = g_ptr_array_new_with_free_func(g_free);
		guint j;

		if (!is_dm) {
			g_ptr_array_add(ctxs, g_strdup(p->target));
			for (i = 0; p->alt_ctxs != NULL && p->alt_ctxs[i] != NULL; i++)
				g_ptr_array_add(ctxs, g_strdup(p->alt_ctxs[i]));
		} else {
			char *h = p->resolve(p->target, kr, p->resolve_data);

			if (h != NULL && *h != '\0')
				g_ptr_array_add(ctxs, g_strconcat("@", h, NULL));
			g_free(h);
		}
		for (j = 0; j < ctxs->len && res->action != E2E_IN_PLAIN; j++) {
			char *octx = in_ctx(kr, p, g_ptr_array_index(ctxs, j));

			row = octx != NULL ? e2e_json_get(e2e_json_get(kr, "outgoing"), octx) : NULL;
			if (row != NULL)
				try_decrypt(w, row, octx, res);
			g_free(octx);
		}
		if (res->action != E2E_IN_PLAIN)
			res->debug = g_strdup_printf("own-copy wire to %s undecryptable (rotated key?) — dropped",
			                             p->target);
		g_ptr_array_unref(ctxs);
		goto out;
	}

	/* the context on this network: an old one of unknown network is off
	   here, nothing is read or asked under it */
	ctx = in_ctx(kr, p, wire);
	if (ctx == NULL) {
		res->debug = g_strdup_printf("wire from %s (%s) for %s dropped: E2E there was set up with no network (by rpe2e.pl) and erssi uses several - /e2e adopt in this window binds it to this network",
		                             p->nick, p->handle, wire);
		goto out;
	}
	g_free(res->ctx);
	res->ctx = ctx;

	row = row_or_twin(kr, p->net, "incoming", p->handle, res->ctx, TRUE, NULL);
	/* an ambiguous +#chan: prefer the reading with a trusted session (a
	   wrong pick cannot leak, the AEAD just fails) */
	for (i = 0; p->alt_ctxs != NULL && p->alt_ctxs[i] != NULL && !is_trusted(row); i++) {
		E2E_JSON *r;
		char *actx = in_ctx(kr, p, p->alt_ctxs[i]);

		if (actx == NULL)
			continue;
		r = row_or_twin(kr, p->net, "incoming", p->handle, actx, TRUE, NULL);
		if (is_trusted(r)) {
			g_free(res->ctx);
			res->ctx = actx;
			actx = NULL;
			row = r;
		}
		g_free(actx);
	}

	/* a channel written otherwise: the session of the same channel to
	   the server */
	if (!is_trusted(row) && !is_dm && try_decrypt_folded(kr, p, w, res))
		goto out;
	if (!is_trusted(row)) {
		res->debug = g_strdup_printf("no trusted incoming for (%s,%s)", p->handle,
		                             e2e_ctx_wire(res->ctx));
		if (e2e_stamp_allow(p->keyreq_stamps, p->handle, E2E_KEYREQ_MIN_INTERVAL)) {
			/* enabled is the PEER-keyed config ("E2E with that peer"), the
			   KEYREQ asks for the direction we receive */
			char *cfg_ctx = is_dm ? dm_config_ctx(kr, p->net, p->handle)
			                      : g_strdup(res->ctx);
			char *error = NULL;

			if (id != NULL && cfg_ctx != NULL && e2e_ctx_enabled(kr, cfg_ctx)) {
				res->keyreq = e2e_build_keyreq(kr, id, res->ctx, p->handle, &error);
				if (res->keyreq != NULL)
					res->save_needed = TRUE;
			}
			g_free(error);
			g_free(cfg_ctx);
		}
		goto out;
	}
	if (!try_decrypt(w, row, res->ctx, res))
		res->debug = g_strdup_printf("decrypt failed for (%s,%s)", p->handle,
		                             e2e_ctx_wire(res->ctx));
out:
	g_free(wire);
	e2e_wire_free(w);
}

char *e2e_plain_with_via(const char *plain, const char *via)
{
	size_t len = strlen(plain);
	char *shown = e2e_text_display(via), *out;

	/* \017 first: colours the text left on must not hide the mark */
	if (e2e_is_action(plain))
		out = g_strdup_printf("%.*s\017 (E2E key of %s)\001", (int) (len - 1), plain, shown);
	else
		out = g_strdup_printf("%s\017 (E2E key of %s)", plain, shown);
	g_free(shown);
	return out;
}

void e2e_in_result_clear(E2E_IN_RESULT *res)
{
	g_free(res->via);
	if (res->plain != NULL)
		e2e_wipe(res->plain, strlen(res->plain));
	g_free(res->plain);
	g_free(res->ctx);
	g_free(res->keyreq);
	g_free(res->debug);
	memset(res, 0, sizeof(*res));
}

/* ---- export / import ---- */

/* { <name> => $key, %$row }: the row's own fields win, as in the Perl
   hash literal */
static E2E_JSON *row_with(const E2E_JSON *row, const char *name, const char *value,
                          const char *name2, const char *value2)
{
	E2E_JSON *out = row != NULL && row->type == E2E_JSON_OBJECT ? e2e_json_copy(row)
	                                                             : e2e_json_new(E2E_JSON_OBJECT);

	if (e2e_json_get(out, name) == NULL)
		e2e_json_set_string(out, name, value);
	if (name2 != NULL && e2e_json_get(out, name2) == NULL)
		e2e_json_set_string(out, name2, value2);
	return out;
}

E2E_JSON *e2e_keyring_export(const E2E_JSON *kr)
{
	E2E_JSON *doc = e2e_json_new(E2E_JSON_OBJECT), *list, *src;
	GPtrArray *keys;
	E2E_JSON *ident = e2e_json_get(kr, "identity");
	guint i;

	e2e_json_set_int(doc, "version", 1);
	e2e_json_set_int(doc, "exportedAt", e2e_now());
	e2e_json_set(doc, "identity", ident != NULL ? e2e_json_copy(ident) : e2e_json_new(E2E_JSON_NULL));

	src = e2e_json_get(kr, "peers");
	list = e2e_json_new(E2E_JSON_ARRAY);
	keys = e2e_json_keys(src);
	for (i = 0; i < keys->len; i++) {
		const char *fp = g_ptr_array_index(keys, i);

		e2e_json_array_add(list, row_with(e2e_json_get(src, fp), "fingerprint", fp, NULL, NULL));
	}
	g_ptr_array_unref(keys);
	e2e_json_set(doc, "peers", list);

	src = e2e_json_get(kr, "incoming");
	list = e2e_json_new(E2E_JSON_ARRAY);
	keys = e2e_json_keys(src);
	for (i = 0; i < keys->len; i++) {
		const char *key = g_ptr_array_index(keys, i);
		const char *bar = strchr(key, '|');
		char *handle = bar != NULL ? g_strndup(key, bar - key) : g_strdup(key);

		e2e_json_array_add(list, row_with(e2e_json_get(src, key), "handle", handle,
		                                  "channel", bar != NULL ? bar + 1 : NULL));
		g_free(handle);
	}
	g_ptr_array_unref(keys);
	e2e_json_set(doc, "incomingSessions", list);

	src = e2e_json_get(kr, "outgoing");
	list = e2e_json_new(E2E_JSON_ARRAY);
	keys = e2e_json_keys(src);
	for (i = 0; i < keys->len; i++) {
		const char *ctx = g_ptr_array_index(keys, i);

		e2e_json_array_add(list, row_with(e2e_json_get(src, ctx), "channel", ctx, NULL, NULL));
	}
	g_ptr_array_unref(keys);
	e2e_json_set(doc, "outgoingSessions", list);

	src = e2e_json_get(kr, "channels");
	list = e2e_json_new(E2E_JSON_ARRAY);
	keys = e2e_json_keys(src);
	for (i = 0; i < keys->len; i++) {
		const char *ctx = g_ptr_array_index(keys, i);

		e2e_json_array_add(list, row_with(e2e_json_get(src, ctx), "channel", ctx, NULL, NULL));
	}
	g_ptr_array_unref(keys);
	e2e_json_set(doc, "channels", list);

	src = e2e_json_get(kr, "autotrust");
	e2e_json_set(doc, "autotrust", src != NULL ? e2e_json_copy(src) : e2e_json_new(E2E_JSON_ARRAY));

	src = e2e_json_get(kr, "outgoing_recipients");
	list = e2e_json_new(E2E_JSON_ARRAY);
	keys = e2e_json_keys(src);
	for (i = 0; i < keys->len; i++)
		e2e_json_array_add(list, e2e_json_copy(e2e_json_get(src, g_ptr_array_index(keys, i))));
	g_ptr_array_unref(keys);
	e2e_json_set(doc, "outgoingRecipients", list);
	return doc;
}

/* a list member of the export, or NULL (and *error) when it is something
   else; a missing one is an empty list */
static gboolean list_member(const E2E_JSON *doc, const char *name, E2E_JSON **list,
                            char **error)
{
	E2E_JSON *node = e2e_json_get(doc, name);

	*list = NULL;
	if (node == NULL || node->type == E2E_JSON_NULL)
		return TRUE;
	if (node->type != E2E_JSON_ARRAY) {
		*error = g_strdup_printf("%s is not a list", name);
		return FALSE;
	}
	*list = node;
	return TRUE;
}

static E2E_JSON *copy_or_null(const E2E_JSON *node)
{
	return node != NULL ? e2e_json_copy(node) : e2e_json_new(E2E_JSON_NULL);
}

/* $a // $b */
static E2E_JSON *defined_or(const E2E_JSON *row, const char *a, const char *b)
{
	E2E_JSON *node = e2e_json_get(row, a);

	if (node == NULL || node->type == E2E_JSON_NULL)
		node = e2e_json_get(row, b);
	return copy_or_null(node);
}

static const char *string_or(const E2E_JSON *row, const char *key, const char *def)
{
	const char *s = e2e_json_get_string(row, key);

	return s != NULL ? s : def;
}

E2E_JSON *e2e_keyring_import(const E2E_JSON *doc, char **error)
{
	E2E_JSON *kr, *list, *node, *row;
	guint i;
	char *k;

	if (doc == NULL || doc->type != E2E_JSON_OBJECT) {
		*error = g_strdup("not a JSON object");
		return NULL;
	}
	kr = e2e_keyring_new();
	node = e2e_json_get(doc, "identity");
	if (node != NULL && node->type != E2E_JSON_NULL && node->type != E2E_JSON_OBJECT) {
		*error = g_strdup("identity is not an object");
		goto fail;
	}
	e2e_json_set(kr, "identity", copy_or_null(node));

	if (!list_member(doc, "peers", &list, error))
		goto fail;
	for (i = 0; list != NULL && i < list->array->len; i++) {
		const char *fp;

		row = g_ptr_array_index(list->array, i);
		fp = e2e_json_get_string(row, "fingerprint");
		if (fp == NULL)
			continue;
		node = e2e_json_copy(row);
		k = g_strdup(fp);
		e2e_json_remove(node, "fingerprint");
		e2e_json_set(e2e_json_get(kr, "peers"), k, node);
		g_free(k);
	}

	if (!list_member(doc, "incomingSessions", &list, error))
		goto fail;
	for (i = 0; list != NULL && i < list->array->len; i++) {
		row = g_ptr_array_index(list->array, i);
		node = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set(node, "fp", defined_or(row, "fp", "fingerprint"));
		e2e_json_set(node, "sk", copy_or_null(e2e_json_get(row, "sk")));
		e2e_json_set(node, "status", copy_or_null(e2e_json_get(row, "status")));
		e2e_json_set(node, "created_at", defined_or(row, "created_at", "createdAt"));
		k = pipe_key(string_or(row, "handle", ""), string_or(row, "channel", ""));
		e2e_json_set(e2e_json_get(kr, "incoming"), k, node);
		g_free(k);
	}

	if (!list_member(doc, "outgoingSessions", &list, error))
		goto fail;
	for (i = 0; list != NULL && i < list->array->len; i++) {
		E2E_JSON *rot;

		row = g_ptr_array_index(list->array, i);
		if (e2e_json_get_string(row, "channel") == NULL)
			continue;
		/* every member is kept (the network that took the name over,
		   members of other clients); the known ones in rpe2e.pl's form */
		node = e2e_json_copy(row);
		e2e_json_remove(node, "channel");
		e2e_json_remove(node, "createdAt");
		e2e_json_remove(node, "pendingRotation");
		e2e_json_set(node, "sk", copy_or_null(e2e_json_get(row, "sk")));
		e2e_json_set(node, "created_at", defined_or(row, "created_at", "createdAt"));
		rot = defined_or(row, "pending_rotation", "pendingRotation");
		if (rot->type == E2E_JSON_NULL) {
			e2e_json_free(rot);
			rot = e2e_json_new_int(0);
		}
		e2e_json_set(node, "pending_rotation", rot);
		e2e_json_set(e2e_json_get(kr, "outgoing"), e2e_json_get_string(row, "channel"), node);
	}

	if (!list_member(doc, "channels", &list, error))
		goto fail;
	for (i = 0; list != NULL && i < list->array->len; i++) {
		row = g_ptr_array_index(list->array, i);
		if (e2e_json_get_string(row, "channel") == NULL)
			continue;
		/* every member is kept: "nick" of a query's config, the network
		   that took the name over, members of other clients */
		node = e2e_json_copy(row);
		e2e_json_remove(node, "channel");
		e2e_json_set_int(node, "enabled", e2e_json_truthy(e2e_json_get(row, "enabled")) ? 1 : 0);
		e2e_json_set_string(node, "mode", string_or(row, "mode", "normal"));
		e2e_json_set(e2e_json_get(kr, "channels"), e2e_json_get_string(row, "channel"), node);
	}

	if (!list_member(doc, "autotrust", &list, error))
		goto fail;
	if (list != NULL)
		e2e_json_set(kr, "autotrust", e2e_json_copy(list));

	if (!list_member(doc, "outgoingRecipients", &list, error))
		goto fail;
	for (i = 0; list != NULL && i < list->array->len; i++) {
		row = g_ptr_array_index(list->array, i);
		k = pipe_key(string_or(row, "channel", ""), string_or(row, "handle", ""));
		e2e_json_set(e2e_json_get(kr, "outgoing_recipients"), k, e2e_json_copy(row));
		g_free(k);
	}
	return kr;
fail:
	e2e_json_free(kr);
	return NULL;
}

char *e2e_pending_inbound_key(const E2E_JSON *kr, const E2E_NET *net, const char *sender,
                              const char *wire)
{
	char *ctx, *k;

	/* where e2e_handle_keyreq puts it */
	ctx = e2e_ctx_find(kr, net, wire);
	if (ctx == NULL)
		return NULL;
	k = pipe_key(sender, ctx);
	g_free(ctx);
	return k;
}

/* what rpe2e.pl cannot see: kr with only the per-network rows of the
   stores keyed by a context (identity, peers and the rules stay) */
static E2E_JSON *scoped_view(const E2E_JSON *kr)
{
	static const char *const stores[] = {
		"channels", "outgoing", "incoming", "accepted", "pending", "pending_inbound",
		"outgoing_recipients", "seen_rekeys", NULL
	};
	E2E_JSON *view = kr != NULL ? e2e_json_copy(kr) : e2e_json_new(E2E_JSON_OBJECT), *list;
	GPtrArray *keys, *doomed;
	guint i, j;

	for (i = 0; stores[i] != NULL; i++) {
		E2E_JSON *store = e2e_json_get(view, stores[i]);

		keys = e2e_json_keys(store);
		doomed = g_ptr_array_new_with_free_func(g_free);
		for (j = 0; j < keys->len; j++)
			if (strchr(g_ptr_array_index(keys, j), E2E_SCOPE_SEP) == NULL)
				g_ptr_array_add(doomed, g_strdup(g_ptr_array_index(keys, j)));
		g_ptr_array_unref(keys);
		for (j = 0; j < doomed->len; j++)
			e2e_json_remove(store, g_ptr_array_index(doomed, j));
		g_ptr_array_unref(doomed);
	}
	list = e2e_json_get(view, "pending_trust_change");
	for (i = 0; list != NULL && list->type == E2E_JSON_ARRAY && i < list->array->len;) {
		const char *c = e2e_json_get_string(g_ptr_array_index(list->array, i), "channel");

		if (c == NULL || strchr(c, E2E_SCOPE_SEP) == NULL)
			g_ptr_array_remove_index(list->array, i);
		else
			i++;
	}
	return view;
}

/* the query (a "@<handle>" config, the script's own or this network's)
   with E2E on that the nick piece of a target list may be: its address
   now (resolve), or the config's "nick", or a peer last seen as that nick
   under the config's handle; or an old bare-nick row of that nick with
   E2E on (nick_enabled() for the module) - nicks folded by the
   map. Its bare name, or NULL. single: piece is the only target, which
   the script looks up itself - an old bare-nick row only when the fold
   alone joins them (the script refuses the name as it is stored).
   *spelling (if not NULL): the name the script may find it under - the
   old row's, or the peer's last_nick - or NULL (by address or "nick"). */
static char *list_query_on(const E2E_JSON *kr, const E2E_NET *net, const char *piece,
                           E2E_RESOLVE_FUNC resolve, void *resolve_data, gboolean single,
                           const char **spelling)
{
	E2E_JSON *channels = e2e_json_get(kr, "channels");
	GPtrArray *keys = e2e_json_keys(channels);
	char *handle = resolve != NULL ? resolve(piece, kr, resolve_data) : NULL, *found = NULL;
	GHashTable *idx = NULL;
	const char *sp = NULL;
	NICK_FOLD f;
	guint i;

	nick_fold_init(&f, net, piece, FALSE);
	for (i = 0; i < keys->len && found == NULL; i++) {
		const char *key = g_ptr_array_index(keys, i), *wire = e2e_ctx_wire(key);

		if (!e2e_ctx_enabled(kr, key) || *wire == '\0' ||
		    (strchr(key, E2E_SCOPE_SEP) != NULL && !e2e_ctx_on_net(key, net)))
			continue;
		if (*wire != '@') {
			/* an old bare-nick row: exact, or folded by the map
			   (more refusals only); a channel is no nick */
			if ((strcmp(wire, piece) == 0 ? !single : nick_fold_same(&f, wire)) &&
			    is_nick_wire(wire)) {
				found = g_strdup(wire);
				sp = wire;
			}
			continue;
		}
		if ((handle != NULL && *handle != '\0' && strcmp(wire + 1, handle) == 0) ||
		    nick_fold_same(&f, e2e_json_get_string(e2e_json_get(channels, key), "nick")))
			found = g_strdup(wire);
		else if ((sp = peer_nick_at(kr, &idx, wire + 1, &f)) != NULL)
			found = g_strdup(wire);
	}
	if (spelling != NULL)
		*spelling = found != NULL ? sp : NULL;
	nick_fold_clear(&f);
	if (idx != NULL)
		g_hash_table_destroy(idx);
	g_free(handle);
	g_ptr_array_unref(keys);
	return found;
}

static gboolean ascii_only(const char *s)
{
	for (; *s != '\0'; s++)
		if ((unsigned char) *s >= 0x80)
			return FALSE;
	return TRUE;
}

/* addresses of several peers tie for nick in the keyring's cache:
   rpe2e.pl 0.2.0 picks one by Perl's hash order, which changes from
   process to process */
static gboolean cache_tied(const E2E_JSON *kr, const char *nick)
{
	gboolean tie;

	find_handle_by_nick(kr, nick, &tie);
	return tie;
}

/*
 A single DM target, rpe2e.pl loaded. The script resolves the nick as the
 module does - live (an open query, a common channel), then the keyring's
 last_nick by lc(), which matches the module's ASCII case only for an
 ASCII nick (on a non-ASCII one it may compare another form of the
 stored name), and picks among peers tied on last_seen by its hash
 order - and with no address looks an old bare-nick row up
 exactly. A cache-only address of a non-ASCII nick, or of a tie, counts
 as none. The refusal, or NULL:
  - an address the script has as well, its "@<handle>" config off, and E2E
    on for the nick at another address (as the module's REFUSE_DM_MOVED);
  - no such address: a query with E2E on, or an old bare-nick row, the
    server may take nick for and the script does not find that way.
 What the script finds itself (its config of the address, the row as it
 is stored) stays its own.
*/
static char *script_single_nick(const E2E_JSON *kr, const E2E_NET *net, const char *nick,
                                E2E_RESOLVE_FUNC resolve, void *resolve_data)
{
	char *handle = resolve != NULL ? resolve(nick, kr, resolve_data) : NULL;
	char *other, *msg = NULL;
	const char *spelling;

	if (handle != NULL && *handle != '\0' && (!ascii_only(nick) || cache_tied(kr, nick))) {
		/* only a live address is sure to be the script's as well */
		char *live = resolve(nick, NULL, resolve_data);

		if (live == NULL || *live == '\0') {
			g_free(handle);
			handle = NULL;
		}
		g_free(live);
	}
	if (handle != NULL && *handle != '\0') {
		char *config = g_strconcat("@", handle, NULL);

		/* the script encrypts under its own config of the address */
		other = e2e_ctx_enabled(kr, config) ? NULL :
		        e2e_enabled_dm_elsewhere(kr, net, nick, handle);
		if (other != NULL)
			msg = g_strdup_printf(E2E_SCRIPT_REFUSE_MOVED, nick, other, nick, handle);
		g_free(other);
		g_free(config);
		g_free(handle);
		return msg;
	}
	g_free(handle);
	other = list_query_on(kr, net, nick, NULL, NULL, TRUE, &spelling);
	if (other == NULL)
		return NULL;
	/* a name the script finds: ASCII (its lc() is the module's there),
	   other than the one it did not find */
	if (spelling != NULL && ascii_only(spelling) && strcmp(spelling, nick) != 0)
		msg = g_strdup_printf(E2E_SCRIPT_REFUSE_NICK, other, nick, spelling);
	else
		msg = g_strdup_printf(E2E_SCRIPT_REFUSE_NICK_UNKNOWN, other, nick, nick);
	g_free(other);
	return msg;
}

/* rpe2e.pl compares channel names exactly, and looks "#a,#b" up as one
   name: a channel E2E is on for in its own (bare) rows that the server
   may take for to under another name, or for a piece of a target list,
   and a query with E2E on that a nick in such a list may be, gets
   clear text from the script. The refusal, or NULL.
   The script's escape hatches (non-ACTION CTCP) stay. dms_only: the line
   is a bot command to a channel (the module and the script send it in
   clear text, whatever the case of the name), but DMs never bypass:
   only the nick pieces of a list are looked at. */
static char *script_other_case(const E2E_JSON *kr, const E2E_NET *net, const char *to,
                               const char *body, gboolean dms_only, E2E_RESOLVE_FUNC resolve,
                               void *resolve_data)
{
	char **pieces, **readings, *other = NULL, *msg = NULL;
	const char *bad, *config;
	gboolean undecided = FALSE, list = strchr(to, ',') != NULL;
	int i;

	if (kr == NULL || (e2e_is_ctcp(body) && !e2e_is_action(body)))
		return NULL;
	pieces = g_strsplit(to, ",", -1);
	for (i = 0; pieces[i] != NULL && other == NULL && msg == NULL && !undecided; i++) {
		readings = e2e_channel_readings(pieces[i]);
		if (readings[0] != NULL && !dms_only)
			other = enabled_other_case(kr, net, readings, list ? OTHER_SCRIPT_ALL : OTHER_SCRIPT,
			                           &undecided, &bad, &config);
		else if (readings[0] == NULL && list && *pieces[i] != '\0')
			/* a nick in a list: the script resolves no query
			   for it */
			other = list_query_on(kr, net, pieces[i], resolve, resolve_data, FALSE, NULL);
		else if (readings[0] == NULL && *pieces[i] != '\0')
			/* the only target, a nick (never a bot command) */
			msg = script_single_nick(kr, net, pieces[i], resolve, resolve_data);
		g_strfreev(readings);
	}
	if (msg != NULL) {
		/* the single nick's */
	} else if (other != NULL && list) {
		msg = g_strdup_printf(E2E_SCRIPT_REFUSE_LIST, to, other);
	} else if (other != NULL) {
		msg = g_strdup_printf(E2E_SCRIPT_REFUSE_CASE, other, to, other);
	} else if (undecided) {
		char *why = g_strdup_printf(REFUSE_UNDECIDED, bad, to, config);

		msg = g_strdup_printf("rpe2e.pl compares channel names exactly - %s; the message shown above was NOT delivered",
		                      why);
		g_free(why);
	}
	g_free(other);
	g_strfreev(pieces);
	return msg;
}

/* buf (without its final CR / LF) holds a CR, LF or NUL: several lines */
static gboolean several_lines(const char *buf, gsize len)
{
	gsize i;

	while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
		len--;
	for (i = 0; i < len; i++)
		if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == '\0')
			return TRUE;
	return FALSE;
}

E2E_GATE_ACTION e2e_script_guard(const E2E_JSON *kr, gboolean kr_ok, gboolean lost,
                                 gboolean nested, const E2E_NET *net, const char *buf,
                                 gsize len, E2E_RESOLVE_FUNC resolve, void *resolve_data,
                                 char **message, char **target)
{
	E2E_GATE_ACTION action = E2E_GATE_PASS;
	gboolean multi = several_lines(buf, len);
	/* one line: what the script cannot see; several: everything, as the
	   script looks at the first line only */
	E2E_JSON *view = !multi ? scoped_view(kr) :
	                 kr != NULL ? e2e_json_copy(kr) : e2e_json_new(E2E_JSON_OBJECT);
	gsize start, end;

	*message = NULL;
	*target = NULL;
	/* every piece a server may take for a line of its own */
	for (start = 0; start < len && action != E2E_GATE_REFUSE; start = end + 1) {
		char *line, *tags, *to, *body, *case_msg;
		E2E_GATE_RESULT res;
		E2E_WIRE *w;

		for (end = start; end < len && buf[end] != '\r' && buf[end] != '\n' && buf[end] != '\0'; end++)
			;
		line = g_strndup(buf + start, end - start);
		if (*line == '\0' || !e2e_parse_privmsg_line(line, &tags, &to, &body)) {
			e2e_wipe(line, strlen(line));
			g_free(line);
			continue;
		}
		e2e_wipe(line, strlen(line));
		g_free(line);
		/* the script's own ciphertext: a line of its own, sent from
		   within its gate; a wire the user typed is text */
		w = nested && !multi ? e2e_wire_parse(body) : NULL;
		if (w != NULL) {
			e2e_wire_free(w);
		} else {
			/* dry: nothing in the view changes, nothing is adopted */
			gate_decide(view, kr_ok, NULL, net, to, body, resolve, resolve_data, TRUE, &res);
			if (res.action == E2E_GATE_CIPHER || res.action == E2E_GATE_REFUSE) {
				char *where = e2e_ctx_display(res.ctx);

				g_free(*message);
				if (lost && res.action == E2E_GATE_REFUSE)
					*message = g_strdup_printf("%s - the message shown above was NOT delivered",
					                           E2E_REFUSE_LOST);
				else if (res.action == E2E_GATE_CIPHER)
					*message = g_strdup_printf(E2E_SCRIPT_REFUSE, where);
				else
					*message = g_strdup_printf(E2E_SCRIPT_REFUSE_OTHER, to, res.message);
				g_free(where);
				g_free(*target);
				*target = g_strdup(to);
				action = E2E_GATE_REFUSE;
			} else if ((res.action == E2E_GATE_PASS || res.action == E2E_GATE_BYPASS) && !multi &&
			           (case_msg = script_other_case(kr, net, to, body,
			                                         res.action == E2E_GATE_BYPASS, resolve,
			                                         resolve_data)) != NULL) {
				/* several lines: the whole keyring was decided above;
				   a bot command: DMs never bypass */
				g_free(*message);
				*message = case_msg;
				g_free(*target);
				*target = g_strdup(to);
				action = E2E_GATE_REFUSE;
			} else if (res.action == E2E_GATE_BYPASS && action == E2E_GATE_PASS &&
			           res.message != NULL) {
				*message = g_strdup(res.message);
				*target = g_strdup(to);
				action = E2E_GATE_BYPASS;
			}
			e2e_gate_result_clear(&res);
		}
		g_free(tags);
		g_free(to);
		e2e_wipe(body, strlen(body));
		g_free(body);
	}
	e2e_json_free(view);
	return action;
}
