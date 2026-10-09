#include <irssi/src/common.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-ansi/term.h>
#include <irssi/src/fe-ansi/sidepanels-text.h>

/* A side panel line is themed once, by format_get_text_theme*(): the
 * format's colour codes are expanded into \004 sequences and the arguments
 * (prefix, nick, window name) are copied verbatim. format_string_expand()
 * expands codes exactly like that, so expanded() below builds the same
 * string, and sidepanel_text_walk() must draw it without a second pass. */

typedef struct {
	GString *text;
	GArray *fg24; /* 24-bit foreground of every character, or UINT_MAX */
} WALK_REC;

static gboolean collect(unichar chr, int width, int color, unsigned int fg24, unsigned int bg24,
                        void *data)
{
	WALK_REC *rec = data;
	unsigned int fg = (color & ATTR_FGCOLOR24) ? fg24 : UINT_MAX;

	g_string_append_unichar(rec->text, chr);
	g_array_append_val(rec->fg24, fg);
	return TRUE;
}

static void walk(const char *text, WALK_REC *rec)
{
	rec->text = g_string_new(NULL);
	rec->fg24 = g_array_new(FALSE, FALSE, sizeof(unsigned int));
	sidepanel_text_walk(text, collect, rec);
}

static void walk_free(WALK_REC *rec)
{
	g_string_free(rec->text, TRUE);
	g_array_free(rec->fg24, TRUE);
}

/* format part + verbatim argument + format part, like format_get_text_args() */
static char *expanded(const char *format1, const char *arg, const char *format2)
{
	char *a, *b, *ret;

	a = format_string_expand(format1, NULL);
	b = format_string_expand(format2, NULL);
	ret = g_strconcat(a, arg, b, NULL);
	g_free(a);
	g_free(b);
	return ret;
}

#define FG(rec, i) g_array_index((rec)->fg24, unsigned int, (i))

/* #0550AE is stored as \004 FORMAT_COLOR_24 '%' 'P' 0xAE '0': expanded a
 * second time, "%P" became a colour code and the trailing '0' was printed
 * in front of every voiced nick */
static void test_channel_byte_05(void)
{
	WALK_REC rec;
	char *text;

	text = expanded(" %Z0969DA%_", "+", "%_%N%Z0550AEnick%N");
	walk(text, &rec);
	g_assert_cmpstr(rec.text->str, ==, " +nick");
	g_assert_cmpuint(FG(&rec, 1), ==, 0x0969DA);
	g_assert_cmpuint(FG(&rec, 2), ==, 0x0550AE);
	g_assert_cmpuint(FG(&rec, 5), ==, 0x0550AE);
	walk_free(&rec);
	g_free(text);
}

/* every value of every channel survives: bytes below 0x21 are stored
 * shifted (0x05 -> '%', 0x04 -> '$'), 0x25 itself is a literal '%' */
static void test_all_channel_bytes(void)
{
	static const int shifts[] = { 16, 8, 0 };
	unsigned int v, color;
	int i;

	for (i = 0; i < 3; i++) {
		for (v = 0; v < 256; v++) {
			WALK_REC rec;
			char *fmt, *text;

			color = 0x808080 & ~(0xffu << shifts[i]);
			color |= v << shifts[i];
			fmt = g_strdup_printf("%%Z%06X", color);
			text = expanded(fmt, "x", "%Ny");
			walk(text, &rec);
			g_assert_cmpstr(rec.text->str, ==, "xy");
			g_assert_cmpuint(FG(&rec, 0), ==, color);
			g_assert_cmpuint(FG(&rec, 1), ==, UINT_MAX);
			walk_free(&rec);
			g_free(text);
			g_free(fmt);
		}
	}
}

/* arguments are text, not formats: the halfop prefix and a channel name
 * with '%' are drawn as they are */
static void test_percent_in_arguments(void)
{
	WALK_REC rec;
	char *text;

	text = expanded("%G", "%", "%N%gnick%N");
	walk(text, &rec);
	g_assert_cmpstr(rec.text->str, ==, "%nick");
	walk_free(&rec);
	g_free(text);

	text = expanded("%Z9A6700", "#100%Zcafe%_x", "%N");
	walk(text, &rec);
	g_assert_cmpstr(rec.text->str, ==, "#100%Zcafe%_x");
	g_assert_cmpuint(FG(&rec, 0), ==, 0x9A6700);
	g_assert_cmpuint(FG(&rec, 12), ==, 0x9A6700);
	walk_free(&rec);
	g_free(text);
}

static gboolean stop_after_two(unichar chr, int width, int color, unsigned int fg24,
                               unsigned int bg24, void *data)
{
	int *count = data;

	return ++*count < 2;
}

static void test_stop(void)
{
	char *text;
	int count = 0;

	text = format_string_expand("%Gabcdef", NULL);
	sidepanel_text_walk(text, stop_after_two, &count);
	g_assert_cmpint(count, ==, 2);
	g_free(text);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/test/sidepanel_text/channel_byte_05", test_channel_byte_05);
	g_test_add_func("/test/sidepanel_text/all_channel_bytes", test_all_channel_bytes);
	g_test_add_func("/test/sidepanel_text/percent_in_arguments", test_percent_in_arguments);
	g_test_add_func("/test/sidepanel_text/stop", test_stop);

#if GLIB_CHECK_VERSION(2,38,0)
	g_test_set_nonfatal_assertions();
#endif
	return g_test_run();
}
