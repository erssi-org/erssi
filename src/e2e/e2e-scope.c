/*
 e2e-scope.c : network scope of contexts and CASEMAPPING folding

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The superset fold (E2E_CASEMAP_UNICODE) is for servers whose CASEMAPPING
 is not one of the three exact ones. Folding more than the server does
 can only make two names look like one channel - a refusal on the way
 out, or on the way in one more decryption attempt, which opens a line
 the server moved between the two (the reader marks those:
 e2e_casefold_narrow) - while folding less could treat one channel as
 two and let clear text out, so every step only adds equalities.
*/

#include "e2e-scope.h"

#include <string.h>

/*
 The label from a chatnet name or tag, which may hold anything: C0 and C1
 controls, DEL and '|' become '_', and so does every byte that is not part
 of a valid UTF-8 character, so the label is valid UTF-8 and never holds
 the separator.
*/
static char *label_from(const char *name)
{
	GString *out = g_string_sized_new(strlen(name));
	const char *p = name;

	while (*p != '\0') {
		gunichar c = g_utf8_get_char_validated(p, -1);

		if (!g_unichar_validate(c)) { /* also (gunichar) -1 and -2 */
			g_string_append_c(out, '_');
			p++;
			continue;
		}
		if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == '|')
			g_string_append_c(out, '_');
		else
			g_string_append_len(out, p, g_utf8_next_char(p) - p);
		p = g_utf8_next_char(p);
	}
	return g_string_free(out, FALSE);
}

void e2e_net_init(E2E_NET *net, const char *chatnet, const char *tag, const char *casemapping,
                  gboolean legacy_ok)
{
	if (chatnet != NULL && *chatnet != '\0')
		net->label = label_from(chatnet);
	else if (tag != NULL && *tag != '\0')
		net->label = label_from(tag);
	else
		net->label = NULL;
	net->casemap = e2e_casemap_parse(casemapping);
	net->legacy_ok = legacy_ok;
}

void e2e_net_clear(E2E_NET *net)
{
	g_free(net->label);
	net->label = NULL;
}

E2E_CASEMAP e2e_casemap_parse(const char *value)
{
	if (value == NULL)
		return E2E_CASEMAP_RFC1459;
	if (g_ascii_strcasecmp(value, "rfc1459") == 0)
		return E2E_CASEMAP_RFC1459;
	if (g_ascii_strcasecmp(value, "strict-rfc1459") == 0)
		return E2E_CASEMAP_STRICT_RFC1459;
	if (g_ascii_strcasecmp(value, "ascii") == 0)
		return E2E_CASEMAP_ASCII;
	if (g_ascii_strcasecmp(value, "utf8") == 0 || g_ascii_strcasecmp(value, "rfc7613") == 0 ||
	    g_ascii_strcasecmp(value, "rfc8265") == 0)
		return E2E_CASEMAP_UNICODE;
	return E2E_CASEMAP_UNKNOWN;
}

E2E_CASEMAP e2e_casemap_of_server(const char *value, gboolean motd_done)
{
	return value != NULL || motd_done ? e2e_casemap_parse(value) : E2E_CASEMAP_UNICODE;
}

/* the exact maps, byte by byte, in place; UNICODE takes RFC1459's.
   latin1: also the Latin-1 capitals C0-DE (not D7) to E0-FE, as the
   ircu family folds them - never inside valid UTF-8 that must
   stay valid */
static void fold_bytes(char *s, E2E_CASEMAP map, gboolean latin1)
{
	for (; *s != '\0'; s++) {
		unsigned char c = (unsigned char) *s;

		if (c >= 'A' && c <= 'Z')
			*s = (char) (c - 'A' + 'a');
		else if (latin1 && c >= 0xc0 && c <= 0xde && c != 0xd7)
			*s = (char) (c + 0x20);
		else if (map == E2E_CASEMAP_ASCII)
			continue;
		else if (c == '[')
			*s = '{';
		else if (c == ']')
			*s = '}';
		else if (c == '\\')
			*s = '|';
		else if (c == '~' && map != E2E_CASEMAP_STRICT_RFC1459)
			*s = '^';
	}
}

/* the full case fold of one code point, when it is one code point */
static gboolean fold_single(gunichar c, gunichar *out)
{
	char buf[8];
	char *folded;
	int len;
	gboolean single;

	len = g_unichar_to_utf8(c, buf);
	folded = g_utf8_casefold(buf, len);
	single = *folded != '\0' && *g_utf8_next_char(folded) == '\0';
	if (single)
		*out = g_utf8_get_char(folded);
	g_free(folded);
	return single;
}

/*
 GLib's g_utf8_casefold() folds what CaseFolding.txt lists and lowercases
 everything else. For Cherokee, CaseFolding.txt folds the small letters to
 the capitals while the fallback lowercases the capitals, so the two fold
 into each other and never meet. A code point c whose fold is a single
 other code point f folding back to c is replaced by the smaller of c and
 f, which is CaseFolding.txt's own target for these pairs.
*/
static char *fold_cycles(const char *s)
{
	GString *out = g_string_sized_new(strlen(s));
	const char *p;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p), f, back;

		if (c >= 0x80 && fold_single(c, &f) && f != c && fold_single(f, &back) &&
		    back == c)
			c = MIN(c, f);
		g_string_append_unichar(out, c);
	}
	return g_string_free(out, FALSE);
}

/*
 Invisible characters, dropped as NFKC_Casefold drops Default_Ignorable
 code points: format characters (Cf: soft hyphen, zero width space and
 joiners, bidi marks, word joiner, BOM, tags), the variation selectors,
 the Mongolian free variation selectors, the Khmer inherent vowels, the
 combining grapheme joiner and the Hangul fillers.
*/
static gboolean is_ignorable(gunichar c)
{
	return g_unichar_type(c) == G_UNICODE_FORMAT || (c >= 0xfe00 && c <= 0xfe0f) ||
	       (c >= 0xe0100 && c <= 0xe01ef) || (c >= 0x180b && c <= 0x180d) || c == 0x180f ||
	       c == 0x17b4 || c == 0x17b5 || c == 0x34f || c == 0x115f || c == 0x1160 || c == 0x3164 || c == 0xffa0;
}

static char *drop_ignorables(const char *s)
{
	GString *out = g_string_sized_new(strlen(s));
	const char *p;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p)) {
		if (!is_ignorable(g_utf8_get_char(p)))
			g_string_append_len(out, p, g_utf8_next_char(p) - p);
	}
	return g_string_free(out, FALSE);
}

/* full case folding, with GLib's Cherokee cycles closed */
static char *casefold_full(const char *s)
{
	char *folded = g_utf8_casefold(s, -1);
	char *result = fold_cycles(folded);

	g_free(folded);
	return result;
}

/* g_utf8_normalize(s, mode), freeing s; NULL for NULL or if GLib refuses */
static char *normalize_take(char *s, GNormalizeMode mode)
{
	char *result = s != NULL ? g_utf8_normalize(s, -1, mode) : NULL;

	g_free(s);
	return result;
}

/* casefold_full(s), freeing s; NULL for NULL */
static char *casefold_take(char *s)
{
	char *result = s != NULL ? casefold_full(s) : NULL;

	g_free(s);
	return result;
}

/*
 U+0345 (ypogegrammeni) is the one combining mark whose case fold is a base
 character, iota. NFD sorts it after the other marks of its letter, a
 server that case folds first puts the iota before them, so the marks end
 on the letter in one and on the iota in the other. Servers disagree, so
 the run of marks directly before and directly after every iota is
 dropped. That over-folds only marks next to a Greek iota.
*/
static char *drop_marks_by_iota(const char *s)
{
	glong i, j, len;
	gunichar *cps = g_utf8_to_ucs4_fast(s, -1, &len);
	gboolean *drop = g_new0(gboolean, len + 1);
	GString *out = g_string_sized_new(strlen(s));

	for (i = 0; i < len; i++) {
		if (cps[i] != 0x3b9)
			continue;
		for (j = i - 1; j >= 0 && g_unichar_ismark(cps[j]); j--)
			drop[j] = TRUE;
		for (j = i + 1; j < len && g_unichar_ismark(cps[j]); j++)
			drop[j] = TRUE;
	}
	for (i = 0; i < len; i++) {
		if (!drop[i])
			g_string_append_unichar(out, cps[i]);
	}
	g_free(cps);
	g_free(drop);
	return g_string_free(out, FALSE);
}

/* drop_marks_by_iota(s), freeing s; NULL for NULL */
static char *drop_marks_by_iota_take(char *s)
{
	char *result = s != NULL ? drop_marks_by_iota(s) : NULL;

	g_free(s);
	return result;
}

/*
 The Turkish i. Full case folding takes U+0130 to i U+0307 and keeps
 U+0131, while servers that map case per code point (C towlower and
 towupper, Go's unicode.ToLower and ToUpper, an upper-case irccmp) take
 U+0130 to plain i and U+0131 to I. So U+0131 becomes i, and a U+0307 in
 the run of marks directly after an i goes: dotted, dotless and plain i
 are one letter.
*/
static char *fold_turkish_i(const char *s)
{
	GString *out = g_string_sized_new(strlen(s));
	gboolean after_i = FALSE;
	const char *p;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p);

		if (c == 0x131)
			c = 'i';
		if (after_i && g_unichar_ismark(c)) {
			if (c != 0x307)
				g_string_append_unichar(out, c);
			continue;
		}
		after_i = c == 'i';
		g_string_append_unichar(out, c);
	}
	return g_string_free(out, FALSE);
}

/* fold_turkish_i(s), freeing s; NULL for NULL */
static char *fold_turkish_i_take(char *s)
{
	char *result = s != NULL ? fold_turkish_i(s) : NULL;

	g_free(s);
	return result;
}

/*
 Unicode's compatibility caseless match (D146),
 NFKD(casefold(NFKD(casefold(NFD(x))))), with the invisible characters
 dropped first and NFKC at the end for one representative. Folding a
 composed form instead (NFKC first) under-folds: U+1FB3 U+0342 and
 U+1FBC U+0342 compose around U+0345 differently and fold apart, and
 casefold leaves marks out of canonical order (U+01F0 U+0338 folds to
 j U+030C U+0338), which only a normalisation after it puts right.
 Then the marks around an iota go (drop_marks_by_iota), the Turkish i
 is merged with i (fold_turkish_i), and NFKC.
*/
static char *fold_unicode(const char *name)
{
	char *s;

	if (!g_utf8_validate(name, -1, NULL))
		return NULL;
	s = drop_ignorables(name);
	s = normalize_take(s, G_NORMALIZE_NFD);
	s = casefold_take(s);
	s = normalize_take(s, G_NORMALIZE_NFKD);
	s = casefold_take(s);
	s = normalize_take(s, G_NORMALIZE_NFKD);
	s = drop_marks_by_iota_take(s);
	s = fold_turkish_i_take(s);
	s = normalize_take(s, G_NORMALIZE_NFKC);
	if (s == NULL)
		return NULL;
	/* the specials are ASCII, so this keeps the string valid UTF-8 */
	fold_bytes(s, E2E_CASEMAP_RFC1459, FALSE);
	return s;
}

static gboolean is_ascii(const char *s)
{
	for (; *s != '\0'; s++) {
		if ((unsigned char) *s >= 0x80)
			return FALSE;
	}
	return TRUE;
}

/* a code point of s (valid UTF-8) is unassigned in GLib's tables */
static gboolean has_unassigned(const char *s)
{
	for (; *s != '\0'; s = g_utf8_next_char(s)) {
		if (g_unichar_type(g_utf8_get_char(s)) == G_UNICODE_UNASSIGNED)
			return TRUE;
	}
	return FALSE;
}

char *e2e_casefold_ex(const char *name, E2E_CASEMAP map, gboolean *exact)
{
	gboolean decided = TRUE;
	char *result;

	if (exact != NULL)
		*exact = FALSE;
	if (name == NULL)
		return NULL;
	if (map == E2E_CASEMAP_UNKNOWN) {
		/* the superset fold; but a byte code page (cp1251, koi8-r)
		   folds the bytes of a name in UTF-8 its own way: only ASCII
		   names are decided */
		map = E2E_CASEMAP_UNICODE;
		decided = is_ascii(name);
	}
	if (map == E2E_CASEMAP_UNICODE && is_ascii(name))
		map = E2E_CASEMAP_RFC1459; /* the Unicode fold of ASCII, faster */
	if (map == E2E_CASEMAP_UNICODE) {
		result = fold_unicode(name);
		if (result != NULL) {
			/* a server with newer tables may fold an unassigned
			   code point any way */
			if (exact != NULL)
				*exact = decided && !has_unassigned(name);
			return result;
		}
		/* not UTF-8: no Unicode fold can be decided, keep rfc1459's
		   (Latin-1 bytes too) and say so */
		result = g_strdup(name);
		fold_bytes(result, E2E_CASEMAP_RFC1459, TRUE);
		return result;
	}
	result = g_strdup(name);
	fold_bytes(result, map, TRUE);
	if (exact != NULL)
		*exact = decided;
	return result;
}

/* Unicode's Cased, approximated with GLib (which has no such property):
   a letter of category Lu, Ll or Lt, a code point GLib maps to another
   case, and the Other_Lowercase / Other_Uppercase code points that are
   neither Lm nor Mn (listed: ª, º, Roman numerals, circled and squared
   Latin letters). The Lm and Mn ones are case ignorable, and
   final_sigma() skips those before it asks. Over every assigned code
   point after "ΑΣ" and before "Σ" this agrees with Python's str.lower()
   (Unicode 15.1, GLib 2.84); a newer Unicode may add cased code points
   it misses - that only changes where a name is marked */
static gboolean sigma_cased(gunichar c)
{
	switch (g_unichar_type(c)) {
	case G_UNICODE_UPPERCASE_LETTER:
	case G_UNICODE_LOWERCASE_LETTER:
	case G_UNICODE_TITLECASE_LETTER:
		return TRUE;
	default:
		break;
	}
	return c == 0xAA || c == 0xBA || (c >= 0x2160 && c <= 0x217F) ||
	       (c >= 0x24B6 && c <= 0x24E9) || (c >= 0x1F130 && c <= 0x1F149) ||
	       (c >= 0x1F150 && c <= 0x1F169) || (c >= 0x1F170 && c <= 0x1F189) ||
	       g_unichar_tolower(c) != c || g_unichar_toupper(c) != c || g_unichar_totitle(c) != c;
}

/* Unicode's Case_Ignorable, approximated with GLib: the categories Mn,
   Me, Cf, Lm and Sk, and the Word_Break MidLetter, MidNumLet and
   Single_Quote code points (listed: GLib has no Word_Break) */
static gboolean sigma_ignorable(gunichar c)
{
	switch (g_unichar_type(c)) {
	case G_UNICODE_NON_SPACING_MARK:
	case G_UNICODE_ENCLOSING_MARK:
	case G_UNICODE_FORMAT:
	case G_UNICODE_MODIFIER_LETTER:
	case G_UNICODE_MODIFIER_SYMBOL:
		return TRUE;
	default:
		break;
	}
	switch (c) {
	case 0x0027: case 0x002E: case 0x003A: case 0x00B7: case 0x0387: case 0x055F:
	case 0x05F4: case 0x2018: case 0x2019: case 0x2024: case 0x2027: case 0xFE13:
	case 0xFE52: case 0xFE55: case 0xFF07: case 0xFF0E: case 0xFF1A:
		return TRUE;
	default:
		return FALSE;
	}
}

/* Unicode's Final_Sigma for the Σ at p in s (valid UTF-8): a cased
   letter before it and none after it, case ignorables skipped both ways
   (as Python's str.lower(), ICU). GLib's g_utf8_strdown() asks only
   whether the next character is a letter */
static gboolean final_sigma(const char *s, const char *p)
{
	const char *q;
	gunichar c = 0;

	for (q = p; q > s;) {
		q = g_utf8_prev_char(q);
		c = g_utf8_get_char(q);
		if (!sigma_ignorable(c))
			break;
	}
	if (q == p || sigma_ignorable(c) || !sigma_cased(c))
		return FALSE;
	for (q = g_utf8_next_char(p); *q != '\0'; q = g_utf8_next_char(q)) {
		c = g_utf8_get_char(q);
		if (!sigma_ignorable(c))
			return !sigma_cased(c);
	}
	return TRUE;
}

/* "<length>:<s>" onto out */
static void append_part(GString *out, const char *s)
{
	g_string_append_printf(out, "%" G_GSIZE_FORMAT ":%s", strlen(s), s);
}

char *e2e_casefold_narrow(const char *name, E2E_CASEMAP map)
{
	GString *out, *part;
	char *nfkc, *folded;
	const char *p;

	if (name == NULL)
		return NULL;
	if (map != E2E_CASEMAP_UNICODE) {
		/* the exact maps' own fold; UNKNOWN: ASCII letters only, any
		   other byte as it is (a byte code page folds them its own way,
		   or not at all) */
		char *result = g_strdup(name);

		if (map == E2E_CASEMAP_UNKNOWN) {
			char *q;

			for (q = result; *q != '\0'; q++)
				*q = g_ascii_tolower(*q);
		} else {
			fold_bytes(result, map, FALSE);
		}
		return result;
	}
	if (!g_utf8_validate(name, -1, NULL))
		return NULL;
	/* every model must take the names for one:
	   - per code point simple lower case (towlower, Go's
	     unicode.ToLower), no normalisation;
	   - whole-string lower case (Final_Sigma: #ΑΣ is #ας), GLib's
	     rule and Unicode's own (#ΑΣ中 is #ας中 to Unicode,
	     #ασ中 to GLib), the full mapping of U+0130 in both;
	   - whole-string upper case (servers that compare in upper case:
	     the Kelvin, Angstrom and Ohm signs, ß / ẞ, θ / ϴ stay apart);
	   - NFKC and full case folding (the PRECIS and utf8 servers).
	   Each part with its length in front, so no two names collide */
	out = g_string_sized_new(strlen(name) * 4 + 16);
	part = g_string_sized_new(strlen(name) * 2);
	for (p = name; *p != '\0'; p = g_utf8_next_char(p))
		g_string_append_unichar(part, g_unichar_tolower(g_utf8_get_char(p)));
	append_part(out, part->str);
	g_string_free(part, TRUE);
	folded = g_utf8_strdown(name, -1);
	append_part(out, folded);
	g_free(folded);
	part = g_string_sized_new(strlen(name) * 2);
	for (p = name; *p != '\0'; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p);

		if (c == 0x03A3)
			g_string_append_unichar(part, final_sigma(name, p) ? 0x03C2 : 0x03C3);
		else if (c == 0x0130)
			g_string_append(part, "i\xcc\x87"); /* SpecialCasing: i + U+0307 */
		else
			g_string_append_unichar(part, g_unichar_tolower(c));
	}
	append_part(out, part->str);
	g_string_free(part, TRUE);
	folded = g_utf8_strup(name, -1);
	append_part(out, folded);
	g_free(folded);
	nfkc = g_utf8_normalize(name, -1, G_NORMALIZE_NFKC);
	if (nfkc == NULL) {
		g_string_free(out, TRUE);
		return NULL;
	}
	folded = g_utf8_casefold(nfkc, -1);
	append_part(out, folded);
	g_free(folded);
	g_free(nfkc);
	return g_string_free(out, FALSE);
}

char *e2e_casefold(const char *name, E2E_CASEMAP map)
{
	return e2e_casefold_ex(name, map, NULL);
}

char *e2e_ctx_scoped(const E2E_NET *net, const char *wire)
{
	if (wire == NULL || strchr(wire, E2E_SCOPE_SEP) != NULL)
		return NULL;
	if (net == NULL)
		return g_strdup(wire);
	if (net->label == NULL)
		return NULL;
	return g_strdup_printf("%s%c%s", net->label, E2E_SCOPE_SEP, wire);
}

char *e2e_ctx_rescope(const char *like, const char *wire)
{
	const char *sep;

	if (wire == NULL || strchr(wire, E2E_SCOPE_SEP) != NULL)
		return NULL;
	sep = like != NULL ? strchr(like, E2E_SCOPE_SEP) : NULL;
	if (sep == NULL)
		return g_strdup(wire);
	return g_strdup_printf("%.*s%c%s", (int) (sep - like), like, E2E_SCOPE_SEP, wire);
}

const char *e2e_ctx_wire(const char *ctx)
{
	const char *sep;

	if (ctx == NULL)
		return NULL;
	sep = strchr(ctx, E2E_SCOPE_SEP);
	return sep != NULL ? sep + 1 : ctx;
}

char *e2e_ctx_network(const char *ctx)
{
	const char *sep;

	if (ctx == NULL)
		return NULL;
	sep = strchr(ctx, E2E_SCOPE_SEP);
	return sep != NULL ? g_strndup(ctx, sep - ctx) : NULL;
}

gboolean e2e_ctx_on_net(const char *ctx, const E2E_NET *net)
{
	const char *sep;
	size_t len;

	if (ctx == NULL)
		return FALSE;
	sep = strchr(ctx, E2E_SCOPE_SEP);
	if (sep == NULL)
		return TRUE;
	if (net == NULL || net->label == NULL)
		return FALSE;
	len = sep - ctx;
	return strlen(net->label) == len && memcmp(ctx, net->label, len) == 0;
}

/* a character that must not reach the screen as it is: C0, DEL, C1, and
   the bidi and format controls that reorder or hide text */
static gboolean display_control(gunichar c)
{
	return c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0x061c || c == 0x200e || c == 0x200f ||
	       (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069);
}

char *e2e_text_display(const char *text)
{
	const char *p, *end;
	GString *out;

	if (text == NULL)
		return NULL;
	end = text + strlen(text);
	out = g_string_sized_new(end - text + 1);
	for (p = text; p < end;) {
		gunichar c = g_utf8_get_char_validated(p, end - p);

		if (c == (gunichar) -1 || c == (gunichar) -2) {
			/* not UTF-8: a raw byte, a C1 control as one too */
			unsigned char b = *p;

			g_string_append_c(out, b >= 0x80 && b <= 0x9f ? '?' : (char) b);
			p++;
		} else {
			const char *next = g_utf8_next_char(p);

			if (display_control(c))
				g_string_append_c(out, '?');
			else
				g_string_append_len(out, p, next - p);
			p = next;
		}
	}
	return g_string_free(out, FALSE);
}

char *e2e_ctx_display(const char *ctx)
{
	const char *sep;
	char *joined, *out;

	if (ctx == NULL)
		return NULL;
	sep = strchr(ctx, E2E_SCOPE_SEP);
	if (sep == NULL)
		return e2e_text_display(ctx);
	joined = g_strdup_printf("%.*s:%s", (int) (sep - ctx), ctx, sep + 1);
	out = e2e_text_display(joined);
	g_free(joined);
	return out;
}

/* ---- legacy rows ---- */

static gboolean is_object(const E2E_JSON *node)
{
	return node != NULL && node->type == E2E_JSON_OBJECT;
}

/* key is the "<handle>|<ctx>" key of row: the handle is the row's
   "handle" member when it has one, else everything before the first '|'
   (a handle holds no '|'; the convention of /e2e list and the export) */
static gboolean key_ends_with_ctx(const char *key, const E2E_JSON *row, const char *ctx)
{
	const char *handle = e2e_json_get_string(row, "handle");
	const char *bar;
	size_t hl;

	if (handle != NULL) {
		hl = strlen(handle);
		return strncmp(key, handle, hl) == 0 && key[hl] == '|' && strcmp(key + hl + 1, ctx) == 0;
	}
	bar = strchr(key, '|');
	return bar != NULL && strcmp(bar + 1, ctx) == 0;
}

/* key is the "<ctx>|<handle>" key of row, or "<ctx>" alone (a request
   sent without a handle): the handle is the row's "handle" member when it
   has one; without one the row's "channel" member names the context, and
   only when it has neither, everything after the last '|' is the handle */
static gboolean key_starts_with_ctx(const char *key, const E2E_JSON *row, const char *ctx)
{
	const char *handle = e2e_json_get_string(row, "handle");
	const char *channel = e2e_json_get_string(row, "channel");
	size_t n = strlen(ctx);

	if (strcmp(key, ctx) == 0)
		return TRUE;
	if (strncmp(key, ctx, n) != 0 || key[n] != '|')
		return FALSE;
	if (handle != NULL && *handle != '\0')
		return strcmp(key + n + 1, handle) == 0;
	if (channel != NULL)
		return strcmp(channel, ctx) == 0;
	return strchr(key + n + 1, '|') == NULL;
}

typedef enum {
	ROW_SAME,		/* the key is the context: channels, outgoing */
	ROW_HANDLE_FIRST,	/* "<handle>|<ctx>", "<fp>|<ctx>" */
	ROW_CTX_FIRST		/* "<ctx>|<handle>" */
} ROW_KEY;

static gboolean key_is_of(const char *key, const E2E_JSON *row, ROW_KEY form, const char *ctx)
{
	switch (form) {
	case ROW_SAME:
		return strcmp(key, ctx) == 0;
	case ROW_HANDLE_FIRST:
		return key_ends_with_ctx(key, row, ctx);
	default:
		return key_starts_with_ctx(key, row, ctx);
	}
}

/* the keys of store that are rows of ctx (copies) */
static GPtrArray *keys_of(const E2E_JSON *store, ROW_KEY form, const char *ctx)
{
	GPtrArray *keys = e2e_json_keys(store), *out = g_ptr_array_new_with_free_func(g_free);
	guint i;

	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);

		if (key_is_of(k, e2e_json_get(store, k), form, ctx))
			g_ptr_array_add(out, g_strdup(k));
	}
	g_ptr_array_unref(keys);
	return out;
}

static gboolean any_row(const E2E_JSON *kr, const char *store, ROW_KEY form, const char *ctx)
{
	GPtrArray *keys = keys_of(e2e_json_get(kr, store), form, ctx);
	gboolean found = keys->len > 0;

	g_ptr_array_unref(keys);
	return found;
}

/* a context exists when its config, our key, a session for it or a key
   request of ours for it is stored */
static gboolean ctx_exists(const E2E_JSON *kr, const char *ctx)
{
	return e2e_json_get(e2e_json_get(kr, "channels"), ctx) != NULL ||
	       e2e_json_get(e2e_json_get(kr, "outgoing"), ctx) != NULL ||
	       any_row(kr, "incoming", ROW_HANDLE_FIRST, ctx) ||
	       any_row(kr, "pending", ROW_CTX_FIRST, ctx);
}

/* old rows of wire were taken over by another network than net: a config
   or key row "<other label>\x1f<wire>" carries the "adopted" marker */
static gboolean adopted_elsewhere(const E2E_JSON *kr, const E2E_NET *net, const char *wire)
{
	static const char *const stores[] = { "channels", "outgoing", NULL };
	gboolean found = FALSE;
	int i;
	guint j;

	for (i = 0; stores[i] != NULL && !found; i++) {
		E2E_JSON *store = e2e_json_get(kr, stores[i]);
		GPtrArray *keys = e2e_json_keys(store);

		for (j = 0; j < keys->len && !found; j++) {
			const char *k = g_ptr_array_index(keys, j);

			found = strchr(k, E2E_SCOPE_SEP) != NULL && strcmp(e2e_ctx_wire(k), wire) == 0 &&
			        !e2e_ctx_on_net(k, net) &&
			        e2e_json_get_string(e2e_json_get(store, k), E2E_ADOPTED_MEMBER) != NULL;
		}
		g_ptr_array_unref(keys);
	}
	return found;
}

E2E_CTX_KIND e2e_ctx_lookup(const E2E_JSON *kr, const E2E_NET *net, const char *wire, char **ctx)
{
	gboolean have_scoped, have_bare, elsewhere;
	char *scoped;

	*ctx = NULL;
	if (wire == NULL)
		return E2E_CTX_BLOCKED;
	/* no network: the context as it is, as before the scope */
	if (net == NULL) {
		*ctx = g_strdup(wire);
		return ctx_exists(kr, wire) ? E2E_CTX_SCOPED : E2E_CTX_NEW;
	}
	/* a wire with 0x1F, or a network without a label: nothing can be
	   stored for it, and an old row of that name would read as scoped */
	scoped = e2e_ctx_scoped(net, wire);
	if (scoped == NULL) {
		*ctx = g_strdup(wire);
		return E2E_CTX_BLOCKED;
	}
	have_scoped = ctx_exists(kr, scoped);
	have_bare = ctx_exists(kr, wire);
	/* old rows of this name went to another network (the user's line
	   there): this one has to set it up by itself (/e2e adopt, /e2e off) */
	elsewhere = !have_scoped && adopted_elsewhere(kr, net, wire);
	/* on the only network old rows are taken over even when some scoped
	   ones exist already (they win row by row): a scoped request of ours
	   must not hide the old sessions of everybody else */
	if (have_bare && net->legacy_ok && !elsewhere) {
		g_free(scoped);
		*ctx = g_strdup(wire);
		return E2E_CTX_LEGACY;
	}
	if (have_scoped) {
		*ctx = scoped;
		return E2E_CTX_SCOPED;
	}
	if (have_bare || elsewhere) {
		g_free(scoped);
		*ctx = g_strdup(wire);
		return E2E_CTX_BLOCKED;
	}
	*ctx = scoped;
	return E2E_CTX_NEW;
}

char *e2e_ctx_find(const E2E_JSON *kr, const E2E_NET *net, const char *wire)
{
	char *ctx, *scoped;

	switch (e2e_ctx_lookup(kr, net, wire, &ctx)) {
	case E2E_CTX_BLOCKED:
		g_free(ctx);
		return NULL;
	case E2E_CTX_LEGACY:
		/* rows of this network next to the old ones: they are the user's
		   later decision and win, as they would in e2e_adopt_legacy */
		scoped = e2e_ctx_scoped(net, wire);
		if (scoped != NULL && ctx_exists(kr, scoped)) {
			g_free(ctx);
			return scoped;
		}
		g_free(scoped);
		return ctx;
	default:
		return ctx;
	}
}

char *e2e_ctx_resolve(E2E_JSON *kr, const E2E_NET *net, const char *wire, gboolean *changed)
{
	char *ctx;

	if (changed != NULL)
		*changed = FALSE;
	switch (e2e_ctx_lookup(kr, net, wire, &ctx)) {
	case E2E_CTX_BLOCKED:
		g_free(ctx);
		return NULL;
	case E2E_CTX_LEGACY:
		g_free(ctx);
		e2e_adopt_legacy(kr, net->label, wire);
		if (changed != NULL)
			*changed = TRUE;
		return e2e_ctx_scoped(net, wire);
	default:
		return ctx;
	}
}

/* the rows of store (of the form form) that belong to wire, under scoped;
   an existing scoped row wins. merge: the members of a dropped row are
   added to the one that wins (used REKEY nonces) */
static guint move_rows(E2E_JSON *kr, const char *name, ROW_KEY form, const char *wire,
                       const char *scoped, gboolean merge, gboolean *found)
{
	E2E_JSON *store = e2e_json_get(kr, name);
	GPtrArray *keys;
	guint i, j, moved = 0;

	if (!is_object(store))
		return 0;
	keys = keys_of(store, form, wire);
	if (keys->len > 0)
		*found = TRUE;
	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);
		E2E_JSON *row = e2e_json_get(store, k), *have;
		size_t wl = strlen(wire);
		char *nk;

		/* the key with wire replaced by scoped */
		if (form == ROW_HANDLE_FIRST)
			nk = g_strdup_printf("%.*s%s", (int) (strlen(k) - wl), k, scoped);
		else
			nk = g_strconcat(scoped, k + wl, NULL);
		have = e2e_json_get(store, nk);
		if (have != NULL) {
			if (merge && is_object(have) && is_object(row)) {
				GPtrArray *members = e2e_json_keys(row);

				for (j = 0; j < members->len; j++) {
					const char *m = g_ptr_array_index(members, j);

					if (e2e_json_get(have, m) == NULL)
						e2e_json_set(have, m, e2e_json_copy(e2e_json_get(row, m)));
				}
				g_ptr_array_unref(members);
			}
		} else {
			E2E_JSON *copy = e2e_json_copy(row);

			if (g_strcmp0(e2e_json_get_string(copy, "channel"), wire) == 0)
				e2e_json_set_string(copy, "channel", scoped);
			e2e_json_set(store, nk, copy);
			moved++;
		}
		e2e_json_remove(store, k);
		g_free(nk);
	}
	g_ptr_array_unref(keys);
	return moved;
}

static guint move_trust_changes(E2E_JSON *kr, const char *wire, const char *scoped,
                                gboolean *found)
{
	E2E_JSON *list = e2e_json_get(kr, "pending_trust_change");
	guint i, j, moved = 0;

	if (list == NULL || list->type != E2E_JSON_ARRAY)
		return 0;
	for (i = 0; i < list->array->len;) {
		E2E_JSON *row = g_ptr_array_index(list->array, i);
		gboolean dup = FALSE;

		if (!is_object(row) || g_strcmp0(e2e_json_get_string(row, "channel"), wire) != 0) {
			i++;
			continue;
		}
		for (j = 0; j < list->array->len && !dup; j++) {
			E2E_JSON *other = g_ptr_array_index(list->array, j);

			dup = g_strcmp0(e2e_json_get_string(other, "channel"), scoped) == 0 &&
			      g_strcmp0(e2e_json_get_string(other, "handle"),
			                e2e_json_get_string(row, "handle")) == 0;
		}
		*found = TRUE;
		if (dup) {
			g_ptr_array_remove_index(list->array, i);
			continue;
		}
		e2e_json_set_string(row, "channel", scoped);
		moved++;
		i++;
	}
	return moved;
}

guint e2e_adopt_legacy(E2E_JSON *kr, const char *label, const char *wire)
{
	E2E_NET net;
	E2E_JSON *row;
	gboolean found = FALSE;
	char *scoped;
	guint n = 0;

	/* a label is never empty and holds neither the separator nor '|'
	   (e2e_net_init) */
	if (label == NULL || *label == '\0' || strchr(label, E2E_SCOPE_SEP) != NULL ||
	    strchr(label, '|') != NULL || !is_object(kr))
		return 0;
	memset(&net, 0, sizeof(net));
	net.label = (char *) label;
	scoped = e2e_ctx_scoped(&net, wire);
	if (scoped == NULL)
		return 0;
	n += move_rows(kr, "channels", ROW_SAME, wire, scoped, FALSE, &found);
	n += move_rows(kr, "outgoing", ROW_SAME, wire, scoped, FALSE, &found);
	n += move_rows(kr, "incoming", ROW_HANDLE_FIRST, wire, scoped, FALSE, &found);
	n += move_rows(kr, "accepted", ROW_HANDLE_FIRST, wire, scoped, FALSE, &found);
	n += move_rows(kr, "pending_inbound", ROW_HANDLE_FIRST, wire, scoped, FALSE, &found);
	n += move_rows(kr, "seen_rekeys", ROW_HANDLE_FIRST, wire, scoped, TRUE, &found);
	n += move_rows(kr, "pending", ROW_CTX_FIRST, wire, scoped, FALSE, &found);
	n += move_rows(kr, "outgoing_recipients", ROW_CTX_FIRST, wire, scoped, FALSE, &found);
	n += move_trust_changes(kr, wire, scoped, &found);
	/* the name is this network's now: other networks see that (BLOCKED,
	   not NEW, e2e_ctx_lookup) */
	if (found) {
		row = e2e_json_get(e2e_json_get(kr, "channels"), scoped);
		if (is_object(row))
			e2e_json_set_string(row, E2E_ADOPTED_MEMBER, label);
		row = e2e_json_get(e2e_json_get(kr, "outgoing"), scoped);
		if (is_object(row))
			e2e_json_set_string(row, E2E_ADOPTED_MEMBER, label);
	}
	g_free(scoped);
	return n;
}


/* the old own contexts ("@<own>") of peer's sessions and of our requests to
   peer: incoming "<peer>|@x", pending "@x|<peer>" - by the rows' "handle"
   member where they have one */
static void own_ctxs_of(const E2E_JSON *kr, const char *peer, GHashTable *out)
{
	E2E_JSON *store = e2e_json_get(kr, "incoming");
	GPtrArray *keys = e2e_json_keys(store);
	size_t pl = strlen(peer);
	guint i;

	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);
		const char *handle = e2e_json_get_string(e2e_json_get(store, k), "handle");

		if (strncmp(k, peer, pl) != 0 || k[pl] != '|' || k[pl + 1] != '@' ||
		    strchr(k + pl + 1, E2E_SCOPE_SEP) != NULL)
			continue;
		/* the peer's own row: its "handle" member says so; without one
		   the rest must be one "@<own>" ("bob@h|@z|@me" is "bob@h|@z"'s) */
		if (handle != NULL ? strcmp(handle, peer) == 0 : strchr(k + pl + 1, '|') == NULL)
			g_hash_table_add(out, g_strdup(k + pl + 1));
	}
	g_ptr_array_unref(keys);
	store = e2e_json_get(kr, "pending");
	keys = e2e_json_keys(store);
	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);
		size_t kl = strlen(k);

		if (*k == '@' && strchr(k, E2E_SCOPE_SEP) == NULL && kl > pl + 1 &&
		    strcmp(k + kl - pl, peer) == 0 && k[kl - pl - 1] == '|' &&
		    g_strcmp0(e2e_json_get_string(e2e_json_get(store, k), "handle"), peer) == 0)
			g_hash_table_add(out, g_strndup(k, kl - pl - 1));
	}
	g_ptr_array_unref(keys);
}

/* the row at key in store under nk instead; an existing one wins (merge:
   members it lacks are added) */
static gboolean move_one(E2E_JSON *store, const char *key, const char *nk, gboolean merge)
{
	E2E_JSON *row = e2e_json_get(store, key), *have;
	gboolean moved = FALSE;
	guint j;

	if (row == NULL)
		return FALSE;
	have = e2e_json_get(store, nk);
	if (have == NULL) {
		e2e_json_set(store, nk, e2e_json_copy(row));
		moved = TRUE;
	} else if (merge && is_object(have) && is_object(row)) {
		GPtrArray *members = e2e_json_keys(row);

		for (j = 0; j < members->len; j++) {
			const char *m = g_ptr_array_index(members, j);

			if (e2e_json_get(have, m) == NULL)
				e2e_json_set(have, m, e2e_json_copy(e2e_json_get(row, m)));
		}
		g_ptr_array_unref(members);
	}
	e2e_json_remove(store, key);
	return moved;
}

guint e2e_adopt_legacy_peer_sessions(E2E_JSON *kr, const char *label, const char *peer)
{
	GHashTable *ctxs;
	GHashTableIter iter;
	gpointer c;
	guint n = 0;

	if (label == NULL || *label == '\0' || strchr(label, E2E_SCOPE_SEP) != NULL ||
	    strchr(label, '|') != NULL || peer == NULL || *peer == '\0' ||
	    strchr(peer, E2E_SCOPE_SEP) != NULL || !is_object(kr))
		return 0;
	ctxs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	own_ctxs_of(kr, peer, ctxs);
	g_hash_table_iter_init(&iter, ctxs);
	while (g_hash_table_iter_next(&iter, &c, NULL)) {
		char *scoped = g_strdup_printf("%s%c%s", label, E2E_SCOPE_SEP, (char *) c);
		char *k = g_strconcat(peer, "|", c, NULL), *nk = g_strconcat(peer, "|", scoped, NULL);
		const char *fp = e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "incoming"), k), "fp");
		char *fk = NULL, *nfk = NULL;

		if (fp != NULL) {
			fk = g_strconcat(fp, "|", c, NULL);
			nfk = g_strconcat(fp, "|", scoped, NULL);
		}
		if (is_object(e2e_json_get(kr, "incoming")))
			n += move_one(e2e_json_get(kr, "incoming"), k, nk, FALSE);
		if (fk != NULL && is_object(e2e_json_get(kr, "seen_rekeys")))
			n += move_one(e2e_json_get(kr, "seen_rekeys"), fk, nfk, TRUE);
		g_free(k);
		g_free(nk);
		k = g_strconcat(c, "|", peer, NULL);
		nk = g_strconcat(scoped, "|", peer, NULL);
		if (is_object(e2e_json_get(kr, "pending"))) {
			E2E_JSON *row = e2e_json_get(e2e_json_get(kr, "pending"), k);

			if (row != NULL && g_strcmp0(e2e_json_get_string(row, "channel"), c) == 0)
				e2e_json_set_string(row, "channel", scoped);
			n += move_one(e2e_json_get(kr, "pending"), k, nk, FALSE);
		}
		g_free(k);
		g_free(nk);
		g_free(fk);
		g_free(nfk);
		g_free(scoped);
	}
	g_hash_table_destroy(ctxs);
	return n;
}

/* the rows of wire's peer under old "@<own>" contexts (for a DM) */
static gboolean peer_has_old_rows(const E2E_JSON *kr, const char *wire)
{
	GHashTable *ctxs;
	gboolean any;

	if (*wire != '@')
		return FALSE;
	ctxs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	own_ctxs_of(kr, wire + 1, ctxs);
	any = g_hash_table_size(ctxs) > 0;
	g_hash_table_destroy(ctxs);
	return any;
}

E2E_ADOPT_RESULT e2e_adopt_explicit(E2E_JSON *kr, const E2E_NET *net, const char *wire,
                                    const char *nick, guint *moved)
{
	E2E_JSON *channels, *cfg;
	gboolean had_old;
	char *scoped;

	*moved = 0;
	if (net == NULL || !is_object(kr))
		return E2E_ADOPT_UNSCOPABLE;
	scoped = e2e_ctx_scoped(net, wire);
	if (scoped == NULL)
		return E2E_ADOPT_UNSCOPABLE;
	channels = e2e_json_get(kr, "channels");
	/* set up here already (also by an adoption): the old rows stay */
	if (e2e_json_get(channels, scoped) != NULL) {
		g_free(scoped);
		return E2E_ADOPT_ALREADY;
	}
	/* old rows: this network's now - for a DM with the peer's sessions to
	   us, as the user's own DM line takes them */
	had_old = ctx_exists(kr, wire) || peer_has_old_rows(kr, wire);
	*moved = e2e_adopt_legacy(kr, net->label, wire);
	if (*wire == '@')
		*moved += e2e_adopt_legacy_peer_sessions(kr, net->label, wire + 1);
	if (had_old || *moved > 0) {
		g_free(scoped);
		return E2E_ADOPT_MOVED;
	}
	/* another network took the name over: a config of this network's
	   own settles it here (E2E on, keys are exchanged anew); nothing of
	   the other network's is read or copied */
	if (adopted_elsewhere(kr, net, wire) && is_object(channels)) {
		cfg = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set_int(cfg, "enabled", 1);
		e2e_json_set_string(cfg, "mode", "normal");
		if (nick != NULL && *wire == '@')
			e2e_json_set_string(cfg, "nick", nick);
		e2e_json_set(channels, scoped, cfg);
		g_free(scoped);
		return E2E_ADOPT_OWN;
	}
	g_free(scoped);
	return E2E_ADOPT_NONE;
}

gboolean e2e_keyring_has_scoped(const E2E_JSON *kr)
{
	static const char *const stores[] = { "channels", "outgoing", "incoming", NULL };
	gboolean found = FALSE;
	int i;
	guint j;

	for (i = 0; stores[i] != NULL && !found; i++) {
		GPtrArray *keys = e2e_json_keys(e2e_json_get(kr, stores[i]));

		for (j = 0; j < keys->len && !found; j++)
			found = strchr(g_ptr_array_index(keys, j), E2E_SCOPE_SEP) != NULL;
		g_ptr_array_unref(keys);
	}
	return found;
}
