/*
 * term-ansi.c : Pure ANSI terminal backend for erssi
 *
 * Copyright (C) 2024 erssi-org team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "module.h"
#include <irssi/src/core/signals.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-text/mainwindows.h>
#include <irssi/src/core/utf8.h>
#include <irssi/src/fe-text/resize-debug.h>

#include "term-ansi.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

/* ========================================================================
 * ANSI ESCAPE SEQUENCES
 * ======================================================================== */

#define CSI "\033["

/* Cursor control */
#define CURSOR_HOME       CSI "H"
#define CURSOR_SAVE       CSI "s"
#define CURSOR_RESTORE    CSI "u"
#define CURSOR_HIDE       CSI "?25l"
#define CURSOR_SHOW       CSI "?25h"

/* Screen control */
#define CLEAR_SCREEN      CSI "2J"
#define CLEAR_LINE        CSI "2K"
#define CLEAR_TO_EOL      CSI "K"

/* SGR (colors/attributes) */
#define SGR_RESET         CSI "0m"
#define SGR_BOLD          CSI "1m"
#define SGR_DIM           CSI "2m"
#define SGR_ITALIC        CSI "3m"
#define SGR_UNDERLINE     CSI "4m"
#define SGR_BLINK         CSI "5m"
#define SGR_REVERSE       CSI "7m"
#define SGR_NO_ITALIC     CSI "23m"
#define SGR_NO_UNDERLINE  CSI "24m"

/* Alternate screen buffer */
#define ALT_SCREEN_ON     CSI "?1049h"
#define ALT_SCREEN_OFF    CSI "?1049l"

/* Mouse SGR mode */
#define MOUSE_ENABLE      CSI "?1000h" CSI "?1002h" CSI "?1006h"
#define MOUSE_DISABLE     CSI "?1000l" CSI "?1002l" CSI "?1006l"

/* Bracketed paste */
#define BRACKETED_PASTE_ON  CSI "?2004h"
#define BRACKETED_PASTE_OFF CSI "?2004l"

/* Application keypad mode */
#define APPKEY_ON         CSI "?1h"
#define APPKEY_OFF        CSI "?1l"

/* DCS passthrough for tmux */
#define TMUX_WRAP_START   "\033Ptmux;\033"
#define TMUX_WRAP_END     "\033\\"

/* ========================================================================
 * INPUT HANDLING
 * ======================================================================== */

typedef int (*INPUT_FUNC)(const unsigned char *buf, int size, unichar *result);

/* ========================================================================
 * GLOBALS
 * ======================================================================== */

ANSI_TERM *ansi_term = NULL;
TERM_WINDOW *root_window = NULL;

int term_width, term_height;
int term_use_colors, term_use_colors24;
int term_type;

/* Color 256 to 16 mapping table */
int term_color256map[] = {
	 0, 4, 2, 6, 1, 5, 3, 7, 8,12,10,14, 9,13,11,15,
	 0, 0, 1, 1, 1, 1, 0, 0, 3, 1, 1, 9, 2, 2, 3, 3, 3, 3,
	 2, 2, 3, 3, 3, 3, 2, 2, 3, 3, 3,11,10,10, 3, 3,11,11,
	 0, 0, 5, 1, 1, 9, 0, 8, 8, 8, 9, 9, 2, 8, 8, 8, 9, 9,
	 2, 8, 8, 8, 9, 9, 2, 8, 8, 3, 3,11,10,10, 3, 3,11,11,
	 4, 4, 5, 5, 5, 5, 4, 8, 8, 8, 9, 9, 6, 8, 8, 8, 9, 9,
	 6, 8, 8, 8, 8, 9, 6, 8, 8, 8, 7, 7, 6, 6, 8, 7, 7, 7,
	 4, 4, 5, 5, 5, 5, 4, 8, 8, 8, 9, 9, 6, 8, 8, 8, 8, 9,
	 6, 8, 8, 8, 7, 7, 6, 6, 8, 7, 7, 7, 6, 6, 7, 7, 7, 7,
	 4, 4, 5, 5, 5,13, 4, 8, 8, 5, 5,13, 6, 8, 8, 8, 7, 7,
	 6, 6, 8, 7, 7, 7, 6, 6, 7, 7, 7, 7,14,14, 7, 7, 7, 7,
	12,12, 5, 5,13,13,12,12, 5, 5,13,13, 6, 6, 8, 7, 7, 7,
	 6, 6, 7, 7, 7, 7,14,14, 7, 7, 7, 7,14,14, 7, 7, 7,15,
	 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	 7, 7, 7, 7, 7, 7, 0 };

/* Internal state */
static INPUT_FUNC input_func;
static unsigned char inbuf[512];
static int inbuf_pos;
static int force_colors;
static int resize_dirty;
static int curs_x, curs_y;

/* SIGCONT handling */
static GSource *sigcont_source;
static volatile sig_atomic_t got_sigcont;

/* ========================================================================
 * TERM_WINDOW STRUCTURE
 * ======================================================================== */

struct _TERM_WINDOW {
	int x, y;
	int width, height;
};

/* ========================================================================
 * RAW MODE
 * ======================================================================== */

static void raw_mode_init(ANSI_TERM *term)
{
	tcgetattr(STDIN_FILENO, &term->old_tio);
	memcpy(&term->tio, &term->old_tio, sizeof(term->tio));

	term->tio.c_lflag &= ~(ICANON | ECHO);
	term->tio.c_iflag &= ~(ICRNL | IXON | IXOFF);
	term->tio.c_cc[VMIN] = 1;
	term->tio.c_cc[VTIME] = 0;
	term->tio.c_cc[VINTR] = _POSIX_VDISABLE;
	term->tio.c_cc[VQUIT] = _POSIX_VDISABLE;
#ifdef VDSUSP
	term->tio.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
#ifdef VSUSP
	term->tio.c_cc[VSUSP] = _POSIX_VDISABLE;
#endif

	term->tio_saved = 1;
}

static void raw_mode_enable(ANSI_TERM *term)
{
	tcsetattr(STDIN_FILENO, TCSADRAIN, &term->tio);
}

static void raw_mode_disable(ANSI_TERM *term)
{
	if (term->tio_saved)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &term->old_tio);
}

/* ========================================================================
 * SIGCONT HANDLING
 * ======================================================================== */

static void sig_cont(int p) { got_sigcont = 1; }

static gboolean sigcont_prepare(GSource *s, gint *t) { *t = -1; return got_sigcont; }
static gboolean sigcont_check(GSource *s) { return got_sigcont; }
static gboolean sigcont_dispatch(GSource *s, GSourceFunc cb, gpointer data)
{
	got_sigcont = 0;
	return cb ? cb(data) : TRUE;
}

static gboolean do_redraw(gpointer unused)
{
	irssi_redraw();
	return TRUE;
}

static GSourceFuncs sigcont_funcs = {
	.prepare = sigcont_prepare,
	.check = sigcont_check,
	.dispatch = sigcont_dispatch
};

/* ========================================================================
 * LOW-LEVEL ANSI OUTPUT
 * ======================================================================== */

void ansi_move(ANSI_TERM *term, int x, int y)
{
	/* ANSI uses 1-based coordinates */
	fprintf(term->out, CSI "%d;%dH", y + 1, x + 1);
}

void ansi_move_relative(ANSI_TERM *term, int oldx, int oldy, int x, int y)
{
	/* For simplicity, use absolute positioning */
	ansi_move(term, x, y);
}

void ansi_set_cursor_visible(ANSI_TERM *term, int visible)
{
	fputs(visible ? CURSOR_SHOW : CURSOR_HIDE, term->out);
}

void ansi_clear(ANSI_TERM *term)
{
	fputs(CURSOR_HOME CLEAR_SCREEN, term->out);
}

void ansi_clear_to_eol(ANSI_TERM *term)
{
	fputs(CLEAR_TO_EOL, term->out);
}

void ansi_scroll(ANSI_TERM *term, int y1, int y2, int count)
{
	int i;

	/* Set scroll region */
	fprintf(term->out, CSI "%d;%dr", y1 + 1, y2 + 1);

	if (count > 0) {
		/* Scroll up */
		ansi_move(term, 0, y2);
		for (i = 0; i < count; i++)
			fputs("\n", term->out);
	} else if (count < 0) {
		/* Scroll down */
		ansi_move(term, 0, y1);
		for (i = 0; i < -count; i++)
			fputs(CSI "M", term->out); /* Reverse index */
	}

	/* Reset scroll region to full screen */
	fprintf(term->out, CSI "r");
}

void ansi_repeat(ANSI_TERM *term, char chr, int count)
{
	int i;
	for (i = 0; i < count; i++)
		fputc(chr, term->out);
}

/* ========================================================================
 * COLOR/ATTRIBUTE CONTROL
 * ======================================================================== */

/* Irssi to ANSI color mapping table.
 * Irssi uses a different color order than standard ANSI:
 *   irssi 0=black  -> ANSI 0
 *   irssi 1=blue   -> ANSI 4
 *   irssi 2=green  -> ANSI 2
 *   irssi 3=cyan   -> ANSI 6
 *   irssi 4=red    -> ANSI 1
 *   irssi 5=magenta-> ANSI 5
 *   irssi 6=yellow -> ANSI 3
 *   irssi 7=white  -> ANSI 7
 *   (8-15 are bright variants)
 */
static const unsigned char ansitab[16] = {
	0, 4, 2, 6, 1, 5, 3, 7,
	8, 12, 10, 14, 9, 13, 11, 15
};

void ansi_set_fg(ANSI_TERM *term, int color)
{
	int ansi_color;

	if (color < 16) {
		ansi_color = ansitab[color];
		if (ansi_color < 8) {
			fprintf(term->out, CSI "%dm", 30 + ansi_color);
		} else {
			fprintf(term->out, CSI "%dm", 90 + (ansi_color - 8));
		}
	} else {
		/* Extended colors (16-255) don't need mapping */
		fprintf(term->out, CSI "38;5;%dm", color);
	}
}

void ansi_set_bg(ANSI_TERM *term, int color)
{
	int ansi_color;

	if (color < 16) {
		ansi_color = ansitab[color];
		if (ansi_color < 8) {
			fprintf(term->out, CSI "%dm", 40 + ansi_color);
		} else {
			fprintf(term->out, CSI "%dm", 100 + (ansi_color - 8));
		}
	} else {
		/* Extended colors (16-255) don't need mapping */
		fprintf(term->out, CSI "48;5;%dm", color);
	}
}

void ansi_set_fg_rgb(ANSI_TERM *term, int r, int g, int b)
{
	fprintf(term->out, CSI "38;2;%d;%d;%dm", r, g, b);
}

void ansi_set_bg_rgb(ANSI_TERM *term, int r, int g, int b)
{
	fprintf(term->out, CSI "48;2;%d;%d;%dm", r, g, b);
}

void ansi_set_normal(ANSI_TERM *term)
{
	fputs(SGR_RESET, term->out);
}

void ansi_set_bold(ANSI_TERM *term)
{
	fputs(SGR_BOLD, term->out);
}

void ansi_set_dim(ANSI_TERM *term)
{
	fputs(SGR_DIM, term->out);
}

void ansi_set_italic(ANSI_TERM *term, int set)
{
	fputs(set ? SGR_ITALIC : SGR_NO_ITALIC, term->out);
}

void ansi_set_underline(ANSI_TERM *term, int set)
{
	fputs(set ? SGR_UNDERLINE : SGR_NO_UNDERLINE, term->out);
}

void ansi_set_blink(ANSI_TERM *term)
{
	fputs(SGR_BLINK, term->out);
}

void ansi_set_reverse(ANSI_TERM *term)
{
	fputs(SGR_REVERSE, term->out);
}

/* ========================================================================
 * SCREEN CONTROL
 * ======================================================================== */

void ansi_alt_screen(ANSI_TERM *term, int enable)
{
	fputs(enable ? ALT_SCREEN_ON : ALT_SCREEN_OFF, term->out);
}

void ansi_beep(ANSI_TERM *term)
{
	fputc('\a', term->out);
	fflush(term->out);
}

void ansi_mouse_enable(ANSI_TERM *term)
{
	fputs(MOUSE_ENABLE, term->out);
}

void ansi_mouse_disable(ANSI_TERM *term)
{
	fputs(MOUSE_DISABLE, term->out);
}

void ansi_bracketed_paste(ANSI_TERM *term, int enable)
{
	fputs(enable ? BRACKETED_PASTE_ON : BRACKETED_PASTE_OFF, term->out);
}

void ansi_appkey_mode(ANSI_TERM *term, int enable)
{
	fputs(enable ? APPKEY_ON : APPKEY_OFF, term->out);
}

/* ========================================================================
 * DCS PASSTHROUGH FOR MULTIPLEXERS
 * ======================================================================== */

void ansi_wrap_dcs_start(ANSI_TERM *term)
{
	if (term->caps.in_tmux) {
		fputs(TMUX_WRAP_START, term->out);
	}
	/* screen passthrough not implemented yet */
}

void ansi_wrap_dcs_end(ANSI_TERM *term)
{
	if (term->caps.in_tmux) {
		fputs(TMUX_WRAP_END, term->out);
	}
}

/* ========================================================================
 * TERMINAL CAPABILITIES DETECTION
 * ======================================================================== */

const char *ansi_graphics_protocol_name(GraphicsProtocol proto)
{
	switch (proto) {
	case GFX_NONE:    return "none";
	case GFX_SYMBOLS: return "symbols";
	case GFX_SIXEL:   return "sixel";
	case GFX_KITTY:   return "kitty";
	case GFX_ITERM2:  return "iterm2";
	default:          return "unknown";
	}
}

void ansi_detect_capabilities(ANSI_TERM *term)
{
	const char *env_term, *env_colorterm, *env_term_program;
	const char *env_tmux, *env_sty;
	const char *env_kitty_pid, *env_ghostty;

	memset(&term->caps, 0, sizeof(term->caps));

	/* Default to 256 colors for modern terminals */
	term->caps.max_colors = 256;
	term->caps.has_bracketed_paste = TRUE;
	term->caps.has_mouse_sgr = TRUE;
	term->caps.has_alt_screen = TRUE;

	env_term = g_getenv("TERM");
	env_colorterm = g_getenv("COLORTERM");
	env_term_program = g_getenv("TERM_PROGRAM");
	env_tmux = g_getenv("TMUX");
	env_sty = g_getenv("STY");
	env_kitty_pid = g_getenv("KITTY_PID");
	env_ghostty = g_getenv("GHOSTTY_RESOURCES_DIR");

	/* Check for multiplexers */
	if (env_tmux && *env_tmux) {
		term->caps.in_tmux = TRUE;
	}
	if (env_sty && *env_sty) {
		term->caps.in_screen = TRUE;
	}

	/* Check for truecolor support */
	if (env_colorterm &&
	    (g_strcmp0(env_colorterm, "truecolor") == 0 ||
	     g_strcmp0(env_colorterm, "24bit") == 0)) {
		term->caps.max_colors = 16777216;
	}

	/* Detect graphics protocol */
	term->caps.protocol = GFX_SYMBOLS; /* Default fallback */

	/* Kitty */
	if (env_kitty_pid && *env_kitty_pid) {
		term->caps.protocol = GFX_KITTY;
		term->caps.max_colors = 16777216;
	}
	/* Ghostty */
	else if (env_ghostty && *env_ghostty) {
		term->caps.protocol = GFX_KITTY;
		term->caps.max_colors = 16777216;
	}
	/* WezTerm */
	else if (env_term_program && g_strcmp0(env_term_program, "WezTerm") == 0) {
		term->caps.protocol = GFX_KITTY;
		term->caps.max_colors = 16777216;
	}
	/* iTerm2 */
	else if (env_term_program && g_strcmp0(env_term_program, "iTerm.app") == 0) {
		term->caps.protocol = GFX_ITERM2;
		term->caps.max_colors = 16777216;
	}
	/* xterm with sixel (check TERM) */
	else if (env_term && (g_str_has_prefix(env_term, "xterm") ||
	                      g_str_has_prefix(env_term, "foot") ||
	                      g_str_has_prefix(env_term, "mlterm"))) {
		/* These terminals may support sixel, but we'd need DA1 query to confirm */
		term->caps.protocol = GFX_SIXEL;
	}

	/* If in tmux, we can still use graphics via passthrough */
	if (term->caps.in_tmux && term->caps.protocol != GFX_NONE) {
		/* Keep the detected protocol, but note we need passthrough */
		/* TODO: detect outer terminal for tmux */
	}
}

/* ========================================================================
 * TERM COMMON FUNCTIONS
 * ======================================================================== */

static void read_settings(void)
{
	const char *str;
	int old_colors = term_use_colors;
	int old_colors24 = term_use_colors24;
	int old_type = term_type;

	str = settings_get_str("term_charset");
	if (g_ascii_strcasecmp(str, "utf-8") == 0)
		term_type = TERM_TYPE_UTF8;
	else if (g_ascii_strcasecmp(str, "big5") == 0)
		term_type = TERM_TYPE_BIG5;
	else
		term_type = TERM_TYPE_8BIT;

	if (old_type != term_type)
		term_set_input_type(term_type);

	if (force_colors != settings_get_bool("term_force_colors")) {
		force_colors = settings_get_bool("term_force_colors");
		term_force_colors(force_colors);
	}

	term_use_colors = settings_get_bool("colors") &&
		(force_colors || term_has_colors());

	term_use_colors24 = settings_get_bool("colors_ansi_24bit") &&
		(force_colors || term_has_colors());

	if (term_use_colors != old_colors || term_use_colors24 != old_colors24)
		irssi_redraw();
}

static void cmd_resize(void)
{
	resize_dirty = TRUE;
	term_resize_dirty();
}

static void cmd_redraw(void)
{
	irssi_redraw();
}

#ifdef SIGWINCH
static void sig_winch(int p)
{
	irssi_set_dirty();
	resize_dirty = TRUE;
}
#endif

void term_common_init(void)
{
	const char *dummy;
#ifdef SIGWINCH
	struct sigaction act;
#endif
	settings_add_bool("lookandfeel", "colors", TRUE);
	settings_add_bool("lookandfeel", "term_force_colors", FALSE);
	settings_add_bool("lookandfeel", "mirc_blink_fix", FALSE);

	/* Stub settings for image_preview (from fe-notcurses) - not functional
	 * in fe-ansi but registered to avoid "unknown settings" warning */
	settings_add_bool("lookandfeel", "image_preview", FALSE);
	settings_add_int("lookandfeel", "image_preview_max_width", 40);
	settings_add_int("lookandfeel", "image_preview_max_height", 10);
	settings_add_str("lookandfeel", "image_preview_blitter", "auto");
	settings_add_size("lookandfeel", "image_preview_cache_size", "100M");
	settings_add_time("lookandfeel", "image_preview_timeout", "10s");
	settings_add_size("lookandfeel", "image_preview_max_file_size", "10M");
	settings_add_bool("lookandfeel", "image_preview_debug", FALSE);

	force_colors = FALSE;
	term_use_colors = term_has_colors() && settings_get_bool("colors");
	settings_add_bool("lookandfeel", "colors_ansi_24bit", FALSE);
	term_use_colors24 = term_has_colors() && settings_get_bool("colors_ansi_24bit");
	read_settings();

	if (g_get_charset(&dummy)) {
		term_type = TERM_TYPE_UTF8;
		term_set_input_type(TERM_TYPE_UTF8);
	}

	signal_add("beep", (SIGNAL_FUNC) term_beep);
	signal_add("setup changed", (SIGNAL_FUNC) read_settings);
	command_bind("resize", NULL, (SIGNAL_FUNC) cmd_resize);
	command_bind("redraw", NULL, (SIGNAL_FUNC) cmd_redraw);

#ifdef SIGWINCH
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_winch;
	sigaction(SIGWINCH, &act, NULL);
#endif
}

void term_common_deinit(void)
{
	command_unbind("resize", (SIGNAL_FUNC) cmd_resize);
	command_unbind("redraw", (SIGNAL_FUNC) cmd_redraw);
	signal_remove("beep", (SIGNAL_FUNC) term_beep);
	signal_remove("setup changed", (SIGNAL_FUNC) read_settings);
}

/* ========================================================================
 * TERM API IMPLEMENTATION
 * ======================================================================== */

static void term_move_real(void)
{
	ANSI_TERM *term = ansi_term;

	if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove) {
		if (term->curs_visible) {
			ansi_set_cursor_visible(term, FALSE);
			term->curs_visible = FALSE;
		}

		if (term->cforcemove) {
			term->crealx = term->crealy = -1;
			term->cforcemove = FALSE;
		}

		ansi_move(term, term->vcx, term->vcy);
		term->crealx = term->vcx;
		term->crealy = term->vcy;
	}
}

static void term_move_reset(int x, int y)
{
	ANSI_TERM *term = ansi_term;

	if (x >= term_width) x = term_width - 1;
	if (y >= term_height) y = term_height - 1;

	term->vcx = x;
	term->vcy = y;
	term->cforcemove = TRUE;
	term_move_real();
}

int term_init(void)
{
	struct sigaction act;
	int width, height;

	ansi_term = g_new0(ANSI_TERM, 1);
	ansi_term->out = stdout;

	ansi_term->last_fg = ansi_term->last_bg = -1;
	ansi_term->last_attrs = 0;
	ansi_term->vcx = ansi_term->vcy = 0;
	ansi_term->crealx = ansi_term->crealy = -1;
	ansi_term->cforcemove = TRUE;
	ansi_term->curs_visible = TRUE;

	raw_mode_init(ansi_term);
	raw_mode_enable(ansi_term);

	/* Detect terminal capabilities */
	ansi_detect_capabilities(ansi_term);

	/* Get terminal size */
	if (term_get_size(&width, &height)) {
		ansi_term->width = width;
		ansi_term->height = height;
	} else {
		ansi_term->width = 80;
		ansi_term->height = 24;
	}

	/* Enter alternate screen buffer */
	ansi_alt_screen(ansi_term, TRUE);
	ansi_clear(ansi_term);

	/* Enable mouse */
	ansi_mouse_enable(ansi_term);

	/* Enable bracketed paste */
	ansi_bracketed_paste(ansi_term, TRUE);

	/* SIGCONT handler */
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_cont;
	sigaction(SIGCONT, &act, NULL);
	sigcont_source = g_source_new(&sigcont_funcs, sizeof(GSource));
	g_source_set_callback(sigcont_source, do_redraw, NULL, NULL);
	g_source_attach(sigcont_source, NULL);

	curs_x = curs_y = 0;
	term_width = ansi_term->width;
	term_height = ansi_term->height;

	root_window = term_window_create(0, 0, term_width, term_height);
	ansi_term->lines_empty = g_new0(char, term_height);

	term_set_input_type(TERM_TYPE_8BIT);
	term_common_init();

	term_use_colors = TRUE;
	term_use_colors24 = (ansi_term->caps.max_colors > 256);

	return TRUE;
}

void term_deinit(void)
{
	if (ansi_term) {
		signal(SIGCONT, SIG_DFL);
		g_source_destroy(sigcont_source);
		g_source_unref(sigcont_source);

		term_common_deinit();

		/* Disable mouse */
		ansi_mouse_disable(ansi_term);

		/* Disable bracketed paste */
		ansi_bracketed_paste(ansi_term, FALSE);

		/* Reset colors */
		ansi_set_normal(ansi_term);

		/* Show cursor */
		ansi_set_cursor_visible(ansi_term, TRUE);

		/* Leave alternate screen */
		ansi_alt_screen(ansi_term, FALSE);

		fflush(ansi_term->out);

		raw_mode_disable(ansi_term);

		g_free(ansi_term->lines_empty);
		g_free(root_window);
		g_free(ansi_term);
		ansi_term = NULL;
		root_window = NULL;
	}
}

int term_get_size(int *width, int *height)
{
#ifdef TIOCGWINSZ
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) < 0)
		return FALSE;

	if (ws.ws_row == 0 && ws.ws_col == 0)
		return FALSE;

	*width = ws.ws_col;
	*height = ws.ws_row;

	if (*width < 20) *width = 20;
	if (*height < 1) *height = 1;
	return TRUE;
#else
	return FALSE;
#endif
}

void term_resize(int width, int height)
{
	if (width < 0 || height < 0) {
		width = ansi_term->width;
		height = ansi_term->height;
	}

	if (term_width != width || term_height != height) {
		term_width = ansi_term->width = width;
		term_height = ansi_term->height = height;
		term_window_move(root_window, 0, 0, term_width, term_height);

		g_free(ansi_term->lines_empty);
		ansi_term->lines_empty = g_new0(char, term_height);

		/* Clear screen and reset cursor after resize */
		ansi_set_normal(ansi_term);
		ansi_clear(ansi_term);
	}

	term_move_reset(0, 0);
}

void term_resize_final(int width, int height)
{
	/* Nothing special needed for ANSI backend */
}

void term_resize_dirty(void)
{
	int width, height;
	int old_width = term_width;
	int old_height = term_height;

	if (!resize_dirty)
		return;

	resize_dirty = FALSE;
	resize_debug_log("TERM_RESIZE", "SIGWINCH received - starting resize");

	if (!term_get_size(&width, &height))
		width = height = -1;

	resize_debug_dimensions("TERM_RESIZE", old_width, old_height, width, height);

	/* Skip resize if character dimensions unchanged (Ghostty sends SIGWINCH
	 * for pixel-only changes which would clear sidepanel caches without redraw) */
	if (width == old_width && height == old_height) {
		resize_debug_log("TERM_RESIZE", "dimensions unchanged, skipping resize");
		return;
	}

	resize_debug_log("TERM_RESIZE", "calling term_resize(%d, %d)", width, height);

	term_resize(width, height);

	resize_debug_log("TERM_RESIZE", "calling mainwindows_resize(%d, %d)", term_width, term_height);
	mainwindows_resize(term_width, term_height);
	term_resize_final(width, height);

	/* Force full redraw after resize */
	resize_debug_log("TERM_RESIZE", "calling irssi_redraw()");
	irssi_redraw();
	resize_debug_log("TERM_RESIZE", "term_resize_dirty complete");
}

int term_has_colors(void)
{
	return TRUE; /* ANSI terminals always have colors */
}

void term_force_colors(int set)
{
	term_use_colors = set;
}

void term_clear(void)
{
	term_set_color(root_window, ATTR_RESET);
	ansi_clear(ansi_term);
	term_move_reset(0, 0);

	memset(ansi_term->lines_empty, 1, term_height);
}

void term_beep(void)
{
	ansi_beep(ansi_term);
}

void term_draw_statusbar_separator(int y)
{
	/* Optional: draw horizontal separator */
	(void) y;
}

void term_set_reserved_lines(int top, int bottom)
{
	/* ANSI backend doesn't need explicit plane management */
	(void) top;
	(void) bottom;
}

/* ========================================================================
 * WINDOW MANAGEMENT
 * ======================================================================== */

TERM_WINDOW *term_window_create(int x, int y, int width, int height)
{
	TERM_WINDOW *window;

	window = g_new0(TERM_WINDOW, 1);
	window->x = x;
	window->y = y;
	window->width = width;
	window->height = height;

	return window;
}

void term_window_destroy(TERM_WINDOW *window)
{
	g_free(window);
}

void term_window_move(TERM_WINDOW *window, int x, int y, int width, int height)
{
	window->x = x;
	window->y = y;
	window->width = width;
	window->height = height;
}

TERM_WINDOW *term_window_create_statusbar(int height)
{
	return term_window_create(0, term_height - height, term_width, height);
}

void term_window_destroy_statusbar(TERM_WINDOW *window)
{
	term_window_destroy(window);
}

TERM_WINDOW *term_window_create_left_panel(int width)
{
	return term_window_create(0, 0, width, term_height);
}

void term_window_destroy_left_panel(TERM_WINDOW *window)
{
	term_window_destroy(window);
}

TERM_WINDOW *term_window_create_right_panel(int width)
{
	return term_window_create(term_width - width, 0, width, term_height);
}

void term_window_destroy_right_panel(TERM_WINDOW *window)
{
	term_window_destroy(window);
}

void term_window_clear(TERM_WINDOW *window)
{
	int y;

	ansi_set_normal(ansi_term);
	if (window->y == 0 && window->height == term_height && window->width == term_width) {
		term_clear();
	} else {
		for (y = 0; y < window->height; y++) {
			term_move(window, 0, y);
			term_clrtoeol(window);
		}
	}
}

void term_window_scroll(TERM_WINDOW *window, int count)
{
	int y;

	/* VT100 scroll regions affect entire rows - only safe when window
	 * spans full terminal width. Otherwise sidepanels get corrupted. */
	if (window->x != 0 || window->width != term_width)
		return;

	ansi_scroll(ansi_term, window->y, window->y + window->height - 1, count);
	term_move_reset(ansi_term->vcx, ansi_term->vcy);

	/* Set the newly scrolled area dirty */
	for (y = 0; (window->y + y) < term_height && y < window->height; y++)
		ansi_term->lines_empty[window->y + y] = FALSE;
}

/* ========================================================================
 * DRAWING FUNCTIONS
 * ======================================================================== */

#define COLOR_RESET UINT_MAX
#define COLOR_BLACK24 (COLOR_RESET - 1)

void term_set_color2(TERM_WINDOW *window, int col, unsigned int fgcol24, unsigned int bgcol24)
{
	ANSI_TERM *term = ansi_term;
	int set_normal;
	unsigned int fg, bg;

	if (col & ATTR_FGCOLOR24) {
		if (fgcol24)
			fg = fgcol24 << 8;
		else
			fg = COLOR_BLACK24;
	} else {
		fg = (col & FG_MASK);
	}

	if (col & ATTR_BGCOLOR24) {
		if (bgcol24)
			bg = bgcol24 << 8;
		else
			bg = COLOR_BLACK24;
	} else {
		bg = ((col & BG_MASK) >> BG_SHIFT);
	}

	if (!term_use_colors && bg > 0)
		col |= ATTR_REVERSE;

	set_normal = ((col & ATTR_RESETFG) && term->last_fg != COLOR_RESET) ||
	             ((col & ATTR_RESETBG) && term->last_bg != COLOR_RESET);

	if (((term->last_attrs & ATTR_BOLD) && (col & ATTR_BOLD) == 0) ||
	    ((term->last_attrs & ATTR_REVERSE) && (col & ATTR_REVERSE) == 0) ||
	    ((term->last_attrs & ATTR_BLINK) && (col & ATTR_BLINK) == 0)) {
		set_normal = TRUE;
	}

	if (set_normal) {
		term->last_fg = term->last_bg = COLOR_RESET;
		term->last_attrs = 0;
		ansi_set_normal(term);
	}

	/* Set foreground color */
	if (fg != term->last_fg && (fg != 0 || (col & ATTR_RESETFG) == 0)) {
		if (term_use_colors) {
			term->last_fg = fg;
			if (fg >> 8) {
				unsigned int rgb = (term->last_fg == COLOR_BLACK24) ? 0 : term->last_fg >> 8;
				ansi_set_fg_rgb(term, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
			} else {
				ansi_set_fg(term, term->last_fg);
			}
		}
	}

	/* Set background color */
	if (col & ATTR_BLINK)
		ansi_set_blink(term);

	if (bg != term->last_bg && (bg != 0 || (col & ATTR_RESETBG) == 0)) {
		if (term_use_colors) {
			term->last_bg = bg;
			if (bg >> 8) {
				unsigned int rgb = (term->last_bg == COLOR_BLACK24) ? 0 : term->last_bg >> 8;
				ansi_set_bg_rgb(term, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
			} else {
				ansi_set_bg(term, term->last_bg);
			}
		}
	}

	/* Reversed text */
	if (col & ATTR_REVERSE)
		ansi_set_reverse(term);

	/* Bold */
	if (col & ATTR_BOLD)
		ansi_set_bold(term);

	/* Underline */
	if (col & ATTR_UNDERLINE) {
		if ((term->last_attrs & ATTR_UNDERLINE) == 0)
			ansi_set_underline(term, TRUE);
	} else if (term->last_attrs & ATTR_UNDERLINE) {
		ansi_set_underline(term, FALSE);
	}

	/* Italic */
	if (col & ATTR_ITALIC) {
		if ((term->last_attrs & ATTR_ITALIC) == 0)
			ansi_set_italic(term, TRUE);
	} else if (term->last_attrs & ATTR_ITALIC) {
		ansi_set_italic(term, FALSE);
	}

	term->last_attrs = col & ~(BG_MASK | FG_MASK);
}

void term_move(TERM_WINDOW *window, int x, int y)
{
	ANSI_TERM *term = ansi_term;

	if (x >= 0 && y >= 0) {
		term->vcx = x + window->x;
		term->vcy = y + window->y;

		if (term->vcx >= term_width)
			term->vcx = term_width - 1;
		if (term->vcy >= term_height)
			term->vcy = term_height - 1;
	}
}

static void term_printed_text(int count)
{
	ANSI_TERM *term = ansi_term;

	term->lines_empty[term->vcy] = FALSE;

	term->vcx += count;
	while (term->vcx >= term_width) {
		term->vcx -= term_width;
		if (term->vcy < term_height - 1) term->vcy++;
		if (term->vcx > 0) term->lines_empty[term->vcy] = FALSE;
	}

	term->crealx += count;
	if (term->crealx >= term_width)
		term->cforcemove = TRUE;
}

void term_addch(TERM_WINDOW *window, char chr)
{
	ANSI_TERM *term = ansi_term;

	if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove)
		term_move_real();

	if (term_type != TERM_TYPE_UTF8 ||
	    (chr & 0x80) == 0 || (chr & 0x40) == 0) {
		term_printed_text(1);
	}

	fputc(chr, term->out);
}

static void term_addch_utf8(unichar chr)
{
	ANSI_TERM *term = ansi_term;
	char buf[10];
	int i, len;

	len = g_unichar_to_utf8(chr, buf);
	for (i = 0; i < len; i++)
		fputc(buf[i], term->out);
}

void term_add_unichar(TERM_WINDOW *window, unichar chr)
{
	ANSI_TERM *term = ansi_term;

	if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove)
		term_move_real();

	switch (term_type) {
	case TERM_TYPE_UTF8:
		term_printed_text(unichar_isprint(chr) ? unichar_width(chr) : 1);
		term_addch_utf8(chr);
		break;
	case TERM_TYPE_BIG5:
		if (chr > 0xff) {
			term_printed_text(2);
			fputc((chr >> 8) & 0xff, term->out);
		} else {
			term_printed_text(1);
		}
		fputc((chr & 0xff), term->out);
		break;
	default:
		term_printed_text(1);
		fputc(chr, term->out);
		break;
	}
}

int term_addstr(TERM_WINDOW *window, const char *str)
{
	ANSI_TERM *term = ansi_term;
	int len, raw_len;

	if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove)
		term_move_real();

	len = 0;
	raw_len = strlen(str);

	if (term_type == TERM_TYPE_UTF8) {
		len = string_width(str, TREAT_STRING_AS_UTF8);
	} else {
		len = raw_len;
	}

	term_printed_text(len);
	fwrite(str, 1, raw_len, term->out);

	return len;
}

void term_clrtoeol(TERM_WINDOW *window)
{
	ANSI_TERM *term = ansi_term;

	if (term->vcx < window->x) {
		term->vcx += window->x;
	}

	if (window->x + window->width < term_width) {
		/* Vertical split - fill with spaces to window boundary only */
		if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove)
			term_move_real();
		ansi_repeat(term, ' ', window->x + window->width - term->vcx);
		ansi_move(term, term->vcx, term->vcy);
		term->lines_empty[term->vcy] = FALSE;
	} else {
		/* Clear to end of line */
		if (term->last_fg == (unsigned int)-1 && term->last_bg == (unsigned int)-1 &&
		    (term->last_attrs & (ATTR_UNDERLINE | ATTR_REVERSE | ATTR_ITALIC)) == 0) {
			if (!term->lines_empty[term->vcy]) {
				if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove)
					term_move_real();
				ansi_clear_to_eol(term);
				if (term->vcx == 0) term->lines_empty[term->vcy] = TRUE;
			}
		} else if (term->vcx < term_width) {
			if (term->vcx != term->crealx || term->vcy != term->crealy || term->cforcemove)
				term_move_real();
			ansi_repeat(term, ' ', term_width - term->vcx);
			ansi_move(term, term->vcx, term->vcy);
			term->lines_empty[term->vcy] = FALSE;
		}
	}
}

void term_window_clrtoeol(TERM_WINDOW *window, int ypos)
{
	ANSI_TERM *term = ansi_term;

	if (ypos >= 0 && window->y + ypos != term->vcy) {
		return;
	}
	term_clrtoeol(window);
}

void term_window_clrtoeol_abs(TERM_WINDOW *window, int ypos_abs)
{
	term_window_clrtoeol(window, ypos_abs - window->y);
}

void term_move_cursor(int x, int y)
{
	curs_x = x;
	curs_y = y;
}

void term_refresh_freeze(void)
{
	ansi_term->freeze++;
}

void term_refresh_thaw(void)
{
	if (--ansi_term->freeze == 0)
		term_refresh(NULL);
}

void term_refresh(TERM_WINDOW *window)
{
	ANSI_TERM *term = ansi_term;

	if (term->freeze > 0)
		return;

	term_move(root_window, curs_x, curs_y);
	term_move_real();

	if (!term->curs_visible) {
		ansi_set_cursor_visible(term, TRUE);
		term->curs_visible = TRUE;
	}

	term_set_color(window, ATTR_RESET);
	fflush(term->out);
}

void term_stop(void)
{
	ANSI_TERM *term = ansi_term;

	ansi_mouse_disable(term);
	ansi_bracketed_paste(term, FALSE);
	ansi_set_normal(term);
	ansi_set_cursor_visible(term, TRUE);
	ansi_alt_screen(term, FALSE);
	fflush(term->out);

	raw_mode_disable(term);

	kill(getpid(), SIGTSTP);

	raw_mode_enable(term);
	ansi_alt_screen(term, TRUE);
	ansi_mouse_enable(term);
	ansi_bracketed_paste(term, TRUE);

	irssi_redraw();
}

void term_set_appkey_mode(int enable)
{
	ansi_appkey_mode(ansi_term, enable);
}

void term_set_bracketed_paste_mode(int enable)
{
	ansi_bracketed_paste(ansi_term, enable);
}

/* ========================================================================
 * INPUT HANDLING
 * ======================================================================== */

static int input_utf8(const unsigned char *buffer, int size, unichar *result)
{
	unichar c = g_utf8_get_char_validated((char *) buffer, size);

	if (c == (unichar)-2 && *buffer == 0 && size > 0)
		c = 0;

	switch (c) {
	case (unichar)-1:
		*result = *buffer;
		return 1;
	case (unichar)-2:
		return -1;
	default:
		*result = c;
		return g_utf8_skip[*buffer];
	}
}

static int input_big5(const unsigned char *buffer, int size, unichar *result)
{
	if (is_big5_hi(*buffer)) {
		if (size == 1)
			return -1;

		if (is_big5_los(buffer[1]) || is_big5_lox(buffer[1])) {
			*result = buffer[1] + ((int) *buffer << 8);
			return 2;
		}
	}

	*result = *buffer;
	return 1;
}

static int input_8bit(const unsigned char *buffer, int size, unichar *result)
{
	*result = *buffer;
	return 1;
}

void term_set_input_type(int type)
{
	switch (type) {
	case TERM_TYPE_UTF8:
		input_func = input_utf8;
		break;
	case TERM_TYPE_BIG5:
		input_func = input_big5;
		break;
	default:
		input_func = input_8bit;
	}
}

void term_gets(GArray *buffer, int *line_count)
{
	int ret, i, char_len;

	ret = read(STDIN_FILENO, inbuf + inbuf_pos, sizeof(inbuf) - inbuf_pos);
	if (ret == 0) {
		ret = -1;
	} else if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
		ret = 0;
	}

	if (ret == -1)
		signal_emit("command quit", 1, "Lost terminal");

	if (ret > 0) {
		inbuf_pos += ret;
		for (i = 0; i < inbuf_pos; ) {
			unichar key;
			char_len = input_func(inbuf + i, inbuf_pos - i, &key);
			if (char_len < 0)
				break;
			g_array_append_val(buffer, key);
			if (key == '\r' || key == '\n')
				(*line_count)++;

			i += char_len;
		}

		if (i >= inbuf_pos)
			inbuf_pos = 0;
		else if (i > 0) {
			memmove(inbuf, inbuf + i, inbuf_pos - i);
			inbuf_pos -= i;
		}
	}
}

void term_environment_check(void)
{
	/* No warnings for pure ANSI backend */
}
