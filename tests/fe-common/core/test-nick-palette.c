/*
 * Tests for the nick_hash_colors palette parser (fe-expandos.c) and the
 * legacy theme section mapping (themes.c).
 */
#include <irssi/src/common.h>
#include <irssi/src/fe-common/core/fe-expandos.h>
#include <irssi/src/fe-common/core/themes.h>

static void test_entry_normalize(void)
{
	char *code;

	code = nick_palette_entry_normalize("g");
	g_assert_cmpstr(code, ==, "g");
	g_free(code);

	code = nick_palette_entry_normalize("%C");
	g_assert_cmpstr(code, ==, "C");
	g_free(code);

	code = nick_palette_entry_normalize("#f59e0b");
	g_assert_cmpstr(code, ==, "Zf59e0b");
	g_free(code);

	code = nick_palette_entry_normalize("%ZFCD34D");
	g_assert_cmpstr(code, ==, "ZFCD34D");
	g_free(code);

	g_assert_null(nick_palette_entry_normalize(""));
	g_assert_null(nick_palette_entry_normalize("x"));
	g_assert_null(nick_palette_entry_normalize("#f59e0"));
	g_assert_null(nick_palette_entry_normalize("#g59e0b"));
	g_assert_null(nick_palette_entry_normalize("blue"));
	g_assert_null(nick_palette_entry_normalize(NULL));
}

/* Every index below count must be a valid colour - the old parser kept
 * invalid tokens in the array, so a hash could pick junk. */
static void test_palette_only_valid_entries(void)
{
	gchar **palette;
	int count;

	palette = parse_color_palette("g  bogus #f59e0b  Zzzzzzz C", &count);
	g_assert_cmpint(count, ==, 3);
	g_assert_cmpstr(palette[0], ==, "g");
	g_assert_cmpstr(palette[1], ==, "Zf59e0b");
	g_assert_cmpstr(palette[2], ==, "C");
	g_assert_null(palette[3]);
	g_strfreev(palette);
}

static void test_palette_default_fallback(void)
{
	gchar **palette;
	int count;

	palette = parse_color_palette("nothing valid here", &count);
	g_assert_cmpint(count, ==, 8);
	g_assert_cmpstr(palette[0], ==, "g");
	g_assert_cmpstr(palette[7], ==, "C");
	g_strfreev(palette);

	palette = parse_color_palette(NULL, &count);
	g_assert_cmpint(count, ==, 8);
	g_strfreev(palette);
}

static void test_legacy_theme_section(void)
{
	g_assert_cmpstr(theme_legacy_format_section("fe-ansi"), ==, "fe-text");
	g_assert_null(theme_legacy_format_section("fe-common/core"));
	g_assert_null(theme_legacy_format_section(NULL));
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/nick_palette/entry_normalize", test_entry_normalize);
	g_test_add_func("/nick_palette/only_valid_entries", test_palette_only_valid_entries);
	g_test_add_func("/nick_palette/default_fallback", test_palette_default_fallback);
	g_test_add_func("/themes/legacy_section", test_legacy_theme_section);
	return g_test_run();
}
