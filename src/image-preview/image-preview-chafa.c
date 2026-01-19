/*
 image-preview-chafa.c : Image rendering using Chafa library

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "image-preview.h"
#include "src/fe-text/mainwindows.h"
#include "src/fe-text/term.h"

#include <irssi/src/core/settings.h>

#ifdef HAVE_CHAFA
#include <chafa.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* STB image for loading - single header library */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

/* Popup state */
static gboolean popup_showing = FALSE;
static int popup_x = 0, popup_y = 0;
static int popup_width = 0, popup_height = 0;
static GString *popup_content = NULL;

#ifdef HAVE_CHAFA

/* Terminal type detected by query or environment */
typedef enum {
	TERM_UNKNOWN = 0,
	/* iTerm2 protocol */
	TERM_ITERM2,
	/* Kitty graphics protocol */
	TERM_KITTY,
	TERM_GHOSTTY,
	TERM_WEZTERM,
	TERM_RIO,
	/* Sixel protocol */
	TERM_XTERM,
	TERM_FOOT,
	TERM_CONTOUR,
	TERM_KONSOLE,
	TERM_MINTTY,
	TERM_MLTERM,
	TERM_WINDOWS_TERMINAL
} DetectedTerminal;

/* Cached terminal detection result */
static DetectedTerminal cached_terminal = TERM_UNKNOWN;
static gboolean terminal_detected = FALSE;

/*
 * Query terminal type via tmux's client_termname variable.
 * Uses 'tmux display-message -p' which knows the real outer terminal.
 * Returns detected terminal type or TERM_UNKNOWN on failure.
 */
static DetectedTerminal query_terminal_type(void)
{
	FILE *fp;
	char buf[256];
	DetectedTerminal result = TERM_UNKNOWN;

	/* Return cached result if already detected */
	if (terminal_detected) {
		return cached_terminal;
	}

	image_preview_debug_print("QUERY: Querying tmux for client terminal");

	/* Query tmux for client's terminal name */
	fp = popen("tmux display-message -p '#{client_termname}' 2>/dev/null", "r");
	if (fp == NULL) {
		image_preview_debug_print("QUERY: Failed to run tmux command");
		goto cache_result;
	}

	if (fgets(buf, sizeof(buf), fp) != NULL) {
		/* Remove trailing newline */
		size_t len = strlen(buf);
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		image_preview_debug_print("QUERY: tmux client_termname: %s", buf);

		/* Match terminal name to type */
		if (strcasestr(buf, "iterm") != NULL) {
			result = TERM_ITERM2;
			image_preview_debug_print("QUERY: Detected iTerm2");
		} else if (strcasestr(buf, "kitty") != NULL) {
			result = TERM_KITTY;
			image_preview_debug_print("QUERY: Detected Kitty");
		} else if (strcasestr(buf, "ghostty") != NULL) {
			result = TERM_GHOSTTY;
			image_preview_debug_print("QUERY: Detected Ghostty");
		} else if (strcasestr(buf, "wezterm") != NULL) {
			result = TERM_WEZTERM;
			image_preview_debug_print("QUERY: Detected WezTerm");
		} else if (strcasestr(buf, "rio") != NULL) {
			result = TERM_RIO;
			image_preview_debug_print("QUERY: Detected Rio");
		} else if (strcasestr(buf, "foot") != NULL) {
			result = TERM_FOOT;
			image_preview_debug_print("QUERY: Detected foot");
		} else if (strcasestr(buf, "contour") != NULL) {
			result = TERM_CONTOUR;
			image_preview_debug_print("QUERY: Detected Contour");
		} else if (strcasestr(buf, "konsole") != NULL) {
			result = TERM_KONSOLE;
			image_preview_debug_print("QUERY: Detected Konsole");
		} else if (strcasestr(buf, "mintty") != NULL) {
			result = TERM_MINTTY;
			image_preview_debug_print("QUERY: Detected mintty");
		} else if (strcasestr(buf, "mlterm") != NULL) {
			result = TERM_MLTERM;
			image_preview_debug_print("QUERY: Detected mlterm");
		} else if (strcasestr(buf, "xterm") != NULL) {
			result = TERM_XTERM;
			image_preview_debug_print("QUERY: Detected xterm");
		} else {
			image_preview_debug_print("QUERY: Unknown terminal: %s", buf);
		}
	}
	pclose(fp);

cache_result:
	/* Cache the result */
	cached_terminal = result;
	terminal_detected = TRUE;

	return result;
}

/* Detect best pixel mode based on terminal */
static ChafaPixelMode detect_pixel_mode(void)
{
	const char *env_term_program, *env_kitty_pid, *env_ghostty;
	const char *env_term, *env_tmux, *env_wt_session;
	DetectedTerminal queried;

	/* Check if we're in tmux - if so, query the real terminal */
	env_tmux = g_getenv("TMUX");
	if (env_tmux && *env_tmux) {
		image_preview_debug_print("CHAFA: In tmux, querying real terminal");
		queried = query_terminal_type();

		if (queried != TERM_UNKNOWN) {
			switch (queried) {
			/* iTerm2 protocol */
			case TERM_ITERM2:
				image_preview_debug_print("CHAFA: Using iTerm2 mode (queried)");
				return CHAFA_PIXEL_MODE_ITERM2;
			/* Kitty graphics protocol */
			case TERM_KITTY:
			case TERM_GHOSTTY:
			case TERM_WEZTERM:
			case TERM_RIO:
				image_preview_debug_print("CHAFA: Using Kitty mode (queried)");
				return CHAFA_PIXEL_MODE_KITTY;
			/* Sixel protocol */
			case TERM_FOOT:
			case TERM_XTERM:
			case TERM_CONTOUR:
			case TERM_KONSOLE:
			case TERM_MINTTY:
			case TERM_MLTERM:
			case TERM_WINDOWS_TERMINAL:
				image_preview_debug_print("CHAFA: Using Sixel mode (queried)");
				return CHAFA_PIXEL_MODE_SIXELS;
			default:
				break;
			}
		}
		/* Query failed or unknown - fall through to env detection */
		image_preview_debug_print("CHAFA: Query failed, falling back to env vars");
	}

	/* Fallback to environment variable detection (for non-tmux or query failure) */
	env_term_program = g_getenv("TERM_PROGRAM");
	env_kitty_pid = g_getenv("KITTY_PID");
	env_ghostty = g_getenv("GHOSTTY_RESOURCES_DIR");
	env_term = g_getenv("TERM");
	env_wt_session = g_getenv("WT_SESSION");

	/* Windows Terminal (doesn't respond to XTVERSION, detect via WT_SESSION) */
	if (env_wt_session && *env_wt_session) {
		image_preview_debug_print("CHAFA: Detected Windows Terminal (env WT_SESSION)");
		return CHAFA_PIXEL_MODE_SIXELS;
	}

	/* Kitty */
	if (env_kitty_pid && *env_kitty_pid) {
		image_preview_debug_print("CHAFA: Detected Kitty terminal (env)");
		return CHAFA_PIXEL_MODE_KITTY;
	}

	/* Ghostty (uses Kitty protocol) */
	if (env_ghostty && *env_ghostty) {
		image_preview_debug_print("CHAFA: Detected Ghostty terminal (env)");
		return CHAFA_PIXEL_MODE_KITTY;
	}

	/* WezTerm */
	if (env_term_program && g_strcmp0(env_term_program, "WezTerm") == 0) {
		image_preview_debug_print("CHAFA: Detected WezTerm terminal (env)");
		return CHAFA_PIXEL_MODE_KITTY;
	}

	/* iTerm2 */
	if (env_term_program && g_strcmp0(env_term_program, "iTerm.app") == 0) {
		image_preview_debug_print("CHAFA: Detected iTerm2 terminal (env)");
		return CHAFA_PIXEL_MODE_ITERM2;
	}

	/* mintty (Cygwin/MSYS2/WSL terminal) */
	if (env_term_program && g_strcmp0(env_term_program, "mintty") == 0) {
		image_preview_debug_print("CHAFA: Detected mintty terminal (env)");
		return CHAFA_PIXEL_MODE_SIXELS;
	}

	/* xterm/foot/mlterm/contour - try sixel */
	if (env_term && (g_str_has_prefix(env_term, "xterm") ||
	                 g_str_has_prefix(env_term, "foot") ||
	                 g_str_has_prefix(env_term, "mlterm") ||
	                 g_str_has_prefix(env_term, "contour"))) {
		image_preview_debug_print("CHAFA: Detected sixel-capable terminal (env)");
		return CHAFA_PIXEL_MODE_SIXELS;
	}

	/* Fallback to symbols */
	image_preview_debug_print("CHAFA: Using symbol fallback mode");
	return CHAFA_PIXEL_MODE_SYMBOLS;
}

/* Parse blitter setting */
static ChafaPixelMode parse_blitter_setting(void)
{
	const char *blitter_str;

	blitter_str = settings_get_str(IMAGE_PREVIEW_BLITTER);
	if (blitter_str == NULL || g_strcmp0(blitter_str, "auto") == 0) {
		return detect_pixel_mode();
	}

	if (g_strcmp0(blitter_str, "kitty") == 0)
		return CHAFA_PIXEL_MODE_KITTY;
	if (g_strcmp0(blitter_str, "iterm2") == 0)
		return CHAFA_PIXEL_MODE_ITERM2;
	if (g_strcmp0(blitter_str, "sixel") == 0)
		return CHAFA_PIXEL_MODE_SIXELS;
	if (g_strcmp0(blitter_str, "symbols") == 0)
		return CHAFA_PIXEL_MODE_SYMBOLS;

	/* Unknown - use auto */
	return detect_pixel_mode();
}

#endif /* HAVE_CHAFA */

/*
 * Render an image file using Chafa
 * Returns GString with escape sequences, caller must free with g_string_free()
 * out_rows is set to the number of terminal rows the image will occupy
 */
GString *image_render_chafa(const char *image_path,
                            int max_cols,
                            int max_rows,
                            int *out_rows)
{
#ifdef HAVE_CHAFA
	ChafaTermDb *term_db = NULL;
	ChafaCanvasConfig *config = NULL;
	ChafaCanvas *canvas = NULL;
	ChafaTermInfo *term_info = NULL;
	ChafaPixelMode pixel_mode;
	GString *output = NULL;
	unsigned char *pixels = NULL;
	int img_width, img_height, img_channels;
	int target_cols, target_rows;
	float aspect_ratio;

	if (image_path == NULL) {
		image_preview_debug_print("CHAFA: NULL image path");
		return NULL;
	}

	image_preview_debug_print("CHAFA: Rendering %s (max %dx%d)",
	                          image_path, max_cols, max_rows);

	/* Load image using stb_image */
	pixels = stbi_load(image_path, &img_width, &img_height, &img_channels, 4);
	if (pixels == NULL) {
		image_preview_debug_print("CHAFA: Failed to load image: %s",
		                          stbi_failure_reason());
		return NULL;
	}

	image_preview_debug_print("CHAFA: Image loaded: %dx%d, %d channels",
	                          img_width, img_height, img_channels);

	/* Calculate target dimensions preserving aspect ratio */
	aspect_ratio = (float)img_width / (float)img_height;
	/* Terminal cells are ~2:1 aspect (height:width), so adjust */
	aspect_ratio *= 2.0f;

	if (aspect_ratio > (float)max_cols / (float)max_rows) {
		/* Width limited */
		target_cols = max_cols;
		target_rows = (int)((float)max_cols / aspect_ratio);
	} else {
		/* Height limited */
		target_rows = max_rows;
		target_cols = (int)((float)max_rows * aspect_ratio);
	}

	if (target_cols < 1) target_cols = 1;
	if (target_rows < 1) target_rows = 1;

	image_preview_debug_print("CHAFA: Target size: %dx%d cells", target_cols, target_rows);

	/* Get pixel mode from our detection (uses terminal query in tmux) */
	pixel_mode = parse_blitter_setting();
	image_preview_debug_print("CHAFA: Using pixel mode: %d (%s)", pixel_mode,
	                          pixel_mode == CHAFA_PIXEL_MODE_ITERM2 ? "iTerm2" :
	                          pixel_mode == CHAFA_PIXEL_MODE_KITTY ? "Kitty" :
	                          pixel_mode == CHAFA_PIXEL_MODE_SIXELS ? "Sixel" : "Symbols");

	/* Create term_info with appropriate escape sequences for our detected terminal.
	 * We can't rely on Chafa's env-based detection in tmux because env vars show
	 * the terminal where tmux was started, not the current terminal.
	 * Solution: Manually set the graphics protocol sequences based on our detection. */
	term_db = chafa_term_db_get_default();
	term_info = chafa_term_info_new();

	/* Supplement with default sequences first (cursor movement, colors, etc.) */
	chafa_term_info_supplement(term_info, chafa_term_db_get_fallback_info(term_db));

	/* Now manually set the graphics protocol sequences based on detected pixel mode.
	 * These sequences are from Chafa source code - no env detection needed. */
	if (pixel_mode == CHAFA_PIXEL_MODE_ITERM2) {
		/* iTerm2 inline image protocol:
		 * ESC ] 1337 ; File = inline=1;width=W;height=H;preserveAspectRatio=0 : base64 BEL */
		GError *err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_ITERM2_IMAGE,
		                        "\033]1337;File=inline=1;width=%1;height=%2;preserveAspectRatio=0:",
		                        &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set BEGIN_ITERM2: %s", err->message);
			g_error_free(err);
		}
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_ITERM2_IMAGE, "\a", &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set END_ITERM2: %s", err->message);
			g_error_free(err);
		}
		image_preview_debug_print("CHAFA: Set iTerm2 sequences directly");
	} else if (pixel_mode == CHAFA_PIXEL_MODE_KITTY) {
		/* Kitty graphics protocol:
		 * ESC _ G a=T,f=BPP,s=W,v=H,c=COLS,r=ROWS,m=1 ESC \ ... ESC _ G m=0 ESC \ */
		GError *err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_KITTY_IMMEDIATE_IMAGE_V1,
		                        "\033_Ga=T,f=%1,s=%2,v=%3,c=%4,r=%5,m=1\033\\",
		                        &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set BEGIN_KITTY: %s", err->message);
			g_error_free(err);
		}
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_KITTY_IMAGE, "\033_Gm=0\033\\", &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set END_KITTY: %s", err->message);
			g_error_free(err);
		}
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_KITTY_IMAGE_CHUNK, "\033_Gm=1;", &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set CHUNK_BEGIN: %s", err->message);
			g_error_free(err);
		}
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_KITTY_IMAGE_CHUNK, "\033\\", &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set CHUNK_END: %s", err->message);
			g_error_free(err);
		}
		image_preview_debug_print("CHAFA: Set Kitty sequences directly");
	} else if (pixel_mode == CHAFA_PIXEL_MODE_SIXELS) {
		/* Sixel graphics protocol:
		 * ESC P p1;p2;p3 q <sixel_data> ESC \ */
		GError *err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_SIXELS,
		                        "\033P%1;%2;%3q", &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set BEGIN_SIXELS: %s", err->message);
			g_error_free(err);
		}
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_SIXELS, "\033\\", &err);
		if (err) {
			image_preview_debug_print("CHAFA: Failed to set END_SIXELS: %s", err->message);
			g_error_free(err);
		}
		image_preview_debug_print("CHAFA: Set Sixel sequences directly");
	}

	/* Create canvas config */
	config = chafa_canvas_config_new();
	chafa_canvas_config_set_geometry(config, target_cols, target_rows);
	chafa_canvas_config_set_pixel_mode(config, pixel_mode);

	/* Set canvas mode based on terminal colors */
	chafa_canvas_config_set_canvas_mode(config, CHAFA_CANVAS_MODE_TRUECOLOR);

	/* Create canvas */
	canvas = chafa_canvas_new(config);
	if (canvas == NULL) {
		image_preview_debug_print("CHAFA: Failed to create canvas");
		stbi_image_free(pixels);
		chafa_canvas_config_unref(config);
		chafa_term_info_unref(term_info);
		return NULL;
	}

	/* Draw image pixels */
	chafa_canvas_draw_all_pixels(canvas,
	                             CHAFA_PIXEL_RGBA8_UNASSOCIATED,
	                             pixels,
	                             img_width, img_height,
	                             img_width * 4);

	/* Print canvas to string using detected term_info */
	output = chafa_canvas_print(canvas, term_info);

	if (output != NULL && output->len > 0) {
		/* Log first 80 bytes as hex for debugging escape sequences */
		GString *hex = g_string_new(NULL);
		size_t i, limit = output->len < 80 ? output->len : 80;
		for (i = 0; i < limit; i++) {
			unsigned char c = (unsigned char)output->str[i];
			if (c == 0x1b)
				g_string_append(hex, "<ESC>");
			else if (c >= 32 && c < 127)
				g_string_append_c(hex, c);
			else
				g_string_append_printf(hex, "<%02x>", c);
		}
		image_preview_debug_print("CHAFA: Rendered %zu bytes, first: %s", output->len, hex->str);
		g_string_free(hex, TRUE);
	} else {
		image_preview_debug_print("CHAFA: Rendered %zu bytes", output ? output->len : 0);
	}

	/* Set output rows */
	if (out_rows != NULL)
		*out_rows = target_rows;

	/* Cleanup */
	stbi_image_free(pixels);
	chafa_term_info_unref(term_info);
	chafa_canvas_unref(canvas);
	chafa_canvas_config_unref(config);

	return output;

#else /* !HAVE_CHAFA */
	(void)image_path;
	(void)max_cols;
	(void)max_rows;
	if (out_rows != NULL)
		*out_rows = 0;
	image_preview_debug_print("CHAFA: Not compiled with Chafa support");
	return NULL;
#endif
}

/*
 * Show popup preview at given position
 */
void image_render_popup(const char *image_path, int x, int y)
{
	int rows = 0;
	int max_width, max_height;

	if (image_path == NULL)
		return;

	/* Get settings */
	max_width = settings_get_int(IMAGE_PREVIEW_MAX_WIDTH);
	max_height = settings_get_int(IMAGE_PREVIEW_MAX_HEIGHT);

	if (max_width <= 0) max_width = IMAGE_PREVIEW_DEFAULT_MAX_WIDTH;
	if (max_height <= 0) max_height = IMAGE_PREVIEW_DEFAULT_MAX_HEIGHT;

	/* Close any existing popup */
	image_render_popup_close();

	/* Render the image */
	popup_content = image_render_chafa(image_path, max_width, max_height, &rows);
	if (popup_content == NULL) {
		image_preview_debug_print("POPUP: Failed to render image");
		return;
	}

	popup_showing = TRUE;
	popup_x = x;
	popup_y = y;
	popup_width = max_width;
	popup_height = rows;

	image_preview_debug_print("POPUP: Showing at %d,%d size %dx%d",
	                          x, y, max_width, rows);

	/* Output will be written by the frontend's refresh cycle */
}

/*
 * Clear graphics from screen based on terminal protocol:
 *
 * Kitty (Ghostty, Kitty, WezTerm): Graphics are rendered in a separate layer
 * on top of text. Send ESC_Ga=d to delete all images - text underneath is
 * preserved and becomes visible immediately. No redraw needed.
 *
 * iTerm2/Sixel/Symbols: Graphics replace text in the terminal buffer (though
 * text remains selectable underneath in iTerm2). No terminal command exists
 * to "delete" images - instead, redraw the mainwindow area to overwrite the
 * image with original text content.
 */
void image_render_clear_graphics(void)
{
#ifdef HAVE_CHAFA
	ChafaPixelMode pixel_mode;
	const char *tmux_start = "";
	const char *tmux_end = "";
	const char *env_tmux;

	pixel_mode = parse_blitter_setting();

	/* Check for tmux passthrough */
	env_tmux = g_getenv("TMUX");
	if (env_tmux && *env_tmux) {
		tmux_start = "\033Ptmux;\033";
		tmux_end = "\033\\";
	}

	if (pixel_mode == CHAFA_PIXEL_MODE_KITTY) {
		/* Kitty graphics protocol: delete all images from graphics layer.
		 * ESC _ G a=d ESC \  (a=d = action:delete, no args = all images) */
		image_preview_debug_print("CLEAR: Kitty - sending delete-all sequence");
		fprintf(stdout, "%s\033_Ga=d\033\033\\%s", tmux_start, tmux_end);
		fflush(stdout);
	} else {
		/* iTerm2/Sixel/Symbols: redraw mainwindow to overwrite image with text.
		 * Only mainwindow needs redraw since popup is displayed within it. */
		image_preview_debug_print("CLEAR: Non-Kitty (%d) - redrawing mainwindow", pixel_mode);
		mainwindows_redraw();
	}
#endif
}

/*
 * Close popup preview
 */
void image_render_popup_close(void)
{
	if (!popup_showing)
		return;

	popup_showing = FALSE;
	popup_x = popup_y = 0;
	popup_width = popup_height = 0;

	if (popup_content != NULL) {
		g_string_free(popup_content, TRUE);
		popup_content = NULL;
	}

	image_preview_debug_print("POPUP: Closed");
}

/*
 * Check if popup is currently showing
 */
gboolean image_render_popup_is_showing(void)
{
	return popup_showing;
}

/*
 * Get popup content for rendering
 */
GString *image_render_popup_get_content(void)
{
	return popup_content;
}

/*
 * Get popup position and size
 */
void image_render_popup_get_geometry(int *x, int *y, int *width, int *height)
{
	if (x != NULL) *x = popup_x;
	if (y != NULL) *y = popup_y;
	if (width != NULL) *width = popup_width;
	if (height != NULL) *height = popup_height;
}

/*
 * Embedded 16x16 error icon (red X on dark background)
 * Format: RGBA, 4 bytes per pixel
 */
static const unsigned char error_icon_16x16[] = {
	/* Row 0 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 1 */
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 2 */
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 3 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff,
	0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff,
	0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 4 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff,
	0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff,
	0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 5 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 6 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff,
	0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 7 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x55,0x55,0xff,
	0xff,0x55,0x55,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 8 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x55,0x55,0xff,
	0xff,0x55,0x55,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 9 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff,
	0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 10 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 11 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff,
	0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff,
	0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 12 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff,
	0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff,
	0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 13 */
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0xcc,0x33,0x33,0xff, 0xff,0x44,0x44,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 14 */
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0xcc,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	/* Row 15 */
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
	0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff, 0x33,0x33,0x33,0xff,
};

/*
 * Render error icon using Chafa (used when image fetch fails)
 * Returns GString with escape sequences, caller must free with g_string_free()
 */
GString *image_render_error_icon(int max_cols, int max_rows, int *out_rows)
{
#ifdef HAVE_CHAFA
	ChafaTermDb *term_db = NULL;
	ChafaCanvasConfig *config = NULL;
	ChafaCanvas *canvas = NULL;
	ChafaTermInfo *term_info = NULL;
	ChafaPixelMode pixel_mode;
	GString *output = NULL;
	int target_cols, target_rows;

	image_preview_debug_print("CHAFA: Rendering error icon (max %dx%d)", max_cols, max_rows);

	/* Use smaller size for error icon */
	target_cols = (max_cols > 8) ? 8 : max_cols;
	target_rows = (max_rows > 4) ? 4 : max_rows;

	/* Get pixel mode */
	pixel_mode = parse_blitter_setting();

	/* Create term_info */
	term_db = chafa_term_db_get_default();
	term_info = chafa_term_info_new();
	chafa_term_info_supplement(term_info, chafa_term_db_get_fallback_info(term_db));

	/* Set graphics protocol sequences based on pixel mode */
	if (pixel_mode == CHAFA_PIXEL_MODE_ITERM2) {
		GError *err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_ITERM2_IMAGE,
		                        "\033]1337;File=inline=1;width=%1;height=%2;preserveAspectRatio=0:",
		                        &err);
		if (err) g_error_free(err);
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_ITERM2_IMAGE, "\a", &err);
		if (err) g_error_free(err);
	} else if (pixel_mode == CHAFA_PIXEL_MODE_KITTY) {
		GError *err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_KITTY_IMMEDIATE_IMAGE_V1,
		                        "\033_Ga=T,f=%1,s=%2,v=%3,c=%4,r=%5,m=1\033\\", &err);
		if (err) g_error_free(err);
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_KITTY_IMAGE, "\033_Gm=0\033\\", &err);
		if (err) g_error_free(err);
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_KITTY_IMAGE_CHUNK, "\033_Gm=1;", &err);
		if (err) g_error_free(err);
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_KITTY_IMAGE_CHUNK, "\033\\", &err);
		if (err) g_error_free(err);
	} else if (pixel_mode == CHAFA_PIXEL_MODE_SIXELS) {
		GError *err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_BEGIN_SIXELS, "\033P%1;%2;%3q", &err);
		if (err) g_error_free(err);
		err = NULL;
		chafa_term_info_set_seq(term_info, CHAFA_TERM_SEQ_END_SIXELS, "\033\\", &err);
		if (err) g_error_free(err);
	}

	/* Create canvas config */
	config = chafa_canvas_config_new();
	chafa_canvas_config_set_geometry(config, target_cols, target_rows);
	chafa_canvas_config_set_pixel_mode(config, pixel_mode);
	chafa_canvas_config_set_canvas_mode(config, CHAFA_CANVAS_MODE_TRUECOLOR);

	/* Create canvas */
	canvas = chafa_canvas_new(config);
	if (canvas == NULL) {
		chafa_canvas_config_unref(config);
		chafa_term_info_unref(term_info);
		return NULL;
	}

	/* Draw error icon pixels */
	chafa_canvas_draw_all_pixels(canvas,
	                             CHAFA_PIXEL_RGBA8_UNASSOCIATED,
	                             error_icon_16x16,
	                             16, 16, 16 * 4);

	/* Print canvas to string */
	output = chafa_canvas_print(canvas, term_info);

	if (out_rows != NULL)
		*out_rows = target_rows;

	/* Cleanup */
	chafa_term_info_unref(term_info);
	chafa_canvas_unref(canvas);
	chafa_canvas_config_unref(config);

	image_preview_debug_print("CHAFA: Error icon rendered (%zu bytes)", output ? output->len : 0);

	return output;
#else
	(void)max_cols;
	(void)max_rows;
	if (out_rows != NULL)
		*out_rows = 0;
	return NULL;
#endif
}
