/*
 * term-ansi.h : Pure ANSI terminal backend for erssi
 *
 * Copyright (C) 2024 erssi-org team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef IRSSI_FE_ANSI_TERM_ANSI_H
#define IRSSI_FE_ANSI_TERM_ANSI_H

#include <glib.h>
#include <termios.h>

/* Graphics protocol types */
typedef enum {
	GFX_NONE = 0,        /* No graphics support - image preview OFF */
	GFX_SYMBOLS,         /* Unicode blocks/braille (Chafa fallback) */
	GFX_SIXEL,           /* Sixel protocol */
	GFX_KITTY,           /* Kitty graphics protocol */
	GFX_ITERM2,          /* iTerm2 inline images */
} GraphicsProtocol;

/* Terminal capabilities */
typedef struct {
	GraphicsProtocol protocol;
	gboolean in_tmux;           /* Requires DCS passthrough */
	gboolean in_screen;         /* Requires screen passthrough */
	gboolean can_query_size;    /* Can query pixel size */
	int max_colors;             /* 16, 256, or 16777216 (truecolor) */
	gboolean has_bracketed_paste;
	gboolean has_mouse_sgr;
	gboolean has_alt_screen;
} TerminalCaps;

/* ANSI terminal state */
typedef struct {
	FILE *out;
	struct termios old_tio;
	struct termios tio;
	int tio_saved;

	/* Terminal size */
	int width, height;

	/* Current cursor position (virtual) */
	int vcx, vcy;
	/* Real cursor position */
	int crealx, crealy;
	int cforcemove;
	int curs_visible;

	/* Current colors and attributes */
	unsigned int last_fg, last_bg;
	int last_attrs;

	/* Line tracking */
	char *lines_empty;

	/* Refresh freeze counter */
	int freeze;

	/* Capabilities */
	TerminalCaps caps;
} ANSI_TERM;

/* Global terminal instance */
extern ANSI_TERM *ansi_term;

/* Terminal capabilities detection */
void ansi_detect_capabilities(ANSI_TERM *term);
const char *ansi_graphics_protocol_name(GraphicsProtocol proto);

/* DCS passthrough for multiplexers */
void ansi_wrap_dcs_start(ANSI_TERM *term);
void ansi_wrap_dcs_end(ANSI_TERM *term);

/* Low-level ANSI output */
void ansi_move(ANSI_TERM *term, int x, int y);
void ansi_move_relative(ANSI_TERM *term, int oldx, int oldy, int x, int y);
void ansi_set_cursor_visible(ANSI_TERM *term, int visible);
void ansi_clear(ANSI_TERM *term);
void ansi_clear_to_eol(ANSI_TERM *term);
void ansi_scroll(ANSI_TERM *term, int y1, int y2, int count);
void ansi_repeat(ANSI_TERM *term, char chr, int count);

/* Color/attribute control */
void ansi_set_fg(ANSI_TERM *term, int color);
void ansi_set_bg(ANSI_TERM *term, int color);
void ansi_set_fg_rgb(ANSI_TERM *term, int r, int g, int b);
void ansi_set_bg_rgb(ANSI_TERM *term, int r, int g, int b);
void ansi_set_normal(ANSI_TERM *term);
void ansi_set_bold(ANSI_TERM *term);
void ansi_set_dim(ANSI_TERM *term);
void ansi_set_italic(ANSI_TERM *term, int set);
void ansi_set_underline(ANSI_TERM *term, int set);
void ansi_set_blink(ANSI_TERM *term);
void ansi_set_reverse(ANSI_TERM *term);

/* Screen control */
void ansi_alt_screen(ANSI_TERM *term, int enable);
void ansi_beep(ANSI_TERM *term);

/* Mouse control */
void ansi_mouse_enable(ANSI_TERM *term);
void ansi_mouse_disable(ANSI_TERM *term);

/* Bracketed paste */
void ansi_bracketed_paste(ANSI_TERM *term, int enable);

/* Application keypad mode */
void ansi_appkey_mode(ANSI_TERM *term, int enable);

#endif /* IRSSI_FE_ANSI_TERM_ANSI_H */
