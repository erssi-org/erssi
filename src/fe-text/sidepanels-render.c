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

void draw_left_contents(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx)
{
	TERM_WINDOW *tw;
	int row;
	int skip;
	int height;
	GSList *sort_list, *s;
	int list_index;

	if (!ctx)
		return;
	tw = ctx->left_tw;
	if (!tw)
		return;
	clear_window_full(tw, ctx->left_w, ctx->left_h);

	row = 0;
	skip = ctx->left_scroll_offset;
	height = ctx->left_h;

	/* Get sorted list using shared function */
	sort_list = build_sorted_window_list();

	/* Draw windows in sorted order */
	list_index = 0;
	for (s = sort_list; s && row < height; s = s->next) {
		WINDOW_SORT_REC *sort_rec = s->data;
		WINDOW_REC *win = sort_rec->win;
		const char *display_name = sort_rec->sort_key;
		int activity = win->data_level;
		int format;

		/* Skip items before our scroll offset */
		if (list_index++ < skip)
			continue;

		/* Determine format based on selection and activity */
		if (win->refnum - 1 == ctx->left_selected_index) {
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

		/* Draw the item */
		term_move(tw, 0, row);
		draw_str_themed(tw, 0, row, mw->active, format,
		                display_name ? display_name : "window");
		row++;
	}

	/* Clean up */
	free_sorted_window_list(sort_list);

	/* Only draw border if right panel is also visible */
	if (ctx->right_tw && ctx->right_h > 0) {
		draw_border_vertical(tw, ctx->left_w, ctx->left_h, 1);
	}
	irssi_set_dirty();
}

/* Nick comparison function for case-insensitive sorting - moved to activity module */

void draw_right_contents(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx)
{
	TERM_WINDOW *tw;
	WINDOW_REC *aw;
	int height;
	int skip;
	int index;
	int row;
	GSList *nt;
	if (!ctx)
		return;
	tw = ctx->right_tw;
	if (!tw)
		return;
	clear_window_full(tw, ctx->right_w, ctx->right_h);
	aw = mw->active;
	height = ctx->right_h;
	skip = ctx->right_scroll_offset;
	index = 0;
	row = 0;
	if (ctx->right_order) {
		g_slist_free(ctx->right_order);
		ctx->right_order = NULL;
	}

	/* If no channel active, just draw border and return */
	if (!aw || !aw->active || !aw->active->visible_name ||
	    !IS_CHANNEL(aw->active)) {
		draw_border_vertical(tw, ctx->right_w, ctx->right_h, 0);
		irssi_set_dirty();
		return;
	}

	if (IS_CHANNEL(aw->active)) {
		CHANNEL_REC *ch = CHANNEL(aw->active);
		GSList *nicks = nicklist_getnicks(ch);
		GSList *ops = NULL, *voices = NULL, *normal = NULL;
		GSList *cur;
		NICK_REC *nick;
		char *truncated_nick;
		int format;
		/* Calculate available width for nick display */
		/* Available width = total panel width - 1 (start position) - 1 (status) - 1 (border margin) */
		int nick_max_width = MAX(1, ctx->right_w - 3);

		/* Split nicks by status */
		for (nt = nicks; nt; nt = nt->next) {
			nick = nt->data;
			if (!nick || !nick->nick)
				continue;
			if (nick->op)
				ops = g_slist_prepend(ops, nick);
			else if (nick->voice)
				voices = g_slist_prepend(voices, nick);
			else
				normal = g_slist_prepend(normal, nick);
		}

		/* Sort each group alphabetically */
		ops = g_slist_sort(ops, ci_nick_compare);
		voices = g_slist_sort(voices, ci_nick_compare);
		normal = g_slist_sort(normal, ci_nick_compare);

		/* Build ordered list and render */
		for (cur = ops; cur && row < height; cur = cur->next) {
			nick = cur->data;
			ctx->right_order = g_slist_append(ctx->right_order, nick);
			if (index++ < skip)
				continue;
			term_move(tw, 1, row);
			format = TXT_SIDEPANEL_NICK_OP_STATUS;
			truncated_nick = truncate_nick_for_sidepanel(nick->nick, nick_max_width);
			draw_str_themed_2params(tw, 1, row, mw->active, format, "@", truncated_nick);
			g_free(truncated_nick);
			row++;
		}
		for (cur = voices; cur && row < height; cur = cur->next) {
			nick = cur->data;
			ctx->right_order = g_slist_append(ctx->right_order, nick);
			if (index++ < skip)
				continue;
			term_move(tw, 1, row);
			format = TXT_SIDEPANEL_NICK_VOICE_STATUS;
			truncated_nick = truncate_nick_for_sidepanel(nick->nick, nick_max_width);
			draw_str_themed_2params(tw, 1, row, mw->active, format, "+", truncated_nick);
			g_free(truncated_nick);
			row++;
		}
		for (cur = normal; cur && row < height; cur = cur->next) {
			nick = cur->data;
			ctx->right_order = g_slist_append(ctx->right_order, nick);
			if (index++ < skip)
				continue;
			term_move(tw, 1, row);
			format = TXT_SIDEPANEL_NICK_NORMAL_STATUS;
			truncated_nick = truncate_nick_for_sidepanel(nick->nick, nick_max_width);
			draw_str_themed_2params(tw, 1, row, mw->active, format, "", truncated_nick);
			g_free(truncated_nick);
			row++;
		}
		g_slist_free(ops);
		g_slist_free(voices);
		g_slist_free(normal);
		g_slist_free(nicks);
	}
	draw_border_vertical(tw, ctx->right_w, ctx->right_h, 0);
	irssi_set_dirty();
}

void redraw_one(MAIN_WINDOW_REC *mw)
{
	SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
	if (!ctx)
		return;
	position_tw(mw, ctx);
	draw_left_contents(mw, ctx);
	/* Only draw right contents if right panel is actually shown */
	if (ctx->right_tw && ctx->right_h > 0) {
		draw_right_contents(mw, ctx);
	}
	draw_main_window_borders(mw);
	irssi_set_dirty();
	term_refresh(NULL);
}

void redraw_all(void)
{
	GSList *t;
	for (t = mainwindows; t; t = t->next)
		redraw_one(t->data);
}

void redraw_right_panels_only(const char *event_name)
{
	/* Redraw only right panels (nicklists) in all main windows */
	GSList *t;
	SP_MAINWIN_CTX *ctx;
	/* Safety check: ensure mainwindows is initialized */
	if (!mainwindows) {
		return;
	}

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
	term_refresh(NULL);
}

void redraw_left_panels_only(const char *event_name)
{
	/* Redraw only left panels (window list) in all main windows */
	GSList *t;
	SP_MAINWIN_CTX *ctx;

	/* Safety check: ensure mainwindows is initialized */
	if (!mainwindows) {
		return;
	}

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
	term_refresh(NULL);
}

void redraw_both_panels_only(const char *event_name)
{
	/* Redraw both left and right panels efficiently in all main windows */
	GSList *t;
	gboolean needs_redraw = FALSE;

	/* Safety check: ensure mainwindows is initialized */
	if (!mainwindows) {
		return;
	}

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
	term_refresh(NULL);
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
