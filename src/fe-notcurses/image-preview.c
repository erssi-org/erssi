/*
 image-preview.c : Image preview main module for erssi-nc

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "image-preview.h"
#include "term-notcurses.h"

#include <irssi/src/core/settings.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/iregex.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-text/gui-windows.h>
#include <irssi/src/fe-text/textbuffer.h>
#include <irssi/src/fe-text/textbuffer-view.h>
#include <irssi/src/fe-text/gui-mouse.h>
#include <irssi/src/fe-text/mainwindows.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* Compiled regex patterns for URL detection */
static Regex *url_regex_direct = NULL;    /* Direct image URLs (.jpg, .png, etc.) */
static Regex *url_regex_imgur = NULL;     /* imgur.com links */
static Regex *url_regex_imgbb = NULL;     /* imgbb.com links */

/* Hash table: LINE_REC* -> IMAGE_PREVIEW_REC* */
static GHashTable *image_previews = NULL;

/* Debug flag and file - defined early so all functions can use it */
static gboolean image_preview_debug = FALSE;
static FILE *debug_file = NULL;

/* Popup preview state - for click-to-show preview */
static struct ncplane *popup_preview_plane = NULL;
static gboolean popup_preview_showing = FALSE;

/* Debug print helper - writes to file to avoid TUI interference */
void image_preview_debug_print(const char *fmt, ...)
{
	va_list args;

	if (!image_preview_debug)
		return;

	if (debug_file == NULL) {
		char *path = g_strdup_printf("%s/image-preview-debug.log", get_irssi_dir());
		debug_file = fopen(path, "a");
		g_free(path);
		if (debug_file == NULL)
			return;
	}

	va_start(args, fmt);
	fprintf(debug_file, "IMG-DEBUG: ");
	vfprintf(debug_file, fmt, args);
	fprintf(debug_file, "\n");
	fflush(debug_file);
	va_end(args);
}

/* Local shorthand */
#define debug_print image_preview_debug_print

/* URL regex patterns */
#define URL_PATTERN_DIRECT  "https?://[^\\s]+\\.(jpg|jpeg|png|gif|webp)(\\?[^\\s]*)?"
#define URL_PATTERN_IMGUR   "https?://(i\\.)?imgur\\.com/[a-zA-Z0-9]+(\\.(jpg|jpeg|png|gif|webp))?"
#define URL_PATTERN_IMGBB   "https?://i\\.ibb\\.co/[a-zA-Z0-9]+/[^\\s]+"

/* Check if image preview is enabled */
gboolean image_preview_enabled(void)
{
	return settings_get_bool(IMAGE_PREVIEW_SETTING);
}

/* Free an IMAGE_PREVIEW_REC */
static void image_preview_rec_free(IMAGE_PREVIEW_REC *rec)
{
	if (rec == NULL)
		return;

	if (rec->plane != NULL) {
		image_render_destroy(rec->plane);
	}

	g_free(rec->url);
	g_free(rec->cache_path);
	g_free(rec->error_message);
	g_free(rec);
}

/* Initialize URL regex patterns */
static gboolean init_url_patterns(void)
{
	GError *error = NULL;

	url_regex_direct = i_regex_new(URL_PATTERN_DIRECT,
	                               G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                               0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile direct URL regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	error = NULL;
	url_regex_imgur = i_regex_new(URL_PATTERN_IMGUR,
	                              G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                              0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile imgur regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	error = NULL;
	url_regex_imgbb = i_regex_new(URL_PATTERN_IMGBB,
	                              G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                              0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile imgbb regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	return TRUE;
}

/* Free URL regex patterns */
static void deinit_url_patterns(void)
{
	if (url_regex_direct != NULL) {
		i_regex_unref(url_regex_direct);
		url_regex_direct = NULL;
	}
	if (url_regex_imgur != NULL) {
		i_regex_unref(url_regex_imgur);
		url_regex_imgur = NULL;
	}
	if (url_regex_imgbb != NULL) {
		i_regex_unref(url_regex_imgbb);
		url_regex_imgbb = NULL;
	}
}

/* Find URLs matching a pattern in text */
static GSList *find_urls_with_pattern(const char *text, Regex *regex, GSList *list)
{
	MatchInfo *match_info = NULL;
	const char *search_pos;
	gint start_pos, end_pos;

	if (regex == NULL || text == NULL)
		return list;

	search_pos = text;
	while (*search_pos != '\0') {
		char *url;
		gboolean duplicate;
		GSList *l;

		if (!i_regex_match(regex, search_pos, 0, &match_info)) {
			if (match_info != NULL)
				i_match_info_free(match_info);
			break;
		}

		if (!i_match_info_matches(match_info) ||
		    !i_match_info_fetch_pos(match_info, 0, &start_pos, &end_pos)) {
			i_match_info_free(match_info);
			break;
		}

		/* Extract URL from match position */
		url = g_strndup(search_pos + start_pos, end_pos - start_pos);

		/* Check for duplicates */
		duplicate = FALSE;
		for (l = list; l != NULL; l = l->next) {
			if (g_strcmp0(l->data, url) == 0) {
				duplicate = TRUE;
				break;
			}
		}
		if (!duplicate) {
			list = g_slist_prepend(list, url);
		} else {
			g_free(url);
		}

		i_match_info_free(match_info);
		match_info = NULL;

		/* Move past this match */
		search_pos += end_pos;
	}

	return list;
}

/* Find image URLs in text */
GSList *image_preview_find_urls(const char *text)
{
	GSList *urls = NULL;

	if (text == NULL || *text == '\0')
		return NULL;

	/* Check each pattern */
	urls = find_urls_with_pattern(text, url_regex_direct, urls);
	urls = find_urls_with_pattern(text, url_regex_imgur, urls);
	urls = find_urls_with_pattern(text, url_regex_imgbb, urls);

	return g_slist_reverse(urls);
}

/* Get preview record for a line */
IMAGE_PREVIEW_REC *image_preview_get(LINE_REC *line)
{
	if (image_previews == NULL || line == NULL)
		return NULL;

	return g_hash_table_lookup(image_previews, line);
}

/* Queue an image fetch */
gboolean image_preview_queue_fetch(const char *url, LINE_REC *line, WINDOW_REC *window)
{
	IMAGE_PREVIEW_REC *rec;
	char *cache_path;

	debug_print("queue_fetch: url=%s", url);

	if (!image_preview_enabled()) {
		debug_print("queue_fetch: preview disabled");
		return FALSE;
	}

	if (url == NULL || line == NULL) {
		debug_print("queue_fetch: NULL params");
		return FALSE;
	}

	/* Check if we already have a preview for this line */
	rec = image_preview_get(line);
	if (rec != NULL) {
		debug_print("queue_fetch: already processing this line");
		return FALSE;
	}

	/* Check if cached */
	cache_path = image_cache_get(url);
	if (cache_path != NULL) {
		/* Already cached - create preview record and mark ready */
		debug_print("queue_fetch: CACHED at %s", cache_path);
		rec = g_new0(IMAGE_PREVIEW_REC, 1);
		rec->line = line;
		rec->window = window;
		rec->url = g_strdup(url);
		rec->cache_path = cache_path;
		rec->fetch_pending = FALSE;
		rec->fetch_failed = FALSE;

		g_hash_table_insert(image_previews, line, rec);

		/* Trigger render for cached image */
		debug_print("queue_fetch: emitting 'image preview ready' for cached image");
		signal_emit("image preview ready", 2, line, window);
		return TRUE;
	}

	debug_print("queue_fetch: not cached, need to fetch");

	/* Need to fetch - generate cache path */
	cache_path = NULL;
	if (cache_path == NULL) {
		/* Generate new cache path */
		GChecksum *checksum;
		const char *hash;
		const char *ext;
		char *cache_dir;

		checksum = g_checksum_new(G_CHECKSUM_SHA256);
		g_checksum_update(checksum, (guchar *)url, strlen(url));
		hash = g_checksum_get_string(checksum);

		/* Extract extension from URL */
		ext = strrchr(url, '.');
		if (ext == NULL || strlen(ext) > 6 || strchr(ext, '/') != NULL) {
			ext = ".img";
		}

		cache_dir = g_strdup_printf("%s/%s", get_irssi_dir(), IMAGE_CACHE_DIR);
		cache_path = g_strdup_printf("%s/%s%s", cache_dir, hash, ext);
		g_free(cache_dir);
		g_checksum_free(checksum);
	}

	/* Create preview record */
	rec = g_new0(IMAGE_PREVIEW_REC, 1);
	rec->line = line;
	rec->window = window;
	rec->url = g_strdup(url);
	rec->cache_path = cache_path;
	rec->fetch_pending = TRUE;
	rec->fetch_failed = FALSE;

	g_hash_table_insert(image_previews, line, rec);

	/* Start async fetch */
	debug_print("queue_fetch: calling image_fetch_start with cache_path=%s", cache_path);
	if (!image_fetch_start(url, cache_path, line, window)) {
		debug_print("queue_fetch: image_fetch_start FAILED");
		rec->fetch_pending = FALSE;
		rec->fetch_failed = TRUE;
		rec->error_message = g_strdup("Failed to start fetch");
		return FALSE;
	}

	debug_print("queue_fetch: fetch started OK");
	return TRUE;
}

/* Cancel pending fetch */
void image_preview_cancel_fetch(const char *url)
{
	image_fetch_cancel(url);
}

/* Clear all rendered planes */
void image_preview_clear_planes(void)
{
	GHashTableIter iter;
	gpointer key, value;

	if (image_previews == NULL)
		return;

	g_hash_table_iter_init(&iter, image_previews);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		IMAGE_PREVIEW_REC *rec = value;
		if (rec->plane != NULL) {
			image_render_destroy(rec->plane);
			rec->plane = NULL;
		}
	}
}

/* Clear all previews */
void image_preview_clear_all(void)
{
	image_fetch_cancel_all();
	image_preview_clear_planes();

	if (image_previews != NULL) {
		g_hash_table_remove_all(image_previews);
	}
}

/* Signal: text line finished printing
 * Parameters: (WINDOW_REC *window, TEXT_DEST_REC *dest) */
static void sig_gui_print_text_finished(WINDOW_REC *window, void *dest)
{
	GUI_WINDOW_REC *gui;
	LINE_REC *line;
	GString *str;
	GSList *urls;

	if (!image_preview_enabled()) {
		return;
	}

	if (window == NULL) {
		debug_print("window is NULL");
		return;
	}

	/* Get GUI window and buffer */
	gui = WINDOW_GUI(window);
	if (gui == NULL || gui->view == NULL || gui->view->buffer == NULL) {
		debug_print("gui/view/buffer is NULL");
		return;
	}

	/* Get the last line that was just added */
	line = textbuffer_line_last(gui->view->buffer);
	if (line == NULL) {
		debug_print("line is NULL");
		return;
	}

	/* Extract line text */
	str = g_string_new(NULL);
	textbuffer_line2text(gui->view->buffer, line, FALSE, str);
	if (str->len == 0) {
		g_string_free(str, TRUE);
		return;
	}

	debug_print("scanning: %.60s%s", str->str, str->len > 60 ? "..." : "");

	/* Find image URLs */
	urls = image_preview_find_urls(str->str);
	g_string_free(str, TRUE);

	if (urls == NULL) {
		return;
	}

	debug_print("found URL: %s", (char *)urls->data);

	/* Queue fetch for first URL only (to avoid spam) */
	if (image_preview_queue_fetch(urls->data, line, window)) {
		debug_print("queued fetch OK");
	} else {
		debug_print("queue fetch FAILED");
	}

	g_slist_free_full(urls, g_free);
}

/* Signal: window changed */
static void sig_window_changed(WINDOW_REC *window)
{
	/* Clear planes when switching windows - they'll be recreated on redraw */
	image_preview_clear_planes();
}

/* Signal: image preview ready - render the image */
static void sig_image_preview_ready(LINE_REC *line, WINDOW_REC *window)
{
	GUI_WINDOW_REC *gui;
	IMAGE_PREVIEW_REC *preview;

	debug_print("sig_image_preview_ready: line=%p window=%p", line, window);

	if (!image_preview_enabled())
		return;

	if (window == NULL)
		return;

	gui = WINDOW_GUI(window);
	if (gui == NULL || gui->view == NULL)
		return;

	preview = image_preview_get(line);
	if (preview == NULL) {
		debug_print("sig_image_preview_ready: no preview record for line");
		return;
	}

	if (preview->cache_path == NULL) {
		debug_print("sig_image_preview_ready: no cache_path");
		return;
	}

	debug_print("sig_image_preview_ready: cached %s (click to preview)", preview->cache_path);

	/* Auto-preview disabled - click-to-preview only */
	/* image_preview_render_view(gui->view, window); */
}

/* Dismiss popup preview */
static void popup_preview_dismiss(void)
{
	if (popup_preview_plane != NULL) {
		debug_print("POPUP: dismissing preview");

		/* CRITICAL: Use ncplane_family_destroy() instead of ncplane_destroy()!
		 *
		 * When ncvisual_blit() is called with NCVISUAL_OPTION_CHILDPLANE, it
		 * creates a CHILD plane containing the sprixel. ncplane_destroy() does
		 * NOT destroy children - it reparents them to the grandparent (stdplane).
		 * This leaves the sprixel orphaned and visible on screen.
		 *
		 * ncplane_family_destroy() recursively destroys all children first,
		 * which properly calls sprixel_hide() on the sprixel plane. */
		ncplane_family_destroy(popup_preview_plane);
		popup_preview_plane = NULL;
		popup_preview_showing = FALSE;

		/* Render to actually send the sprixel hide sequence to terminal.
		 * sprixel_hide() only MARKS the sprixel for hiding - the actual
		 * terminal escape sequence is sent during notcurses_render(). */
		if (nc_ctx != NULL && nc_ctx->nc != NULL) {
			notcurses_render(nc_ctx->nc);
		}

		/* Then do the normal irssi redraw to restore TUI */
		irssi_redraw();
	}
}

/* Get the best blitter for current terminal */
static ncblitter_e get_best_blitter(struct notcurses *nc)
{
	ncpixelimpl_e pixel_impl;
	const char *blitter_setting;

	if (nc == NULL)
		return NCBLIT_2x2;

	/* Check if user forced a specific blitter */
	blitter_setting = settings_get_str("image_preview_blitter");
	if (blitter_setting != NULL && *blitter_setting != '\0') {
		if (g_ascii_strcasecmp(blitter_setting, "blocks") == 0 ||
		    g_ascii_strcasecmp(blitter_setting, "2x2") == 0) {
			return NCBLIT_2x2;
		}
		if (g_ascii_strcasecmp(blitter_setting, "braille") == 0) {
			return NCBLIT_BRAILLE;
		}
		if (g_ascii_strcasecmp(blitter_setting, "pixel") == 0 ||
		    g_ascii_strcasecmp(blitter_setting, "sixel") == 0) {
			return NCBLIT_PIXEL;
		}
		/* "auto" or anything else - fall through to auto-detect */
	}

	/* Check for pixel graphics support */
	pixel_impl = notcurses_check_pixel_support(nc);
	if (pixel_impl != NCPIXEL_NONE) {
		return NCBLIT_PIXEL;
	}

	/* Fallback to half-blocks */
	return NCBLIT_2x2;
}

/* Check if running inside tmux */
static gboolean is_in_tmux(void)
{
	return g_getenv("TMUX") != NULL;
}

/* Send cursor position through tmux DCS passthrough to outer terminal.
 * This ensures kitty graphics appears at the correct position regardless
 * of where tmux's internal cursor is. */
static void send_cursor_through_passthrough(int row, int col)
{
	char buf[64];
	int len;

	/* Build passthrough: ESC P tmux ; ESC ESC [ row ; col H ESC \ */
	/* The inner ESC must be doubled for passthrough */
	len = snprintf(buf, sizeof(buf), "\x1bPtmux;\x1b\x1b[%d;%dH\x1b\\", row, col);
	if (len > 0 && len < (int)sizeof(buf)) {
		write(STDOUT_FILENO, buf, len);
		debug_print("PASSTHROUGH: sent cursor to row=%d col=%d", row, col);
	}
}

/* Show centered popup preview for an image - centered to MAIN WINDOW */
static void popup_preview_show(const char *image_path)
{
	struct ncvisual *ncv = NULL;
	struct ncvisual_options vopts = {0};
	struct ncplane_options nopts = {0};
	unsigned int term_rows, term_cols;
	int max_width, max_height;
	int target_rows, target_cols;
	int y_pos, x_pos;
	ncvgeom geom = {0};
	ncblitter_e blitter;
	gboolean in_tmux;
	MAIN_WINDOW_REC *mainwin;
	int mw_top, mw_left, mw_height, mw_width;

	debug_print("POPUP: showing preview for %s", image_path);

	if (nc_ctx == NULL || nc_ctx->nc == NULL || nc_ctx->stdplane == NULL) {
		debug_print("POPUP: nc_ctx not ready");
		return;
	}

	in_tmux = is_in_tmux();

	/* Get main window dimensions for centering */
	mainwin = WINDOW_MAIN(active_win);
	if (mainwin != NULL) {
		mw_top = mainwin->first_line + mainwin->statusbar_lines_top;
		mw_left = mainwin->first_column;
		mw_height = mainwin->height - mainwin->statusbar_lines;
		mw_width = mainwin->width;
		debug_print("POPUP: main window: top=%d left=%d height=%d width=%d",
		            mw_top, mw_left, mw_height, mw_width);
	} else {
		/* Fallback to terminal dimensions */
		mw_top = 0;
		mw_left = 0;
		ncplane_dim_yx(nc_ctx->stdplane, &term_rows, &term_cols);
		mw_height = term_rows;
		mw_width = term_cols;
		debug_print("POPUP: no mainwin, using terminal size %dx%d", mw_width, mw_height);
	}

	/* Dismiss any existing popup first */
	popup_preview_dismiss();

	/* Get terminal dimensions */
	ncplane_dim_yx(nc_ctx->stdplane, &term_rows, &term_cols);
	debug_print("POPUP: terminal size %ux%u", term_cols, term_rows);

	/* Calculate max size (50% of MAIN WINDOW, not terminal) */
	max_width = mw_width / 2;
	max_height = mw_height / 2;
	if (max_width < 20) max_width = 20;
	if (max_height < 10) max_height = 10;

	/* Check if notcurses can open images */
	if (!notcurses_canopen_images(nc_ctx->nc)) {
		debug_print("POPUP: notcurses cannot open images");
		return;
	}

	/* Load image */
	ncv = ncvisual_from_file(image_path);
	if (ncv == NULL) {
		debug_print("POPUP: failed to load image %s", image_path);
		return;
	}

	/* Get best blitter for current terminal */
	blitter = get_best_blitter(nc_ctx->nc);
	debug_print("POPUP: using blitter %d (PIXEL=%d, 2x2=%d) in_tmux=%d", blitter, NCBLIT_PIXEL, NCBLIT_2x2, in_tmux);

	/* Get geometry */
	vopts.blitter = blitter;
	vopts.scaling = NCSCALE_SCALE;
	if (ncvisual_geom(nc_ctx->nc, ncv, &vopts, &geom) != 0) {
		debug_print("POPUP: ncvisual_geom failed");
		ncvisual_destroy(ncv);
		return;
	}

	debug_print("POPUP: image geom pixy=%u pixx=%u", geom.pixy, geom.pixx);

	/* Calculate target size maintaining aspect ratio */
	if (geom.pixx > 0 && geom.pixy > 0) {
		float aspect = (float)geom.pixx / (float)geom.pixy;
		target_cols = max_width;
		target_rows = (int)(target_cols / aspect / 2);  /* /2 for terminal cell aspect */
		if (target_rows > max_height) {
			target_rows = max_height;
			target_cols = (int)(target_rows * aspect * 2);
		}
	} else {
		target_cols = max_width;
		target_rows = max_height;
	}

	if (target_cols < 10) target_cols = 10;
	if (target_rows < 5) target_rows = 5;

	/* Center position WITHIN MAIN WINDOW */
	y_pos = mw_top + (mw_height - target_rows) / 2;
	x_pos = mw_left + (mw_width - target_cols) / 2;

	debug_print("POPUP: creating plane at y=%d x=%d size %dx%d (centered in main window)", y_pos, x_pos, target_cols, target_rows);

	/* Create plane */
	nopts.y = y_pos;
	nopts.x = x_pos;
	nopts.rows = target_rows;
	nopts.cols = target_cols;
	nopts.name = "popup-preview";
	nopts.flags = 0;

	popup_preview_plane = ncplane_create(nc_ctx->stdplane, &nopts);
	if (popup_preview_plane == NULL) {
		debug_print("POPUP: ncplane_create failed");
		ncvisual_destroy(ncv);
		return;
	}

	/* Fill with dark background to make it stand out */
	{
		uint64_t channels = 0;
		ncchannels_set_bg_rgb8(&channels, 0x20, 0x20, 0x20);
		ncchannels_set_fg_rgb8(&channels, 0x20, 0x20, 0x20);
		ncplane_set_base(popup_preview_plane, " ", 0, channels);
	}

	/* Render image */
	vopts.n = popup_preview_plane;
	vopts.scaling = NCSCALE_SCALE;
	vopts.y = 0;
	vopts.x = 0;
	vopts.blitter = blitter;
	vopts.flags = NCVISUAL_OPTION_CHILDPLANE;  /* Required for sixel sprixel creation */

	if (ncvisual_blit(nc_ctx->nc, ncv, &vopts) == NULL) {
		debug_print("POPUP: ncvisual_blit failed");
		ncplane_destroy(popup_preview_plane);
		popup_preview_plane = NULL;
		ncvisual_destroy(ncv);
		return;
	}

	ncvisual_destroy(ncv);
	popup_preview_showing = TRUE;

	/* In tmux, send cursor position through passthrough BEFORE render.
	 * This tells the outer terminal (Ghostty/Kitty) where to place the image.
	 * Kitty graphics protocol places images at cursor position. */
	if (in_tmux) {
		/* CSI uses 1-based coordinates */
		send_cursor_through_passthrough(y_pos + 1, x_pos + 1);
		debug_print("POPUP: sent passthrough cursor to y=%d x=%d", y_pos + 1, x_pos + 1);
	}

	/* Render to screen */
	notcurses_render(nc_ctx->nc);

	debug_print("POPUP: preview shown successfully");
}

/* Find LINE_REC at given screen Y coordinate */
static LINE_REC *find_line_at_screen_y(TEXT_BUFFER_VIEW_REC *view, MAIN_WINDOW_REC *mainwin, int screen_y)
{
	LINE_REC *line;
	int text_area_top;
	int current_y;
	int line_count;

	if (view == NULL || view->startline == NULL || mainwin == NULL)
		return NULL;

	/* Calculate where text area starts on screen */
	text_area_top = mainwin->first_line + mainwin->statusbar_lines_top;

	/* Check if click is in text area */
	if (screen_y < text_area_top)
		return NULL;

	/* Convert screen Y to line-relative Y */
	current_y = text_area_top;

	for (line = view->startline; line != NULL; line = line->next) {
		/* Get line cache to know how many rows this line takes */
		LINE_CACHE_REC *cache = textbuffer_view_get_line_cache(view, line);
		line_count = cache ? cache->count : 1;

		if (screen_y >= current_y && screen_y < current_y + line_count) {
			return line;
		}

		current_y += line_count;

		/* Stop if we've gone past the visible area */
		if (current_y >= text_area_top + view->height)
			break;
	}

	return NULL;
}

/* Mouse click handler for image preview */
static gboolean image_preview_mouse_handler(const GuiMouseEvent *event, gpointer user_data)
{
	WINDOW_REC *window;
	GUI_WINDOW_REC *gui;
	MAIN_WINDOW_REC *mainwin;
	LINE_REC *line;
	IMAGE_PREVIEW_REC *preview;

	/* Only handle left button press */
	if (event->button != MOUSE_BUTTON_LEFT || !event->press)
		return FALSE;

	/* If popup is showing, dismiss on any click */
	if (popup_preview_showing) {
		popup_preview_dismiss();
		return TRUE;  /* Consume the click */
	}

	if (!image_preview_enabled())
		return FALSE;

	/* Get active window */
	window = active_win;
	if (window == NULL)
		return FALSE;

	gui = WINDOW_GUI(window);
	if (gui == NULL || gui->view == NULL)
		return FALSE;

	mainwin = WINDOW_MAIN(window);
	if (mainwin == NULL)
		return FALSE;

	debug_print("CLICK: at y=%d x=%d", event->y, event->x);

	/* Find the line at click position */
	line = find_line_at_screen_y(gui->view, mainwin, event->y);
	if (line == NULL) {
		debug_print("CLICK: no line at position");
		return FALSE;
	}

	/* Check if this line has an image preview */
	preview = image_preview_get(line);
	if (preview == NULL) {
		debug_print("CLICK: line has no preview");
		return FALSE;
	}

	if (preview->cache_path == NULL) {
		debug_print("CLICK: preview has no cache_path");
		return FALSE;
	}

	debug_print("CLICK: found preview, showing popup for %s", preview->cache_path);

	/* Show the popup preview */
	popup_preview_show(preview->cache_path);

	return TRUE;  /* Consume the click */
}

/* Key press handler - dismiss popup on non-escape keys */
static void sig_key_pressed_preview(gpointer keyp)
{
	unichar key = GPOINTER_TO_INT(keyp);

	if (popup_preview_showing) {
		/* Don't intercept ESC or CSI characters - let mouse parser handle them.
		 * Mouse sequences start with ESC and the mouse handler will dismiss
		 * the popup when the click event is fully parsed. */
		if (key == 0x1b || key == '[' || key == '<' ||
		    (key >= '0' && key <= '9') || key == ';' ||
		    key == 'M' || key == 'm')
			return;

		debug_print("KEY: dismissing popup on key 0x%x", key);
		popup_preview_dismiss();
		signal_stop();
	}
}

/* Command: /IMAGE */
static void cmd_image(const char *data, SERVER_REC *server, void *item)
{
	if (data == NULL || *data == '\0') {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "Usage: /IMAGE on|off|clear|stats");
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "  on    - Enable image preview");
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "  off   - Disable image preview");
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "  clear - Clear image cache");
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "  stats - Show cache statistics");
		return;
	}

	if (g_ascii_strcasecmp(data, "on") == 0) {
		settings_set_bool(IMAGE_PREVIEW_SETTING, TRUE);
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "Image preview enabled");
	} else if (g_ascii_strcasecmp(data, "off") == 0) {
		settings_set_bool(IMAGE_PREVIEW_SETTING, FALSE);
		image_preview_clear_all();
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "Image preview disabled");
	} else if (g_ascii_strcasecmp(data, "clear") == 0) {
		image_cache_clear_all();
		image_preview_clear_all();
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "Image cache cleared");
	} else if (g_ascii_strcasecmp(data, "stats") == 0) {
		image_cache_print_stats();
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "Unknown option: %s", data);
	}
}

/* Module initialization */
void image_preview_init(void)
{
	/* Register settings FIRST (before subsystems that use them) */
	settings_add_bool("lookandfeel", IMAGE_PREVIEW_SETTING, FALSE);
	settings_add_int("lookandfeel", IMAGE_PREVIEW_MAX_WIDTH,
	                 IMAGE_PREVIEW_DEFAULT_MAX_WIDTH);
	settings_add_int("lookandfeel", IMAGE_PREVIEW_MAX_HEIGHT,
	                 IMAGE_PREVIEW_DEFAULT_MAX_HEIGHT);
	settings_add_str("lookandfeel", "image_preview_blitter", "auto");
	/* blitter: auto, blocks/2x2, pixel/sixel */
	settings_add_size("misc", IMAGE_PREVIEW_CACHE_SIZE,
	                  IMAGE_PREVIEW_DEFAULT_CACHE_SIZE);
	settings_add_time("misc", IMAGE_PREVIEW_TIMEOUT,
	                  IMAGE_PREVIEW_DEFAULT_TIMEOUT);
	settings_add_int("misc", IMAGE_PREVIEW_MAX_FILE_SIZE,
	                 IMAGE_PREVIEW_DEFAULT_MAX_FILE_SIZE);

	/* Initialize URL patterns */
	if (!init_url_patterns()) {
		g_warning("image-preview: Failed to initialize URL patterns");
		return;
	}

	/* Create preview tracking hash table */
	image_previews = g_hash_table_new_full(g_direct_hash, g_direct_equal,
	                                       NULL, (GDestroyNotify)image_preview_rec_free);

	/* Initialize subsystems (after settings are registered) */
	image_cache_init();
	image_fetch_init();

	/* Register signals */
	signal_add("gui print text finished", (SIGNAL_FUNC)sig_gui_print_text_finished);
	signal_add("window changed", (SIGNAL_FUNC)sig_window_changed);
	signal_add("image preview ready", (SIGNAL_FUNC)sig_image_preview_ready);

	/* Register key handler for popup dismiss (first priority) */
	signal_add_first("gui key pressed", (SIGNAL_FUNC)sig_key_pressed_preview);

	/* Register mouse handler for click-to-preview */
	gui_mouse_add_handler(image_preview_mouse_handler, NULL);

	/* Register commands */
	command_bind("image", NULL, (SIGNAL_FUNC)cmd_image);
}

/* Module deinitialization */
void image_preview_deinit(void)
{
	/* Dismiss any open popup preview */
	popup_preview_dismiss();

	/* Unregister mouse handler */
	gui_mouse_remove_handler(image_preview_mouse_handler, NULL);

	/* Unregister commands */
	command_unbind("image", (SIGNAL_FUNC)cmd_image);

	/* Unregister signals (reverse order of registration) */
	signal_remove("gui key pressed", (SIGNAL_FUNC)sig_key_pressed_preview);
	signal_remove("image preview ready", (SIGNAL_FUNC)sig_image_preview_ready);
	signal_remove("window changed", (SIGNAL_FUNC)sig_window_changed);
	signal_remove("gui print text finished", (SIGNAL_FUNC)sig_gui_print_text_finished);

	/* Cleanup subsystems */
	image_fetch_deinit();
	image_cache_deinit();

	/* Free preview records */
	if (image_previews != NULL) {
		g_hash_table_destroy(image_previews);
		image_previews = NULL;
	}

	/* Free URL patterns */
	deinit_url_patterns();

	/* Close debug file */
	if (debug_file != NULL) {
		fclose(debug_file);
		debug_file = NULL;
	}
}
