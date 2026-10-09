/* Enable GNU extensions (strcasestr) */
#define _GNU_SOURCE

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
#include "src/fe-ansi/mainwindows.h"
#include "src/fe-ansi/term.h"

#include <irssi/src/core/settings.h>

#ifdef HAVE_CHAFA
#include <chafa.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

/* STB image for loading - single header library */
#define STB_IMAGE_IMPLEMENTATION
/* A small file can declare a huge image: refuse it instead of allocating
 * gigabytes on the main loop */
#define STBI_MAX_DIMENSIONS 8192
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
	TERM_SUBTERM,
	/* Sixel protocol */
	TERM_FOOT,
	TERM_CONTOUR,
	TERM_KONSOLE,
	TERM_MINTTY,
	TERM_MLTERM,
	TERM_WINDOWS_TERMINAL,
	/* Known terminal with no native graphics protocol */
	TERM_ALACRITTY
} DetectedTerminal;

/* Cached terminal detection result */
static DetectedTerminal cached_tmux_terminal = TERM_UNKNOWN;
static gboolean tmux_terminal_detected = FALSE;

static gboolean str_contains_ci(const char *str, const char *needle)
{
	return str != NULL && needle != NULL && strcasestr(str, needle) != NULL;
}

static DetectedTerminal match_terminal_name(const char *name)
{
	if (name == NULL || *name == '\0')
		return TERM_UNKNOWN;

	if (str_contains_ci(name, "iterm"))
		return TERM_ITERM2;
	if (str_contains_ci(name, "ghostty"))
		return TERM_GHOSTTY;
	if (str_contains_ci(name, "kitty"))
		return TERM_KITTY;
	if (str_contains_ci(name, "subterm"))
		return TERM_SUBTERM;
	if (str_contains_ci(name, "wezterm"))
		return TERM_WEZTERM;
	if (str_contains_ci(name, "rio"))
		return TERM_RIO;
	if (str_contains_ci(name, "foot"))
		return TERM_FOOT;
	if (str_contains_ci(name, "contour"))
		return TERM_CONTOUR;
	if (str_contains_ci(name, "konsole"))
		return TERM_KONSOLE;
	if (str_contains_ci(name, "mintty"))
		return TERM_MINTTY;
	if (str_contains_ci(name, "mlterm"))
		return TERM_MLTERM;

	return TERM_UNKNOWN;
}

static const char *terminal_name(DetectedTerminal terminal)
{
	switch (terminal) {
	case TERM_ITERM2:           return "iTerm2";
	case TERM_KITTY:            return "Kitty";
	case TERM_GHOSTTY:          return "Ghostty";
	case TERM_WEZTERM:          return "WezTerm";
	case TERM_RIO:              return "Rio";
	case TERM_SUBTERM:          return "Subterm";
	case TERM_FOOT:             return "foot";
	case TERM_CONTOUR:          return "Contour";
	case TERM_KONSOLE:          return "Konsole";
	case TERM_MINTTY:           return "mintty";
	case TERM_MLTERM:           return "mlterm";
	case TERM_WINDOWS_TERMINAL: return "Windows Terminal";
	case TERM_ALACRITTY:        return "Alacritty";
	default:                    return "unknown";
	}
}

static gboolean terminal_to_pixel_mode(DetectedTerminal terminal,
                                       ChafaPixelMode *pixel_mode)
{
	switch (terminal) {
	case TERM_ITERM2:
	case TERM_WEZTERM:
		*pixel_mode = CHAFA_PIXEL_MODE_ITERM2;
		return TRUE;
	case TERM_KITTY:
	case TERM_GHOSTTY:
	case TERM_RIO:
	case TERM_SUBTERM:
		*pixel_mode = CHAFA_PIXEL_MODE_KITTY;
		return TRUE;
	case TERM_FOOT:
	case TERM_CONTOUR:
	case TERM_KONSOLE:
	case TERM_MINTTY:
	case TERM_MLTERM:
	case TERM_WINDOWS_TERMINAL:
		*pixel_mode = CHAFA_PIXEL_MODE_SIXELS;
		return TRUE;
	case TERM_ALACRITTY:
		*pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean env_is_set(const char *name)
{
	const char *value = g_getenv(name);
	return value != NULL && *value != '\0';
}

static char *tmux_query_raw(const char *format)
{
	FILE *fp;
	char cmd[128];
	char buf[256];
	char *result = NULL;

	g_snprintf(cmd, sizeof(cmd), "tmux display-message -p '%s' 2>/dev/null", format);
	fp = popen(cmd, "r");
	if (fp == NULL)
		return NULL;

	if (fgets(buf, sizeof(buf), fp) != NULL) {
		g_strchomp(buf);
		if (*buf != '\0')
			result = g_strdup(buf);
	}

	pclose(fp);
	return result;
}

/*
 * Query terminal type via tmux's client_termname variable.
 * Uses 'tmux display-message -p' which knows the real outer terminal.
 * Returns detected terminal type or TERM_UNKNOWN on failure.
 */
static DetectedTerminal detect_via_tmux(void)
{
	char *termtype, *termname;
	DetectedTerminal result = TERM_UNKNOWN;

	/* Return cached result if already detected */
	if (tmux_terminal_detected) {
		return cached_tmux_terminal;
	}

	image_preview_debug_print("QUERY: Querying tmux for client terminal");

	termtype = tmux_query_raw("#{client_termtype}");
	termname = tmux_query_raw("#{client_termname}");

	image_preview_debug_print("QUERY: tmux client_termtype: %s",
	                          termtype != NULL ? termtype : "(none)");
	image_preview_debug_print("QUERY: tmux client_termname: %s",
	                          termname != NULL ? termname : "(none)");

	result = match_terminal_name(termtype);
	if (result != TERM_UNKNOWN) {
		image_preview_debug_print("QUERY: Detected %s from client_termtype",
		                          terminal_name(result));
		goto cache_result;
	}

	result = match_terminal_name(termname);
	if (result != TERM_UNKNOWN) {
		image_preview_debug_print("QUERY: Detected %s from client_termname",
		                          terminal_name(result));
		goto cache_result;
	}

	/* Alacritty under tmux reports a generic xterm termtype and an empty
	 * termname. It has no native image protocol, so use symbol fallback. */
	if (termtype != NULL && g_str_has_prefix(termtype, "xterm") &&
	    (termname == NULL || *termname == '\0')) {
		result = TERM_ALACRITTY;
		image_preview_debug_print("QUERY: Detected Alacritty heuristic");
	}

cache_result:
	/* Cache the result */
	cached_tmux_terminal = result;
	tmux_terminal_detected = TRUE;

	g_free(termtype);
	g_free(termname);

	return result;
}

/* Detect best pixel mode based on terminal */
static ChafaPixelMode detect_pixel_mode(void)
{
	const char *env_term_program, *env_lc_terminal;
	const char *env_term, *env_tmux;
	DetectedTerminal queried;
	DetectedTerminal env_terminal;
	ChafaPixelMode pixel_mode;

	/* Check if we're in tmux - if so, query the real terminal */
	env_tmux = g_getenv("TMUX");
	if (env_tmux && *env_tmux) {
		image_preview_debug_print("CHAFA: In tmux, querying real terminal");
		queried = detect_via_tmux();

		if (terminal_to_pixel_mode(queried, &pixel_mode)) {
			image_preview_debug_print("CHAFA: Using %s mode (tmux query: %s)",
			                          pixel_mode == CHAFA_PIXEL_MODE_ITERM2 ? "iTerm2" :
			                          pixel_mode == CHAFA_PIXEL_MODE_KITTY ? "Kitty" :
			                          pixel_mode == CHAFA_PIXEL_MODE_SIXELS ? "Sixel" : "Symbols",
			                          terminal_name(queried));
			return pixel_mode;
		}
		/* Query failed or unknown - fall through to env detection */
		image_preview_debug_print("CHAFA: Query failed, falling back to env vars");
	}

	/* Fallback to environment variable detection (for non-tmux or query failure).
	 * This mirrors repartee's direct-terminal detection path. */
	env_lc_terminal = g_getenv("LC_TERMINAL");
	env_term_program = g_getenv("TERM_PROGRAM");
	env_term = g_getenv("TERM");

	if (env_lc_terminal && *env_lc_terminal) {
		env_terminal = match_terminal_name(env_lc_terminal);
		if (terminal_to_pixel_mode(env_terminal, &pixel_mode)) {
			image_preview_debug_print("CHAFA: Detected %s (env LC_TERMINAL)",
			                          terminal_name(env_terminal));
			return pixel_mode;
		}
	}

	if (env_is_set("ITERM_SESSION_ID")) {
		image_preview_debug_print("CHAFA: Detected iTerm2 (env ITERM_SESSION_ID)");
		return CHAFA_PIXEL_MODE_ITERM2;
	}

	if (env_is_set("GHOSTTY_RESOURCES_DIR")) {
		image_preview_debug_print("CHAFA: Detected Ghostty (env GHOSTTY_RESOURCES_DIR)");
		return CHAFA_PIXEL_MODE_KITTY;
	}

	if (env_is_set("KITTY_PID")) {
		image_preview_debug_print("CHAFA: Detected Kitty terminal (env KITTY_PID)");
		return CHAFA_PIXEL_MODE_KITTY;
	}

	if (env_is_set("WEZTERM_EXECUTABLE")) {
		image_preview_debug_print("CHAFA: Detected WezTerm (env WEZTERM_EXECUTABLE)");
		return CHAFA_PIXEL_MODE_ITERM2;
	}

	/* Windows Terminal (doesn't respond to XTVERSION, detect via WT_SESSION) */
	if (env_is_set("WT_SESSION")) {
		image_preview_debug_print("CHAFA: Detected Windows Terminal (env WT_SESSION)");
		return CHAFA_PIXEL_MODE_SIXELS;
	}

	if (!(env_tmux && *env_tmux) && env_term_program && *env_term_program &&
	    g_strcmp0(env_term_program, "tmux") != 0) {
		env_terminal = match_terminal_name(env_term_program);
		if (terminal_to_pixel_mode(env_terminal, &pixel_mode)) {
			image_preview_debug_print("CHAFA: Detected %s (env TERM_PROGRAM)",
			                          terminal_name(env_terminal));
			return pixel_mode;
		}
	}

	env_terminal = match_terminal_name(env_term);
	if (terminal_to_pixel_mode(env_terminal, &pixel_mode)) {
		image_preview_debug_print("CHAFA: Detected %s (env TERM)",
		                          terminal_name(env_terminal));
		return pixel_mode;
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
	int max_bytes, source_w, source_h, estimated_bytes;
	float scale;

	if (image_path == NULL) {
		image_preview_debug_print("CHAFA: NULL image path");
		return NULL;
	}

	image_preview_debug_print("CHAFA: Rendering %s (max %dx%d)",
	                          image_path, max_cols, max_rows);

	/* Load image using stb_image - at most 40 megapixels */
	if (!stbi_info(image_path, &img_width, &img_height, &img_channels) ||
	    (gint64)img_width * img_height > 40 * 1000 * 1000) {
		image_preview_debug_print("CHAFA: Not loading %s: unknown format or too large",
		                          image_path);
		return NULL;
	}
	pixels = stbi_load(image_path, &img_width, &img_height, &img_channels, 4);
	if (pixels == NULL) {
		image_preview_debug_print("CHAFA: Failed to load image: %s",
		                          stbi_failure_reason());
		return NULL;
	}

	image_preview_debug_print("CHAFA: Image loaded: %dx%d, %d channels",
	                          img_width, img_height, img_channels);

	/* Calculate target dimensions preserving aspect ratio.
	 * Terminal cells are ~8x16 pixels (2:1 height:width), so we multiply
	 * aspect ratio by 2 to get correct number of columns.
	 * We also set cell_geometry(8,16) below so Chafa generates correct
	 * source pixels for the Kitty protocol. */
	aspect_ratio = (float)img_width / (float)img_height;
	aspect_ratio *= 2.0f;  /* Adjust for 2:1 cell aspect ratio */

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

	/* Auto-scale down if estimated output exceeds byte limit (for tmux DCS passthrough).
	 * Estimated size: source_pixels * 4 bytes RGBA * 1.4 (base64 + overhead)
	 * Source pixels with cell_geometry(8,16): cols*8 * rows*16 */
	max_bytes = settings_get_int(IMAGE_PREVIEW_MAX_BYTES);
	if (max_bytes <= 0) max_bytes = IMAGE_PREVIEW_DEFAULT_MAX_BYTES;

	source_w = target_cols * 8;
	source_h = target_rows * 16;
	estimated_bytes = (int)((float)(source_w * source_h * 4) * 1.4f);

	if (estimated_bytes > max_bytes) {
		/* Scale down to fit byte limit */
		scale = sqrtf((float)max_bytes / (float)estimated_bytes);
		target_cols = (int)((float)target_cols * scale);
		target_rows = (int)((float)target_rows * scale);
		if (target_cols < 1) target_cols = 1;
		if (target_rows < 1) target_rows = 1;

		source_w = target_cols * 8;
		source_h = target_rows * 16;
		estimated_bytes = (int)((float)(source_w * source_h * 4) * 1.4f);

		image_preview_debug_print("CHAFA: Scaled down to fit %d bytes limit (est: %d)",
		                          max_bytes, estimated_bytes);
	}

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

	/* Tell Chafa about actual terminal cell dimensions (width x height in pixels).
	 * Most terminal fonts use approximately 8x16 pixel cells (2:1 height:width).
	 * This ensures Chafa generates source pixels with correct aspect ratio
	 * so images display correctly without manual aspect adjustment hacks. */
	chafa_canvas_config_set_cell_geometry(config, 8, 16);

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
		/* Small delay to let tmux process the clear before new image */
		if (env_tmux && *env_tmux) {
			usleep(10000);  /* 10ms */
		}
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
	chafa_canvas_config_set_cell_geometry(config, 8, 16);
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
