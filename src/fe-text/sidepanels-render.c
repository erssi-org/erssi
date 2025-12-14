/*
 * sidepanels-render.c : erssi
 *
 * Copyright (C) 2024-2025 erssi-org team
 * Lead Developer: Jerzy (kofany) Dąbrowski <https://github.com/kofany>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "module.h"
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/fe-text/mainwindows.h>
#include <irssi/src/fe-text/sidepanels.h>
#include <irssi/src/fe-text/sidepanels-render.h>
#include <irssi/src/fe-text/sidepanels-activity.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-text/module-formats.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-text/textbuffer-view.h>
#include <irssi/src/core/special-vars.h>
#include <stdarg.h>
#include <stdlib.h>

/* SP_MAINWIN_CTX is now defined in sidepanels-types.h */

/* Color attribute masks from textbuffer-view.c */
#define FGATTR (ATTR_NOCOLORS | ATTR_RESETFG | FG_MASK | ATTR_FGCOLOR24)
#define BGATTR (ATTR_NOCOLORS | ATTR_RESETBG | BG_MASK | ATTR_BGCOLOR24)

/* Redraw batching system to prevent excessive redraws during mass events */
gboolean redraw_pending = FALSE;
int redraw_timer_tag = -1;
int redraw_batch_timeout = 5; /* ms - WeeChat uses 1ms, we use 5ms for safety */
gboolean batch_mode_active = FALSE;

/* External functions we need */
extern void sp_logf(const char *fmt, ...);
extern SP_MAINWIN_CTX *get_ctx(MAIN_WINDOW_REC *mw, gboolean create);
extern void position_tw(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx);
extern void draw_main_window_borders(MAIN_WINDOW_REC *mw);

/* Forward declarations - ci_nick_compare is declared in activity header */

/*
 * ============================================================================
 * DIFFERENTIAL RENDERING CACHE MANAGEMENT
 * ============================================================================
 *
 * These functions manage the line cache for differential rendering.
 * Instead of clearing and redrawing entire panels, we compare new state
 * with cached state and only redraw lines that actually changed.
 */

/* Allocate and initialize a panel cache */
SP_PANEL_CACHE *sp_cache_create(void)
{
	SP_PANEL_CACHE *cache = g_new0(SP_PANEL_CACHE, 1);
	cache->initialized = TRUE;
	return cache;
}

/* Free a single cache line's allocated memory */
static void sp_cache_line_free(SP_LINE_CACHE *line)
{
	if (!line)
		return;
	g_free(line->text);
	g_free(line->prefix);
	line->text = NULL;
	line->prefix = NULL;
	line->valid = FALSE;
}

/* Clear all lines in a panel cache */
void sp_cache_clear(SP_PANEL_CACHE *cache)
{
	int i;
	if (!cache)
		return;
	for (i = 0; i < cache->count; i++) {
		sp_cache_line_free(&cache->lines[i]);
	}
	cache->count = 0;
}

/* Free a panel cache completely */
void sp_cache_free(SP_PANEL_CACHE *cache)
{
	if (!cache)
		return;
	sp_cache_clear(cache);
	g_free(cache);
}

/* Check if a cache line matches new content */
static gboolean sp_cache_line_matches(SP_LINE_CACHE *cached, const char *text,
                                       const char *prefix, int format, int refnum)
{
	if (!cached || !cached->valid)
		return FALSE;

	if (cached->format != format)
		return FALSE;

	if (cached->refnum != refnum)
		return FALSE;

	if (g_strcmp0(cached->text, text) != 0)
		return FALSE;

	if (g_strcmp0(cached->prefix, prefix) != 0)
		return FALSE;

	return TRUE;
}

/* Update a cache line with new content */
static void sp_cache_line_update(SP_LINE_CACHE *line, const char *text,
                                  const char *prefix, int format, int refnum)
{
	g_free(line->text);
	g_free(line->prefix);

	line->text = g_strdup(text);
	line->prefix = prefix ? g_strdup(prefix) : NULL;
	line->format = format;
	line->refnum = refnum;
	line->valid = TRUE;
}

/* Clear a single line on screen (fill with spaces + clrtoeol) */
static void sp_clear_line(TERM_WINDOW *tw, int y, int width)
{
	if (!tw)
		return;
	term_set_color(tw, ATTR_RESET);
	term_move(tw, 0, y);
	term_clrtoeol(tw);
}

/* Invalidate cache when panel dimensions change */
static gboolean sp_cache_needs_full_redraw(SP_PANEL_CACHE *cache, int height,
                                            int width, int scroll_offset)
{
	if (!cache || !cache->initialized)
		return TRUE;

	/* Full redraw needed if dimensions changed */
	if (cache->panel_height != height || cache->panel_width != width)
		return TRUE;

	/* Full redraw needed if scroll position changed */
	if (cache->scroll_offset != scroll_offset)
		return TRUE;

	return FALSE;
}

/* UTF-8 character reading function based on textbuffer-view.c */
static inline unichar read_unichar(const unsigned char *data, const unsigned char **next,
                                   int *width)
{
	unichar chr = g_utf8_get_char_validated((const char *) data, -1);
	if (chr & 0x80000000) {
		chr = 0xfffd; /* replacement character for invalid UTF-8 */
		*next = data + 1;
		*width = 1;
	} else {
		/* Use string_advance for proper grapheme cluster handling */
		char const *str_ptr = (char const *)data;
		*width = string_advance(&str_ptr, TREAT_STRING_AS_UTF8);
		*next = (unsigned char *)str_ptr;
	}
	return chr;
}

void clear_window_full(TERM_WINDOW *tw, int width, int height)
{
	int y;
	int x;
	if (!tw)
		return;
	term_set_color(tw, ATTR_RESET);
	for (y = 0; y < height; y++) {
		term_move(tw, 0, y);
		for (x = 0; x < width; x++)
			term_addch(tw, ' ');
	}
}

void draw_border_vertical(TERM_WINDOW *tw, int width, int height, int right_border)
{
	int y;
	int x = right_border ? width - 1 : 0;
	if (!tw)
		return;
	for (y = 0; y < height; y++) {
		term_move(tw, x, y);
		term_addch(tw, '|');
	}
}

/* 24-bit color handling function from textbuffer-view.c */
static void unformat_24bit_line_color(const unsigned char **ptr, int off, int *flags, unsigned int *fg, unsigned int *bg)
{
	unsigned int color;
	unsigned char rgbx[4];
	unsigned int i;
	for (i = 0; i < 4; ++i) {
		if ((*ptr)[i + off] == '\0')
			return;
		rgbx[i] = (*ptr)[i + off];
	}
	rgbx[3] -= 0x20;
	*ptr += 4;
	for (i = 0; i < 3; ++i) {
		if (rgbx[3] & (0x10 << i))
			rgbx[i] -= 0x20;
	}
	color = rgbx[0] << 16 | rgbx[1] << 8 | rgbx[2];
	if (rgbx[3] & 0x1) {
		*flags = (*flags & FGATTR) | ATTR_BGCOLOR24;
		*bg = color;
	}
	else {
		*flags = (*flags & BGATTR) | ATTR_FGCOLOR24;
		*fg = color;
	}
}

/* Format processing function for color codes - exact copy from textbuffer-view.c */
static inline void unformat(const unsigned char **ptr, int *color, unsigned int *fg24,
                            unsigned int *bg24)
{
	switch (**ptr) {
	case FORMAT_STYLE_BLINK:
		*color ^= ATTR_BLINK;
		break;
	case FORMAT_STYLE_UNDERLINE:
		*color ^= ATTR_UNDERLINE;
		break;
	case FORMAT_STYLE_BOLD:
		*color ^= ATTR_BOLD;
		break;
	case FORMAT_STYLE_REVERSE:
		*color ^= ATTR_REVERSE;
		break;
	case FORMAT_STYLE_ITALIC:
		*color ^= ATTR_ITALIC;
		break;
	case FORMAT_STYLE_MONOSPACE:
		/* *color ^= ATTR_MONOSPACE; */
		break;
	case FORMAT_STYLE_DEFAULTS:
		*color = ATTR_RESET;
		break;
	case FORMAT_STYLE_CLRTOEOL:
		break;
#define SET_COLOR_EXT_FG_BITS(base, pc)                                                            \
	*color &= ~ATTR_FGCOLOR24;                                                                 \
	*color = (*color & BGATTR) | (base + *pc - FORMAT_COLOR_NOCHANGE)
#define SET_COLOR_EXT_BG_BITS(base, pc)                                                            \
	*color &= ~ATTR_BGCOLOR24;                                                                 \
	*color = (*color & FGATTR) | ((base + *pc - FORMAT_COLOR_NOCHANGE) << BG_SHIFT)
	case FORMAT_COLOR_EXT1:
		SET_COLOR_EXT_FG_BITS(0x10, ++*ptr);
		break;
	case FORMAT_COLOR_EXT1_BG:
		SET_COLOR_EXT_BG_BITS(0x10, ++*ptr);
		break;
	case FORMAT_COLOR_EXT2:
		SET_COLOR_EXT_FG_BITS(0x60, ++*ptr);
		break;
	case FORMAT_COLOR_EXT2_BG:
		SET_COLOR_EXT_BG_BITS(0x60, ++*ptr);
		break;
	case FORMAT_COLOR_EXT3:
		SET_COLOR_EXT_FG_BITS(0xb0, ++*ptr);
		break;
	case FORMAT_COLOR_EXT3_BG:
		SET_COLOR_EXT_BG_BITS(0xb0, ++*ptr);
		break;
#undef SET_COLOR_EXT_BG_BITS
#undef SET_COLOR_EXT_FG_BITS
	case FORMAT_COLOR_24:
		unformat_24bit_line_color(ptr, 1, color, fg24, bg24);
		break;
	default:
		if (**ptr != FORMAT_COLOR_NOCHANGE) {
			if (**ptr == (unsigned char) 0xff) {
				*color = (*color & BGATTR) | ATTR_RESETFG;
			} else {
				*color = (*color & BGATTR) | (((unsigned char) **ptr - '0') & 0xf);
			}
		}
		if ((*ptr)[1] == '\0')
			break;

		(*ptr)++;
		if (**ptr != FORMAT_COLOR_NOCHANGE) {
			if (**ptr == (unsigned char) 0xff) {
				*color = (*color & FGATTR) | ATTR_RESETBG;
			} else {
				*color = (*color & FGATTR) |
				         ((((unsigned char) **ptr - '0') & 0xf) << BG_SHIFT);
			}
		}
	}
	if (**ptr == '\0')
		return;

	(*ptr)++;
}

void draw_str_themed(TERM_WINDOW *tw, int x, int y, WINDOW_REC *wctx, int format_id,
                            const char *text)
{
	TEXT_DEST_REC dest;
	THEME_REC *theme;
	char *out, *expanded;
	const unsigned char *ptr;
	const unsigned char *next_ptr;
	int color;
	int char_width;
	unsigned int fg24, bg24;
	unichar chr;

	format_create_dest(&dest, NULL, NULL, 0, wctx);
	theme = window_get_theme(wctx);
	out = format_get_text_theme(theme, MODULE_NAME, &dest, format_id, text);

	if (out != NULL && *out != '\0') {
		/* Convert theme color codes and render with proper color handling */
		expanded = format_string_expand(out, NULL);

		/* Initialize color state */
		color = ATTR_RESET;
		fg24 = bg24 = UINT_MAX;
		ptr = (const unsigned char *) expanded;

		term_move(tw, x, y);
		term_set_color(tw, ATTR_RESET);

		/* Process each character with color codes (like textbuffer-view.c) */
		while (*ptr != '\0') {
			if (*ptr == 4) {
				/* Format code - process color change */
				ptr++;
				if (*ptr == '\0')
					break;
				unformat(&ptr, &color, &fg24, &bg24);
				term_set_color2(tw, color, fg24, bg24);
				continue;
			}

			/* Regular character - read UTF-8 properly */
			chr = read_unichar(ptr, &next_ptr, &char_width);

			if (unichar_isprint(chr)) {
				term_add_unichar(tw, chr);
			}
			ptr = next_ptr;
		}

		g_free(expanded);
	} else {
		/* Fallback: display plain text if theme formatting fails */
		term_move(tw, x, y);
		term_addstr(tw, text ? text : "");
	}
	g_free(out);
}

/*
 * NEW DUAL-PARAMETER THEME FORMATS FOR NICKLIST CUSTOMIZATION
 *
 * The new *_status formats allow separate styling of status symbols and nicks:
 *
 * Example theme customization:
 *
 * "fe-text" = {
 *   # Different colors for @ symbol vs nick:
 *   sidepanel_nick_op_status = "%R$0%Y$1";        # Red @ + Yellow nick
 *
 *   # Decorative brackets around status:
 *   sidepanel_nick_op_status = "%Y[$0]%N$1";      # [@]nick
 *
 *   # Hide status completely (just colorized nick):
 *   sidepanel_nick_op_status = "%Y$1";            # Only yellow nick, no @
 *
 *   # Custom symbols instead of @ and +:
 *   sidepanel_nick_op_status = "%R⚡%N%Y$1";       # ⚡nick instead of @nick
 *   sidepanel_nick_voice_status = "%C◆%N%c$1";    # ◆nick instead of +nick
 * };
 */
void draw_str_themed_2params(TERM_WINDOW *tw, int x, int y, WINDOW_REC *wctx, int format_id,
                                    const char *param1, const char *param2)
{
	TEXT_DEST_REC dest;
	THEME_REC *theme;
	char *out, *expanded;
	const unsigned char *ptr;
	const unsigned char *next_ptr;
	int color;
	int char_width;
	unsigned int fg24, bg24;
	unichar chr;
	char *args[3];

	format_create_dest(&dest, NULL, NULL, 0, wctx);
	theme = window_get_theme(wctx);

	/* Create args array for format_get_text_theme_charargs */
	args[0] = (char *) param1;
	args[1] = (char *) param2;
	args[2] = NULL;

	out = format_get_text_theme_charargs(theme, MODULE_NAME, &dest, format_id, args);

	if (out != NULL && *out != '\0') {
		/* Convert theme color codes and render with proper color handling */
		expanded = format_string_expand(out, NULL);

		/* Initialize color state */
		color = ATTR_RESET;
		fg24 = bg24 = UINT_MAX;
		ptr = (const unsigned char *) expanded;

		term_move(tw, x, y);
		term_set_color(tw, ATTR_RESET);

		/* Process each character with color codes (like textbuffer-view.c) */
		while (*ptr != '\0') {
			if (*ptr == 4) {
				/* Format code - process color change */
				ptr++;
				if (*ptr == '\0')
					break;
				unformat(&ptr, &color, &fg24, &bg24);
				term_set_color2(tw, color, fg24, bg24);
				continue;
			}

			/* Regular character - read UTF-8 properly */
			chr = read_unichar(ptr, &next_ptr, &char_width);

			if (unichar_isprint(chr)) {
				term_add_unichar(tw, chr);
			}
			ptr = next_ptr;
		}

		g_free(expanded);
	} else {
		/* Fallback: display plain text if theme formatting fails */
		term_move(tw, x, y);
		term_addstr(tw, param1 ? param1 : "");
		term_addstr(tw, param2 ? param2 : "");
	}
	g_free(out);
}

char *truncate_nick_for_sidepanel(const char *nick, int max_width)
{
	char *result;
	const char *p;
	int truncated_len;
	int width;

	if (!nick)
		return g_strdup("");

	if (max_width <= 0)
		return g_strdup("+");

	/* Calculate display width using UTF-8 aware function */
	width = string_width(nick, -1);

	if (width <= max_width) {
		/* Nick fits completely */
		return g_strdup(nick);
	} else {
		/* Need to truncate */
		if (max_width >= 2) {
			/* Find truncation point that leaves space for + */
			p = nick;
			width = 0;
			while (*p && width < max_width - 1) {
				char const *str_ptr = p;
				int char_width = string_advance(&str_ptr, TREAT_STRING_AS_UTF8);
				if (width + char_width > max_width - 1)
					break;
				width += char_width;
				p = str_ptr;
			}

			if (p > nick) {
				/* Create truncated nick with + */
				truncated_len = p - nick;
				result = g_malloc(truncated_len + 2); /* +1 for +, +1 for \0 */
				memcpy(result, nick, truncated_len);
				result[truncated_len] = '+';
				result[truncated_len + 1] = '\0';
			} else {
				/* No space for nick, just return + */
				result = g_strdup("+");
			}
		} else {
			/* max_width is 1, just return + */
			result = g_strdup("+");
		}
	}

	return result;
}

/*
 * Determine format for a window entry based on selection and activity
 */
static int get_window_format(WINDOW_REC *win, WINDOW_SORT_REC *sort_rec,
                              int selected_index)
{
	int activity = win->data_level;
	int format;

	/* Determine format based on selection and activity */
	if (win->refnum - 1 == selected_index) {
		format = TXT_SIDEPANEL_ITEM_SELECTED;
	} else if (sort_rec->sort_group == 0 || sort_rec->sort_group == 1) {
		/* Notices and server status windows use header format unless selected */
		if (activity >= DATA_LEVEL_HILIGHT) {
			/* Check if this is a nick mention (has hilight_color indicating
			 * nick mention) */
			if (win->hilight_color != NULL) {
				format = TXT_SIDEPANEL_ITEM_NICK_MENTION;
			} else {
				format = TXT_SIDEPANEL_ITEM_HIGHLIGHT;
			}
		} else if (activity > DATA_LEVEL_NONE) {
			format = TXT_SIDEPANEL_ITEM_ACTIVITY;
		} else {
			format = TXT_SIDEPANEL_HEADER;
		}
	} else {
		/* Channels, queries, and other windows - SIMPLE PRIORITY SYSTEM */
		int current_priority = get_window_current_priority(win);

		switch (current_priority) {
		case 4: /* PRIORITY 4: Nick mention OR Query messages (magenta) */
			/* Use QUERY_MSG only for query windows, NICK_MENTION for channel
			 * mentions */
			if (win->active && IS_QUERY(win->active)) {
				format = TXT_SIDEPANEL_ITEM_QUERY_MSG;
			} else {
				format = TXT_SIDEPANEL_ITEM_NICK_MENTION;
			}
			break;
		case 3: /* PRIORITY 3: Channel activity (yellow) */
			format = TXT_SIDEPANEL_ITEM_ACTIVITY;
			break;
		case 2: /* PRIORITY 2: Highlights (red) */
			format = TXT_SIDEPANEL_ITEM_HIGHLIGHT;
			break;
		case 1: /* PRIORITY 1: Events (cyan) */
			format = TXT_SIDEPANEL_ITEM_EVENTS;
			break;
		default: /* PRIORITY 0: No activity (white) */
			format = TXT_SIDEPANEL_ITEM;
			break;
		}
	}
	return format;
}

void draw_left_contents(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx)
{
	TERM_WINDOW *tw;
	int row;
	int skip;
	int height;
	int width;
	GSList *sort_list, *s;
	int list_index;
	SP_PANEL_CACHE *cache;
	gboolean full_redraw;
	int new_count;
	int lines_changed = 0;

	if (!ctx)
		return;
	tw = ctx->left_tw;
	if (!tw)
		return;

	height = ctx->left_h;
	width = ctx->left_w;
	skip = ctx->left_scroll_offset;

	/* Ensure cache exists */
	if (!ctx->left_cache) {
		ctx->left_cache = sp_cache_create();
	}
	cache = ctx->left_cache;

	/* Check if we need full redraw (dimensions or scroll changed) */
	full_redraw = sp_cache_needs_full_redraw(cache, height, width, skip);

	/* Get sorted list using shared function */
	sort_list = build_sorted_window_list();

	/* Count visible items and build new state */
	row = 0;
	list_index = 0;
	new_count = 0;

	for (s = sort_list; s && row < height; s = s->next) {
		WINDOW_SORT_REC *sort_rec = s->data;
		WINDOW_REC *win = sort_rec->win;
		const char *display_name = sort_rec->sort_key;
		int format;
		char refnum_str[16];
		int display_num;
		char *truncated_name;
		int refnum_width;
		int name_max_width;
		gboolean line_changed;

		/* Calculate display number (1-based position in sorted list) */
		display_num = list_index + 1;

		/* Skip items before our scroll offset */
		if (list_index++ < skip)
			continue;

		/* Get format for this window */
		format = get_window_format(win, sort_rec, ctx->left_selected_index);

		/* Build display string */
		g_snprintf(refnum_str, sizeof(refnum_str), "%d", display_num);

		/* Calculate available width for channel name */
		/* Format is "$0. $1" so we need space for: number + ". " (3 chars) + name */
		refnum_width = string_width(refnum_str, -1);
		name_max_width = MAX(1, width - refnum_width - 3);

		/* Truncate display name if needed */
		truncated_name = truncate_nick_for_sidepanel(
			display_name ? display_name : "window", name_max_width);

		/* Check if this line changed from cache */
		line_changed = full_redraw ||
		               row >= cache->count ||
		               !sp_cache_line_matches(&cache->lines[row], truncated_name,
		                                       refnum_str, format, win->refnum);

		if (line_changed) {
			/* Clear line first with clrtoeol (no full panel clear!) */
			term_set_color(tw, ATTR_RESET);
			term_move(tw, 0, row);
			term_clrtoeol(tw);

			/* Draw the new content */
			draw_str_themed_2params(tw, 0, row, mw->active, format,
			                        refnum_str, truncated_name);

			/* Update cache */
			sp_cache_line_update(&cache->lines[row], truncated_name,
			                      refnum_str, format, win->refnum);
			lines_changed++;
		}

		g_free(truncated_name);
		row++;
		new_count++;
	}

	/* Clear any remaining lines that are no longer needed */
	if (cache->count > new_count) {
		int i;
		for (i = new_count; i < cache->count && i < height; i++) {
			sp_clear_line(tw, i, width);
			sp_cache_line_free(&cache->lines[i]);
			lines_changed++;
		}
	}

	/* Update cache metadata */
	cache->count = new_count;
	cache->scroll_offset = skip;
	cache->panel_height = height;
	cache->panel_width = width;

	/* Clean up */
	free_sorted_window_list(sort_list);

	/* Only draw border if right panel is also visible */
	if (ctx->right_tw && ctx->right_h > 0) {
		draw_border_vertical(tw, width, height, 1);
	}

	/* Only mark dirty if something changed */
	if (lines_changed > 0) {
		irssi_set_dirty();
	}
}

/* Nick comparison function for case-insensitive sorting - moved to activity module */

/* Wrapper for nicklist_compare to work with g_slist_sort_with_data */
static gint nick_display_compare_with_server(gconstpointer a, gconstpointer b, gpointer user_data)
{
	const NICK_REC *nick_a = a;
	const NICK_REC *nick_b = b;
	const char *nick_prefix = user_data;

	return nicklist_compare((NICK_REC *) nick_a, (NICK_REC *) nick_b, nick_prefix);
}

/* Get format and prefix string for a nick based on their highest privilege */
static void get_nick_format_and_prefix(NICK_REC *nick, int *format, const char **prefix_str)
{
	char prefix = nick->prefixes[0];

	switch (prefix) {
	case '~': /* Owner */
		*format = TXT_SIDEPANEL_NICK_OWNER_STATUS;
		*prefix_str = "~";
		break;
	case '&': /* Admin */
		*format = TXT_SIDEPANEL_NICK_ADMIN_STATUS;
		*prefix_str = "&";
		break;
	case '@': /* Operator */
		*format = TXT_SIDEPANEL_NICK_OP_STATUS;
		*prefix_str = "@";
		break;
	case '%': /* Half-op */
		*format = TXT_SIDEPANEL_NICK_HALFOP_STATUS;
		*prefix_str = "%";
		break;
	case '+': /* Voice */
		*format = TXT_SIDEPANEL_NICK_VOICE_STATUS;
		*prefix_str = "+";
		break;
	default: /* Regular user */
		*format = TXT_SIDEPANEL_NICK_NORMAL_STATUS;
		*prefix_str = "";
		break;
	}
}

void draw_right_contents(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx)
{
	TERM_WINDOW *tw;
	WINDOW_REC *aw;
	int height;
	int width;
	int skip;
	int index;
	int row;
	SP_PANEL_CACHE *cache;
	gboolean full_redraw;
	int new_count;
	int lines_changed = 0;

	if (!ctx)
		return;
	tw = ctx->right_tw;
	if (!tw)
		return;

	height = ctx->right_h;
	width = ctx->right_w;
	skip = ctx->right_scroll_offset;
	aw = mw->active;
	index = 0;
	row = 0;
	new_count = 0;

	/* Ensure cache exists */
	if (!ctx->right_cache) {
		ctx->right_cache = sp_cache_create();
	}
	cache = ctx->right_cache;

	/* Check if we need full redraw (dimensions or scroll changed) */
	full_redraw = sp_cache_needs_full_redraw(cache, height, width, skip);

	/* Free previous right_order list */
	if (ctx->right_order) {
		g_slist_free(ctx->right_order);
		ctx->right_order = NULL;
	}

	/* If no channel active, clear panel and draw border */
	if (!aw || !aw->active || !aw->active->visible_name ||
	    !IS_CHANNEL(aw->active)) {
		/* Clear any cached lines */
		if (cache->count > 0) {
			int i;
			for (i = 0; i < cache->count && i < height; i++) {
				sp_clear_line(tw, i, width);
				sp_cache_line_free(&cache->lines[i]);
			}
			cache->count = 0;
			lines_changed = 1;
		}
		draw_border_vertical(tw, width, height, 0);
		if (lines_changed > 0) {
			irssi_set_dirty();
		}
		return;
	}

	if (IS_CHANNEL(aw->active)) {
		CHANNEL_REC *ch = CHANNEL(aw->active);
		SERVER_REC *server = ch->server;
		GSList *nicks = nicklist_getnicks(ch);
		GSList *sorted_nicks;
		GSList *cur;
		NICK_REC *nick;
		char *truncated_nick;
		int format;
		const char *prefix_str;
		const char *nick_prefix;
		/* Calculate available width for nick display */
		/* Available width = total panel width - 1 (start position) - 1 (status) - 1 (border margin) */
		int nick_max_width = MAX(1, width - 3);

		/* Safety check for server */
		if (!server) {
			g_slist_free(nicks);
			/* Clear any cached lines */
			if (cache->count > 0) {
				int i;
				for (i = 0; i < cache->count && i < height; i++) {
					sp_clear_line(tw, i, width);
					sp_cache_line_free(&cache->lines[i]);
				}
				cache->count = 0;
			}
			draw_border_vertical(tw, width, height, 0);
			irssi_set_dirty();
			return;
		}

		/* Get nick prefix order from server (from ISUPPORT PREFIX) */
		nick_prefix = server->get_nick_flags ? server->get_nick_flags(server) : NULL;
		if (!nick_prefix || *nick_prefix == '\0')
			nick_prefix = "~&@%+"; /* fallback for servers without PREFIX */

		/* Sort all nicks using official nicklist_compare with server's prefix order */
		sorted_nicks = g_slist_copy(nicks);
		sorted_nicks = g_slist_sort_with_data(sorted_nicks,
		                                      (GCompareDataFunc) nick_display_compare_with_server,
		                                      (gpointer) nick_prefix);
		g_slist_free(nicks);

		/* Render sorted nicks with differential rendering */
		for (cur = sorted_nicks; cur; cur = cur->next) {
			gboolean line_changed;
			int nick_hash;

			nick = cur->data;
			if (!nick || !nick->nick)
				continue;

			/* Use prepend (O(1)) instead of append (O(n)) - will reverse at end */
			ctx->right_order = g_slist_prepend(ctx->right_order, nick);

			if (index++ < skip)
				continue;
			if (row >= height)
				continue;

			/* Get appropriate format and prefix for this nick */
			get_nick_format_and_prefix(nick, &format, &prefix_str);

			truncated_nick = truncate_nick_for_sidepanel(nick->nick, nick_max_width);

			/* Use pointer as hash for nick identity (unique per channel) */
			nick_hash = GPOINTER_TO_INT(nick);

			/* Check if this line changed from cache */
			line_changed = full_redraw ||
			               row >= cache->count ||
			               !sp_cache_line_matches(&cache->lines[row], truncated_nick,
			                                       prefix_str, format, nick_hash);

			if (line_changed) {
				/* Clear line first with clrtoeol (no full panel clear!) */
				term_set_color(tw, ATTR_RESET);
				term_move(tw, 0, row);
				term_clrtoeol(tw);

				/* Draw the new content */
				draw_str_themed_2params(tw, 1, row, mw->active, format,
				                        prefix_str, truncated_nick);

				/* Update cache */
				sp_cache_line_update(&cache->lines[row], truncated_nick,
				                      prefix_str, format, nick_hash);
				lines_changed++;
			}

			g_free(truncated_nick);
			row++;
			new_count++;
		}

		/* Reverse to get correct order (we used prepend for performance) */
		ctx->right_order = g_slist_reverse(ctx->right_order);

		g_slist_free(sorted_nicks);
	}

	/* Clear any remaining lines that are no longer needed */
	if (cache->count > new_count) {
		int i;
		for (i = new_count; i < cache->count && i < height; i++) {
			sp_clear_line(tw, i, width);
			sp_cache_line_free(&cache->lines[i]);
			lines_changed++;
		}
	}

	/* Update cache metadata */
	cache->count = new_count;
	cache->scroll_offset = skip;
	cache->panel_height = height;
	cache->panel_width = width;

	draw_border_vertical(tw, width, height, 0);

	/* Only mark dirty if something changed */
	if (lines_changed > 0) {
		irssi_set_dirty();
	}
}

void redraw_one(MAIN_WINDOW_REC *mw)
{
	SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
	if (!ctx)
		return;

	/* Freeze terminal updates to prevent flicker */
	term_refresh_freeze();

	position_tw(mw, ctx);
	draw_left_contents(mw, ctx);
	/* Only draw right contents if right panel is actually shown */
	if (ctx->right_tw && ctx->right_h > 0) {
		draw_right_contents(mw, ctx);
	}
	draw_main_window_borders(mw);
	irssi_set_dirty();

	/* Thaw and flush all updates at once */
	term_refresh_thaw();
}

void redraw_all(void)
{
	GSList *t;

	/* Freeze terminal updates to prevent flicker */
	term_refresh_freeze();

	for (t = mainwindows; t; t = t->next) {
		MAIN_WINDOW_REC *mw = t->data;
		SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
		if (!ctx)
			continue;
		position_tw(mw, ctx);
		draw_left_contents(mw, ctx);
		if (ctx->right_tw && ctx->right_h > 0) {
			draw_right_contents(mw, ctx);
		}
		draw_main_window_borders(mw);
		irssi_set_dirty();
	}

	/* Thaw and flush all updates at once */
	term_refresh_thaw();
}

void redraw_right_panels_only(const char *event_name)
{
	/* Redraw only right panels (nicklists) in all main windows */
	GSList *t;
	SP_MAINWIN_CTX *ctx;

	(void) event_name; /* unused */

	/* Safety check: ensure mainwindows is initialized */
	if (!mainwindows) {
		return;
	}

	/* Freeze terminal updates to prevent flicker */
	term_refresh_freeze();

	for (t = mainwindows; t; t = t->next) {
		MAIN_WINDOW_REC *mw = t->data;

		/* Safety check: ensure mw is valid */
		if (!mw) {
			continue;
		}

		ctx = get_ctx(mw, FALSE);
		if (!ctx) {
			continue;
		}

		/* Only redraw right panel if it exists and is visible */
		if (ctx->right_tw && ctx->right_h > 0) {
			position_tw(mw, ctx);
			draw_right_contents(mw, ctx);
			draw_main_window_borders(mw);
			irssi_set_dirty();
		}
	}

	/* Thaw and flush all updates at once */
	term_refresh_thaw();
}

void redraw_left_panels_only(const char *event_name)
{
	/* Redraw only left panels (window list) in all main windows */
	GSList *t;
	SP_MAINWIN_CTX *ctx;

	(void) event_name; /* unused */

	/* Safety check: ensure mainwindows is initialized */
	if (!mainwindows) {
		return;
	}

	/* Freeze terminal updates to prevent flicker */
	term_refresh_freeze();

	for (t = mainwindows; t; t = t->next) {
		MAIN_WINDOW_REC *mw = t->data;

		/* Safety check: ensure mw is valid */
		if (!mw) {
			sp_logf("DEBUG: NULL mainwindow in list, skipping redraw_left_panels_only");
			continue;
		}

		ctx = get_ctx(mw, FALSE);
		if (!ctx) {
			continue;
		}

		/* Only redraw left panel if it exists and is visible */
		if (ctx->left_tw && ctx->left_h > 0) {
			/* Position panels if needed (only if left panel exists) */
			position_tw(mw, ctx);
			/* Draw only left contents */
			draw_left_contents(mw, ctx);
			/* Update borders as activity colors may have changed */
			draw_main_window_borders(mw);
			irssi_set_dirty();
		}
	}

	/* Thaw and flush all updates at once */
	term_refresh_thaw();
}

void redraw_both_panels_only(const char *event_name)
{
	/* Redraw both left and right panels efficiently in all main windows */
	GSList *t;
	gboolean needs_redraw = FALSE;

	(void) event_name; /* unused */

	/* Safety check: ensure mainwindows is initialized */
	if (!mainwindows) {
		return;
	}

	/* Freeze terminal updates to prevent flicker */
	term_refresh_freeze();

	for (t = mainwindows; t; t = t->next) {
		MAIN_WINDOW_REC *mw = t->data;
		SP_MAINWIN_CTX *ctx;

		/* Safety check: ensure mw is valid */
		if (!mw) {
			continue;
		}

		ctx = get_ctx(mw, FALSE);
		if (!ctx) {
			continue;
		}

		needs_redraw = FALSE;

		/* Redraw left panel if it exists and is visible */
		if (ctx->left_tw && ctx->left_h > 0) {
			position_tw(mw, ctx);
			draw_left_contents(mw, ctx);
			needs_redraw = TRUE;
		}

		/* Redraw right panel if it exists and is visible */
		if (ctx->right_tw && ctx->right_h > 0) {
			/* Position already done above if left panel exists */
			if (!ctx->left_tw || ctx->left_h == 0) {
				position_tw(mw, ctx);
			}
			draw_right_contents(mw, ctx);
			needs_redraw = TRUE;
		}

		/* Update borders if any panel was redrawn */
		if (needs_redraw) {
			draw_main_window_borders(mw);
			irssi_set_dirty();
		}
	}

	/* Thaw and flush all updates at once */
	term_refresh_thaw();
}

/* Batching system for efficient redraws */
static gboolean batched_redraw_timeout(gpointer data)
{
	const char *event_name = (const char *) data;

	redraw_both_panels_only(event_name);
	redraw_pending = FALSE;
	redraw_timer_tag = -1;
	batch_mode_active = FALSE;
	return FALSE; /* Don't repeat */
}

void schedule_batched_redraw(const char *event_name)
{
	if (redraw_pending) {
		/* Already scheduled, just update the event name if needed */
		return;
	}

	redraw_pending = TRUE;
	batch_mode_active = TRUE;
	redraw_timer_tag = g_timeout_add(redraw_batch_timeout, batched_redraw_timeout, (gpointer) event_name);
}

void sidepanels_render_init(void)
{
	redraw_pending = FALSE;
	redraw_timer_tag = -1;
	batch_mode_active = FALSE;
}

void sidepanels_render_deinit(void)
{
	if (redraw_timer_tag != -1) {
		g_source_remove(redraw_timer_tag);
		redraw_timer_tag = -1;
	}
	redraw_pending = FALSE;
	batch_mode_active = FALSE;
}
