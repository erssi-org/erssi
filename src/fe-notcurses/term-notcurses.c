/*
 term-notcurses.c : erssi notcurses terminal backend

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "module.h"
#include <irssi/src/core/signals.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/core/utf8.h>

#include "term-notcurses.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>

/* Input function type for character conversion */
typedef int (*TERM_INPUT_FUNC)(const unsigned char *buffer, int size, unichar *result);

/* Global context */
NC_CONTEXT *nc_ctx = NULL;
TERM_WINDOW *root_window;

/* Virtual cursor position */
static int vcx, vcy;
static int curs_x, curs_y;

/* Input handling */
static TERM_INPUT_FUNC input_func;
static unsigned char term_inbuf[512];
static int term_inbuf_pos;

/* Terminal I/O settings for raw mode */
static struct termios old_tio;
static struct termios tio;
static gboolean tio_saved = FALSE;

/* SIGCONT handling */
static GSource *sigcont_source;
static volatile sig_atomic_t got_sigcont;

/* SIGCONT handler */
static void sig_cont(int p)
{
	got_sigcont = TRUE;
}

static gboolean sigcont_prepare(GSource *source, gint *timeout)
{
	*timeout = -1;
	return got_sigcont;
}

static gboolean sigcont_check(GSource *source)
{
	return got_sigcont;
}

static gboolean sigcont_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
	got_sigcont = FALSE;
	if (callback == NULL)
		return TRUE;
	return callback(user_data);
}

static gboolean do_redraw(gpointer unused)
{
	/* Refresh notcurses after SIGCONT */
	if (nc_ctx && nc_ctx->nc) {
		notcurses_refresh(nc_ctx->nc, NULL, NULL);
	}
	irssi_redraw();
	return 1;
}

static GSourceFuncs sigcont_funcs = {
	.prepare = sigcont_prepare,
	.check = sigcont_check,
	.dispatch = sigcont_dispatch
};

static void term_atexit(void)
{
	term_deinit();
}

/* Set up terminal for raw input mode - disables special character handling */
static void terminfo_input_init0(void)
{
	tcgetattr(STDIN_FILENO, &old_tio);
	memcpy(&tio, &old_tio, sizeof(tio));

	tio.c_lflag &= ~(ICANON | ECHO); /* CBREAK, no ECHO */
	/* Disable ICRNL to disambiguate ^J and Enter, also disable
	 * software flow control to leave ^Q and ^S ready to be bound */
	tio.c_iflag &= ~(ICRNL | IXON | IXOFF);
	tio.c_cc[VMIN] = 1;  /* read() satisfied after 1 char */
	tio.c_cc[VTIME] = 0; /* No timer */

	/* Disable INTR, QUIT, VDSUSP and SUSP keys so Ctrl+C sends 0x03 character */
	tio.c_cc[VINTR] = _POSIX_VDISABLE;
	tio.c_cc[VQUIT] = _POSIX_VDISABLE;
#ifdef VDSUSP
	tio.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
#ifdef VSUSP
	tio.c_cc[VSUSP] = _POSIX_VDISABLE;
#endif

	tio_saved = TRUE;
}

static void terminfo_input_init(void)
{
	tcsetattr(STDIN_FILENO, TCSADRAIN, &tio);
}

static void terminfo_input_deinit(void)
{
	if (tio_saved)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &old_tio);
}

NC_CONTEXT *nc_context_init(void)
{
	NC_CONTEXT *ctx;
	struct notcurses_options opts = {0};

	ctx = g_new0(NC_CONTEXT, 1);

	/* Configure notcurses options for fast startup.
	 * DRAIN_INPUT tells notcurses we handle input ourselves - this may
	 * prevent it from enabling Kitty keyboard protocol. */
	opts.flags = NCOPTION_SUPPRESS_BANNERS |    /* No startup banner */
	             NCOPTION_PRESERVE_CURSOR |      /* Keep cursor visible */
	             NCOPTION_NO_FONT_CHANGES |      /* Skip font detection */
	             NCOPTION_NO_WINCH_SIGHANDLER |  /* We handle SIGWINCH via irssi */
	             NCOPTION_NO_QUIT_SIGHANDLERS |  /* We handle quit signals via irssi */
	             NCOPTION_DRAIN_INPUT;           /* We handle input ourselves */

#ifdef HAVE_IMAGE_PREVIEW
	/* Use full notcurses_init for multimedia/image support */
	ctx->nc = notcurses_init(&opts, NULL);
#else
	ctx->nc = notcurses_core_init(&opts, NULL);
#endif
	if (ctx->nc == NULL) {
		g_free(ctx);
		return NULL;
	}

	ctx->stdplane = notcurses_stdplane(ctx->nc);
	ctx->freeze_counter = 0;
	ctx->cursor_visible = TRUE;
	ctx->last_fg = ctx->last_bg = UINT_MAX;
	ctx->last_attrs = 0;

	return ctx;
}

void nc_context_deinit(NC_CONTEXT *ctx)
{
	if (ctx == NULL)
		return;

	if (ctx->nc != NULL) {
		notcurses_stop(ctx->nc);
		ctx->nc = NULL;
	}

	g_free(ctx);
}

int term_init(void)
{
	struct sigaction act;
	unsigned int width, height;

	/* Set up terminal raw mode first - this disables special character
	 * handling so Ctrl+C sends 0x03 instead of generating SIGINT */
	terminfo_input_init0();

	nc_ctx = nc_context_init();
	if (nc_ctx == NULL)
		return FALSE;

	/* Get terminal dimensions */
	ncplane_dim_yx(nc_ctx->stdplane, &height, &width);
	term_width = width;
	term_height = height;

	/* Apply terminal raw mode settings */
	terminfo_input_init();

	/* NOTE: Mouse tracking is NOT enabled here - it's handled by gui_mouse.c
	 * via gui_mouse_enable_tracking() called from sidepanels-core.c.
	 * This matches ncurses behavior and ensures consistent mouse handling. */

	/* Reset keyboard to traditional mode - we read raw stdin and need standard sequences.
	 * Different terminals use different keyboard enhancement protocols:
	 * - Kitty: CSI > flags u (push) / CSI < u (pop) / CSI = 0 u (reset to base)
	 * - XTMODKEYS: CSI > 4 ; 0 m (disable modifyOtherKeys)
	 * Send all reset sequences to ensure traditional keyboard input everywhere. */
	{
		static const char keyboard_reset[] =
			"\033[<u\033[<u\033[<u\033[<u"  /* Kitty: pop keyboard mode x4 */
			"\033[=0u"                       /* Kitty: reset to base mode */
			"\033[>4;0m"                     /* XTMODKEYS: disable modifyOtherKeys */
			"\033[>0m";                      /* XTMODKEYS: reset all */
		write(STDOUT_FILENO, keyboard_reset, sizeof(keyboard_reset) - 1);
	}

	/* Flush any pending terminal responses from notcurses capability detection.
	 * When using NCOPTION_DRAIN_INPUT, we handle input ourselves, but notcurses_init()
	 * may have sent terminal queries (e.g., Kitty graphics queries like Gi=1,a=q;).
	 * The responses would otherwise end up in our input buffer and leak into display.
	 * Small delay allows terminal to send responses before we flush. */
	{
		struct timespec delay = {0, 50000000};  /* 50ms - enough for terminal response */
		nanosleep(&delay, NULL);
		tcflush(STDIN_FILENO, TCIFLUSH);  /* Discard any unread input */
	}

	/* Grab CONT signal */
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_cont;
	sigaction(SIGCONT, &act, NULL);
	sigcont_source = g_source_new(&sigcont_funcs, sizeof(GSource));
	g_source_set_callback(sigcont_source, do_redraw, NULL, NULL);
	g_source_attach(sigcont_source, NULL);

	/* Initialize cursor position */
	vcx = vcy = 0;
	curs_x = curs_y = 0;

	/* Create root window covering entire terminal */
	root_window = term_window_create(0, 0, term_width, term_height);

	/* Set default input type */
	term_set_input_type(TERM_TYPE_8BIT);
	term_common_init();
	atexit(term_atexit);

	/* Colors are always available with notcurses */
	term_use_colors = TRUE;
	term_use_colors24 = TRUE;

	return TRUE;
}

void term_deinit(void)
{
	if (nc_ctx != NULL) {
		/* NOTE: Mouse tracking disable is handled by gui_mouse_deinit() */

		signal(SIGCONT, SIG_DFL);
		g_source_destroy(sigcont_source);
		g_source_unref(sigcont_source);

		term_common_deinit();

		if (root_window != NULL) {
			/* Don't destroy stdplane, it's owned by notcurses */
			g_free(root_window);
			root_window = NULL;
		}

		nc_context_deinit(nc_ctx);
		nc_ctx = NULL;
	}

	/* Restore terminal settings */
	terminfo_input_deinit();
}

void term_resize(int width, int height)
{
	unsigned int nc_height, nc_width;

	if (nc_ctx == NULL || nc_ctx->nc == NULL)
		return;

	/* Query actual terminal size from notcurses */
	if (width < 0 || height < 0) {
		ncplane_dim_yx(nc_ctx->stdplane, &nc_height, &nc_width);
		width = nc_width;
		height = nc_height;
	}

	if (term_width != width || term_height != height) {
		term_width = width;
		term_height = height;

		/* Update root window */
		if (root_window != NULL) {
			root_window->width = width;
			root_window->height = height;
		}
	}

	vcx = vcy = 0;
}

void term_resize_final(int width, int height)
{
	/* Tell notcurses to refresh after resize - this updates stdplane size */
	if (nc_ctx != NULL && nc_ctx->nc != NULL) {
		notcurses_refresh(nc_ctx->nc, NULL, NULL);
	}
}

int term_has_colors(void)
{
	return TRUE; /* notcurses always supports colors */
}

void term_force_colors(int set)
{
	term_use_colors = set;
}

void term_clear(void)
{
	if (nc_ctx == NULL || nc_ctx->stdplane == NULL)
		return;

	ncplane_erase(nc_ctx->stdplane);
	vcx = vcy = 0;
}

void term_beep(void)
{
	/* Send bell character */
	if (nc_ctx && nc_ctx->nc)
		fprintf(stderr, "\a");
}

TERM_WINDOW *term_window_create(int x, int y, int width, int height)
{
	TERM_WINDOW *window;

	window = g_new0(TERM_WINDOW, 1);
	window->x = x;
	window->y = y;
	window->width = width;
	window->height = height;

	/* For root window, use stdplane; for others, create child plane */
	if (x == 0 && y == 0 && width == term_width && height == term_height) {
		window->plane = nc_ctx->stdplane;
	} else {
		struct ncplane_options nopts = {
			.y = y,
			.x = x,
			.rows = height,
			.cols = width,
		};
		window->plane = ncplane_create(nc_ctx->stdplane, &nopts);
	}

	return window;
}

void term_window_destroy(TERM_WINDOW *window)
{
	if (window == NULL)
		return;

	/* Don't destroy stdplane */
	if (window->plane != NULL && window->plane != nc_ctx->stdplane) {
		ncplane_destroy(window->plane);
	}

	g_free(window);
}

void term_window_move(TERM_WINDOW *window, int x, int y, int width, int height)
{
	if (window == NULL)
		return;

	window->x = x;
	window->y = y;
	window->width = width;
	window->height = height;

	/* Move and resize plane if not stdplane */
	if (window->plane != NULL && window->plane != nc_ctx->stdplane) {
		ncplane_move_yx(window->plane, y, x);
		ncplane_resize_simple(window->plane, height, width);
	}
}

void term_window_clear(TERM_WINDOW *window)
{
	if (window == NULL)
		return;

	if (window->plane != NULL) {
		ncplane_erase(window->plane);
	} else {
		/* No plane - clear region on stdplane */
		for (int row = 0; row < window->height; row++) {
			ncplane_cursor_move_yx(nc_ctx->stdplane, window->y + row, window->x);
			for (int col = 0; col < window->width; col++) {
				ncplane_putchar(nc_ctx->stdplane, ' ');
			}
		}
	}
}

void term_window_scroll(TERM_WINDOW *window, int count)
{
	int y;

	if (window == NULL)
		return;

	/* notcurses doesn't have native scroll regions like terminfo,
	 * so we need to redraw. For now, just mark the area as needing
	 * a refresh - the higher level code will handle the actual content. */
	(void)count;
	(void)y;

	/* TODO: Implement proper scrolling using ncplane content manipulation
	 * or ncplane_scrollup() when available */
}

/* irssi to ANSI color mapping (irssi uses different color order for 0-15) */
static const unsigned char irssi_to_ansi[16] = {
	0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15
};

void term_set_color2(TERM_WINDOW *window, int col, unsigned int fgcol24, unsigned int bgcol24)
{
	uint64_t channels = 0;
	uint16_t styles = 0;
	struct ncplane *plane;

	if (nc_ctx == NULL)
		return;

	if (window && window->plane)
		plane = window->plane;
	else
		plane = nc_ctx->stdplane;

	/* Handle foreground color */
	if (col & ATTR_RESETFG) {
		/* Reset to default foreground */
		ncchannels_set_fg_default(&channels);
	} else if (col & ATTR_FGCOLOR24) {
		/* 24-bit RGB foreground */
		unsigned int r, g, b;
		if (fgcol24 == 0) {
			/* Black requested via 24-bit */
			r = g = b = 0;
		} else {
			r = (fgcol24 >> 16) & 0xFF;
			g = (fgcol24 >> 8) & 0xFF;
			b = fgcol24 & 0xFF;
		}
		ncchannels_set_fg_rgb8(&channels, r, g, b);
	} else {
		/* Palette color (0-255) - apply irssi->ANSI mapping for 0-15 */
		int fg = col & FG_MASK;
		if (fg < 16)
			fg = irssi_to_ansi[fg];
		ncchannels_set_fg_palindex(&channels, fg);
	}

	/* Handle background color */
	if (col & ATTR_RESETBG) {
		/* Reset to default background */
		ncchannels_set_bg_default(&channels);
	} else if (col & ATTR_BGCOLOR24) {
		/* 24-bit RGB background */
		unsigned int r, g, b;
		if (bgcol24 == 0) {
			/* Black requested via 24-bit */
			r = g = b = 0;
		} else {
			r = (bgcol24 >> 16) & 0xFF;
			g = (bgcol24 >> 8) & 0xFF;
			b = bgcol24 & 0xFF;
		}
		ncchannels_set_bg_rgb8(&channels, r, g, b);
	} else {
		/* Palette color (0-255) - apply irssi->ANSI mapping for 0-15 */
		int bg = (col & BG_MASK) >> BG_SHIFT;
		if (bg < 16)
			bg = irssi_to_ansi[bg];
		ncchannels_set_bg_palindex(&channels, bg);
	}

	/* Handle text attributes */
	if (col & ATTR_BOLD)
		styles |= NCSTYLE_BOLD;
	if (col & ATTR_UNDERLINE)
		styles |= NCSTYLE_UNDERLINE;
	if (col & ATTR_ITALIC)
		styles |= NCSTYLE_ITALIC;

	/* Handle ATTR_REVERSE by swapping fg/bg colors */
	if (col & ATTR_REVERSE) {
		uint64_t swapped = 0;
		uint32_t fg_color, bg_color;
		unsigned fg_alpha, bg_alpha;
		int fg_default, bg_default;

		/* Extract current fg/bg from channels */
		fg_color = ncchannels_fg_rgb(channels);
		bg_color = ncchannels_bg_rgb(channels);
		fg_alpha = ncchannels_fg_alpha(channels);
		bg_alpha = ncchannels_bg_alpha(channels);
		fg_default = ncchannels_fg_default_p(channels);
		bg_default = ncchannels_bg_default_p(channels);

		/* Swap: fg becomes bg, bg becomes fg */
		if (bg_default) {
			ncchannels_set_fg_default(&swapped);
		} else {
			ncchannels_set_fg_rgb(&swapped, bg_color);
		}
		if (fg_default) {
			ncchannels_set_bg_default(&swapped);
		} else {
			ncchannels_set_bg_rgb(&swapped, fg_color);
		}
		ncchannels_set_fg_alpha(&swapped, bg_alpha);
		ncchannels_set_bg_alpha(&swapped, fg_alpha);
		channels = swapped;
	}

	/* Apply to plane */
	ncplane_set_channels(plane, channels);
	ncplane_set_styles(plane, styles);

	/* Cache for optimization */
	nc_ctx->current_channels = channels;
	nc_ctx->current_style = styles;
}

void term_move(TERM_WINDOW *window, int x, int y)
{
	struct ncplane *plane;

	if (x < 0 || y < 0)
		return;

	/* Convert to absolute coordinates */
	vcx = x + (window ? window->x : 0);
	vcy = y + (window ? window->y : 0);

	if (vcx >= term_width)
		vcx = term_width - 1;
	if (vcy >= term_height)
		vcy = term_height - 1;

	/* Move cursor on the appropriate plane */
	plane = (window && window->plane) ? window->plane : nc_ctx->stdplane;
	if (window && window->plane && window->plane != nc_ctx->stdplane) {
		/* For child planes, use relative coordinates */
		ncplane_cursor_move_yx(plane, y, x);
	} else {
		/* For stdplane, use absolute coordinates */
		ncplane_cursor_move_yx(plane, vcy, vcx);
	}
}

void term_addch(TERM_WINDOW *window, char chr)
{
	struct ncplane *plane;

	if (nc_ctx == NULL)
		return;

	plane = (window && window->plane) ? window->plane : nc_ctx->stdplane;
	ncplane_putchar(plane, chr);
	vcx++;
}

void term_add_unichar(TERM_WINDOW *window, unichar chr)
{
	struct ncplane *plane;
	char buf[8];
	int len;

	if (nc_ctx == NULL)
		return;

	plane = (window && window->plane) ? window->plane : nc_ctx->stdplane;

	/* Convert unichar to UTF-8 and output */
	len = g_unichar_to_utf8(chr, buf);
	buf[len] = '\0';

	ncplane_putstr(plane, buf);
	vcx += unichar_isprint(chr) ? unichar_width(chr) : 1;
}

int term_addstr(TERM_WINDOW *window, const char *str)
{
	struct ncplane *plane;
	int len;

	if (nc_ctx == NULL || str == NULL)
		return 0;

	plane = (window && window->plane) ? window->plane : nc_ctx->stdplane;

	/* Calculate display width */
	if (term_type == TERM_TYPE_UTF8) {
		len = string_width(str, TREAT_STRING_AS_UTF8);
	} else {
		len = strlen(str);
	}

	ncplane_putstr(plane, str);
	vcx += len;

	return len;
}

void term_clrtoeol(TERM_WINDOW *window)
{
	struct ncplane *plane;
	int start_x, end_x;
	unsigned int cur_y, cur_x;

	if (nc_ctx == NULL)
		return;

	plane = (window && window->plane) ? window->plane : nc_ctx->stdplane;

	/* Get current cursor position */
	ncplane_cursor_yx(plane, &cur_y, &cur_x);

	/* Calculate clear range */
	if (window) {
		start_x = cur_x;
		end_x = (window->plane == nc_ctx->stdplane) ?
		        window->x + window->width : window->width;
	} else {
		start_x = cur_x;
		end_x = term_width;
	}

	/* Clear from cursor to end of line/window */
	for (int x = start_x; x < end_x; x++) {
		ncplane_putchar(plane, ' ');
	}

	/* Restore cursor position */
	ncplane_cursor_move_yx(plane, cur_y, cur_x);
}

void term_window_clrtoeol(TERM_WINDOW *window, int ypos)
{
	if (ypos >= 0 && window && window->y + ypos != vcy) {
		return;
	}
	term_clrtoeol(window);
}

void term_window_clrtoeol_abs(TERM_WINDOW *window, int ypos)
{
	term_window_clrtoeol(window, ypos - (window ? window->y : 0));
}

void term_move_cursor(int x, int y)
{
	curs_x = x;
	curs_y = y;
}

/* Helper to disable Kitty keyboard protocol after render */
static void disable_kitty_kbd(void)
{
	static const char disable_seq[] = "\033[<u";
	write(STDOUT_FILENO, disable_seq, sizeof(disable_seq) - 1);
}

void term_refresh(TERM_WINDOW *window)
{
	if (nc_ctx == NULL || nc_ctx->freeze_counter > 0)
		return;

	/* Move cursor to final position */
	ncplane_cursor_move_yx(nc_ctx->stdplane, curs_y, curs_x);

	/* Enable cursor visibility at the input position */
	if (nc_ctx->cursor_visible)
		notcurses_cursor_enable(nc_ctx->nc, curs_y, curs_x);

	/* Render all planes */
	notcurses_render(nc_ctx->nc);

	/* Disable Kitty keyboard protocol after render (notcurses may re-enable it) */
	disable_kitty_kbd();
}

void term_refresh_freeze(void)
{
	if (nc_ctx)
		nc_ctx->freeze_counter++;
}

void term_refresh_thaw(void)
{
	if (nc_ctx && --nc_ctx->freeze_counter == 0)
		term_refresh(NULL);
}

void term_stop(void)
{
	struct notcurses_options opts;
	static const char keyboard_reset[] =
		"\033[<u\033[<u\033[<u\033[<u"  /* Kitty: pop keyboard mode x4 */
		"\033[=0u"                       /* Kitty: reset to base mode */
		"\033[>4;0m"                     /* XTMODKEYS: disable modifyOtherKeys */
		"\033[>0m";                      /* XTMODKEYS: reset all */

	if (nc_ctx && nc_ctx->nc) {
		/* Restore terminal settings before suspending */
		terminfo_input_deinit();
		notcurses_stop(nc_ctx->nc);
		kill(getpid(), SIGTSTP);
		/* Reinitialize after resume with same fast-startup flags */
		memset(&opts, 0, sizeof(opts));
		opts.flags = NCOPTION_SUPPRESS_BANNERS |
		             NCOPTION_PRESERVE_CURSOR |
		             NCOPTION_NO_FONT_CHANGES |
		             NCOPTION_NO_WINCH_SIGHANDLER |
		             NCOPTION_NO_QUIT_SIGHANDLERS |
		             NCOPTION_DRAIN_INPUT;
#ifdef HAVE_IMAGE_PREVIEW
		nc_ctx->nc = notcurses_init(&opts, NULL);
#else
		nc_ctx->nc = notcurses_core_init(&opts, NULL);
#endif
		if (nc_ctx->nc) {
			nc_ctx->stdplane = notcurses_stdplane(nc_ctx->nc);
			/* Reset keyboard to traditional mode after reinit */
			write(STDOUT_FILENO, keyboard_reset, sizeof(keyboard_reset) - 1);
			/* Re-apply terminal raw mode settings */
			terminfo_input_init();
		}
		irssi_redraw();
	}
}

void term_set_appkey_mode(int enable)
{
	/* Application keypad mode - notcurses handles this internally */
	/* No explicit action needed as notcurses manages keypad modes */
	(void)enable;
}

void term_set_bracketed_paste_mode(int enable)
{
	/* Bracketed paste mode - notcurses handles this internally */
	/* No explicit action needed as notcurses manages paste modes */
	(void)enable;
}

/* Input handling functions */
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

/* Input: read raw bytes from stdin (hybrid approach - notcurses for output only) */
void term_gets(GArray *buffer, int *line_count)
{
	int ret, i, char_len;

	/* Read raw bytes from stdin - notcurses sets up terminal but we handle input */
	ret = read(STDIN_FILENO, term_inbuf + term_inbuf_pos,
	           sizeof(term_inbuf) - term_inbuf_pos);

	if (ret == 0) {
		/* EOF - terminal got lost */
		ret = -1;
	} else if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
		ret = 0;
	}

	if (ret == -1)
		signal_emit("command quit", 1, "Lost terminal");

	if (ret > 0) {
		/* Convert input bytes to unicode characters */
		term_inbuf_pos += ret;
		for (i = 0; i < term_inbuf_pos; ) {
			unichar key;
			char_len = input_func(term_inbuf + i, term_inbuf_pos - i, &key);
			if (char_len < 0)
				break;
			g_array_append_val(buffer, key);
			if (key == '\r' || key == '\n')
				(*line_count)++;
			i += char_len;
		}

		if (i >= term_inbuf_pos)
			term_inbuf_pos = 0;
		else if (i > 0) {
			memmove(term_inbuf, term_inbuf + i, term_inbuf_pos - i);
			term_inbuf_pos -= i;
		}
	}
}

void term_environment_check(void)
{
	/* notcurses handles terminal detection automatically */
}
