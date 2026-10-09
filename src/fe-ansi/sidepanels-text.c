/*
 * sidepanels-text.c : erssi
 *
 * Copyright (C) 2024-2025 erssi-org team
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

#include <irssi/src/common.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-ansi/term.h>
#include <irssi/src/fe-ansi/sidepanels-text.h>

/* Color attribute masks from textbuffer-view.c */
#define FGATTR (ATTR_NOCOLORS | ATTR_RESETFG | FG_MASK | ATTR_FGCOLOR24)
#define BGATTR (ATTR_NOCOLORS | ATTR_RESETBG | BG_MASK | ATTR_BGCOLOR24)

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

void sidepanel_text_walk(const char *text, SIDEPANEL_TEXT_FUNC func, void *data)
{
	const unsigned char *ptr, *next_ptr;
	unsigned int fg24, bg24;
	int color, char_width;
	unichar chr;

	color = ATTR_RESET;
	fg24 = bg24 = UINT_MAX;
	ptr = (const unsigned char *) text;
	while (*ptr != '\0') {
		if (*ptr == 4) {
			/* format code - colour change */
			ptr++;
			if (*ptr == '\0')
				break;
			unformat(&ptr, &color, &fg24, &bg24);
			continue;
		}

		chr = read_unichar(ptr, &next_ptr, &char_width);
		if (unichar_isprint(chr) && !func(chr, char_width, color, fg24, bg24, data))
			break;
		ptr = next_ptr;
	}
}
