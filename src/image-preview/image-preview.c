/*
 image-preview.c : Image preview main module for erssi (Chafa-based)

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "image-preview.h"

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
#include <unistd.h>

/* Compiled regex patterns for URL detection */
static Regex *url_regex_direct = NULL;        /* Direct image URLs (.jpg, .png, etc.) */
static Regex *url_regex_imgur_direct = NULL;  /* i.imgur.com direct links */
static Regex *url_regex_imgbb_direct = NULL;  /* i.ibb.co direct links */
static Regex *url_regex_imgur_page = NULL;    /* imgur.com page links */
static Regex *url_regex_imgbb_page = NULL;    /* ibb.co page links */
static Regex *url_regex_kermit = NULL;        /* kermit.pw links */

/* Hash table: LINE_REC* -> IMAGE_PREVIEW_REC* */
static GHashTable *image_previews = NULL;

/* Debug flag and file */
static gboolean image_preview_debug = FALSE;
static FILE *debug_file = NULL;

/* Popup preview state */
static gboolean popup_preview_showing = FALSE;
static GString *popup_content = NULL;
static int popup_x = 0, popup_y = 0;
static int popup_width = 0, popup_height = 0;
static LINE_REC *popup_current_line = NULL;  /* Line whose image is currently displayed */

/* Forward declarations */
static void popup_preview_show_for_line(const char *image_path, LINE_REC *line);
static void popup_preview_dismiss(void);
static gboolean cache_cleanup_callback(gpointer user_data);

/* Debug print helper - writes to file to avoid TUI interference */
void image_preview_debug_print(const char *fmt, ...)
{
	va_list args;
	GDateTime *now;
	char *timestamp;

	if (!image_preview_debug)
		return;

	if (debug_file == NULL) {
		char *path = g_strdup_printf("%s/image-preview-debug.log", get_irssi_dir());
		debug_file = fopen(path, "a");
		g_free(path);
		if (debug_file == NULL)
			return;
		now = g_date_time_new_now_local();
		timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
		fprintf(debug_file, "\n=== Image Preview Debug Log Started %s ===\n", timestamp);
		g_free(timestamp);
		g_date_time_unref(now);
	}

	now = g_date_time_new_now_local();
	timestamp = g_date_time_format(now, "%H:%M:%S");

	va_start(args, fmt);
	fprintf(debug_file, "[%s] ", timestamp);
	vfprintf(debug_file, fmt, args);
	fprintf(debug_file, "\n");
	fflush(debug_file);
	va_end(args);

	g_free(timestamp);
	g_date_time_unref(now);
}

#define debug_print image_preview_debug_print

/* URL regex patterns - direct images */
#define URL_PATTERN_DIRECT       "https?://[^\\s]+\\.(jpg|jpeg|png|gif|webp)(\\?[^\\s]*)?"
#define URL_PATTERN_IMGUR_DIRECT "https?://i\\.imgur\\.com/[a-zA-Z0-9]+(\\.(jpg|jpeg|png|gif|webp))?"
#define URL_PATTERN_IMGBB_DIRECT "https?://i\\.ibb\\.co/[a-zA-Z0-9]+/[^\\s]+"

/* URL regex patterns - page URLs (require og:image extraction) */
#define URL_PATTERN_IMGUR_PAGE   "https?://imgur\\.com/[a-zA-Z0-9_-]+"
#define URL_PATTERN_IMGBB_PAGE   "https?://ibb\\.co/[a-zA-Z0-9_-]+"
#define URL_PATTERN_KERMIT       "https?://kermit\\.pw/[a-zA-Z0-9_-]+"

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

	/* Cancel any pending cache cleanup timer */
	if (rec->cache_cleanup_tag != 0) {
		g_source_remove(rec->cache_cleanup_tag);
		rec->cache_cleanup_tag = 0;
	}

	if (rec->rendered != NULL) {
		g_string_free(rec->rendered, TRUE);
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
	url_regex_imgur_direct = i_regex_new(URL_PATTERN_IMGUR_DIRECT,
	                                     G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                                     0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile imgur direct regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	error = NULL;
	url_regex_imgbb_direct = i_regex_new(URL_PATTERN_IMGBB_DIRECT,
	                                     G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                                     0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile imgbb direct regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	error = NULL;
	url_regex_imgur_page = i_regex_new(URL_PATTERN_IMGUR_PAGE,
	                                   G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                                   0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile imgur page regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	error = NULL;
	url_regex_imgbb_page = i_regex_new(URL_PATTERN_IMGBB_PAGE,
	                                   G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                                   0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile imgbb page regex: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	error = NULL;
	url_regex_kermit = i_regex_new(URL_PATTERN_KERMIT,
	                               G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
	                               0, &error);
	if (error != NULL) {
		g_warning("image-preview: Failed to compile kermit regex: %s", error->message);
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
	if (url_regex_imgur_direct != NULL) {
		i_regex_unref(url_regex_imgur_direct);
		url_regex_imgur_direct = NULL;
	}
	if (url_regex_imgbb_direct != NULL) {
		i_regex_unref(url_regex_imgbb_direct);
		url_regex_imgbb_direct = NULL;
	}
	if (url_regex_imgur_page != NULL) {
		i_regex_unref(url_regex_imgur_page);
		url_regex_imgur_page = NULL;
	}
	if (url_regex_imgbb_page != NULL) {
		i_regex_unref(url_regex_imgbb_page);
		url_regex_imgbb_page = NULL;
	}
	if (url_regex_kermit != NULL) {
		i_regex_unref(url_regex_kermit);
		url_regex_kermit = NULL;
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

		url = g_strndup(search_pos + start_pos, end_pos - start_pos);

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

	urls = find_urls_with_pattern(text, url_regex_direct, urls);
	urls = find_urls_with_pattern(text, url_regex_imgur_direct, urls);
	urls = find_urls_with_pattern(text, url_regex_imgbb_direct, urls);
	urls = find_urls_with_pattern(text, url_regex_imgur_page, urls);
	urls = find_urls_with_pattern(text, url_regex_imgbb_page, urls);
	urls = find_urls_with_pattern(text, url_regex_kermit, urls);

	return g_slist_reverse(urls);
}

/* Classify a URL to determine if it's a direct image or page URL */
ImageUrlType image_preview_classify_url(const char *url)
{
	if (url == NULL) {
		debug_print("classify_url: NULL url");
		return URL_TYPE_DIRECT_IMAGE;
	}

	debug_print("classify_url: checking '%s'", url);

	if (i_regex_match(url_regex_direct, url, 0, NULL)) {
		debug_print("classify_url: MATCHED direct image pattern");
		return URL_TYPE_DIRECT_IMAGE;
	}
	if (i_regex_match(url_regex_imgur_direct, url, 0, NULL)) {
		debug_print("classify_url: MATCHED i.imgur.com direct");
		return URL_TYPE_DIRECT_IMAGE;
	}
	if (i_regex_match(url_regex_imgbb_direct, url, 0, NULL)) {
		debug_print("classify_url: MATCHED i.ibb.co direct");
		return URL_TYPE_DIRECT_IMAGE;
	}

	if (i_regex_match(url_regex_imgur_page, url, 0, NULL)) {
		debug_print("classify_url: MATCHED imgur.com PAGE");
		return URL_TYPE_PAGE_IMGUR;
	}
	if (i_regex_match(url_regex_imgbb_page, url, 0, NULL)) {
		debug_print("classify_url: MATCHED ibb.co PAGE");
		return URL_TYPE_PAGE_IMGBB;
	}
	if (i_regex_match(url_regex_kermit, url, 0, NULL)) {
		debug_print("classify_url: MATCHED kermit.pw PAGE");
		return URL_TYPE_PAGE_KERMIT;
	}

	debug_print("classify_url: no match, defaulting to direct image");
	return URL_TYPE_DIRECT_IMAGE;
}

/* Get preview record for a line */
IMAGE_PREVIEW_REC *image_preview_get(LINE_REC *line)
{
	if (image_previews == NULL || line == NULL)
		return NULL;

	return g_hash_table_lookup(image_previews, line);
}

/* Register URL for a line without starting fetch */
gboolean image_preview_register_url(const char *url, LINE_REC *line, WINDOW_REC *window)
{
	IMAGE_PREVIEW_REC *rec;
	char *cache_path;

	debug_print("register_url: url=%s", url);

	if (!image_preview_enabled()) {
		debug_print("register_url: preview disabled");
		return FALSE;
	}

	if (url == NULL || line == NULL) {
		debug_print("register_url: NULL params");
		return FALSE;
	}

	rec = image_preview_get(line);
	if (rec != NULL) {
		debug_print("register_url: already registered");
		return FALSE;
	}

	cache_path = image_cache_get(url);
	if (cache_path != NULL) {
		debug_print("register_url: CACHED at %s", cache_path);
		rec = g_new0(IMAGE_PREVIEW_REC, 1);
		rec->line = line;
		rec->window = window;
		rec->url = g_strdup(url);
		rec->cache_path = cache_path;
		rec->fetch_pending = FALSE;
		rec->fetch_failed = FALSE;
		rec->show_on_complete = FALSE;

		g_hash_table_insert(image_previews, line, rec);
		return TRUE;
	}

	debug_print("register_url: not cached, will fetch on click");
	rec = g_new0(IMAGE_PREVIEW_REC, 1);
	rec->line = line;
	rec->window = window;
	rec->url = g_strdup(url);
	rec->cache_path = NULL;
	rec->fetch_pending = FALSE;
	rec->fetch_failed = FALSE;
	rec->show_on_complete = FALSE;

	g_hash_table_insert(image_previews, line, rec);
	return TRUE;
}

/* Queue an image fetch - starts download immediately */
gboolean image_preview_queue_fetch(const char *url, LINE_REC *line, WINDOW_REC *window)
{
	IMAGE_PREVIEW_REC *rec;
	char *cache_path;
	ImageUrlType url_type;
	gboolean is_page_url;

	debug_print("queue_fetch: url=%s", url);

	if (!image_preview_enabled()) {
		debug_print("queue_fetch: preview disabled");
		return FALSE;
	}

	if (url == NULL || line == NULL) {
		debug_print("queue_fetch: NULL params");
		return FALSE;
	}

	url_type = image_preview_classify_url(url);
	is_page_url = (url_type != URL_TYPE_DIRECT_IMAGE);
	debug_print("queue_fetch: url_type=%d is_page_url=%d", url_type, is_page_url);

	rec = image_preview_get(line);
	if (rec != NULL) {
		if (rec->fetch_pending) {
			debug_print("queue_fetch: already fetching (rec=%p url=%s)",
			            (void*)rec, rec->url ? rec->url : "(null)");
			image_fetch_debug_dump();
			return FALSE;
		}
		if (rec->cache_path != NULL && !rec->fetch_failed) {
			/* Verify cache file still exists */
			if (access(rec->cache_path, R_OK) == 0) {
				debug_print("queue_fetch: already cached at %s", rec->cache_path);
				signal_emit("image preview ready", 2, line, window);
				return TRUE;
			} else {
				/* Cache file disappeared - reset for fresh fetch */
				debug_print("queue_fetch: cache file missing, will re-fetch");
				g_free(rec->cache_path);
				rec->cache_path = NULL;
				rec->retry_count = 0;
			}
		}
		if (rec->fetch_failed) {
			debug_print("queue_fetch: previous fetch failed");
			return FALSE;
		}
	} else {
		rec = g_new0(IMAGE_PREVIEW_REC, 1);
		rec->line = line;
		rec->window = window;
		rec->url = g_strdup(url);
		g_hash_table_insert(image_previews, line, rec);
	}

	cache_path = image_cache_get(url);
	if (cache_path != NULL) {
		/* Double-check file exists (image_cache_get validates magic bytes) */
		if (access(cache_path, R_OK) == 0) {
			debug_print("queue_fetch: CACHED at %s", cache_path);
			rec->cache_path = cache_path;
			rec->fetch_pending = FALSE;
			rec->fetch_failed = FALSE;
			signal_emit("image preview ready", 2, line, window);
			return TRUE;
		} else {
			debug_print("queue_fetch: cache_get returned path but file missing: %s", cache_path);
			g_free(cache_path);
			/* Continue to fetch */
		}
	}

	if (rec->cache_path == NULL) {
		GChecksum *checksum;
		const char *hash;
		const char *ext;
		char *cache_dir;

		checksum = g_checksum_new(G_CHECKSUM_SHA256);
		g_checksum_update(checksum, (guchar *)url, strlen(url));
		hash = g_checksum_get_string(checksum);

		if (is_page_url) {
			ext = ".img";
		} else {
			ext = strrchr(url, '.');
			if (ext == NULL || strlen(ext) > 6 || strchr(ext, '/') != NULL) {
				ext = ".img";
			}
		}

		cache_dir = g_strdup_printf("%s/%s", get_irssi_dir(), IMAGE_CACHE_DIR);
		rec->cache_path = g_strdup_printf("%s/%s%s", cache_dir, hash, ext);
		g_free(cache_dir);
		g_checksum_free(checksum);
	}

	rec->fetch_pending = TRUE;
	rec->fetch_failed = FALSE;

	debug_print("queue_fetch: starting fetch to %s", rec->cache_path);
	if (!image_fetch_start(url, rec->cache_path, line, window, is_page_url)) {
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

/* Clear all rendered previews */
void image_preview_clear_planes(void)
{
	GHashTableIter iter;
	gpointer key, value;

	if (image_previews == NULL)
		return;

	g_hash_table_iter_init(&iter, image_previews);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		IMAGE_PREVIEW_REC *rec = value;
		if (rec->rendered != NULL) {
			g_string_free(rec->rendered, TRUE);
			rec->rendered = NULL;
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

/* Render visible image previews for a view - stub for now */
void image_preview_render_view(TEXT_BUFFER_VIEW_REC *view, WINDOW_REC *window)
{
	/* TODO: Implement inline thumbnails if needed */
	(void)view;
	(void)window;
}

/* NOTE: sig_gui_print_text_finished was removed.
 * We no longer scan lines on display - URL detection happens only on click
 * in sig_mouse_button_clicked() -> find_url_in_line().
 * This avoids unnecessary processing and allows dynamic detection. */

/* Signal: window changed */
static void sig_window_changed(WINDOW_REC *window)
{
	(void)window;
	image_preview_clear_planes();
	popup_preview_dismiss();
}

/* Signal: image preview ready */
static void sig_image_preview_ready(LINE_REC *line, WINDOW_REC *window)
{
	IMAGE_PREVIEW_REC *preview;

	debug_print("sig_image_preview_ready: line=%p", line);

	if (!image_preview_enabled())
		return;

	if (window == NULL)
		return;

	preview = image_preview_get(line);
	if (preview == NULL) {
		debug_print("sig_image_preview_ready: no preview record");
		return;
	}

	if (preview->cache_path == NULL) {
		debug_print("sig_image_preview_ready: no cache_path");
		return;
	}

	debug_print("sig_image_preview_ready: cached %s, show_on_complete=%d",
	            preview->cache_path, preview->show_on_complete);

	if (preview->show_on_complete) {
		debug_print("sig_image_preview_ready: showing popup");
		preview->show_on_complete = FALSE;
		popup_preview_show_for_line(preview->cache_path, line);
	}
}

/* Timer callback to clean up cached image 30 seconds after display */
static gboolean cache_cleanup_callback(gpointer user_data)
{
	IMAGE_PREVIEW_REC *preview = user_data;

	debug_print("CACHE_CLEANUP: timer fired for preview %p", (void*)preview);

	if (preview == NULL)
		return FALSE;

	/* Clear the timer tag */
	preview->cache_cleanup_tag = 0;

	/* Delete the cached file */
	if (preview->cache_path != NULL) {
		debug_print("CACHE_CLEANUP: deleting %s", preview->cache_path);
		unlink(preview->cache_path);
		g_free(preview->cache_path);
		preview->cache_path = NULL;
	}

	/* Clear rendered content */
	if (preview->rendered != NULL) {
		g_string_free(preview->rendered, TRUE);
		preview->rendered = NULL;
	}

	/* Reset retry count for next fetch */
	preview->retry_count = 0;
	preview->fetch_failed = FALSE;
	g_free(preview->error_message);
	preview->error_message = NULL;

	debug_print("CACHE_CLEANUP: done, preview reset for next click");
	return FALSE;  /* Don't repeat timer */
}

/* Dismiss popup preview */
static void popup_preview_dismiss(void)
{
	if (!popup_preview_showing)
		return;

	debug_print("POPUP: dismissing preview");

	popup_preview_showing = FALSE;
	popup_x = popup_y = 0;
	popup_width = popup_height = 0;
	popup_current_line = NULL;

	if (popup_content != NULL) {
		g_string_free(popup_content, TRUE);
		popup_content = NULL;
	}

	/* Clear the popup from screen:
	 * - Kitty: sends delete-all sequence (graphics are in separate layer)
	 * - iTerm2/Sixel: redraws mainwindow to overwrite image with text
	 * Sidepanels are not touched since popup only covers mainwindow area. */
	image_render_clear_graphics();
}

/* Show error popup (when fetch fails after retry) */
void image_preview_show_error_popup(void)
{
	MAIN_WINDOW_REC *mainwin;
	int mw_top, mw_left, mw_height, mw_width;
	int rows = 0;
	GString *error_content;

	debug_print("ERROR_POPUP: showing error icon");

	/* Get main window dimensions for centering */
	mainwin = WINDOW_MAIN(active_win);
	if (mainwin != NULL) {
		mw_top = mainwin->first_line + mainwin->statusbar_lines_top;
		mw_left = mainwin->first_column;
		mw_height = mainwin->height - mainwin->statusbar_lines;
		mw_width = mainwin->width;
	} else {
		mw_top = 0;
		mw_left = 0;
		mw_height = term_height;
		mw_width = term_width;
	}

	/* Dismiss any existing popup first */
	popup_preview_dismiss();

	/* Render error icon */
	error_content = image_render_error_icon(mw_width / 4, mw_height / 4, &rows);
	if (error_content == NULL) {
		debug_print("ERROR_POPUP: failed to render error icon");
		return;
	}

	/* Store as popup content */
	popup_content = error_content;

	/* Center position within main window */
	popup_y = mw_top + (mw_height - rows) / 2;
	popup_x = mw_left + (mw_width - 8) / 2;  /* Error icon is about 8 cols wide */
	popup_width = 8;
	popup_height = rows;

	debug_print("ERROR_POPUP: position y=%d x=%d size %dx%d", popup_y, popup_x, popup_width, popup_height);

	popup_preview_showing = TRUE;

	/* Output the error icon */
	{
		const char *tmux_env = g_getenv("TMUX");
		gboolean in_tmux = (tmux_env != NULL && *tmux_env != '\0');

		/* Save cursor position */
		fprintf(stdout, "\0337");
		/* Move to popup position */
		fprintf(stdout, "\033[%d;%dH", popup_y + 1, popup_x + 1);

		if (in_tmux) {
			/* Wrap in tmux DCS passthrough */
			size_t i;
			fprintf(stdout, "\033Ptmux;");
			for (i = 0; i < popup_content->len; i++) {
				char c = popup_content->str[i];
				if (c == '\033') {
					fprintf(stdout, "\033\033");
				} else {
					fputc(c, stdout);
				}
			}
			fprintf(stdout, "\033\\");
		} else {
			fwrite(popup_content->str, 1, popup_content->len, stdout);
		}

		/* Restore cursor position */
		fprintf(stdout, "\0338");
		fflush(stdout);

		debug_print("ERROR_POPUP: shown successfully (%zu bytes)", popup_content->len);
	}
}

/* Show centered popup preview for an image with optional LINE_REC for cache tracking */
static void popup_preview_show_for_line(const char *image_path, LINE_REC *line)
{
	MAIN_WINDOW_REC *mainwin;
	IMAGE_PREVIEW_REC *preview;
	int max_width, max_height;
	int mw_top, mw_left, mw_height, mw_width;
	int rows = 0;

	debug_print("POPUP: showing preview for %s (line=%p)", image_path, (void*)line);

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
		mw_top = 0;
		mw_left = 0;
		mw_height = term_height;
		mw_width = term_width;
		debug_print("POPUP: no mainwin, using terminal size");
	}

	/* Dismiss any existing popup first */
	popup_preview_dismiss();

	/* Calculate max size (50% of main window) */
	max_width = mw_width / 2;
	max_height = mw_height / 2;
	if (max_width < 20) max_width = 20;
	if (max_height < 10) max_height = 10;

	/* Render using Chafa */
	popup_content = image_render_chafa(image_path, max_width, max_height, &rows);
	if (popup_content == NULL) {
		debug_print("POPUP: failed to render image - invalidating cache");

		/* Render failed - cache file is likely corrupt or incomplete */
		/* Delete the bad cache file and reset preview state */
		if (line != NULL) {
			preview = image_preview_get(line);
			if (preview != NULL && preview->cache_path != NULL) {
				debug_print("POPUP: deleting corrupt cache file: %s", preview->cache_path);
				unlink(preview->cache_path);
				g_free(preview->cache_path);
				preview->cache_path = NULL;
				/* Reset state so next click can retry */
				preview->fetch_pending = FALSE;
				preview->fetch_failed = FALSE;
				preview->retry_count = 0;
				g_free(preview->error_message);
				preview->error_message = NULL;
			}
		}
		return;
	}

	/* Center position within main window */
	popup_y = mw_top + (mw_height - rows) / 2;
	popup_x = mw_left + (mw_width - max_width) / 2;
	popup_width = max_width;
	popup_height = rows;
	popup_current_line = line;

	debug_print("POPUP: position y=%d x=%d size %dx%d", popup_y, popup_x, popup_width, popup_height);

	popup_preview_showing = TRUE;

	/* Start 30-second cache cleanup timer for this preview */
	if (line != NULL) {
		preview = image_preview_get(line);
		if (preview != NULL) {
			/* Cancel any existing timer */
			if (preview->cache_cleanup_tag != 0) {
				g_source_remove(preview->cache_cleanup_tag);
				preview->cache_cleanup_tag = 0;
			}
			/* Start new 30-second timer */
			preview->cache_cleanup_tag = g_timeout_add(30000, cache_cleanup_callback, preview);
			debug_print("POPUP: started 30-second cache cleanup timer (tag=%u)", preview->cache_cleanup_tag);
		}
	}

	/* Check if we're in tmux and need DCS passthrough */
	{
		const char *tmux_env = g_getenv("TMUX");
		gboolean in_tmux = (tmux_env != NULL && *tmux_env != '\0');

		if (in_tmux) {
			debug_print("POPUP: tmux detected, using DCS passthrough");
		}

		/* Move cursor to position and output the image */
		/* Save cursor position */
		fprintf(stdout, "\0337");
		/* Move to popup position */
		fprintf(stdout, "\033[%d;%dH", popup_y + 1, popup_x + 1);

		if (in_tmux) {
			/* Wrap in tmux DCS passthrough - need to double all ESC chars */
			size_t i;
			fprintf(stdout, "\033Ptmux;");
			for (i = 0; i < popup_content->len; i++) {
				char c = popup_content->str[i];
				if (c == '\033') {
					/* Double the escape for tmux passthrough */
					fprintf(stdout, "\033\033");
				} else {
					fputc(c, stdout);
				}
			}
			fprintf(stdout, "\033\\");
		} else {
			/* Direct output */
			fwrite(popup_content->str, 1, popup_content->len, stdout);
		}

		/* Restore cursor position */
		fprintf(stdout, "\0338");
		fflush(stdout);

		debug_print("POPUP: preview shown successfully (%zu bytes, tmux=%d)", popup_content->len, in_tmux);
	}
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

	text_area_top = mainwin->first_line + mainwin->statusbar_lines_top;

	if (screen_y < text_area_top)
		return NULL;

	current_y = text_area_top;

	for (line = view->startline; line != NULL; line = line->next) {
		LINE_CACHE_REC *cache = textbuffer_view_get_line_cache(view, line);
		line_count = cache ? cache->count : 1;

		if (screen_y >= current_y && screen_y < current_y + line_count) {
			return line;
		}

		current_y += line_count;

		if (current_y >= text_area_top + view->height)
			break;
	}

	return NULL;
}

/* Check if click X coordinate is within mainwindow text area (not sidepanels) */
static gboolean is_click_in_text_area(MAIN_WINDOW_REC *mainwin, int x)
{
	int text_left, text_right;

	if (mainwin == NULL)
		return FALSE;

	/* Text area starts after left statusbar columns (sidepanel) */
	text_left = mainwin->first_column + mainwin->statusbar_columns_left;
	/* Text area ends before right statusbar columns (sidepanel) */
	text_right = mainwin->first_column + mainwin->width - mainwin->statusbar_columns_right;

	return (x >= text_left && x < text_right);
}

/* Dynamically find URL in line text at click time (doesn't rely on registration) */
static char *find_url_in_line(TEXT_BUFFER_REC *buffer, LINE_REC *line)
{
	GString *str;
	GSList *urls;
	char *url = NULL;

	if (buffer == NULL || line == NULL)
		return NULL;

	str = g_string_new(NULL);
	textbuffer_line2text(buffer, line, FALSE, str);

	if (str->len == 0) {
		g_string_free(str, TRUE);
		return NULL;
	}

	urls = image_preview_find_urls(str->str);
	g_string_free(str, TRUE);

	if (urls != NULL) {
		/* Take first URL found */
		url = g_strdup(urls->data);
		g_slist_free_full(urls, g_free);
	}

	return url;
}

/* Mouse click handler for image preview */
static gboolean image_preview_mouse_handler(const GuiMouseEvent *event, gpointer user_data)
{
	WINDOW_REC *window;
	GUI_WINDOW_REC *gui;
	MAIN_WINDOW_REC *mainwin;
	LINE_REC *line;
	IMAGE_PREVIEW_REC *preview;
	char *url;

	(void)user_data;

	/* Only handle left button press */
	if (event->button != MOUSE_BUTTON_LEFT || !event->press)
		return FALSE;

	/* If popup is showing, dismiss on any click */
	if (popup_preview_showing) {
		popup_preview_dismiss();
		return TRUE;
	}

	if (!image_preview_enabled())
		return FALSE;

	window = active_win;
	if (window == NULL)
		return FALSE;

	gui = WINDOW_GUI(window);
	if (gui == NULL || gui->view == NULL)
		return FALSE;

	mainwin = WINDOW_MAIN(window);
	if (mainwin == NULL)
		return FALSE;

	/* Check if click is in text area (not sidepanels) */
	if (!is_click_in_text_area(mainwin, event->x)) {
		debug_print("CLICK: x=%d is outside text area (sidepanel), ignoring", event->x);
		return FALSE;
	}

	debug_print("CLICK: at y=%d x=%d (in text area)", event->y, event->x);

	line = find_line_at_screen_y(gui->view, mainwin, event->y);
	if (line == NULL) {
		debug_print("CLICK: no line at position");
		return FALSE;
	}

	/* Dynamically scan line for URLs (doesn't rely on prior registration) */
	url = find_url_in_line(gui->view->buffer, line);
	if (url == NULL) {
		debug_print("CLICK: no image URL found in line");
		return FALSE;
	}

	debug_print("CLICK: found URL in line: %s", url);

	/* Check if we already have a preview record for this line */
	preview = image_preview_get(line);

	/* Case 1: Already cached - verify file exists then show popup */
	if (preview != NULL && preview->cache_path != NULL &&
	    !preview->fetch_pending && !preview->fetch_failed) {
		/* Verify cache file still exists and is valid */
		if (access(preview->cache_path, R_OK) == 0) {
			debug_print("CLICK: cached, showing popup for %s", preview->cache_path);
			g_free(url);
			popup_preview_show_for_line(preview->cache_path, line);
			return TRUE;
		} else {
			/* Cache file disappeared - reset and fall through to fetch */
			debug_print("CLICK: cache file missing: %s - will re-fetch", preview->cache_path);
			g_free(preview->cache_path);
			preview->cache_path = NULL;
			preview->retry_count = 0;
			/* Fall through to Case 3 */
		}
	}

	/* Case 2: Fetch in progress - but check if it's actually stuck */
	if (preview != NULL && preview->fetch_pending) {
		debug_print("CLICK: fetch in progress, will show when complete");
		debug_print("CLICK: preview->url=%s retry_count=%d",
		            preview->url ? preview->url : "(null)", preview->retry_count);
		image_fetch_debug_dump();

		/* Recovery: If fetch says "pending" but there's nothing actually happening,
		 * the fetch got stuck (timer stopped without processing completion).
		 * Reset state and allow a new fetch to start. */
		if (!image_fetch_is_active(preview->url ? preview->url : url)) {
			debug_print("CLICK: STUCK FETCH DETECTED! Cleaning up and retrying...");
			/* Clean up the stuck fetch from active_fetches hash table */
			image_fetch_cleanup_stuck(preview->url ? preview->url : url);
			/* Reset preview state */
			preview->fetch_pending = FALSE;
			preview->fetch_failed = FALSE;  /* Not failed, just stuck - allow fresh retry */
			preview->retry_count = 0;  /* Reset retry count for fresh attempt */
			g_free(preview->error_message);
			preview->error_message = NULL;
			/* Fall through to Case 3 to start new fetch */
		} else {
			preview->show_on_complete = TRUE;
			g_free(url);
			return TRUE;
		}
	}

	/* Case 3: Not fetched or previous fetch failed - start fresh fetch */
	debug_print("CLICK: starting fetch for %s (preview=%p)", url, (void*)preview);
	if (preview != NULL) {
		debug_print("CLICK: existing preview: fetch_pending=%d fetch_failed=%d cache_path=%s",
		            preview->fetch_pending, preview->fetch_failed,
		            preview->cache_path ? preview->cache_path : "(null)");
	}

	/* Create or update preview record */
	if (preview == NULL) {
		preview = g_new0(IMAGE_PREVIEW_REC, 1);
		preview->line = line;
		preview->window = window;
		preview->url = g_strdup(url);
		g_hash_table_insert(image_previews, line, preview);
		debug_print("CLICK: created new preview record");
	} else {
		/* Reset failed state for retry */
		preview->fetch_failed = FALSE;
		g_free(preview->error_message);
		preview->error_message = NULL;
		debug_print("CLICK: reset existing preview for retry");
	}

	preview->show_on_complete = TRUE;

	if (!image_preview_queue_fetch(url, line, window)) {
		debug_print("CLICK: queue_fetch FAILED!");
		preview->show_on_complete = FALSE;
		g_free(url);
		return FALSE;
	}

	debug_print("CLICK: fetch started OK, will show popup when complete");
	g_free(url);
	return TRUE;
}

/* Signal: settings changed */
static void sig_setup_changed(void)
{
	gboolean old_debug = image_preview_debug;
	image_preview_debug = settings_get_bool(IMAGE_PREVIEW_DEBUG_SETTING);

	if (image_preview_debug && !old_debug) {
		debug_print("DEBUG ENABLED - Chafa image preview active");
	}
}

/* Key press handler - dismiss popup */
static void sig_key_pressed_preview(gpointer keyp)
{
	unichar key = GPOINTER_TO_INT(keyp);

	if (popup_preview_showing) {
		/* Don't intercept mouse escape sequences */
		if (key == 0x1b || key == '[' || key == '<' ||
		    (key >= '0' && key <= '9') || key == ';' ||
		    key == 'M' || key == 'm')
			return;

		debug_print("KEY: dismissing popup on key 0x%x", key);
		popup_preview_dismiss();
		signal_stop();
	}
}

/* Signal: line removed from textbuffer - cleanup hash table */
static void sig_textbuffer_line_removed(TEXT_BUFFER_VIEW_REC *view,
                                        LINE_REC *line, LINE_REC *prev_line)
{
	(void)view;
	(void)prev_line;

	if (image_previews == NULL || line == NULL)
		return;

	if (g_hash_table_remove(image_previews, line)) {
		debug_print("LINE_REMOVED: cleaned up preview for line %p", line);
	}
}

/* Command: /IMAGE */
static void cmd_image(const char *data, SERVER_REC *server, void *item)
{
	(void)server;
	(void)item;

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
		          "Image preview enabled (Chafa)");
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
	/* Register settings - use fe-text module so they save to correct config section */
	settings_add_bool_module("fe-text", "lookandfeel", IMAGE_PREVIEW_SETTING, FALSE);
	settings_add_int_module("fe-text", "lookandfeel", IMAGE_PREVIEW_MAX_WIDTH,
	                        IMAGE_PREVIEW_DEFAULT_MAX_WIDTH);
	settings_add_int_module("fe-text", "lookandfeel", IMAGE_PREVIEW_MAX_HEIGHT,
	                        IMAGE_PREVIEW_DEFAULT_MAX_HEIGHT);
	settings_add_str_module("fe-text", "lookandfeel", IMAGE_PREVIEW_BLITTER, "auto");
	settings_add_size_module("fe-text", "misc", IMAGE_PREVIEW_CACHE_SIZE,
	                         IMAGE_PREVIEW_DEFAULT_CACHE_SIZE);
	settings_add_time_module("fe-text", "misc", IMAGE_PREVIEW_TIMEOUT,
	                         IMAGE_PREVIEW_DEFAULT_TIMEOUT);
	settings_add_int_module("fe-text", "misc", IMAGE_PREVIEW_MAX_FILE_SIZE,
	                        IMAGE_PREVIEW_DEFAULT_MAX_FILE_SIZE);
	settings_add_bool_module("fe-text", "lookandfeel", IMAGE_PREVIEW_DEBUG_SETTING, FALSE);

	image_preview_debug = settings_get_bool(IMAGE_PREVIEW_DEBUG_SETTING);

	if (!init_url_patterns()) {
		g_warning("image-preview: Failed to initialize URL patterns");
		return;
	}

	image_previews = g_hash_table_new_full(g_direct_hash, g_direct_equal,
	                                       NULL, (GDestroyNotify)image_preview_rec_free);

	image_cache_init();
	image_fetch_init();

	/* NOTE: We don't scan lines on display anymore - only on click.
	 * signal_add("gui print text finished", ...) was removed intentionally.
	 * URL detection happens dynamically in sig_mouse_button_clicked(). */
	signal_add("window changed", (SIGNAL_FUNC)sig_window_changed);
	signal_add("image preview ready", (SIGNAL_FUNC)sig_image_preview_ready);
	signal_add("setup changed", (SIGNAL_FUNC)sig_setup_changed);
	signal_add_first("gui key pressed", (SIGNAL_FUNC)sig_key_pressed_preview);
	signal_add("gui textbuffer line removed", (SIGNAL_FUNC)sig_textbuffer_line_removed);

	gui_mouse_add_handler(image_preview_mouse_handler, NULL);

	command_bind("image", NULL, (SIGNAL_FUNC)cmd_image);

	debug_print("Image preview module initialized (Chafa backend)");
}

/* Module deinitialization */
void image_preview_deinit(void)
{
	popup_preview_dismiss();

	gui_mouse_remove_handler(image_preview_mouse_handler, NULL);

	command_unbind("image", (SIGNAL_FUNC)cmd_image);

	signal_remove("gui textbuffer line removed", (SIGNAL_FUNC)sig_textbuffer_line_removed);
	signal_remove("gui key pressed", (SIGNAL_FUNC)sig_key_pressed_preview);
	signal_remove("setup changed", (SIGNAL_FUNC)sig_setup_changed);
	signal_remove("image preview ready", (SIGNAL_FUNC)sig_image_preview_ready);
	signal_remove("window changed", (SIGNAL_FUNC)sig_window_changed);

	image_fetch_deinit();
	image_cache_deinit();

	if (image_previews != NULL) {
		g_hash_table_destroy(image_previews);
		image_previews = NULL;
	}

	deinit_url_patterns();

	if (debug_file != NULL) {
		fclose(debug_file);
		debug_file = NULL;
	}
}
