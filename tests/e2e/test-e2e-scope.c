/*
 test-e2e-scope.c : network scope of contexts and CASEMAPPING folding

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

#include "e2e-legacy-fixture.h"

static const E2E_CASEMAP all_maps[] = {
	E2E_CASEMAP_RFC1459,
	E2E_CASEMAP_STRICT_RFC1459,
	E2E_CASEMAP_ASCII,
	E2E_CASEMAP_UNICODE,
	E2E_CASEMAP_UNKNOWN,
};

static void assert_fold(const char *a, const char *b, E2E_CASEMAP map, gboolean same)
{
	char *fa, *fb;

	fa = e2e_casefold(a, map);
	fb = e2e_casefold(b, map);
	g_assert_nonnull(fa);
	g_assert_nonnull(fb);
	if (same)
		g_assert_cmpstr(fa, ==, fb);
	else
		g_assert_cmpstr(fa, !=, fb);
	g_free(fa);
	g_free(fb);
}

#define SAME(a, b, m) assert_fold((a), (b), (m), TRUE)
#define DIFF(a, b, m) assert_fold((a), (b), (m), FALSE)

static void test_casemap_parse(void)
{
	g_assert_cmpint(e2e_casemap_parse(NULL), ==, E2E_CASEMAP_RFC1459);
	g_assert_cmpint(e2e_casemap_parse("ascii"), ==, E2E_CASEMAP_ASCII);
	g_assert_cmpint(e2e_casemap_parse("RFC1459"), ==, E2E_CASEMAP_RFC1459);
	g_assert_cmpint(e2e_casemap_parse("rfc1459"), ==, E2E_CASEMAP_RFC1459);
	g_assert_cmpint(e2e_casemap_parse("strict-rfc1459"), ==, E2E_CASEMAP_STRICT_RFC1459);
	g_assert_cmpint(e2e_casemap_parse("rfc7613"), ==, E2E_CASEMAP_UNICODE);
	g_assert_cmpint(e2e_casemap_parse("rfc8265"), ==, E2E_CASEMAP_UNICODE);
	g_assert_cmpint(e2e_casemap_parse("utf8"), ==, E2E_CASEMAP_UNICODE);
	g_assert_cmpint(e2e_casemap_parse("UTF8"), ==, E2E_CASEMAP_UNICODE);
	/* anything else is unknown: the superset fold, and no
	   non-ASCII name is decided (a byte code page such as cp1251) */
	g_assert_cmpint(e2e_casemap_parse("future-thing"), ==, E2E_CASEMAP_UNKNOWN);
	g_assert_cmpint(e2e_casemap_parse("cp1251"), ==, E2E_CASEMAP_UNKNOWN);
	g_assert_cmpint(e2e_casemap_parse("koi8-r"), ==, E2E_CASEMAP_UNKNOWN);
	/* an empty or truncated value is unknown, so it gets the superset fold */
	g_assert_cmpint(e2e_casemap_parse(""), ==, E2E_CASEMAP_UNKNOWN);
	g_assert_cmpint(e2e_casemap_parse("rfc145"), ==, E2E_CASEMAP_UNKNOWN);
	g_assert_cmpint(e2e_casemap_parse("ascii2"), ==, E2E_CASEMAP_UNKNOWN);
	g_assert_cmpint(e2e_casemap_parse("utf8x"), ==, E2E_CASEMAP_UNKNOWN);

	/* until the server named its CASEMAPPING, or ended
	   its MOTD without one (it sends none: rfc1459), the widest */
	g_assert_cmpint(e2e_casemap_of_server(NULL, FALSE), ==, E2E_CASEMAP_UNICODE);
	g_assert_cmpint(e2e_casemap_of_server("ascii", FALSE), ==, E2E_CASEMAP_ASCII);
	g_assert_cmpint(e2e_casemap_of_server(NULL, TRUE), ==, E2E_CASEMAP_RFC1459);
	g_assert_cmpint(e2e_casemap_of_server("ascii", TRUE), ==, E2E_CASEMAP_ASCII);
	g_assert_cmpint(e2e_casemap_of_server("cp1251", FALSE), ==, E2E_CASEMAP_UNKNOWN);
}

/* every string the casefold test uses, for the idempotence check */
static const char *const fold_inputs[] = {
	"#Secret", "#secret", "#a[b]\\~", "#a{b}|^", "#a[b", "#a{b", "#a[", "#a{",
	"#a~", "#a^",
	"#\xc5\xbb" "aba", "#\xc5\xbc" "aba", "#z\xcc\x87" "aba", "#\xff",
	"#A\xff[", "#\xef\xbc\xbb" "X",
	"#\xe1\x8e\xa0", "#\xea\xad\xb0", "#\xe1\x8f\xb8", "#\xe1\x8f\xb0",
	"#Stra\xc3\x9f" "e", "#STRASSE", "#\xc4\xb0", "#i\xcc\x87",
	"#\xc7\xb0\xcc\xb8", "#J\xcc\xb8\xcc\x8c", "#\xc4\xb0\xcc\xa7", "#i\xcc\xa7\xcc\x87",
	"#a\xc2\xad" "b", "#a\xcd\x8f\xcc\x81\xcc\x80", "#\xe1\xbe\xb3\xcd\x82", "#\xe1\xbe\xbc\xcd\x82", "",
};

static void test_casefold(void)
{
	gsize i, j;
	char *s, *t;

	SAME("#Secret", "#secret", E2E_CASEMAP_ASCII);
	SAME("#a[b]\\~", "#a{b}|^", E2E_CASEMAP_RFC1459);
	DIFF("#a[b", "#a{b", E2E_CASEMAP_ASCII);
	SAME("#a[", "#a{", E2E_CASEMAP_STRICT_RFC1459);
	DIFF("#a~", "#a^", E2E_CASEMAP_STRICT_RFC1459);
	SAME("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_UNICODE); /* #Żaba / #żaba */
	DIFF("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_ASCII);
	DIFF("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_RFC1459);
	SAME("#z\xcc\x87" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_UNICODE); /* NFD vs NFC */
	SAME("#\xff", "#\xff", E2E_CASEMAP_UNICODE); /* invalid UTF-8: byte-wise, no crash */

	/* each exact map, letter by letter */
	SAME("#ABCXYZ", "#abcxyz", E2E_CASEMAP_ASCII);
	DIFF("#a]", "#a}", E2E_CASEMAP_ASCII);
	DIFF("#a\\", "#a|", E2E_CASEMAP_ASCII);
	DIFF("#a~", "#a^", E2E_CASEMAP_ASCII);
	SAME("#a]", "#a}", E2E_CASEMAP_STRICT_RFC1459);
	SAME("#a\\", "#a|", E2E_CASEMAP_STRICT_RFC1459);
	SAME("#a~", "#a^", E2E_CASEMAP_RFC1459);
	/* the exact maps also fold Latin-1 C0-DE to E0-FE (not D7), as
	   the ircu family does; continuation bytes 80-BF stay, so two names
	   in valid UTF-8 never meet that way */
	DIFF("#\xc3\x80", "#\xc3\xa0", E2E_CASEMAP_RFC1459);
	SAME("#\xe9", "#\xc9", E2E_CASEMAP_RFC1459);
	SAME("#\xe9", "#\xc9", E2E_CASEMAP_STRICT_RFC1459);
	SAME("#\xe9", "#\xc9", E2E_CASEMAP_ASCII);
	SAME("#\xc0\xde", "#\xe0\xfe", E2E_CASEMAP_RFC1459);
	DIFF("#\xd7", "#\xf7", E2E_CASEMAP_RFC1459);
	DIFF("#\xbf", "#\xdf", E2E_CASEMAP_RFC1459);
	DIFF("#\xdf", "#\xff", E2E_CASEMAP_RFC1459);
	SAME("#\xc3\xa9", "#\xe3\xa9", E2E_CASEMAP_RFC1459); /* #é and its lead byte folded */
	/* not in the superset fold of valid UTF-8 (it would break it) */
	DIFF("#\xc3\xa9", "#\xe3\xa9", E2E_CASEMAP_UNICODE);
	SAME("#\xe9", "#\xc9", E2E_CASEMAP_UNICODE); /* not UTF-8: byte-wise */
	/* not idempotent on invalid UTF-8 (e2e-scope.h): the Latin-1 byte
	   fold makes it valid, which the next fold takes the Unicode way */
	s = e2e_casefold("#\xcf\xbc\xa1", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, "#\xef\xbc\xa1");
	t = e2e_casefold(s, E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(t, ==, "#a");
	g_free(s);
	g_free(t);
	/* unknown values fold as the superset does */
	SAME("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_UNKNOWN);
	SAME("#a[B", "#A{b", E2E_CASEMAP_UNKNOWN);

	/* the superset fold: rfc1459's specials, NFKC, full case folding */
	SAME("#a[b]\\~", "#A{B}|^", E2E_CASEMAP_UNICODE);
	SAME("#\xef\xbc\xbb" "X", "#{x", E2E_CASEMAP_UNICODE); /* fullwidth [ */
	SAME("#Stra\xc3\x9f" "e", "#STRASSE", E2E_CASEMAP_UNICODE);
	/* invalid UTF-8 still gets rfc1459's byte-wise fold */
	SAME("#A\xff[", "#a\xff{", E2E_CASEMAP_UNICODE);
	/* Cherokee: CaseFolding.txt folds the small letters to the capitals,
	   GLib's fallback folds the capitals to the small letters */
	SAME("#\xe1\x8e\xa0", "#\xea\xad\xb0", E2E_CASEMAP_UNICODE); /* U+13A0 / U+AB70 */
	SAME("#\xe1\x8f\xb0", "#\xe1\x8f\xb8", E2E_CASEMAP_UNICODE); /* U+13F0 / U+13F8 */
	SAME("#\xc4\xb0", "#i\xcc\x87", E2E_CASEMAP_UNICODE); /* U+0130: full case folding */
	/* the Turkish i: per code point case mappings take U+0130 to i and
	   U+0131 to I, so dotted, dotless and plain i are one letter */
	SAME("#\xc4\xb1", "#I", E2E_CASEMAP_UNICODE);                   /* U+0131 */
	SAME("#\xc4\xb0", "#i", E2E_CASEMAP_UNICODE);                   /* U+0130 */
	SAME("#\xc4\xb1i", "#II", E2E_CASEMAP_UNICODE);
	SAME("#\xf0\x9d\x9a\xa4", "#i", E2E_CASEMAP_UNICODE);         /* U+1D6A4 */
	SAME("#i\xcc\x87\xcc\x81", "#i\xcc\x81", E2E_CASEMAP_UNICODE); /* the dot in the marks */
	SAME("#\xc4\xb1\xcc\x87", "#i", E2E_CASEMAP_UNICODE);
	DIFF("#a\xcc\x87", "#a", E2E_CASEMAP_UNICODE);                  /* not after an i */
	DIFF("#i\xcc\x81", "#i", E2E_CASEMAP_UNICODE);                  /* other marks stay */
	DIFF("#ix\xcc\x87", "#ix", E2E_CASEMAP_UNICODE);                /* only directly after */
	/* the run is every mark (g_unichar_ismark), ccc 0 ones like U+20DE too */
	SAME("#i\xe2\x83\x9e\xcc\x87", "#i\xe2\x83\x9e", E2E_CASEMAP_UNICODE);
	DIFF("#\xc4\xb1", "#I", E2E_CASEMAP_RFC1459);
	/* the representative of a Cherokee pair is CaseFolding.txt's, the capital */
	s = e2e_casefold("#\xea\xad\xb0", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, "#\xe1\x8e\xa0");
	g_free(s);
	s = e2e_casefold("#\xe1\x8f\xb8", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, "#\xe1\x8f\xb0");
	g_free(s);
	/* casefold output is normalised again: these are one name under full
	   case folding but come out of casefold in different mark orders */
	SAME("#\xc7\xb0\xcc\xb8", "#J\xcc\xb8\xcc\x8c", E2E_CASEMAP_UNICODE);  /* U+01F0 U+0338 */
	SAME("#\xc4\xb0\xcc\xa7", "#i\xcc\xa7\xcc\x87", E2E_CASEMAP_UNICODE);  /* U+0130 U+0327 */
	/* folding a composed form under-folds: U+0345 joins another base */
	SAME("#\xe1\xbe\xb3\xcd\x82", "#\xe1\xbe\xbc\xcd\x82", E2E_CASEMAP_UNICODE); /* U+1FB3/U+1FBC U+0342 */
	SAME("#\xe1\xbe\xb7", "#\xce\x91\xcd\x82\xce\xb9", E2E_CASEMAP_UNICODE);    /* U+1FB7 */
	/* servers that casefold first put U+0345's iota before the other
	   marks, NFD puts it after them: the marks next to an iota go */
	SAME("#\xe1\xbe\x88\xcc\xaf", "#\xe1\xbc\x88\xce\x99\xcc\xaf", E2E_CASEMAP_UNICODE); /* U+1F88 U+032F */
	SAME("#\xe1\xbf\xbc\xcc\xa8", "#\xcf\x89\xce\xb9\xcc\xa8", E2E_CASEMAP_UNICODE);     /* U+1FFC U+0328 */
	SAME("#\xcf\x80\xce\xbf\xce\xaf\xce\xb7\xce\xbc\xce\xb1", "#\xcf\x80\xce\xbf\xce\xb9\xce\xb7\xce\xbc\xce\xb1",
	     E2E_CASEMAP_UNICODE); /* the over-fold this costs */
	DIFF("#\xce\xb1\xcc\x81", "#\xce\xb1", E2E_CASEMAP_UNICODE); /* marks elsewhere stay */
	SAME("#\xce\xb9\xcc\x81\xcc\x80", "#\xce\xb9", E2E_CASEMAP_UNICODE);  /* marks at the end */
	SAME("\xcc\x81\xcc\x80\xce\xb9", "\xce\xb9", E2E_CASEMAP_UNICODE);    /* marks at the start */
	SAME("#a\xcc\x81\xcd\x85", "#a\xce\xb9", E2E_CASEMAP_UNICODE);        /* a U+0345 */
	DIFF("#a\xcc\x81\xcd\x85", "#\xce\xb9", E2E_CASEMAP_UNICODE);         /* the base stays */
	s = e2e_casefold("#\xc7\xb0\xcc\xb8", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, "#\xc7\xb0\xcc\xb8"); /* NFKC of j U+030C U+0338 */
	g_free(s);
	/* a plain result for the exact maps too */
	s = e2e_casefold("#A[]\\~\xc3\x80", E2E_CASEMAP_RFC1459);
	g_assert_cmpstr(s, ==, "#a{}|^\xe3\x80");
	g_free(s);

	g_assert_null(e2e_casefold(NULL, E2E_CASEMAP_RFC1459));

	/* idempotence: fold(fold(x)) == fold(x) */
	for (i = 0; i < G_N_ELEMENTS(fold_inputs); i++) {
		for (j = 0; j < G_N_ELEMENTS(all_maps); j++) {
			char *once = e2e_casefold(fold_inputs[i], all_maps[j]);
			char *twice = e2e_casefold(once, all_maps[j]);

			g_assert_cmpstr(once, ==, twice);
			g_free(once);
			g_free(twice);
		}
	}
}

/* invisible characters the superset fold drops, as NFKC_Casefold does */
static const char *const ignorables[] = {
	"\xc2\xad",         /* U+00AD soft hyphen (Cf) */
	"\xd8\x80",         /* U+0600 (Cf) */
	"\xe2\x80\x8b",     /* U+200B zero width space (Cf) */
	"\xe2\x80\x8d",     /* U+200D zero width joiner (Cf) */
	"\xe2\x80\x8f",     /* U+200F (Cf) */
	"\xe2\x81\xa0",     /* U+2060 word joiner (Cf) */
	"\xef\xbb\xbf",     /* U+FEFF (Cf) */
	"\xf3\xa0\x80\x81", /* U+E0001 language tag (Cf) */
	"\xef\xb8\x80",     /* U+FE00 variation selector 1 */
	"\xef\xb8\x8f",     /* U+FE0F variation selector 16 */
	"\xf3\xa0\x84\x80", /* U+E0100 variation selector 17 */
	"\xf3\xa0\x87\xaf", /* U+E01EF variation selector 256 */
	"\xcd\x8f",         /* U+034F combining grapheme joiner */
	"\xe1\xa0\x8b",     /* U+180B Mongolian free variation selector 1 */
	"\xe1\xa0\x8c",     /* U+180C Mongolian free variation selector 2 */
	"\xe1\xa0\x8d",     /* U+180D Mongolian free variation selector 3 */
	"\xe1\xa0\x8f",     /* U+180F Mongolian free variation selector 4 */
	"\xe1\x9e\xb4",     /* U+17B4 Khmer vowel inherent aq */
	"\xe1\x9e\xb5",     /* U+17B5 Khmer vowel inherent aa */
	"\xe1\x85\x9f",     /* U+115F Hangul choseong filler */
	"\xe1\x85\xa0",     /* U+1160 Hangul jungseong filler */
	"\xe3\x85\xa4",     /* U+3164 Hangul filler */
	"\xef\xbe\xa0",     /* U+FFA0 halfwidth Hangul filler */
};

/* their neighbours outside the ranges are kept */
static const char *const kept_neighbours[] = {
	"\xef\xb8\x90",     /* U+FE10 (NFKC: ',') */
	"\xef\xb7\xbf",     /* U+FDFF */
	"\xf3\xa0\x83\xbf", /* U+E00FF */
	"\xf3\xa0\x87\xb0", /* U+E01F0 */
	"\xcd\x8e",         /* U+034E */
	"\xe1\xa0\x8a",     /* U+180A */
	"\xe1\xa0\x90",     /* U+1810 */
	"\xe1\x9e\xb3",     /* U+17B3 */
	"\xe1\x9e\xb6",     /* U+17B6 */
	"\xe1\x85\x9e",     /* U+115E */
	"\xe1\x85\xa1",     /* U+1161 */
	"\xe3\x85\xa5",     /* U+3165 */
	"\xef\xbe\xa1",     /* U+FFA1 */
};

static gboolean oracle_ignorable(gunichar c)
{
	return g_unichar_type(c) == G_UNICODE_FORMAT || (c >= 0xfe00 && c <= 0xfe0f) ||
	       (c >= 0xe0100 && c <= 0xe01ef) || (c >= 0x180b && c <= 0x180d) || c == 0x180f ||
	       c == 0x17b4 || c == 0x17b5 || c == 0x34f || c == 0x115f || c == 0x1160 || c == 0x3164 || c == 0xffa0;
}

static void test_casefold_ignorables(void)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS(ignorables); i++) {
		char *with = g_strconcat("#a", ignorables[i], "b", NULL);
		char *only = g_strconcat("#", ignorables[i], NULL);

		SAME("#ab", with, E2E_CASEMAP_UNICODE);
		SAME("#", only, E2E_CASEMAP_UNICODE);
		DIFF("#ab", with, E2E_CASEMAP_ASCII);
		DIFF("#ab", with, E2E_CASEMAP_RFC1459);
		DIFF("#ab", with, E2E_CASEMAP_STRICT_RFC1459);
		g_free(with);
		g_free(only);
	}
	for (i = 0; i < G_N_ELEMENTS(kept_neighbours); i++) {
		char *with = g_strconcat("#a", kept_neighbours[i], "b", NULL);

		DIFF("#ab", with, E2E_CASEMAP_UNICODE);
		g_free(with);
	}
	SAME("#a\xe1\xa0\x8b" "b", "#ab", E2E_CASEMAP_UNICODE); /* U+180B */
	DIFF("#a\xe1\xa0\x8b" "b", "#ab", E2E_CASEMAP_RFC1459);
	/* a joiner between marks blocks reordering until it is dropped */
	SAME("#a\xcc\x81\xcd\x8f\xcc\x80", "#a\xcc\x81\xcc\x80", E2E_CASEMAP_UNICODE);
	SAME("#\xc3\x81\xcd\x8f", "#a\xcc\x81", E2E_CASEMAP_UNICODE);
}

/* what every server of the map takes for one name, at
   least: two names e2e_casefold_narrow() keeps apart may be two channels */
static void assert_narrow(const char *a, const char *b, E2E_CASEMAP map, gboolean same)
{
	char *na = e2e_casefold_narrow(a, map), *nb = e2e_casefold_narrow(b, map);

	g_assert_nonnull(na);
	g_assert_nonnull(nb);
	if (same)
		g_assert_cmpstr(na, ==, nb);
	else
		g_assert_cmpstr(na, !=, nb);
	g_free(na);
	g_free(nb);
}

static void test_casefold_narrow(void)
{
	/* UNICODE: per code point simple lower case (towlower, Go) and NFKC
	   with case folding (the PRECIS / utf8 servers) must both agree */
	assert_narrow("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_UNICODE, TRUE);  /* Ż / ż */
	assert_narrow("#ABC", "#abc", E2E_CASEMAP_UNICODE, TRUE);
	/* and whole-string lower case (Final_Sigma), and upper case
	   (servers that compare in upper case) */
	assert_narrow("#\xc5\xbb" "ABA", "#\xc5\xbc" "aba", E2E_CASEMAP_UNICODE, TRUE);
	assert_narrow("#k", "#\xe2\x84\xaa", E2E_CASEMAP_UNICODE, FALSE);                /* Kelvin sign: K / U+212A in upper case */
	assert_narrow("#\xce\x91\xce\xa3", "#\xce\xb1\xcf\x83", E2E_CASEMAP_UNICODE, FALSE); /* #ΑΣ / #ασ: ας */
	assert_narrow("#\xc3\x85", "#\xe2\x84\xab", E2E_CASEMAP_UNICODE, FALSE);        /* Å / Angstrom sign */
	assert_narrow("#\xc3\x9f", "#\xe1\xba\x9e", E2E_CASEMAP_UNICODE, FALSE);        /* ß / ẞ */
	assert_narrow("#\xce\xb8", "#\xcf\xb4", E2E_CASEMAP_UNICODE, FALSE);             /* θ / ϴ */
	assert_narrow("#\xcf\x89", "#\xe2\x84\xa6", E2E_CASEMAP_UNICODE, FALSE);        /* ω / Ohm sign */
	assert_narrow("#\xce\xa9", "#\xcf\x89", E2E_CASEMAP_UNICODE, TRUE);              /* Ω / ω */
	/* and Unicode's own Final_Sigma (a cased letter before, case
	   ignorables skipped both ways, no cased letter after); GLib's rule
	   ("the next character is not a letter") takes a final Σ before a
	   letter without case for σ */
	assert_narrow("#\xce\x91\xce\xa3\xe4\xb8\xad", "#\xce\xb1\xcf\x83\xe4\xb8\xad",
	              E2E_CASEMAP_UNICODE, FALSE);                                            /* #ΑΣ中 / #ασ中 */
	assert_narrow("#\xce\x91\xce\xa3\xd7\x91", "#\xce\xb1\xcf\x83\xd7\x91",
	              E2E_CASEMAP_UNICODE, FALSE);                                            /* #ΑΣב / #ασב */
	assert_narrow("#\xce\x91\xce\xa3\xe0\xa4\x95", "#\xce\xb1\xcf\x83\xe0\xa4\x95",
	              E2E_CASEMAP_UNICODE, FALSE);                                            /* #ΑΣक / #ασक */
	assert_narrow("#\xce\x91\xce\xa3\xca\xb0", "#\xce\xb1\xcf\x83\xca\xb0",
	              E2E_CASEMAP_UNICODE, FALSE);                                            /* #ΑΣʰ / #ασʰ: ʰ case ignorable */
	assert_narrow("#\xce\x91\xcc\x81\xce\xa3\xe4\xb8\xad", "#\xce\xb1\xcc\x81\xcf\x83\xe4\xb8\xad",
	              E2E_CASEMAP_UNICODE, FALSE);                                            /* #Ά́Σ中: a mark before Σ skipped */
	assert_narrow("#\xce\x91'\xce\xa3\xe4\xb8\xad", "#\xce\xb1'\xcf\x83\xe4\xb8\xad",
	              E2E_CASEMAP_UNICODE, FALSE);                                            /* #Α'Σ中: an apostrophe skipped */
	assert_narrow("#\xce\x91\xce\xa3\xce\x91", "#\xce\xb1\xcf\x83\xce\xb1",
	              E2E_CASEMAP_UNICODE, TRUE);                                             /* #ΑΣΑ / #ασα: not final */
	assert_narrow("#\xce\x91\xce\xa3\xca\xb0\xce\x91", "#\xce\xb1\xcf\x83\xca\xb0\xce\xb1",
	              E2E_CASEMAP_UNICODE, TRUE);                                             /* #ΑΣʰΑ: ʰ skipped, Α after */
	assert_narrow("#\xce\x91\xce\xa3\xc2\xaa", "#\xce\xb1\xcf\x83\xc2\xaa",
	              E2E_CASEMAP_UNICODE, TRUE);                                             /* #ΑΣª: ª is cased (Lo) */
	assert_narrow("#\xce\xa3\xe4\xb8\xad", "#\xcf\x83\xe4\xb8\xad",
	              E2E_CASEMAP_UNICODE, TRUE);                                             /* #Σ中: no cased letter before, σ */
	assert_narrow("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_UNICODE, TRUE);  /* #Żaba / #żaba */
	assert_narrow("#ss", "#\xc3\x9f", E2E_CASEMAP_UNICODE, FALSE);                    /* ß */
	assert_narrow("#e\xcc\x81", "#\xc3\xa9", E2E_CASEMAP_UNICODE, FALSE);           /* NFD é */
	assert_narrow("#ff", "#\xef\xac\x80", E2E_CASEMAP_UNICODE, FALSE);               /* ﬀ */
	assert_narrow("#i", "#\xc4\xb0", E2E_CASEMAP_UNICODE, FALSE);                     /* İ: tolower i, casefold i+U+0307 */
	assert_narrow("#i", "#\xc4\xb1", E2E_CASEMAP_UNICODE, FALSE);                     /* ı */
	assert_narrow("#a[b", "#A{B", E2E_CASEMAP_UNICODE, FALSE);                          /* no specials */
	assert_narrow("#ab", "#a\xe2\x80\x8b" "b", E2E_CASEMAP_UNICODE, FALSE);          /* nothing dropped */
	g_assert_null(e2e_casefold_narrow("#\xff", E2E_CASEMAP_UNICODE));
	/* UNKNOWN: a byte code page may fold any non-ASCII byte its own way,
	   or none: ASCII letters only */
	assert_narrow("#ABC", "#abc", E2E_CASEMAP_UNKNOWN, TRUE);
	assert_narrow("#\xc5\xbb" "aba", "#\xc5\xbc" "aba", E2E_CASEMAP_UNKNOWN, FALSE);
	assert_narrow("#\xd0\x98", "#\xd0\xb8", E2E_CASEMAP_UNKNOWN, FALSE);            /* И / и */
	assert_narrow("#a[", "#a{", E2E_CASEMAP_UNKNOWN, FALSE);
	assert_narrow("#\xff", "#\xff", E2E_CASEMAP_UNKNOWN, TRUE);
	/* the exact maps: their fold without the Latin-1 bytes */
	assert_narrow("#A[\xc9", "#a{\xc9", E2E_CASEMAP_RFC1459, TRUE);
	assert_narrow("#\xc9", "#\xe9", E2E_CASEMAP_RFC1459, FALSE);
	assert_narrow("#A[~", "#a[~", E2E_CASEMAP_ASCII, TRUE);
	assert_narrow("#a[", "#a{", E2E_CASEMAP_ASCII, FALSE);
	g_assert_null(e2e_casefold_narrow(NULL, E2E_CASEMAP_RFC1459));
}

static void test_casefold_ex(void)
{
	gboolean exact;
	char *s, *t;

	exact = FALSE;
	s = e2e_casefold_ex("#\xc5\xbb" "aba", E2E_CASEMAP_UNICODE, &exact);
	g_assert_true(exact);
	t = e2e_casefold("#\xc5\xbb" "aba", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, t);
	g_assert_cmpstr(s, ==, "#\xc5\xbc" "aba");
	g_free(s);
	g_free(t);

	/* the byte-wise fallback says so, with the same result */
	exact = TRUE;
	s = e2e_casefold_ex("#A\xff[", E2E_CASEMAP_UNICODE, &exact);
	g_assert_false(exact);
	g_assert_cmpstr(s, ==, "#a\xff{");
	t = e2e_casefold("#A\xff[", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, t);
	g_free(s);
	g_free(t);

	/* the exact maps are exact whatever the bytes */
	exact = FALSE;
	s = e2e_casefold_ex("#A\xff[", E2E_CASEMAP_RFC1459, &exact);
	g_assert_true(exact);
	g_assert_cmpstr(s, ==, "#a\xff{");
	g_free(s);
	exact = FALSE;
	s = e2e_casefold_ex("#A", E2E_CASEMAP_ASCII, &exact);
	g_assert_true(exact);
	g_free(s);
	exact = FALSE;
	s = e2e_casefold_ex("#A~", E2E_CASEMAP_STRICT_RFC1459, &exact);
	g_assert_true(exact);
	g_assert_cmpstr(s, ==, "#a~");
	g_free(s);

	/* an unassigned code point may be anything to a server with
	   newer tables: not decided (the fold is still given) */
	exact = TRUE;
	s = e2e_casefold_ex("#a\xe2\x81\xa5" "b", E2E_CASEMAP_UNICODE, &exact);
	g_assert_false(exact);
	g_assert_nonnull(s);
	g_free(s);
	exact = TRUE;
	s = e2e_casefold_ex("#\xf3\xb0\x80\x80", E2E_CASEMAP_UNICODE, &exact); /* U+F0000 private use: assigned */
	g_assert_true(exact);
	g_free(s);
	/* an unknown CASEMAPPING decides ASCII names only */
	exact = FALSE;
	s = e2e_casefold_ex("#A[b", E2E_CASEMAP_UNKNOWN, &exact);
	g_assert_true(exact);
	g_assert_cmpstr(s, ==, "#a{b");
	g_free(s);
	exact = TRUE;
	s = e2e_casefold_ex("#\xd0\x98", E2E_CASEMAP_UNKNOWN, &exact); /* #И */
	g_assert_false(exact);
	t = e2e_casefold("#\xd0\x98", E2E_CASEMAP_UNICODE);
	g_assert_cmpstr(s, ==, t);
	g_free(s);
	g_free(t);

	/* any byte from 0x80 up is not ASCII: no fast path for it */
	exact = TRUE;
	s = e2e_casefold_ex("#A\x80", E2E_CASEMAP_UNICODE, &exact);
	g_assert_false(exact);
	g_assert_cmpstr(s, ==, "#a\x80");
	g_free(s);

	/* a NULL name decides nothing */
	exact = TRUE;
	g_assert_null(e2e_casefold_ex(NULL, E2E_CASEMAP_UNICODE, &exact));
	g_assert_false(exact);
	exact = TRUE;
	g_assert_null(e2e_casefold_ex(NULL, E2E_CASEMAP_ASCII, &exact));
	g_assert_false(exact);

	/* exact may be NULL */
	s = e2e_casefold_ex("#\xff", E2E_CASEMAP_UNICODE, NULL);
	g_assert_cmpstr(s, ==, "#\xff");
	g_free(s);
}

#define RANDOM_SEED       0x52504532u
#define RANDOM_ITERATIONS 100000

/* a code point from pools that stress normalisation and case folding */
static gunichar random_code_point(GRand *rand)
{
	static const char ascii[] = "AZaz09[]\\~{}|^#-";
	static const gunichar special[] = {
		0x130, 0x131, 0x1f0, 0xdf, 0x1e9e, 0x3a3, 0x3c2, 0x345, 0x390, 0x1fb3,
		0x13a0, 0x13f5, 0x13f8, 0xab70, 0xabbf, 0x1c4, 0x1c5, 0x1c6, 0xfb00, 0xfb03,
		0x212a, 0x212b, 0x2126, 0x1e9b, 0x149, 0x17f, 0xb5, 0x3d0, 0x10400, 0x10428,
	};
	static const struct { gunichar lo, hi; } ranges[] = {
		{ 0xc0, 0x24f },       /* Latin letters */
		{ 0x300, 0x36f },      /* combining marks */
		{ 0x327, 0x338 },      /* cedilla .. long solidus overlay */
		{ 0x370, 0x3ff },      /* Greek */
		{ 0x400, 0x4ff },      /* Cyrillic */
		{ 0x1100, 0x11ff },    /* Hangul jamo */
		{ 0xac00, 0xd7a3 },    /* Hangul syllables */
		{ 0x1e00, 0x1fff },    /* Latin extended additional, Greek extended */
		{ 0xff01, 0xff5e },    /* fullwidth */
		{ 0x2160, 0x217f },    /* Roman numerals */
		{ 0x24b6, 0x24e9 },    /* circled letters */
		{ 0x1d400, 0x1d7ff },  /* mathematical letters */
		{ 0x10a0, 0x10ff },    /* Georgian */
	};
	gunichar c;

	switch (g_rand_int_range(rand, 0, 6)) {
	case 0:
		return ascii[g_rand_int_range(rand, 0, sizeof(ascii) - 1)];
	case 1:
		return special[g_rand_int_range(rand, 0, G_N_ELEMENTS(special))];
	case 2: {
		const char *s = ignorables[g_rand_int_range(rand, 0, G_N_ELEMENTS(ignorables))];

		return g_utf8_get_char(s);
	}
	case 3:
	case 4: {
		int r = g_rand_int_range(rand, 0, G_N_ELEMENTS(ranges));

		return g_rand_int_range(rand, ranges[r].lo, ranges[r].hi + 1);
	}
	default:
		do
			c = g_rand_int_range(rand, 0x20, 0x110000);
		while (!g_unichar_validate(c));
		return c;
	}
}

/* the same name another way: other case per letter, rfc1459 specials,
   another normalisation form, invisible characters added */
static char *variant_of(GRand *rand, const char *s)
{
	static const GNormalizeMode modes[] = {
		G_NORMALIZE_NFD, G_NORMALIZE_NFC, G_NORMALIZE_NFKD, G_NORMALIZE_NFKC,
	};
	GString *out = g_string_new(NULL);
	const char *p;
	char *normal;
	int mode, extra;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p), v = c;
		const char *swap;

		if (g_rand_boolean(rand)) {
			if (c < 0x80 && (swap = strchr("[]\\~{}|^", c)) != NULL)
				v = "{}|^[]\\~"[swap - "[]\\~{}|^"];
			else if (g_rand_boolean(rand))
				v = g_unichar_toupper(c);
			else if (g_rand_boolean(rand))
				v = g_unichar_tolower(c);
			else
				v = g_unichar_totitle(c);
		}
		g_string_append_unichar(out, v);
	}
	mode = g_rand_int_range(rand, 0, G_N_ELEMENTS(modes) + 1);
	if (mode < (int) G_N_ELEMENTS(modes)) {
		normal = g_utf8_normalize(out->str, -1, modes[mode]);
		g_string_assign(out, normal);
		g_free(normal);
	}
	for (extra = g_rand_int_range(rand, 0, 3); extra > 0; extra--) {
		glong at = g_rand_int_range(rand, 1, g_utf8_strlen(out->str, -1) + 1);
		const char *ign = ignorables[g_rand_int_range(rand, 0, G_N_ELEMENTS(ignorables))];

		g_string_insert(out, g_utf8_offset_to_pointer(out->str, at) - out->str, ign);
	}
	return g_string_free(out, FALSE);
}

/* the code points of s, for messages */
static char *code_points(const char *s)
{
	GString *out = g_string_new(NULL);
	const char *p;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p))
		g_string_append_printf(out, "%sU+%04X", out->len > 0 ? " " : "", g_utf8_get_char(p));
	return g_string_free(out, FALSE);
}

#define SERVER_MODELS 9

/* s with every code point mapped by map (towlower / unicode.ToLower) */
static char *map_code_points(const char *s, gunichar (*map)(gunichar))
{
	GString *out = g_string_new(NULL);
	const char *p;

	for (p = s; *p != '\0'; p = g_utf8_next_char(p))
		g_string_append_unichar(out, map(g_utf8_get_char(p)));
	return g_string_free(out, FALSE);
}

/* what a server may take as the same name as s: rfc8265 / PRECIS order,
   tests/e2e/e2eircd.py's utf8 model, the other order, whole-string GLib
   case mappings, per code point case mappings (C towlower / towupper, Go
   unicode.ToLower / ToUpper, hybrid's and solanum's upper-case irccmp),
   and upper case after NFKC */
static char *server_image(int model, const char *s)
{
	char *a, *b;

	switch (model) {
	case 6:
		return map_code_points(s, g_unichar_tolower);
	case 7:
		return map_code_points(s, g_unichar_toupper);
	case 8:
		a = g_utf8_normalize(s, -1, G_NORMALIZE_NFKC);
		b = g_utf8_strup(a, -1);
		break;
	case 0:
		a = g_utf8_casefold(s, -1);
		b = g_utf8_normalize(a, -1, G_NORMALIZE_NFC);
		break;
	case 1:
		a = g_utf8_normalize(s, -1, G_NORMALIZE_NFKC);
		b = g_utf8_casefold(a, -1);
		break;
	case 2:
		a = g_utf8_casefold(s, -1);
		b = g_utf8_normalize(a, -1, G_NORMALIZE_NFKC);
		break;
	case 3:
		return g_utf8_strup(s, -1);
	case 4:
		return g_utf8_strdown(s, -1);
	default:
		return g_utf8_casefold(s, -1);
	}
	g_free(a);
	return b;
}

/* seeded random names of several code points: the superset fold is
   idempotent, drops every invisible character, folds every variant of a
   name (rfc1459-equal ones among them) to the same string, and every name
   a server model takes as the same */
static void test_casefold_random(void)
{
	GRand *rand = g_rand_new_with_seed(RANDOM_SEED);
	GString *name = g_string_new(NULL);
	int i, n, model;

	for (i = 0; i < RANDOM_ITERATIONS; i++) {
		char *once, *twice, *variant, *vfold;
		const char *p;

		g_string_assign(name, "#");
		for (n = g_rand_int_range(rand, 1, 7); n > 0; n--)
			g_string_append_unichar(name, random_code_point(rand));

		once = e2e_casefold(name->str, E2E_CASEMAP_UNICODE);
		g_assert_true(g_utf8_validate(once, -1, NULL));
		for (p = once; *p != '\0'; p = g_utf8_next_char(p)) {
			if (oracle_ignorable(g_utf8_get_char(p)))
				g_error("iteration %d: U+%04X left in the fold", i,
				        g_utf8_get_char(p));
		}
		twice = e2e_casefold(once, E2E_CASEMAP_UNICODE);
		if (strcmp(once, twice) != 0)
			g_error("iteration %d: fold of \"%s\" is not idempotent", i, name->str);

		variant = variant_of(rand, name->str);
		vfold = e2e_casefold(variant, E2E_CASEMAP_UNICODE);
		if (strcmp(once, vfold) != 0)
			g_error("iteration %d: %s and %s fold apart", i, code_points(name->str),
			        code_points(variant));

		for (model = 0; model < SERVER_MODELS; model++) {
			char *image, *ifold;

			image = server_image(model, name->str);
			ifold = e2e_casefold(image, E2E_CASEMAP_UNICODE);

			if (strcmp(once, ifold) != 0)
				g_error("iteration %d: %s and its server model %d image %s fold apart",
				        i, code_points(name->str), model, code_points(image));
			g_free(image);
			g_free(ifold);
		}

		g_free(once);
		g_free(twice);
		g_free(variant);
		g_free(vfold);
	}
	g_string_free(name, TRUE);
	g_rand_free(rand);
}

/* random ASCII names: the UNICODE fold of a pure ASCII name equals the
   full Unicode path (forced by an invisible U+200B, which it drops) and
   the byte-wise rfc1459 fold */
static void test_casefold_ascii(void)
{
	GRand *rand = g_rand_new_with_seed(RANDOM_SEED);
	GString *name = g_string_new(NULL);
	int i, n;

	for (i = 0; i < 20000; i++) {
		char *fast, *slow, *bytes, *forced;
		gboolean exact = FALSE;

		g_string_truncate(name, 0);
		for (n = g_rand_int_range(rand, 0, 12); n > 0; n--)
			g_string_append_c(name, (char) g_rand_int_range(rand, 1, 0x80));
		forced = g_strconcat(name->str, "\xe2\x80\x8b", NULL);

		fast = e2e_casefold_ex(name->str, E2E_CASEMAP_UNICODE, &exact);
		g_assert_true(exact);
		slow = e2e_casefold(forced, E2E_CASEMAP_UNICODE);
		bytes = e2e_casefold(name->str, E2E_CASEMAP_RFC1459);
		g_assert_cmpstr(fast, ==, slow);
		g_assert_cmpstr(fast, ==, bytes);
		g_free(fast);
		g_free(slow);
		g_free(bytes);
		g_free(forced);
	}
	g_string_free(name, TRUE);
	g_rand_free(rand);
}

/* every code point: the superset fold is idempotent and equates a letter
   with its upper and lower case, U+0130 and U+0131 too */
static void test_casefold_all_code_points(void)
{
	gunichar u, variants[2];
	char buf[8], vbuf[8];
	const char *p;
	int len, k;

	for (u = 0x80; u <= 0x10ffff; u++) {
		char *once, *twice;

		if (!g_unichar_validate(u))
			continue;
		len = g_unichar_to_utf8(u, buf);
		buf[len] = '\0';
		once = e2e_casefold(buf, E2E_CASEMAP_UNICODE);
		g_assert_true(g_utf8_validate(once, -1, NULL));
		if (oracle_ignorable(u) ? *once != '\0' : *once == '\0')
			g_error("U+%04X: dropped wrongly or kept wrongly", u);
		for (p = once; *p != '\0'; p = g_utf8_next_char(p)) {
			if (oracle_ignorable(g_utf8_get_char(p)))
				g_error("U+%04X folds to an invisible U+%04X", u, g_utf8_get_char(p));
		}
		twice = e2e_casefold(once, E2E_CASEMAP_UNICODE);
		if (strcmp(once, twice) != 0)
			g_error("fold of U+%04X is not idempotent", u);
		g_free(twice);

		variants[0] = g_unichar_toupper(u);
		variants[1] = g_unichar_tolower(u);
		for (k = 0; k < 2; k++) {
			char *other;

			if (variants[k] == u)
				continue;
			len = g_unichar_to_utf8(variants[k], vbuf);
			vbuf[len] = '\0';
			other = e2e_casefold(vbuf, E2E_CASEMAP_UNICODE);
			if (strcmp(once, other) != 0)
				g_error("U+%04X and U+%04X fold apart", u, variants[k]);
			g_free(other);
		}
		g_free(once);
	}
}

static void test_contexts(void)
{
	E2E_NET net, odd, bare, none, ctl;
	char *s, *t;

	e2e_net_init(&net, "IRCnet", "IRCnet2", NULL, FALSE);
	g_assert_cmpstr(net.label, ==, "IRCnet");
	g_assert_cmpint(net.casemap, ==, E2E_CASEMAP_RFC1459);
	g_assert_false(net.legacy_ok);

	s = e2e_ctx_scoped(&net, "#test");
	g_assert_cmpstr(s, ==, "IRCnet\x1f#test");
	g_assert_cmpstr(e2e_ctx_wire(s), ==, "#test");
	g_assert_cmpstr(e2e_ctx_wire("#test"), ==, "#test");
	t = e2e_ctx_network(s);
	g_assert_cmpstr(t, ==, "IRCnet");
	g_free(t);
	g_assert_null(e2e_ctx_network("#test"));
	t = e2e_ctx_display(s);
	g_assert_cmpstr(t, ==, "IRCnet:#test");
	g_free(t);
	t = e2e_ctx_display("@a@b");
	g_assert_cmpstr(t, ==, "@a@b");
	g_free(t);
	t = e2e_ctx_rescope(s, "@me@host");
	g_assert_cmpstr(t, ==, "IRCnet\x1f@me@host");
	g_free(t);
	t = e2e_ctx_rescope("#test", "@me@host");
	g_assert_cmpstr(t, ==, "@me@host");
	g_free(t);
	g_free(s);

	s = e2e_ctx_scoped(NULL, "#test");
	g_assert_cmpstr(s, ==, "#test");
	g_free(s);
	g_assert_null(e2e_ctx_scoped(&net, NULL));
	g_assert_cmpstr(e2e_ctx_wire(NULL), ==, NULL);
	g_assert_null(e2e_ctx_network(NULL));
	g_assert_null(e2e_ctx_display(NULL));

	g_assert_true(e2e_ctx_on_net("IRCnet\x1f#a", &net));
	g_assert_false(e2e_ctx_on_net("Libera\x1f#a", &net));
	g_assert_false(e2e_ctx_on_net("IRCnet2\x1f#a", &net));
	g_assert_false(e2e_ctx_on_net("IRCne\x1f#a", &net));
	g_assert_false(e2e_ctx_on_net("\x1f#a", &net));
	g_assert_true(e2e_ctx_on_net("#a", &net)); /* legacy row: any network */
	g_assert_false(e2e_ctx_on_net(NULL, &net));
	/* a scoped row is on no network that has no label */
	g_assert_false(e2e_ctx_on_net("IRCnet\x1f#a", NULL));
	g_assert_true(e2e_ctx_on_net("#a", NULL));

	/* only the first separator splits: the label never has one */
	g_assert_cmpstr(e2e_ctx_wire("IRCnet\x1f#a\x1f" "b"), ==, "#a\x1f" "b");
	t = e2e_ctx_network("IRCnet\x1f#a\x1f" "b");
	g_assert_cmpstr(t, ==, "IRCnet");
	g_free(t);
	t = e2e_ctx_rescope("IRCnet\x1f#a\x1f" "b", "#c");
	g_assert_cmpstr(t, ==, "IRCnet\x1f#c");
	g_free(t);
	/* never a raw control byte: a further 0x1F, formatting codes */
	t = e2e_ctx_display("IRCnet\x1f#a\x1f" "b");
	g_assert_cmpstr(t, ==, "IRCnet:#a?b");
	g_free(t);
	t = e2e_ctx_display("#a\x02" "b\x03" "4c\x7f");
	g_assert_cmpstr(t, ==, "#a?b?4c?");
	g_free(t);
	t = e2e_text_display("bob\x1f@h\x0f");
	g_assert_cmpstr(t, ==, "bob?@h?");
	g_free(t);
	t = e2e_text_display("ża\x01");
	g_assert_cmpstr(t, ==, "ża?");
	g_free(t);
	g_assert_null(e2e_text_display(NULL));
	/* C1 controls, decoded (U+0085) or as raw bytes, and the bidi and
	   format controls that reorder or hide text */
	t = e2e_text_display("a\xc2\x85" "b\x85" "c\x9b" "d\xc2\x9f" "e");
	g_assert_cmpstr(t, ==, "a?b?c?d?e");
	g_free(t);
	t = e2e_text_display("1\xe2\x80\xaa" "2\xe2\x80\xae" "3\xe2\x81\xa6" "4\xe2\x81\xa9"
	                     "5\xe2\x80\x8e" "6\xe2\x80\x8f" "7\xd8\x9c" "8");
	g_assert_cmpstr(t, ==, "1?2?3?4?5?6?7?8");
	g_free(t);
	/* their neighbours stay: NBSP, U+202F, U+2065, U+200D, ż, a stray
	   byte that is no C1 control */
	t = e2e_text_display("\xc2\xa0\xe2\x80\xaf\xe2\x81\xa5\xe2\x80\x8d\xc5\xbc\xff\xa0");
	g_assert_cmpstr(t, ==, "\xc2\xa0\xe2\x80\xaf\xe2\x81\xa5\xe2\x80\x8d\xc5\xbc\xff\xa0");
	g_free(t);
	t = e2e_ctx_display("IRCnet\x1f#a\xe2\x80\xae" "b\xc2\x9b");
	g_assert_cmpstr(t, ==, "IRCnet:#a?b?");
	g_free(t);

	/* a wire context holding the separator cannot be scoped */
	g_assert_null(e2e_ctx_scoped(&net, "#a\x1f" "b"));
	g_assert_null(e2e_ctx_scoped(&net, "\x1f"));
	g_assert_null(e2e_ctx_scoped(NULL, "#a\x1f" "b"));
	g_assert_null(e2e_ctx_rescope("IRCnet\x1f#a", "#a\x1f" "b"));
	g_assert_null(e2e_ctx_rescope("#a", "#a\x1f" "b"));
	g_assert_null(e2e_ctx_rescope(NULL, "\x1f"));
	g_assert_null(e2e_ctx_rescope("IRCnet\x1f#a", NULL));
	t = e2e_ctx_rescope(NULL, "#b");
	g_assert_cmpstr(t, ==, "#b");
	g_free(t);

	e2e_net_init(&odd, "IR|C\x1fnet", "t", "ascii", TRUE);
	g_assert_cmpstr(odd.label, ==, "IR_C_net");
	g_assert_cmpint(odd.casemap, ==, E2E_CASEMAP_ASCII);
	g_assert_true(odd.legacy_ok);
	s = e2e_ctx_scoped(&odd, "#x");
	g_assert_cmpstr(s, ==, "IR_C_net\x1f#x");
	g_assert_true(e2e_ctx_on_net(s, &odd));
	g_free(s);

	e2e_net_init(&bare, NULL, "libera", NULL, TRUE);
	g_assert_cmpstr(bare.label, ==, "libera");
	e2e_net_clear(&bare);
	e2e_net_init(&bare, "", "libera", NULL, TRUE);
	g_assert_cmpstr(bare.label, ==, "libera");

	e2e_net_init(&ctl, NULL, "\x01" "a\x1f" "b|c\x7f", NULL, FALSE);
	g_assert_cmpstr(ctl.label, ==, "_a_b_c_");
	e2e_net_clear(&ctl);
	e2e_net_init(&ctl, "My Net", "t", NULL, FALSE);
	g_assert_cmpstr(ctl.label, ==, "My Net"); /* space and printable ASCII kept */
	e2e_net_clear(&ctl);
	e2e_net_init(&ctl, " !~}", "t", NULL, FALSE);
	g_assert_cmpstr(ctl.label, ==, " !~}");
	e2e_net_clear(&ctl);
	/* C1 controls are one '_' each; U+00A0 and other letters are kept */
	e2e_net_init(&ctl, "a\xc2\x80" "b\xc2\x85" "c\xc2\x9f" "d\xc2\xa0" "e\xc5\xbb", "t", NULL,
	             FALSE);
	g_assert_cmpstr(ctl.label, ==, "a_b_c_d\xc2\xa0" "e\xc5\xbb");
	e2e_net_clear(&ctl);
	/* every byte of invalid UTF-8 is a '_' */
	e2e_net_init(&ctl, "a\xff" "b\xc5", "t", NULL, FALSE);
	g_assert_cmpstr(ctl.label, ==, "a_b_");
	e2e_net_clear(&ctl);
	e2e_net_init(&ctl, "\xc5" "a\xc0\x80" "b\xed\xa0\x80" "c\xf4\x90\x80\x80", "t", NULL, FALSE);
	g_assert_cmpstr(ctl.label, ==, "_a__b___c____");
	e2e_net_clear(&ctl);
	e2e_net_init(&ctl, "\xe2\x82", "t", NULL, FALSE); /* a truncated sequence */
	g_assert_cmpstr(ctl.label, ==, "__");
	g_assert_true(g_utf8_validate(ctl.label, -1, NULL));

	e2e_net_init(&none, NULL, NULL, "utf8", FALSE);
	g_assert_null(none.label);
	g_assert_cmpint(none.casemap, ==, E2E_CASEMAP_UNICODE);
	/* no label, no scoped key: never a bare one in its place */
	g_assert_null(e2e_ctx_scoped(&none, "#test"));
	g_assert_false(e2e_ctx_on_net("IRCnet\x1f#a", &none));
	e2e_net_clear(&none);
	e2e_net_init(&none, "", "", NULL, FALSE);
	g_assert_null(none.label);

	e2e_net_clear(&net);
	g_assert_null(net.label);
	e2e_net_clear(&net); /* twice is fine */
	e2e_net_clear(&odd);
	e2e_net_clear(&bare);
	e2e_net_clear(&ctl);
	e2e_net_clear(&none);
}

static void test_json_roundtrip(void)
{
	E2E_JSON *root, *channels, *back;
	GPtrArray *keys;
	const char *error = NULL;
	char *text;

	root = e2e_json_new(E2E_JSON_OBJECT);
	channels = e2e_json_object_member(root, "channels");
	e2e_json_set(channels, "IRCnet\x1f#t", e2e_json_new(E2E_JSON_OBJECT));
	e2e_json_set_int(e2e_json_get(channels, "IRCnet\x1f#t"), "enabled", 1);

	text = e2e_json_encode(root, FALSE);
	g_assert_nonnull(strstr(text, "IRCnet\\u001f#t"));
	g_assert_null(strchr(text, '\x1f'));

	back = e2e_json_parse(text, strlen(text), &error);
	g_assert_nonnull(back);
	keys = e2e_json_keys(e2e_json_get(back, "channels"));
	g_assert_cmpuint(keys->len, ==, 1);
	g_assert_cmpmem(g_ptr_array_index(keys, 0), strlen(g_ptr_array_index(keys, 0)),
	                "IRCnet\x1f#t", 9);
	g_assert_cmpint(e2e_json_get_int(e2e_json_get(e2e_json_get(back, "channels"),
	                                              "IRCnet\x1f#t"),
	                                 "enabled", 0),
	                ==, 1);
	g_ptr_array_free(keys, TRUE);

	e2e_json_free(back);
	e2e_json_free(root);
	g_free(text);
}

/* ---- the keyring: legacy rows ---- */

static E2E_JSON *store_of(E2E_JSON *kr, const char *name)
{
	E2E_JSON *s = e2e_json_get(kr, name);

	if (s == NULL) {
		s = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set(kr, name, s);
	}
	return s;
}

static void put_row(E2E_JSON *kr, const char *store, const char *key)
{
	E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_int(row, "created_at", 1);
	e2e_json_set(store_of(kr, store), key, row);
}

static E2E_JSON *fixture(void)
{
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT);

	legacy_fixture_fill(kr);
	return kr;
}

static E2E_CTX_KIND lookup(const E2E_JSON *kr, const E2E_NET *net, const char *wire,
                           const char *want_ctx)
{
	char *ctx = (char *) "unset";
	E2E_CTX_KIND kind = e2e_ctx_lookup(kr, net, wire, &ctx);

	g_assert_cmpstr(ctx, ==, want_ctx);
	g_free(ctx);
	return kind;
}

/* the decision table of e2e_ctx_lookup */
static void test_ctx_lookup(void)
{
	E2E_NET one = { (char *) "IRCnet", E2E_CASEMAP_RFC1459, TRUE };
	E2E_NET many = { (char *) "IRCnet", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET other = { (char *) "Libera", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET none = { NULL, E2E_CASEMAP_RFC1459, TRUE };
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT);
	static const char *const how[] = { "channels", "outgoing", NULL };
	int i;

	/* nothing stored */
	g_assert_cmpint(lookup(kr, &one, "#a", "IRCnet\x1f#a"), ==, E2E_CTX_NEW);
	g_assert_cmpint(lookup(kr, &many, "#a", "IRCnet\x1f#a"), ==, E2E_CTX_NEW);
	/* no network: the context as it is (the behaviour before the scope) */
	g_assert_cmpint(lookup(kr, NULL, "#a", "#a"), ==, E2E_CTX_NEW);
	put_row(kr, "channels", "#a");
	g_assert_cmpint(lookup(kr, NULL, "#a", "#a"), ==, E2E_CTX_SCOPED);
	e2e_json_remove(e2e_json_get(kr, "channels"), "#a");

	/* each kind of row makes a context exist: a scoped one is SCOPED,
	   an old one LEGACY on the only network and BLOCKED on several */
	for (i = 0; how[i] != NULL; i++) {
		put_row(kr, how[i], "IRCnet\x1f#s");
		g_assert_cmpint(lookup(kr, &many, "#s", "IRCnet\x1f#s"), ==, E2E_CTX_SCOPED);
		g_assert_cmpint(lookup(kr, &other, "#s", "Libera\x1f#s"), ==, E2E_CTX_NEW);
		e2e_json_remove(e2e_json_get(kr, how[i]), "IRCnet\x1f#s");
		put_row(kr, how[i], "#l");
		g_assert_cmpint(lookup(kr, &one, "#l", "#l"), ==, E2E_CTX_LEGACY);
		g_assert_cmpint(lookup(kr, &many, "#l", "#l"), ==, E2E_CTX_BLOCKED);
		e2e_json_remove(e2e_json_get(kr, how[i]), "#l");
	}
	/* a session ("<handle>|<ctx>"), a request of ours ("<ctx>|<handle>",
	   or "<ctx>" without a handle) */
	put_row(kr, "incoming", "bob@b.example.net|#l");
	g_assert_cmpint(lookup(kr, &many, "#l", "#l"), ==, E2E_CTX_BLOCKED);
	g_assert_cmpint(lookup(kr, &many, "#", "IRCnet\x1f#"), ==, E2E_CTX_NEW);
	g_assert_cmpint(lookup(kr, &many, "l", "IRCnet\x1fl"), ==, E2E_CTX_NEW);
	e2e_json_remove(e2e_json_get(kr, "incoming"), "bob@b.example.net|#l");
	put_row(kr, "incoming", "bob@b.example.net|IRCnet\x1f#s");
	g_assert_cmpint(lookup(kr, &many, "#s", "IRCnet\x1f#s"), ==, E2E_CTX_SCOPED);
	put_row(kr, "pending", "IRCnet\x1f@me@host|bob@b.example.net");
	g_assert_cmpint(lookup(kr, &many, "@me@host", "IRCnet\x1f@me@host"), ==, E2E_CTX_SCOPED);
	put_row(kr, "pending", "#p");
	g_assert_cmpint(lookup(kr, &many, "#p", "#p"), ==, E2E_CTX_BLOCKED);
	/* only whole contexts: "#x|#a|b" is no row of "#a|b", "#a" no row of
	   "#a|b" */
	put_row(kr, "incoming", "bob@b.example.net|#x|#a|b");
	g_assert_cmpint(lookup(kr, &many, "#a|b", "IRCnet\x1f#a|b"), ==, E2E_CTX_NEW);
	put_row(kr, "pending", "#a|b|bob@b.example.net");
	g_assert_cmpint(lookup(kr, &many, "#a", "IRCnet\x1f#a"), ==, E2E_CTX_NEW);
	g_assert_cmpint(lookup(kr, &many, "#a|b", "#a|b"), ==, E2E_CTX_BLOCKED);
	/* a request without a handle for "#c|d": "#c" has no row there */
	{
		E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);

		e2e_json_set_string(row, "channel", "#c|d");
		e2e_json_set(store_of(kr, "pending"), "#c|d", row);
	}
	g_assert_cmpint(lookup(kr, &many, "#c", "IRCnet\x1f#c"), ==, E2E_CTX_NEW);
	g_assert_cmpint(lookup(kr, &many, "#c|d", "#c|d"), ==, E2E_CTX_BLOCKED);

	/* scoped wins over old rows */
	put_row(kr, "channels", "#w");
	put_row(kr, "channels", "IRCnet\x1f#w");
	g_assert_cmpint(lookup(kr, &many, "#w", "IRCnet\x1f#w"), ==, E2E_CTX_SCOPED);
	/* on the only network the old ones are taken over (the scoped win) */
	g_assert_cmpint(lookup(kr, &one, "#w", "#w"), ==, E2E_CTX_LEGACY);
	e2e_json_remove(e2e_json_get(kr, "channels"), "#w");
	g_assert_cmpint(lookup(kr, &one, "#w", "IRCnet\x1f#w"), ==, E2E_CTX_SCOPED);
	put_row(kr, "channels", "#w");
	/* another network's scoped row does not hide the old one */
	g_assert_cmpint(lookup(kr, &other, "#w", "#w"), ==, E2E_CTX_BLOCKED);

	/* a name with 0x1F can never be scoped, and an old row under it would
	   read as another network's: always BLOCKED, the bare name returned */
	g_assert_cmpint(lookup(kr, &one, "#a\x1f" "b", "#a\x1f" "b"), ==, E2E_CTX_BLOCKED);
	put_row(kr, "channels", "#a\x1f" "b");
	g_assert_cmpint(lookup(kr, &one, "#a\x1f" "b", "#a\x1f" "b"), ==, E2E_CTX_BLOCKED);
	g_assert_cmpint(lookup(kr, &many, "#a\x1f" "b", "#a\x1f" "b"), ==, E2E_CTX_BLOCKED);
	/* a network without a label stores nothing */
	g_assert_cmpint(lookup(kr, &none, "#a", "#a"), ==, E2E_CTX_BLOCKED);
	/* no keyring member of the right type: nothing exists */
	e2e_json_set_int(kr, "incoming", 3);
	g_assert_cmpint(lookup(kr, &many, "#zz", "IRCnet\x1f#zz"), ==, E2E_CTX_NEW);
	g_assert_cmpint(lookup(kr, &many, NULL, NULL), ==, E2E_CTX_BLOCKED);
	e2e_json_free(kr);
}

/* a name taken over on one network is BLOCKED on the others until they
   have a config of their own; fresh scoped rows bind nothing */
static void test_ctx_adopted_elsewhere(void)
{
	E2E_NET a = { (char *) "IRCnet", E2E_CASEMAP_RFC1459, TRUE };
	E2E_NET b = { (char *) "Libera", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET b1 = { (char *) "Libera", E2E_CASEMAP_RFC1459, TRUE };
	E2E_JSON *kr = fixture();
	char *ctx;

	put_row(kr, "channels", "IRCnet\x1f#fresh");
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#fresh"), ==, 0);
	g_assert_null(e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#fresh"),
	                                  "adopted"));
	g_assert_cmpint(lookup(kr, &b, "#fresh", "Libera\x1f#fresh"), ==, E2E_CTX_NEW);
	/* an old trust change alone is an old row of the name too */
	{
		E2E_JSON *tc = e2e_json_new(E2E_JSON_OBJECT);

		e2e_json_set_string(tc, "handle", "eve@127.0.0.1");
		e2e_json_set_string(tc, "channel", "#fresh");
		e2e_json_array_add(e2e_json_get(kr, "pending_trust_change"), tc);
	}
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#fresh"), ==, 1);
	g_assert_cmpstr(e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#fresh"),
	                                    "adopted"), ==, "IRCnet");
	g_assert_cmpint(lookup(kr, &b, "#fresh", "#fresh"), ==, E2E_CTX_BLOCKED);
	ctx = e2e_ctx_resolve(kr, &a, "#test", NULL);
	g_assert_cmpstr(ctx, ==, "IRCnet\x1f#test");
	g_free(ctx);
	g_assert_cmpstr(e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"),
	                                    "adopted"), ==, "IRCnet");
	g_assert_cmpint(lookup(kr, &a, "#test", "IRCnet\x1f#test"), ==, E2E_CTX_SCOPED);
	g_assert_cmpint(lookup(kr, &b, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	/* also when Libera is the only network now */
	g_assert_cmpint(lookup(kr, &b1, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	g_assert_null(e2e_ctx_resolve(kr, &b1, "#test", NULL));
	/* old rows of the name written again (rpe2e.pl): not Libera's either */
	put_row(kr, "channels", "#test");
	g_assert_cmpint(lookup(kr, &b1, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	g_assert_cmpint(lookup(kr, &a, "#test", "#test"), ==, E2E_CTX_LEGACY);
	/* unless Libera has rows of its own for it: then they are Libera's
	   business, taken over there like any old rows */
	put_row(kr, "outgoing", "Libera\x1f#test");
	g_assert_cmpint(lookup(kr, &b1, "#test", "#test"), ==, E2E_CTX_LEGACY);
	g_assert_cmpint(lookup(kr, &b, "#test", "Libera\x1f#test"), ==, E2E_CTX_SCOPED);
	e2e_json_remove(e2e_json_get(kr, "outgoing"), "Libera\x1f#test");
	e2e_json_remove(e2e_json_get(kr, "channels"), "#test");
	/* the marker on the key row alone counts as well */
	e2e_json_remove(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"), "adopted");
	g_assert_cmpint(lookup(kr, &b, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	e2e_json_remove(e2e_json_get(e2e_json_get(kr, "outgoing"), "IRCnet\x1f#test"), "adopted");
	g_assert_cmpint(lookup(kr, &b, "#test", "Libera\x1f#test"), ==, E2E_CTX_NEW);
	/* a config of Libera's own settles it there */
	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"), "adopted", "IRCnet");
	put_row(kr, "channels", "Libera\x1f#test");
	g_assert_cmpint(lookup(kr, &b, "#test", "Libera\x1f#test"), ==, E2E_CTX_SCOPED);
	/* e2e_ctx_find: the storage context without taking anything over */
	e2e_json_free(kr);
	kr = fixture();
	ctx = e2e_ctx_find(kr, &a, "#test");
	g_assert_cmpstr(ctx, ==, "#test");
	g_free(ctx);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	g_assert_null(e2e_ctx_find(kr, &b, "#test"));
	ctx = e2e_ctx_find(kr, &b, "#new");
	g_assert_cmpstr(ctx, ==, "Libera\x1f#new");
	g_free(ctx);
	/* old rows next to this network's own: its own are used */
	put_row(kr, "channels", "IRCnet\x1f#test");
	g_assert_cmpint(lookup(kr, &a, "#test", "#test"), ==, E2E_CTX_LEGACY);
	ctx = e2e_ctx_find(kr, &a, "#test");
	g_assert_cmpstr(ctx, ==, "IRCnet\x1f#test");
	g_free(ctx);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	e2e_json_free(kr);
}

/* item 3: the rows taken along with a peer are its own: a handle that
   starts like the peer's ("bob@h|@z") or a row naming another handle */
static void test_adopt_peer_sessions_own_only(void)
{
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT), *row;

	put_row(kr, "incoming", "bob@h|@me@x");
	put_row(kr, "incoming", "bob@h|@z|@me@x");
	put_row(kr, "incoming", "bob@h|@other@x");
	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|@other@x"), "handle", "bob@h|@other");
	put_row(kr, "incoming", "bob@h|@named@x");
	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|@named@x"), "handle", "bob@h");
	row = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(row, "handle", "bob@h");
	e2e_json_set(store_of(kr, "pending"), "@me@x|bob@h", row);
	g_assert_cmpuint(e2e_adopt_legacy_peer_sessions(kr, "IRCnet", "bob@h"), ==, 3);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|IRCnet\x1f@me@x"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|IRCnet\x1f@named@x"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "pending"), "IRCnet\x1f@me@x|bob@h"));
	/* not bob's */
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|@z|@me@x"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|@other@x"));
	/* the other handle's own rows go with it */
	g_assert_cmpuint(e2e_adopt_legacy_peer_sessions(kr, "IRCnet", "bob@h|@z"), ==, 1);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@h|@z|IRCnet\x1f@me@x"));
	e2e_json_free(kr);
}

static void test_ctx_resolve(void)
{
	E2E_NET one = { (char *) "IRCnet", E2E_CASEMAP_RFC1459, TRUE };
	E2E_NET many = { (char *) "IRCnet", E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = fixture();
	gboolean changed = TRUE;
	char *ctx, *before, *after;

	before = e2e_json_encode(kr, FALSE);
	/* several networks: an old context is not used, nothing changes */
	g_assert_null(e2e_ctx_resolve(kr, &many, "#test", &changed));
	g_assert_false(changed);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	/* a new one is the scoped name */
	ctx = e2e_ctx_resolve(kr, &many, "#new", &changed);
	g_assert_cmpstr(ctx, ==, "IRCnet\x1f#new");
	g_assert_false(changed);
	g_free(ctx);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	/* no network: unchanged */
	ctx = e2e_ctx_resolve(kr, NULL, "#test", &changed);
	g_assert_cmpstr(ctx, ==, "#test");
	g_assert_false(changed);
	g_free(ctx);
	g_assert_null(e2e_ctx_resolve(kr, &one, "#a\x1f" "b", &changed));
	g_assert_false(changed);
	/* the only network: taken over */
	ctx = e2e_ctx_resolve(kr, &one, "#test", &changed);
	g_assert_cmpstr(ctx, ==, "IRCnet\x1f#test");
	g_assert_true(changed);
	g_free(ctx);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"));
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	/* the second time it is scoped */
	changed = TRUE;
	ctx = e2e_ctx_resolve(kr, &many, "#test", &changed);
	g_assert_cmpstr(ctx, ==, "IRCnet\x1f#test");
	g_assert_false(changed);
	g_free(ctx);
	g_free(before);
	e2e_json_free(kr);
}

static const char *row_str(E2E_JSON *kr, const char *store, const char *key, const char *field)
{
	return e2e_json_get_string(e2e_json_get(e2e_json_get(kr, store), key), field);
}

static void test_adopt_legacy(void)
{
	E2E_JSON *kr = fixture(), *list, *seen;
	char *other_before, *other_after;

	/* the "#other" rows, to compare */
	other_before = g_strdup_printf("%s %s %s",
	                               row_str(kr, "outgoing", "#other", "sk"),
	                               row_str(kr, "incoming", "bob@127.0.0.1|#other", "sk"),
	                               row_str(kr, "outgoing_recipients", "#other|bob@127.0.0.1", "channel"));
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#test"), ==, LEGACY_ROWS_TEST);
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"));
	g_assert_cmpstr(row_str(kr, "outgoing", "IRCnet\x1f#test", "sk"), ==, LEGACY_OUT_SK);
	g_assert_cmpstr(row_str(kr, "incoming", "bob@127.0.0.1|IRCnet\x1f#test", "sk"), ==, LEGACY_BOB_SK);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "accepted"), "bob@127.0.0.1|IRCnet\x1f#test"));
	g_assert_cmpstr(row_str(kr, "pending", "IRCnet\x1f#test|carol@127.0.0.1", "channel"), ==, "IRCnet\x1f#test");
	g_assert_cmpstr(row_str(kr, "outgoing_recipients", "IRCnet\x1f#test|bob@127.0.0.1", "channel"), ==, "IRCnet\x1f#test");
	g_assert_cmpstr(row_str(kr, "pending_inbound", "dave@127.0.0.1|IRCnet\x1f#test", "channel"), ==, "IRCnet\x1f#test");
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "seen_rekeys"), LEGACY_BOB_FP "|IRCnet\x1f#test"));
	list = e2e_json_get(kr, "pending_trust_change");
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, 0), "channel"), ==, "IRCnet\x1f#test");
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, 1), "channel"), ==, "#other");
	/* the others are untouched */
	other_after = g_strdup_printf("%s %s %s",
	                              row_str(kr, "outgoing", "#other", "sk"),
	                              row_str(kr, "incoming", "bob@127.0.0.1|#other", "sk"),
	                              row_str(kr, "outgoing_recipients", "#other|bob@127.0.0.1", "channel"));
	g_assert_cmpstr(other_before, ==, other_after);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#test2"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|#test2"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|#te"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "pending"), "#te|st|carol@127.0.0.1"));
	/* nothing left to move */
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#test"), ==, 0);
	/* a row with '|' in its context: only its own rows */
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#te"), ==, 1);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "pending"), "#te|st|carol@127.0.0.1"));
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#te|st"), ==, 1);
	g_assert_cmpstr(row_str(kr, "pending", "IRCnet\x1f#te|st|carol@127.0.0.1", "channel"), ==, "IRCnet\x1f#te|st");
	/* a handle holding '|': its row's "handle" member tells where the
	   context starts or ends */
	{
		E2E_JSON *row = e2e_json_new(E2E_JSON_OBJECT);

		e2e_json_set_string(row, "handle", "x|y@h");
		e2e_json_set_string(row, "channel", "#p");
		e2e_json_set(e2e_json_get(kr, "pending_inbound"), "x|y@h|#p", row);
		/* (no "channel" member: the handle alone tells) */
		row = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set_string(row, "handle", "x|y@h");
		e2e_json_set(e2e_json_get(kr, "pending"), "#p|x|y@h", row);
	}
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "y@h|#p"), ==, 0);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#p|x"), ==, 0);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#p"), ==, 2);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "pending_inbound"), "x|y@h|IRCnet\x1f#p"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "pending"), "IRCnet\x1f#p|x|y@h"));
	g_free(other_before);
	g_free(other_after);
	e2e_json_free(kr);

	/* a scoped row that exists wins, the old one is dropped; used REKEY
	   nonces are kept from both */
	kr = fixture();
	put_row(kr, "channels", "IRCnet\x1f#test");
	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"), "mode", "quiet");
	seen = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_int(seen, "bmV3bm9uY2VuZXdub25jZQ==", 2);
	e2e_json_set(e2e_json_get(kr, "seen_rekeys"), LEGACY_BOB_FP "|IRCnet\x1f#test", seen);
	list = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(list, "handle", "eve@127.0.0.1");
	e2e_json_set_string(list, "channel", "IRCnet\x1f#test");
	e2e_json_set_string(list, "change", "revoked");
	e2e_json_array_add(e2e_json_get(kr, "pending_trust_change"), list);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#test"), ==, LEGACY_ROWS_TEST - 3);
	list = e2e_json_get(kr, "pending_trust_change");
	g_assert_cmpuint(list->array->len, ==, 2);
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, 0), "channel"), ==, "#other");
	g_assert_cmpstr(e2e_json_get_string(g_ptr_array_index(list->array, 1), "change"), ==, "revoked");
	g_assert_cmpstr(row_str(kr, "channels", "IRCnet\x1f#test", "mode"), ==, "quiet");
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	seen = e2e_json_get(e2e_json_get(kr, "seen_rekeys"), LEGACY_BOB_FP "|IRCnet\x1f#test");
	g_assert_cmpuint(e2e_json_size(seen), ==, 2);
	g_assert_null(e2e_json_get(e2e_json_get(kr, "seen_rekeys"), LEGACY_BOB_FP "|#test"));
	e2e_json_free(kr);

	/* refused: a label or wire that cannot make a scoped name, a keyring
	   with no stores */
	kr = fixture();
	g_assert_cmpuint(e2e_adopt_legacy(kr, NULL, "#test"), ==, 0);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", NULL), ==, 0);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRC\x1fnet", "#test"), ==, 0);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#te\x1fst"), ==, 0);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	e2e_json_free(kr);
	kr = e2e_json_new(E2E_JSON_OBJECT);
	g_assert_cmpuint(e2e_adopt_legacy(kr, "IRCnet", "#test"), ==, 0);
	e2e_json_free(kr);
}

/* every row of every store (and trust change) whose key or "channel"
   names a context of label: what another network must never change */
static char *rows_of(const E2E_JSON *kr, const char *label)
{
	char *mark = g_strdup_printf("%s\x1f", label);
	GString *out = g_string_new(NULL);
	GPtrArray *stores = e2e_json_keys(kr);
	guint i, j;

	for (i = 0; i < stores->len; i++) {
		const char *name = g_ptr_array_index(stores, i);
		E2E_JSON *store = e2e_json_get(kr, name);

		if (store->type == E2E_JSON_OBJECT) {
			GPtrArray *keys = e2e_json_keys(store);

			for (j = 0; j < keys->len; j++) {
				const char *k = g_ptr_array_index(keys, j);
				char *text;

				if (strstr(k, mark) == NULL)
					continue;
				text = e2e_json_encode(e2e_json_get(store, k), FALSE);
				g_string_append_printf(out, "%s/%s=%s\n", name, k, text);
				g_free(text);
			}
			g_ptr_array_unref(keys);
		} else if (store->type == E2E_JSON_ARRAY) {
			for (j = 0; j < store->array->len; j++) {
				E2E_JSON *row = g_ptr_array_index(store->array, j);
				const char *c = e2e_json_get_string(row, "channel");
				char *text;

				if (c == NULL || strstr(c, mark) == NULL)
					continue;
				text = e2e_json_encode(row, FALSE);
				g_string_append_printf(out, "%s[]=%s\n", name, text);
				g_free(text);
			}
		}
	}
	g_ptr_array_unref(stores);
	g_free(mark);
	return g_string_free(out, FALSE);
}

static E2E_ADOPT_RESULT adopt(E2E_JSON *kr, const E2E_NET *net, const char *wire,
                              const char *nick, guint want_moved)
{
	guint moved = 12345;
	E2E_ADOPT_RESULT r = e2e_adopt_explicit(kr, net, wire, nick, &moved);

	g_assert_cmpuint(moved, ==, want_moved);
	return r;
}

/* /e2e adopt: the user's decision, on any number of networks; one
   network's rows never become another's */
static void test_legacy_adopt_explicit(void)
{
	E2E_NET a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET a1 = { (char *) "E2E", E2E_CASEMAP_RFC1459, TRUE };
	E2E_NET b = { (char *) "E2E2", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET c = { (char *) "E2E3", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET none = { NULL, E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = fixture(), *cfg;
	char *before, *after, *rows_a, *rows_b;

	/* the brief's lines: the 9 rows of the fixture */
	g_assert_cmpuint(e2e_adopt_legacy(kr, "E2E", "#test"), ==, 9);
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "E2E\x1f#test"));
	e2e_json_free(kr);

	/* several networks, old rows: BLOCKED until /e2e adopt */
	kr = fixture();
	g_assert_cmpint(lookup(kr, &a, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	g_assert_cmpint(adopt(kr, &a, "#test", NULL, LEGACY_ROWS_TEST), ==, E2E_ADOPT_MOVED);
	g_assert_cmpint(lookup(kr, &a, "#test", "E2E\x1f#test"), ==, E2E_CTX_SCOPED);
	g_assert_cmpstr(row_str(kr, "channels", "E2E\x1f#test", "adopted"), ==, "E2E");
	g_assert_cmpstr(row_str(kr, "outgoing", "E2E\x1f#test", "sk"), ==, LEGACY_OUT_SK);
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));
	/* only #test: the others stay old */
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#other"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|#te"));
	/* again: the config is there, nothing changes */
	before = e2e_json_encode(kr, FALSE);
	g_assert_cmpint(adopt(kr, &a, "#test", NULL, 0), ==, E2E_ADOPT_ALREADY);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	g_free(before);

	/* the other network: BLOCKED by E2E's marker; /e2e adopt there writes
	   E2E2's own config and touches none of E2E's rows */
	g_assert_cmpint(lookup(kr, &b, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	rows_a = rows_of(kr, "E2E");
	/* (a nick is only a query's) */
	g_assert_cmpint(adopt(kr, &b, "#test", "bob", 0), ==, E2E_ADOPT_OWN);
	after = rows_of(kr, "E2E");
	g_assert_cmpstr(rows_a, ==, after);
	g_free(after);
	cfg = e2e_json_get(e2e_json_get(kr, "channels"), "E2E2\x1f#test");
	g_assert_nonnull(cfg);
	g_assert_cmpint(e2e_json_get_int(cfg, "enabled", 0), ==, 1);
	g_assert_cmpstr(e2e_json_get_string(cfg, "mode"), ==, "normal");
	/* nothing was taken over on E2E2: no marker, no key, no session */
	g_assert_null(e2e_json_get(cfg, "adopted"));
	g_assert_null(e2e_json_get(cfg, "nick"));
	g_assert_null(e2e_json_get(e2e_json_get(kr, "outgoing"), "E2E2\x1f#test"));
	g_assert_null(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|E2E2\x1f#test"));
	g_assert_cmpint(lookup(kr, &b, "#test", "E2E2\x1f#test"), ==, E2E_CTX_SCOPED);
	g_assert_cmpint(lookup(kr, &a, "#test", "E2E\x1f#test"), ==, E2E_CTX_SCOPED);
	/* for E2E2 only: a third network is still refused */
	g_assert_cmpint(lookup(kr, &c, "#test", "#test"), ==, E2E_CTX_BLOCKED);
	before = e2e_json_encode(kr, FALSE);
	g_assert_cmpint(adopt(kr, &b, "#test", NULL, 0), ==, E2E_ADOPT_ALREADY);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	g_free(before);

	/* old rows written again later (rpe2e.pl): E2E3's /e2e adopt takes
	   them, E2E's and E2E2's rows stay as they are */
	put_row(kr, "channels", "#test");
	put_row(kr, "incoming", "eve@127.0.0.1|#test");
	g_free(rows_a);
	rows_a = rows_of(kr, "E2E");
	rows_b = rows_of(kr, "E2E2");
	g_assert_cmpint(adopt(kr, &c, "#test", NULL, 2), ==, E2E_ADOPT_MOVED);
	after = rows_of(kr, "E2E");
	g_assert_cmpstr(rows_a, ==, after);
	g_free(after);
	after = rows_of(kr, "E2E2");
	g_assert_cmpstr(rows_b, ==, after);
	g_free(after);
	g_free(rows_a);
	g_free(rows_b);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "eve@127.0.0.1|E2E3\x1f#test"));
	g_assert_cmpstr(row_str(kr, "channels", "E2E3\x1f#test", "adopted"), ==, "E2E3");
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#test"));

	/* nothing old, nothing elsewhere: nothing to do (a DM session of a
	   handle "nothing" is not a row of the channel #nothing) */
	put_row(kr, "incoming", "nothing|@me@127.0.0.1");
	before = e2e_json_encode(kr, FALSE);
	g_assert_cmpint(adopt(kr, &a, "#nothing", NULL, 0), ==, E2E_ADOPT_NONE);
	/* a name that cannot be scoped, a network without a label */
	g_assert_cmpint(adopt(kr, &a, "#te\x1fst", NULL, 0), ==, E2E_ADOPT_UNSCOPABLE);
	g_assert_cmpint(adopt(kr, &none, "#other", NULL, 0), ==, E2E_ADOPT_UNSCOPABLE);
	g_assert_cmpint(adopt(kr, &a, NULL, NULL, 0), ==, E2E_ADOPT_UNSCOPABLE);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	g_free(before);
	/* the only network: the same */
	g_assert_cmpint(adopt(kr, &a1, "#other", NULL, 7), ==, E2E_ADOPT_MOVED);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "E2E\x1f#other"));
	/* old rows whose scoped twins exist already: those win, the old ones
	   go (and that is saved) */
	put_row(kr, "incoming", "eve@127.0.0.1|#twin");
	put_row(kr, "incoming", "eve@127.0.0.1|E2E\x1f#twin");
	e2e_json_set_int(e2e_json_get(e2e_json_get(kr, "incoming"), "eve@127.0.0.1|E2E\x1f#twin"), "mine", 1);
	g_assert_cmpint(adopt(kr, &a, "#twin", NULL, 0), ==, E2E_ADOPT_MOVED);
	g_assert_null(e2e_json_get(e2e_json_get(kr, "incoming"), "eve@127.0.0.1|#twin"));
	g_assert_cmpint(e2e_json_get_int(e2e_json_get(e2e_json_get(kr, "incoming"), "eve@127.0.0.1|E2E\x1f#twin"),
	                                 "mine", 0), ==, 1);
	e2e_json_free(kr);
}

/* /e2e adopt in a query: "@<peer>" and the peer's own rows under every old
   "@<own>" - its sessions, used nonces and our requests to it - as the
   user's own DM line does; other peers' DM rows stay old */
static void test_legacy_adopt_query(void)
{
	E2E_NET a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET b = { (char *) "E2E2", E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT), *row, *cfg;
	char *rows_a, *after;

	put_row(kr, "channels", "@bob@127.0.0.1");
	put_row(kr, "outgoing", "@bob@127.0.0.1");
	put_row(kr, "incoming", "bob@127.0.0.1|@me@127.0.0.1");
	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|@me@127.0.0.1"),
	                    "fp", LEGACY_BOB_FP);
	put_row(kr, "seen_rekeys", LEGACY_BOB_FP "|@me@127.0.0.1");
	row = e2e_json_new(E2E_JSON_OBJECT);
	e2e_json_set_string(row, "handle", "bob@127.0.0.1");
	e2e_json_set_string(row, "channel", "@me@127.0.0.1");
	e2e_json_set(store_of(kr, "pending"), "@me@127.0.0.1|bob@127.0.0.1", row);
	put_row(kr, "incoming", "carol@127.0.0.1|@me@127.0.0.1");
	put_row(kr, "channels", "@carol@127.0.0.1");

	g_assert_cmpint(adopt(kr, &a, "@bob@127.0.0.1", "bob", 5), ==, E2E_ADOPT_MOVED);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "E2E\x1f@bob@127.0.0.1"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "outgoing"), "E2E\x1f@bob@127.0.0.1"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|E2E\x1f@me@127.0.0.1"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "seen_rekeys"), LEGACY_BOB_FP "|E2E\x1f@me@127.0.0.1"));
	g_assert_cmpstr(row_str(kr, "pending", "E2E\x1f@me@127.0.0.1|bob@127.0.0.1", "channel"), ==,
	                "E2E\x1f@me@127.0.0.1");
	g_assert_null(e2e_json_get(e2e_json_get(kr, "incoming"), "bob@127.0.0.1|@me@127.0.0.1"));
	/* carol's are not bob's */
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "carol@127.0.0.1|@me@127.0.0.1"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "@carol@127.0.0.1"));
	g_assert_cmpint(lookup(kr, &a, "@bob@127.0.0.1", "E2E\x1f@bob@127.0.0.1"), ==, E2E_CTX_SCOPED);
	/* bob on the other network: blocked by the marker, then a config of
	   its own with the query's nick, E2E's rows untouched */
	g_assert_cmpint(lookup(kr, &b, "@bob@127.0.0.1", "@bob@127.0.0.1"), ==, E2E_CTX_BLOCKED);
	rows_a = rows_of(kr, "E2E");
	g_assert_cmpint(adopt(kr, &b, "@bob@127.0.0.1", "bob", 0), ==, E2E_ADOPT_OWN);
	after = rows_of(kr, "E2E");
	g_assert_cmpstr(rows_a, ==, after);
	g_free(after);
	g_free(rows_a);
	cfg = e2e_json_get(e2e_json_get(kr, "channels"), "E2E2\x1f@bob@127.0.0.1");
	g_assert_cmpstr(e2e_json_get_string(cfg, "nick"), ==, "bob");
	g_assert_cmpint(e2e_json_get_int(cfg, "enabled", 0), ==, 1);
	g_assert_cmpint(lookup(kr, &b, "@bob@127.0.0.1", "E2E2\x1f@bob@127.0.0.1"), ==, E2E_CTX_SCOPED);
	/* only the peer's sessions old, no config: they are taken along */
	put_row(kr, "incoming", "dave@127.0.0.1|@me@127.0.0.1");
	g_assert_cmpint(adopt(kr, &a, "@dave@127.0.0.1", "dave", 1), ==, E2E_ADOPT_MOVED);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "incoming"), "dave@127.0.0.1|E2E\x1f@me@127.0.0.1"));
	/* ... also when their scoped twins exist: those win, the old go */
	put_row(kr, "incoming", "erin@127.0.0.1|@me@127.0.0.1");
	put_row(kr, "incoming", "erin@127.0.0.1|E2E\x1f@me@127.0.0.1");
	g_assert_cmpint(adopt(kr, &a, "@erin@127.0.0.1", "erin", 0), ==, E2E_ADOPT_MOVED);
	g_assert_null(e2e_json_get(e2e_json_get(kr, "incoming"), "erin@127.0.0.1|@me@127.0.0.1"));
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "E2E\x1f@erin@127.0.0.1"));
	e2e_json_free(kr);
}

static void test_legacy_has_scoped(void)
{
	static const char *const stores[] = { "channels", "outgoing", "incoming", NULL };
	E2E_JSON *rpe2e_pl_fixture = fixture(), *after_adopt = fixture(), *kr;
	int i;

	g_assert_cmpuint(e2e_adopt_legacy(after_adopt, "E2E", "#test"), ==, LEGACY_ROWS_TEST);
	g_assert_false(e2e_keyring_has_scoped(rpe2e_pl_fixture));
	g_assert_true(e2e_keyring_has_scoped(after_adopt));
	g_assert_false(e2e_keyring_has_scoped(NULL));
	/* each of the three stores alone */
	for (i = 0; stores[i] != NULL; i++) {
		kr = fixture();
		put_row(kr, stores[i], stores[i][0] == 'i' ? "bob@h|E2E\x1f#x" : "E2E\x1f#x");
		g_assert_true(e2e_keyring_has_scoped(kr));
		e2e_json_free(kr);
	}
	/* a 0x1F in a value is no scoped entry */
	kr = fixture();
	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "channels"), "#test"), "note", "E2E\x1f#x");
	g_assert_false(e2e_keyring_has_scoped(kr));
	e2e_json_free(kr);
	e2e_json_free(rpe2e_pl_fixture);
	e2e_json_free(after_adopt);
}

static void add_rule(E2E_JSON *kr, const char *scope, const char *pattern)
{
	E2E_JSON *rule = e2e_json_new(E2E_JSON_OBJECT), *rules = e2e_json_get(kr, "autotrust");

	if (rules == NULL) {
		rules = e2e_json_new(E2E_JSON_ARRAY);
		e2e_json_set(kr, "autotrust", rules);
	}
	e2e_json_set_string(rule, "scope", scope);
	e2e_json_set_string(rule, "handle_pattern", pattern);
	e2e_json_array_add(rules, rule);
}

/* an old rule (a bare channel) applies to that name on every network; a
   rule of one network ("<label>\x1f#chan") only there */
static void test_legacy_autotrust_old_rule(void)
{
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT);

	add_rule(kr, "#test", "*@127.0.0.1");
	g_assert_true(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E\x1f#test"));
	g_assert_true(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E2\x1f#test"));
	g_assert_true(e2e_autotrust_matches(kr, "bob@127.0.0.1", "#test"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@example.org", "E2E\x1f#test"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E\x1f#test2"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E\x1f#tes"));
	e2e_json_free(kr);

	kr = e2e_json_new(E2E_JSON_OBJECT);
	add_rule(kr, "E2E\x1f#x", "*@127.0.0.1");
	add_rule(kr, "E2E\x1f@bob@127.0.0.1", "bob@127.0.0.1");
	g_assert_true(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E\x1f#x"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E2\x1f#x"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "#x"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E"));
	/* a rule of a network never matches through what follows a context's
	   first separator */
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "X\x1f" "E2E\x1f#x"));
	g_assert_true(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E\x1f@bob@127.0.0.1"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@127.0.0.1", "E2E2\x1f@bob@127.0.0.1"));
	/* global: every context of every network */
	add_rule(kr, "global", "*.example.org");
	g_assert_true(e2e_autotrust_matches(kr, "eve@a.example.org", "E2E2\x1f#y"));
	g_assert_true(e2e_autotrust_matches(kr, "eve@a.example.org", "#y"));
	e2e_json_free(kr);
}

/* ---- rpe2e.pl loaded: what the module still stops ---- */

static char *guard_resolve(const char *nick, const E2E_JSON *kr, void *data)
{
	return g_ascii_strcasecmp(nick, "bob") == 0 ? g_strdup("bob@127.0.0.1") : NULL;
}

/* e2e_script_guard on buf (len bytes), checked: the keyring never
   changes, the target and a part of the message are as wanted */
static E2E_GATE_ACTION guard_ex(const E2E_JSON *kr, gboolean kr_ok, gboolean lost, gboolean nested,
                                const E2E_NET *net, const char *buf, gsize len,
                                const char *want_target, const char *want_in_msg)
{
	char *message = (char *) "unset", *target = (char *) "unset", *before, *after;
	E2E_GATE_ACTION a;

	before = e2e_json_encode(kr, FALSE);
	a = e2e_script_guard(kr, kr_ok, lost, nested, net, buf, len, guard_resolve, NULL, &message,
	                     &target);
	after = e2e_json_encode(kr, FALSE);
	/* never changes the keyring */
	g_assert_cmpstr(before, ==, after);
	g_free(before);
	g_free(after);
	g_assert_cmpstr(target, ==, want_target);
	if (want_in_msg == NULL) {
		g_assert_null(message);
	} else {
		g_assert_nonnull(message);
		if (strstr(message, want_in_msg) == NULL)
			g_test_message("%.*s: \"%s\" not in \"%s\"", (int) len, buf, want_in_msg, message);
		g_assert_nonnull(strstr(message, want_in_msg));
	}
	if (message != NULL)
		g_assert_null(strchr(message, '\x1f'));
	g_free(message);
	g_free(target);
	return a;
}

/* a line the user sent (not nested), the keyring readable */
static E2E_GATE_ACTION guard(const E2E_JSON *kr, gboolean kr_ok, const E2E_NET *net,
                             const char *buf, const char *want_target, const char *want_in_msg)
{
	return guard_ex(kr, kr_ok, FALSE, FALSE, net, buf, strlen(buf), want_target, want_in_msg);
}

static void put_cfg(E2E_JSON *kr, const char *ctx, int enabled, const char *nick)
{
	E2E_JSON *cfg = e2e_json_new(E2E_JSON_OBJECT);

	e2e_json_set_int(cfg, "enabled", enabled);
	e2e_json_set_string(cfg, "mode", "normal");
	if (nick != NULL)
		e2e_json_set_string(cfg, "nick", nick);
	e2e_json_set(store_of(kr, "channels"), ctx, cfg);
}

static void test_legacy_script_guard(void)
{
	E2E_NET a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET a1 = { (char *) "E2E", E2E_CASEMAP_RFC1459, TRUE };
	E2E_NET b = { (char *) "E2E2", E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = fixture();
	unsigned char key[32];
	const char *error = NULL;
	GPtrArray *wires;
	char *line;
	guint moved;

	put_cfg(kr, "E2E\x1f#only", 1, NULL);
	put_cfg(kr, "E2E\x1f#off", 0, NULL);
	put_cfg(kr, "E2E\x1f@bob@127.0.0.1", 1, "bob");
	put_cfg(kr, "E2E\x1f@carol@127.0.0.1", 1, "carol");
	/* E2E only in the module's entries: refused, named <network>:<name> */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #only :secret words", "#only",
	                      "E2E is on for E2E:#only in the e2e module"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "@time=x PRIVMSG #only :secret\r\n", "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #ONLY :secret", "#ONLY", "the same channel as #ONLY"), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #only :\001ACTION waves\001", "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	/* a query, also while the address is unknown */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG bob :secret", "bob", "E2E:@bob@127.0.0.1"), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG carol :secret", "carol", "rpe2e.pl does not know"),
	                ==, E2E_GATE_REFUSE);
	/* several lines, several targets */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #free :a\r\nPRIVMSG #only :b", "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	/* (a server may end a line at CR alone) */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #free :a\rPRIVMSG #only :b", "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #free,#only :b", "#free,#only", "rpe2e.pl"), ==,
	                E2E_GATE_REFUSE);
	/* passes: off, not set up, another network, no PRIVMSG, CTCP */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #off :hi", NULL, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #free :hi", NULL, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &b, "PRIVMSG #only :hi", NULL, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a, "NOTICE #only :hi", NULL, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #only :\001VERSION\001", NULL, NULL), ==,
	                E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a, "", NULL, NULL), ==, E2E_GATE_PASS);
	/* the old rows are the script's: on, off or of unknown network (two
	   networks), they never refuse here */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #test :hi", NULL, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a1, "PRIVMSG #other :hi", NULL, NULL), ==, E2E_GATE_PASS);
	/* the script's own chunks pass (sent from within its gate: nested) */
	memset(key, 0x42, sizeof(key));
	wires = e2e_encrypt_plain(key, "#only", "from the script", 1791564000, &error);
	g_assert_nonnull(wires);
	line = g_strconcat("PRIVMSG #only :", (char *) g_ptr_array_index(wires, 0), NULL);
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, TRUE, &a, line, strlen(line), NULL, NULL), ==,
	                E2E_GATE_PASS);
	g_free(line);
	/* ... but a wire does not cover a clear line next to it */
	line = g_strconcat("PRIVMSG #only :", (char *) g_ptr_array_index(wires, 0),
	                   "\nPRIVMSG #only :clear", NULL);
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, TRUE, &a, line, strlen(line), "#only", "E2E:#only"), ==,
	                E2E_GATE_REFUSE);
	g_free(line);
	g_ptr_array_unref(wires);
	/* "+RPE2E01 " that is no wire is text */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #only :+RPE2E01 not a wire", "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	/* a bot command goes out, with the warning */
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #only :!seen x", "#only", "CLEARTEXT"), ==,
	                E2E_GATE_BYPASS);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #free :!seen x", NULL, NULL), ==, E2E_GATE_PASS);
	/* a keyring that cannot be read: nothing but bot commands and CTCPs */
	g_assert_cmpint(guard(kr, FALSE, &a, "PRIVMSG #free :hi", "#free", "keyring"), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, FALSE, &a, "PRIVMSG #free :!seen x", "#free", "CLEARTEXT"), ==,
	                E2E_GATE_BYPASS);
	/* a name another network took over: refused there too */
	g_assert_cmpint(e2e_adopt_explicit(kr, &a, "#test", NULL, &moved), ==, E2E_ADOPT_MOVED);
	g_assert_cmpint(guard(kr, TRUE, &b, "PRIVMSG #test :hi", "#test", "rpe2e.pl does not know"), ==,
	                E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #test :hi", "#test", "E2E:#test"), ==,
	                E2E_GATE_REFUSE);
	e2e_json_free(kr);
}

/* a wire passes only as the script's own chunk, sent from within its
   gate (a nested emission), and only as a line of its own */
static void test_legacy_script_guard_nested(void)
{
	E2E_NET a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = e2e_keyring_new();
	unsigned char key[32];
	const char *error = NULL;
	GPtrArray *wires;
	GString *buf;
	char *line;

	put_cfg(kr, "E2E\x1f#only", 1, NULL);
	memset(key, 0x42, sizeof(key));
	wires = e2e_encrypt_plain(key, "#only", "pasted", 1791564000, &error);
	g_assert_nonnull(wires);
	line = g_strconcat("PRIVMSG #only :", (char *) g_ptr_array_index(wires, 0), NULL);
	/* typed or pasted by the user: text like any other */
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, FALSE, &a, line, strlen(line), "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	/* the script's chunk */
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, TRUE, &a, line, strlen(line), NULL, NULL), ==,
	                E2E_GATE_PASS);
	/* (with its line end) */
	buf = g_string_new(line);
	g_string_append(buf, "\r\n");
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, TRUE, &a, buf->str, buf->len, NULL, NULL), ==,
	                E2E_GATE_PASS);
	g_string_free(buf, TRUE);
	/* a clear piece after a NUL rides along with no wire */
	buf = g_string_new(line);
	g_string_append_c(buf, '\0');
	g_string_append(buf, "secret");
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, TRUE, &a, buf->str, buf->len, "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	g_string_free(buf, TRUE);
	/* two chunks in one buffer: not as the script sends them */
	buf = g_string_new(line);
	g_string_append_printf(buf, "\r\n%s", line);
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, TRUE, &a, buf->str, buf->len, "#only", "E2E:#only"),
	                ==, E2E_GATE_REFUSE);
	g_string_free(buf, TRUE);
	/* a wire to a channel E2E is not on for is nothing to stop */
	g_free(line);
	line = g_strconcat("PRIVMSG #free :", (char *) g_ptr_array_index(wires, 0), NULL);
	g_assert_cmpint(guard_ex(kr, TRUE, FALSE, FALSE, &a, line, strlen(line), NULL, NULL), ==,
	                E2E_GATE_PASS);
	g_free(line);
	g_ptr_array_unref(wires);
	e2e_json_free(kr);
}

/* rpe2e.pl decides a buffer of several lines by its first one: each
   PRIVMSG in it is decided on the whole keyring, old rows too */
static void test_legacy_script_guard_multi(void)
{
	E2E_NET a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
	E2E_NET a1 = { (char *) "E2E", E2E_CASEMAP_RFC1459, TRUE };
	E2E_JSON *kr = e2e_keyring_new();

	put_cfg(kr, "#legacy", 1, NULL);
	put_cfg(kr, "#legacyoff", 0, NULL);
	put_cfg(kr, "E2E\x1f#only", 1, NULL);
	/* one line: the script's own (bare) entry is its business */
	g_assert_cmpint(guard(kr, TRUE, &a1, "PRIVMSG #legacy :secret", NULL, NULL), ==, E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #legacy :secret", NULL, NULL), ==, E2E_GATE_PASS);
	/* behind another line it is not looked at by the script */
	g_assert_cmpint(guard(kr, TRUE, &a1, "PRIVMSG #free :a\r\nPRIVMSG #legacy :secret", "#legacy",
	                      "rpe2e.pl"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a, "PRIVMSG #free :a\r\nPRIVMSG #legacy :secret", "#legacy",
	                      "rpe2e.pl"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a1, "NOTICE #free :a\nPRIVMSG #legacy :secret", "#legacy",
	                      "rpe2e.pl"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard(kr, TRUE, &a1, "PRIVMSG #free :a\rPRIVMSG #only :secret", "#only",
	                      "E2E:#only"), ==, E2E_GATE_REFUSE);
	/* nothing on in the lines: they go */
	g_assert_cmpint(guard(kr, TRUE, &a1, "PRIVMSG #free :a\r\nPRIVMSG #legacyoff :b", NULL, NULL), ==,
	                E2E_GATE_PASS);
	g_assert_cmpint(guard(kr, TRUE, &a1, "PRIVMSG #free :a\r\nNOTICE #legacy :b", NULL, NULL), ==,
	                E2E_GATE_PASS);
	e2e_json_free(kr);
}

/* a lost keyring (a corrupt one moved aside): the refusal says /e2e reset */
static void test_legacy_script_guard_lost(void)
{
	E2E_NET a = { (char *) "E2E", E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = e2e_keyring_new();
	static const char hi[] = "PRIVMSG #free :hi", bot[] = "PRIVMSG #free :!seen x";

	g_assert_cmpint(guard_ex(kr, FALSE, TRUE, FALSE, &a, hi, strlen(hi), "#free", E2E_REFUSE_LOST),
	                ==, E2E_GATE_REFUSE);
	g_assert_cmpint(guard_ex(kr, FALSE, TRUE, FALSE, &a, bot, strlen(bot), "#free", "CLEARTEXT"),
	                ==, E2E_GATE_BYPASS);
	/* unreadable, not lost: no /e2e reset (it would start a new keyring) */
	g_assert_cmpint(guard_ex(kr, FALSE, FALSE, FALSE, &a, hi, strlen(hi), "#free", "keyring read failed"),
	                ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);
}

/* ---- regression scenarios of networks kept apart ---- */

static E2E_NET rv_a = { (char *) "A", E2E_CASEMAP_RFC1459, FALSE };
static E2E_NET rv_b = { (char *) "B", E2E_CASEMAP_RFC1459, FALSE };
static E2E_NET rv_c = { (char *) "C", E2E_CASEMAP_RFC1459, FALSE };

/* the gate's action on a copy (the gate may adopt or make a key) */
static E2E_GATE_ACTION rv_gate(E2E_JSON *kr, const E2E_NET *net, const char *target)
{
	E2E_JSON *copy = e2e_json_copy(kr);
	E2E_GATE_RESULT g;
	E2E_GATE_ACTION a;

	e2e_gate_decide(copy, TRUE, NULL, net, target, "secret words", guard_resolve, NULL, &g);
	a = g.action;
	e2e_gate_result_clear(&g);
	e2e_json_free(copy);
	return a;
}

static gboolean not_clear(E2E_GATE_ACTION a)
{
	return a == E2E_GATE_REFUSE || a == E2E_GATE_CIPHER;
}

/* old rows on several networks: refused everywhere until adopted; then
   only the adopting network encrypts */
static void test_regress_refused_until_adopt(void)
{
	E2E_NET b1 = { (char *) "B", E2E_CASEMAP_RFC1459, TRUE };
	E2E_JSON *kr = fixture();
	guint moved;

	g_assert_cmpint(rv_gate(kr, &rv_a, "#test"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(rv_gate(kr, &rv_b, "#test"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_a, "#test", NULL, &moved), ==, E2E_ADOPT_MOVED);
	g_assert_cmpuint(moved, ==, LEGACY_ROWS_TEST);
	g_assert_true(not_clear(rv_gate(kr, &rv_a, "#test")));
	g_assert_cmpint(rv_gate(kr, &rv_b, "#test"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(rv_gate(kr, &rv_c, "#test"), ==, E2E_GATE_REFUSE);
	g_assert_cmpint(rv_gate(kr, &rv_a, "#other"), ==, E2E_GATE_REFUSE);
	/* B as the only network: still blocked by A's marker */
	g_assert_cmpint(rv_gate(kr, &b1, "#test"), ==, E2E_GATE_REFUSE);
	e2e_json_free(kr);
}

/* adopt on B (OWN) and on C (MOVED, new old rows) never touch A's rows
   or marker; networks not adopted stay refused */
static void test_regress_adopt_b_keeps_a(void)
{
	E2E_NET d = { (char *) "D", E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = fixture();
	char *a0, *a1, *b0;
	guint moved;

	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_a, "#test", NULL, &moved), ==, E2E_ADOPT_MOVED);
	a0 = rows_of(kr, "A");
	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_b, "#test", NULL, &moved), ==, E2E_ADOPT_OWN);
	a1 = rows_of(kr, "A");
	g_assert_cmpstr(a0, ==, a1);
	g_free(a1);
	/* B: a config only - no key, no session, no marker */
	b0 = rows_of(kr, "B");
	g_assert_cmpstr(b0, ==, "channels/B\x1f#test={\"enabled\":1,\"mode\":\"normal\"}\n");
	g_assert_true(not_clear(rv_gate(kr, &rv_b, "#test")));
	g_assert_cmpint(rv_gate(kr, &rv_c, "#test"), ==, E2E_GATE_REFUSE);
	put_cfg(kr, "#test", 1, NULL);
	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_c, "#test", NULL, &moved), ==, E2E_ADOPT_MOVED);
	a1 = rows_of(kr, "A");
	g_assert_cmpstr(a0, ==, a1);
	g_free(a1);
	a1 = rows_of(kr, "B");
	g_assert_cmpstr(b0, ==, a1);
	g_free(a1);
	g_assert_cmpint(rv_gate(kr, &d, "#test"), ==, E2E_GATE_REFUSE);
	g_free(a0);
	g_free(b0);
	e2e_json_free(kr);
}

/* ALREADY, NONE and UNSCOPABLE change nothing at all */
static void test_regress_no_change_outcomes(void)
{
	E2E_NET nolabel = { NULL, E2E_CASEMAP_RFC1459, FALSE };
	E2E_JSON *kr = fixture();
	char *before, *after;
	guint moved;

	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_a, "#test", NULL, &moved), ==, E2E_ADOPT_MOVED);
	before = e2e_json_encode(kr, FALSE);
	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_a, "#test", NULL, &moved), ==, E2E_ADOPT_ALREADY);
	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_a, "#nothing", NULL, &moved), ==, E2E_ADOPT_NONE);
	g_assert_cmpint(e2e_adopt_explicit(kr, &rv_a, "#a\x1f" "b", NULL, &moved), ==,
	                E2E_ADOPT_UNSCOPABLE);
	g_assert_cmpint(e2e_adopt_explicit(kr, &nolabel, "#other", NULL, &moved), ==,
	                E2E_ADOPT_UNSCOPABLE);
	g_assert_cmpint(e2e_adopt_explicit(kr, NULL, "#other", NULL, &moved), ==,
	                E2E_ADOPT_UNSCOPABLE);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_free(before);
	g_free(after);
	e2e_json_free(kr);
}

/* a rule of A never auto-trusts on B; an old bare rule holds for that
   exact name on every network; a NULL ctx never matches */
static void test_regress_autotrust_scope(void)
{
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT);

	add_rule(kr, "A\x1f#sec", "*@*");
	add_rule(kr, "#old", "bob@*");
	g_assert_true(e2e_autotrust_matches(kr, "eve@x", "A\x1f#sec"));
	g_assert_false(e2e_autotrust_matches(kr, "eve@x", "B\x1f#sec"));
	g_assert_false(e2e_autotrust_matches(kr, "eve@x", "#sec"));
	g_assert_false(e2e_autotrust_matches(kr, "eve@x", "#sec\x1f" "A"));
	g_assert_false(e2e_autotrust_matches(kr, "eve@x", NULL));
	g_assert_true(e2e_autotrust_matches(kr, "bob@h", "Z\x1f#old"));
	g_assert_false(e2e_autotrust_matches(kr, "eve@h", "Z\x1f#old"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@h", "Z\x1f#OLD"));
	g_assert_false(e2e_autotrust_matches(kr, "bob@h", "Z\x1f#old2"));
	e2e_json_free(kr);
}

/* why the module still gates with rpe2e.pl loaded: a channel only the
   module set up has no row rpe2e.pl reads, while the module itself never lets it out in clear;
   the dry decisions leave the keyring alone */
static void test_regress_script_premise(void)
{
	static const char line[] = "PRIVMSG #only :secret words";
	E2E_JSON *kr = e2e_keyring_new();
	char *before, *after, *msg;

	put_cfg(kr, "A\x1f#only", 1, NULL);
	g_assert_true(e2e_keyring_has_scoped(kr));
	g_assert_null(e2e_json_get(e2e_json_get(kr, "channels"), "#only"));
	g_assert_true(not_clear(rv_gate(kr, &rv_a, "#only")));
	before = e2e_json_encode(kr, FALSE);
	msg = e2e_gate_multiline(kr, TRUE, &rv_a, line, strlen(line), guard_resolve, NULL);
	g_assert_nonnull(msg);
	g_free(msg);
	after = e2e_json_encode(kr, FALSE);
	g_assert_cmpstr(before, ==, after);
	g_assert_null(e2e_gate_multiline(kr, TRUE, &rv_a, "PRIVMSG #free :hi", 17, guard_resolve, NULL));
	g_assert_null(e2e_gate_multiline(kr, TRUE, &rv_b, line, strlen(line), guard_resolve, NULL));
	g_free(before);
	g_free(after);
	e2e_json_free(kr);
}

/* has_scoped looks at incoming keys too */
static void test_regress_has_scoped_incoming(void)
{
	E2E_JSON *kr = e2e_keyring_new();

	g_assert_false(e2e_keyring_has_scoped(kr));
	put_row(kr, "incoming", "bob@h|A\x1f#x");
	g_assert_true(e2e_keyring_has_scoped(kr));
	e2e_json_free(kr);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/e2e/scope/casemap-parse", test_casemap_parse);
	g_test_add_func("/e2e/scope/casefold", test_casefold);
	g_test_add_func("/e2e/scope/casefold-ignorables", test_casefold_ignorables);
	g_test_add_func("/e2e/scope/casefold-ex", test_casefold_ex);
	g_test_add_func("/e2e/scope/casefold-narrow", test_casefold_narrow);
	g_test_add_func("/e2e/scope/casefold-random", test_casefold_random);
	g_test_add_func("/e2e/scope/casefold-ascii", test_casefold_ascii);
	g_test_add_func("/e2e/scope/casefold-all-code-points", test_casefold_all_code_points);
	g_test_add_func("/e2e/scope/contexts", test_contexts);
	g_test_add_func("/e2e/scope/json-roundtrip", test_json_roundtrip);
	g_test_add_func("/e2e/scope/ctx-lookup", test_ctx_lookup);
	g_test_add_func("/e2e/scope/ctx-resolve", test_ctx_resolve);
	g_test_add_func("/e2e/scope/adopt-peer-sessions-own-only", test_adopt_peer_sessions_own_only);
	g_test_add_func("/e2e/scope/ctx-adopted-elsewhere", test_ctx_adopted_elsewhere);
	g_test_add_func("/e2e/scope/adopt-legacy", test_adopt_legacy);
	g_test_add_func("/e2e/legacy/adopt-explicit", test_legacy_adopt_explicit);
	g_test_add_func("/e2e/legacy/adopt-query", test_legacy_adopt_query);
	g_test_add_func("/e2e/legacy/has-scoped", test_legacy_has_scoped);
	g_test_add_func("/e2e/legacy/autotrust-old-rule", test_legacy_autotrust_old_rule);
	g_test_add_func("/e2e/legacy/script-guard", test_legacy_script_guard);
	g_test_add_func("/e2e/legacy/script-guard-nested", test_legacy_script_guard_nested);
	g_test_add_func("/e2e/legacy/script-guard-multi", test_legacy_script_guard_multi);
	g_test_add_func("/e2e/legacy/script-guard-lost", test_legacy_script_guard_lost);
	g_test_add_func("/e2e/regress/refused-until-adopt", test_regress_refused_until_adopt);
	g_test_add_func("/e2e/regress/adopt-b-keeps-a", test_regress_adopt_b_keeps_a);
	g_test_add_func("/e2e/regress/no-change-outcomes", test_regress_no_change_outcomes);
	g_test_add_func("/e2e/regress/autotrust-scope", test_regress_autotrust_scope);
	g_test_add_func("/e2e/regress/script-premise", test_regress_script_premise);
	g_test_add_func("/e2e/regress/has-scoped-incoming", test_regress_has_scoped_incoming);

	return g_test_run();
}
