/*
 image-preview-fetch.c : Async HTTP image fetching for erssi-nc

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

#include <curl/curl.h>
#include <string.h>
#include <errno.h>

/* Maximum HTML size for og:image extraction (512KB) */
#define MAX_HTML_SIZE (512 * 1024)

/* Fetch request state */
struct _IMAGE_FETCH_REC {
	char *url;               /* URL being fetched */
	char *cache_path;        /* Target cache path */
	CURL *curl_handle;       /* curl easy handle */
	FILE *fp;                /* Output file handle */
	LINE_REC *line;          /* Associated line in textbuffer */
	WINDOW_REC *window;      /* Target window */
	gint64 start_time;       /* Start timestamp for timeout */
	gint64 content_length;   /* Content-Length from response */
	gboolean cancelled;      /* Cancellation flag */

	/* Two-stage fetch support for page URLs */
	FetchStage stage;        /* Current fetch stage */
	char *original_url;      /* Original page URL (for stage 2) */
	GString *html_buffer;    /* HTML response buffer (stage 1) */
};

/* curl multi-handle for concurrent requests */
static CURLM *curl_multi = NULL;

/* Active fetches: url -> IMAGE_FETCH_REC */
static GHashTable *active_fetches = NULL;

/* GMainLoop timer for processing */
static guint curl_timer_tag = 0;

/* Maximum concurrent fetches */
#define MAX_CONCURRENT_FETCHES 3

/* Forward declarations */
static void fetch_complete(IMAGE_FETCH_REC *fetch, gboolean success, const char *error);
static void fetch_rec_free(IMAGE_FETCH_REC *fetch);
static void image_fetch_start_stage2(IMAGE_FETCH_REC *fetch, const char *og_image_url);
static char *extract_og_image(const char *html);

/* Track bytes written for debug */
static gint64 total_bytes_written = 0;

/* Write callback for curl */
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	IMAGE_FETCH_REC *fetch = userdata;
	size_t total = size * nmemb;
	size_t written;
	static gint64 last_log = 0;

	if (fetch->cancelled) {
		image_preview_debug_print("FETCH: write_callback cancelled");
		return 0;  /* Abort transfer */
	}

	if (fetch->fp == NULL) {
		image_preview_debug_print("FETCH: write_callback fp is NULL!");
		return 0;
	}

	written = fwrite(ptr, size, nmemb, fetch->fp);
	total_bytes_written += written;

	/* Log every ~100KB */
	if (total_bytes_written - last_log > 100000) {
		image_preview_debug_print("FETCH: write_callback stage=%d written=%zu total=%" G_GINT64_FORMAT,
		                          fetch->stage, written, total_bytes_written);
		last_log = total_bytes_written;
	}

	if (written != total) {
		image_preview_debug_print("FETCH: write_callback ERROR: wrote %zu of %zu bytes!",
		                          written, total);
	}

	return written;
}

/* Header callback for curl - check Content-Length and log headers */
static size_t header_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	IMAGE_FETCH_REC *fetch = userdata;
	size_t total = size * nmemb;
	char *header = ptr;
	gint64 max_size;

	/* Log important headers for debugging */
	if (g_ascii_strncasecmp(header, "HTTP/", 5) == 0) {
		/* Log HTTP status line */
		char status[128];
		char *nl;
		size_t len = total < 127 ? total : 127;
		strncpy(status, header, len);
		status[len] = '\0';
		/* Remove trailing newlines */
		nl = strchr(status, '\r');
		if (nl) *nl = '\0';
		nl = strchr(status, '\n');
		if (nl) *nl = '\0';
		image_preview_debug_print("FETCH: HTTP response: %s (stage=%d)", status, fetch->stage);
	}

	if (g_ascii_strncasecmp(header, "Content-Type:", 13) == 0) {
		char ctype[256];
		char *nl;
		size_t len = total < 255 ? total : 255;
		strncpy(ctype, header, len);
		ctype[len] = '\0';
		nl = strchr(ctype, '\r');
		if (nl) *nl = '\0';
		nl = strchr(ctype, '\n');
		if (nl) *nl = '\0';
		image_preview_debug_print("FETCH: %s (stage=%d)", ctype, fetch->stage);
	}

	/* Check for Content-Length header */
	if (g_ascii_strncasecmp(header, "Content-Length:", 15) == 0) {
		fetch->content_length = g_ascii_strtoll(header + 15, NULL, 10);
		image_preview_debug_print("FETCH: Content-Length: %lld (stage=%d)",
		                          (long long)fetch->content_length, fetch->stage);

		/* For HTML stage, limit is different */
		if (fetch->stage == FETCH_STAGE_HTML) {
			if (fetch->content_length > MAX_HTML_SIZE) {
				image_preview_debug_print("FETCH: HTML too large in header, cancelling");
				fetch->cancelled = TRUE;
				return 0;
			}
			return total;
		}

		/* Check against max file size for image downloads */
		max_size = settings_get_int(IMAGE_PREVIEW_MAX_FILE_SIZE) * 1024 * 1024;
		if (fetch->content_length > max_size) {
			image_preview_debug_print("FETCH: Image too large (%lld > %lld), cancelling",
			                          (long long)fetch->content_length, (long long)max_size);
			fetch->cancelled = TRUE;
			return 0;  /* Abort */
		}
	}

	return total;
}

/* Write callback for HTML pages (stage 1) - accumulate in buffer */
static size_t write_callback_html(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	IMAGE_FETCH_REC *fetch = userdata;
	size_t total = size * nmemb;

	if (fetch->cancelled) {
		image_preview_debug_print("FETCH: write_callback_html - cancelled, returning 0");
		return 0;
	}

	if (fetch->html_buffer == NULL) {
		image_preview_debug_print("FETCH: write_callback_html - html_buffer is NULL!");
		return 0;
	}

	/* Accumulate HTML in buffer */
	g_string_append_len(fetch->html_buffer, ptr, total);

	image_preview_debug_print("FETCH: write_callback_html - received %zu bytes, total now %zu",
	                          total, fetch->html_buffer->len);

	/* Enforce size limit */
	if (fetch->html_buffer->len > MAX_HTML_SIZE) {
		image_preview_debug_print("FETCH: HTML too large (%zu > %d), cancelling",
		                          fetch->html_buffer->len, MAX_HTML_SIZE);
		fetch->cancelled = TRUE;
		return 0;
	}

	return total;
}

/* Extract og:image URL from HTML content */
static char *extract_og_image(const char *html)
{
	GRegex *regex;
	GMatchInfo *match_info;
	char *og_image = NULL;
	GError *error = NULL;

	if (html == NULL || *html == '\0') {
		image_preview_debug_print("FETCH: extract_og_image - HTML is NULL or empty");
		return NULL;
	}

	image_preview_debug_print("FETCH: extract_og_image - parsing %zu bytes of HTML", strlen(html));

	/* Log first 500 chars of HTML for debugging */
	if (strlen(html) > 0) {
		char preview[501];
		strncpy(preview, html, 500);
		preview[500] = '\0';
		/* Replace newlines with spaces for logging */
		for (char *p = preview; *p; p++) {
			if (*p == '\n' || *p == '\r') *p = ' ';
		}
		image_preview_debug_print("FETCH: HTML preview: %.200s...", preview);
	}

	/* Pattern matches both attribute orders:
	 * <meta property="og:image" content="URL">
	 * <meta content="URL" property="og:image"> */
	regex = g_regex_new(
		"<meta[^>]+property=[\"']og:image[\"'][^>]+content=[\"']([^\"']+)[\"']"
		"|<meta[^>]+content=[\"']([^\"']+)[\"'][^>]+property=[\"']og:image[\"']",
		G_REGEX_CASELESS | G_REGEX_DOTALL, 0, &error);

	if (error != NULL) {
		image_preview_debug_print("FETCH: og:image regex compile failed: %s", error->message);
		g_error_free(error);
		return NULL;
	}

	if (g_regex_match(regex, html, 0, &match_info)) {
		image_preview_debug_print("FETCH: regex matched!");
		/* Try first capture group */
		og_image = g_match_info_fetch(match_info, 1);
		image_preview_debug_print("FETCH: group 1 = '%s'", og_image ? og_image : "(null)");
		if (og_image == NULL || *og_image == '\0') {
			g_free(og_image);
			/* Try second capture group */
			og_image = g_match_info_fetch(match_info, 2);
			image_preview_debug_print("FETCH: group 2 = '%s'", og_image ? og_image : "(null)");
		}
	} else {
		image_preview_debug_print("FETCH: regex did NOT match - no og:image meta tag found");
		/* Try to find any og:image text in HTML for debugging */
		if (strstr(html, "og:image") != NULL) {
			image_preview_debug_print("FETCH: 'og:image' string exists but regex didn't match pattern");
		} else {
			image_preview_debug_print("FETCH: 'og:image' string NOT found in HTML at all");
		}
	}

	g_match_info_free(match_info);
	g_regex_unref(regex);

	if (og_image != NULL && *og_image != '\0') {
		image_preview_debug_print("FETCH: SUCCESS extracted og:image: %s", og_image);
	} else {
		g_free(og_image);
		og_image = NULL;
		image_preview_debug_print("FETCH: FAILED to extract og:image");
	}

	return og_image;
}

/* Process curl events - called from GMainLoop */
static gboolean curl_process(gpointer data)
{
	int still_running;
	CURLMcode mc;
	CURLMsg *msg;
	int msgs_left;
	static int call_count = 0;
	int numfds;

	if (curl_multi == NULL)
		return FALSE;

	call_count++;

	/* Use curl_multi_poll to wait for activity (up to 1ms) - much more efficient */
	mc = curl_multi_poll(curl_multi, NULL, 0, 1, &numfds);
	if (mc != CURLM_OK) {
		image_preview_debug_print("FETCH: curl_multi_poll failed: %s",
		                          curl_multi_strerror(mc));
	}

	/* Perform transfers */
	mc = curl_multi_perform(curl_multi, &still_running);
	if (mc != CURLM_OK) {
		image_preview_debug_print("FETCH: curl_multi_perform failed: %s",
		                          curl_multi_strerror(mc));
	}

	/* Log periodically to confirm timer is running */
	if (call_count % 500 == 0 && still_running > 0) {
		image_preview_debug_print("FETCH: timer tick #%d, still_running=%d", call_count, still_running);
	}

	/* Check for completed transfers */
	while ((msg = curl_multi_info_read(curl_multi, &msgs_left)) != NULL) {
		if (msg->msg == CURLMSG_DONE) {
			CURL *easy = msg->easy_handle;
			CURLcode result = msg->data.result;
			IMAGE_FETCH_REC *fetch = NULL;

			/* Get our fetch record from the easy handle */
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &fetch);

			image_preview_debug_print("FETCH: transfer done, result=%d (%s)",
			                          result, curl_easy_strerror(result));

			if (fetch != NULL) {
				if (result == CURLE_OK && !fetch->cancelled) {
					fetch_complete(fetch, TRUE, NULL);
				} else if (fetch->cancelled) {
					fetch_complete(fetch, FALSE, "Cancelled or file too large");
				} else {
					fetch_complete(fetch, FALSE, curl_easy_strerror(result));
				}
			}
		}
	}

	/* Re-check still_running in case new transfers were added during
	 * completion handling (e.g., stage 2 of a two-stage fetch).
	 * The fetch_complete() -> image_fetch_start_stage2() path may have
	 * added new handles that we need to continue processing. */
	mc = curl_multi_perform(curl_multi, &still_running);

	/* Continue if there are active transfers */
	if (still_running > 0) {
		return TRUE;
	}

	/* No more transfers - stop timer */
	curl_timer_tag = 0;
	return FALSE;
}

/* Start processing timer if not already running */
static void ensure_processing_timer(void)
{
	if (curl_timer_tag == 0) {
		/* Poll every 10ms for faster downloads */
		curl_timer_tag = g_timeout_add(10, curl_process, NULL);
	}
}

/* Handle fetch completion */
static void fetch_complete(IMAGE_FETCH_REC *fetch, gboolean success, const char *error)
{
	IMAGE_PREVIEW_REC *preview;

	if (fetch == NULL)
		return;

	image_preview_debug_print("FETCH: complete url=%s stage=%d success=%d error=%s",
	                          fetch->url, fetch->stage, success, error ? error : "none");

	/* Handle HTML stage completion (stage 1 of two-stage fetch) */
	if (fetch->stage == FETCH_STAGE_HTML) {
		char *og_image = NULL;

		image_preview_debug_print("FETCH: HTML stage complete, success=%d", success);

		/* Remove from curl multi but don't destroy handle yet */
		if (fetch->curl_handle != NULL) {
			curl_multi_remove_handle(curl_multi, fetch->curl_handle);
		}

		if (success && fetch->html_buffer != NULL && fetch->html_buffer->len > 0) {
			image_preview_debug_print("FETCH: HTML buffer has %zu bytes, parsing for og:image...",
			                          fetch->html_buffer->len);
			/* Parse HTML for og:image */
			og_image = extract_og_image(fetch->html_buffer->str);
		} else {
			image_preview_debug_print("FETCH: HTML stage failed - success=%d buffer=%p len=%zu",
			                          success,
			                          (void*)fetch->html_buffer,
			                          fetch->html_buffer ? fetch->html_buffer->len : 0);
		}

		if (og_image != NULL) {
			/* Start stage 2: fetch the actual image */
			image_preview_debug_print("FETCH: og:image found! Starting stage 2 fetch for: %s", og_image);
			image_fetch_start_stage2(fetch, og_image);
			g_free(og_image);
			return;  /* Don't cleanup yet - stage 2 will continue */
		}

		/* HTML fetch failed or no og:image found - mark as failed */
		image_preview_debug_print("FETCH: HTML stage FAILED - no og:image found, giving up");

		/* Cleanup curl handle now */
		if (fetch->curl_handle != NULL) {
			curl_easy_cleanup(fetch->curl_handle);
			fetch->curl_handle = NULL;
		}

		/* Update preview record with failure */
		preview = image_preview_get(fetch->line);
		if (preview != NULL) {
			preview->fetch_pending = FALSE;
			preview->fetch_failed = TRUE;
			preview->error_message = g_strdup("No og:image found in page");
		}

		/* Remove from active fetches */
		if (active_fetches != NULL) {
			g_hash_table_remove(active_fetches, fetch->original_url ? fetch->original_url : fetch->url);
		}
		return;
	}

	/* Direct image fetch or stage 2 (og:image fetch) completion */
	image_preview_debug_print("FETCH: image download complete, stage=%d total_bytes=%" G_GINT64_FORMAT,
	                          fetch->stage, total_bytes_written);

	/* Close file */
	if (fetch->fp != NULL) {
		fflush(fetch->fp);
		fclose(fetch->fp);
		fetch->fp = NULL;
	}

	/* Remove from curl multi */
	if (fetch->curl_handle != NULL) {
		curl_multi_remove_handle(curl_multi, fetch->curl_handle);
		curl_easy_cleanup(fetch->curl_handle);
		fetch->curl_handle = NULL;
	}

	/* Update preview record */
	preview = image_preview_get(fetch->line);
	if (preview != NULL) {
		preview->fetch_pending = FALSE;
		if (success) {
			preview->fetch_failed = FALSE;
			image_preview_debug_print("FETCH: saved to %s", fetch->cache_path);
		} else {
			preview->fetch_failed = TRUE;
			preview->error_message = g_strdup(error ? error : "Unknown error");
			/* Remove failed download file */
			if (fetch->cache_path != NULL) {
				unlink(fetch->cache_path);
			}
		}
	} else {
		image_preview_debug_print("FETCH: WARNING preview record is NULL!");
	}

	/* Emit signal for UI update */
	if (success) {
		image_preview_debug_print("FETCH: emitting 'image preview ready' signal");
		signal_emit("image preview ready", 2, fetch->line, fetch->window);
	}

	/* Remove from active fetches - use original_url if this was a stage 2 fetch */
	if (active_fetches != NULL) {
		const char *key = fetch->original_url ? fetch->original_url : fetch->url;
		g_hash_table_remove(active_fetches, key);
	}
}

/* Free fetch record */
static void fetch_rec_free(IMAGE_FETCH_REC *fetch)
{
	if (fetch == NULL)
		return;

	if (fetch->fp != NULL) {
		fclose(fetch->fp);
	}

	if (fetch->curl_handle != NULL) {
		if (curl_multi != NULL) {
			curl_multi_remove_handle(curl_multi, fetch->curl_handle);
		}
		curl_easy_cleanup(fetch->curl_handle);
	}

	if (fetch->html_buffer != NULL) {
		g_string_free(fetch->html_buffer, TRUE);
	}

	g_free(fetch->url);
	g_free(fetch->cache_path);
	g_free(fetch->original_url);
	g_free(fetch);
}

/* Start stage 2: fetch actual image from og:image URL */
static void image_fetch_start_stage2(IMAGE_FETCH_REC *fetch, const char *og_image_url)
{
	CURLMcode mc;
	int timeout_ms;

	image_preview_debug_print("FETCH: stage2 start og_image=%s", og_image_url);

	/* Reset bytes counter for stage 2 */
	total_bytes_written = 0;

	/* Cleanup stage 1 resources */
	if (fetch->html_buffer != NULL) {
		g_string_free(fetch->html_buffer, TRUE);
		fetch->html_buffer = NULL;
	}

	/* Setup stage 2 */
	fetch->stage = FETCH_STAGE_OG_IMAGE;
	g_free(fetch->url);
	fetch->url = g_strdup(og_image_url);
	fetch->content_length = 0;

	/* Open cache file for image */
	fetch->fp = fopen(fetch->cache_path, "wb");
	if (fetch->fp == NULL) {
		image_preview_debug_print("FETCH: stage2 failed to open cache file: %s", strerror(errno));
		fetch_complete(fetch, FALSE, "Failed to open cache file");
		return;
	}

	/* Reconfigure curl for image fetch */
	curl_easy_reset(fetch->curl_handle);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_URL, og_image_url);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_WRITEDATA, fetch);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_HEADERFUNCTION, header_callback);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_HEADERDATA, fetch);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_PRIVATE, fetch);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_USERAGENT, "erssi-nc/1.0");
	curl_easy_setopt(fetch->curl_handle, CURLOPT_NOSIGNAL, 1L);

	/* Set timeout */
	timeout_ms = settings_get_time(IMAGE_PREVIEW_TIMEOUT);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_TIMEOUT_MS, (long)timeout_ms);

	/* Re-add to multi handle */
	mc = curl_multi_add_handle(curl_multi, fetch->curl_handle);
	if (mc != CURLM_OK) {
		image_preview_debug_print("FETCH: stage2 curl_multi_add_handle failed: %s",
		                          curl_multi_strerror(mc));
		fetch_complete(fetch, FALSE, "curl_multi_add_handle failed");
		return;
	}

	/* CRITICAL: Kick curl immediately after adding stage 2!
	 * We're likely being called from inside curl_process() callback where
	 * still_running was already set to 0 (from stage 1 completion).
	 * If we don't call curl_multi_perform() now, the callback will return
	 * FALSE and stop the timer before stage 2 can be processed.
	 * This call updates still_running so the timer continues. */
	{
		int still_running;
		curl_multi_perform(curl_multi, &still_running);
		image_preview_debug_print("FETCH: stage2 kicked curl, still_running=%d", still_running);
	}

	/* Also ensure timer is running (in case we weren't called from callback) */
	ensure_processing_timer();

	image_preview_debug_print("FETCH: stage2 started successfully");
}

/* Start async fetch */
gboolean image_fetch_start(const char *url, const char *cache_path,
                           LINE_REC *line, WINDOW_REC *window,
                           gboolean is_page_url)
{
	IMAGE_FETCH_REC *fetch;
	CURLMcode mc;
	int timeout_ms;

	image_preview_debug_print("FETCH: start url=%s cache=%s is_page=%d",
	                          url, cache_path, is_page_url);

	if (curl_multi == NULL || url == NULL || cache_path == NULL) {
		image_preview_debug_print("FETCH: start failed - NULL params (multi=%p)", curl_multi);
		return FALSE;
	}

	/* Check if already fetching this URL */
	if (active_fetches != NULL && g_hash_table_contains(active_fetches, url)) {
		image_preview_debug_print("FETCH: already fetching this URL");
		return FALSE;
	}

	/* Check concurrent fetch limit */
	if (active_fetches != NULL && g_hash_table_size(active_fetches) >= MAX_CONCURRENT_FETCHES) {
		image_preview_debug_print("FETCH: concurrent limit reached");
		return FALSE;
	}

	/* Create fetch record */
	fetch = g_new0(IMAGE_FETCH_REC, 1);
	fetch->url = g_strdup(url);
	fetch->cache_path = g_strdup(cache_path);
	fetch->line = line;
	fetch->window = window;
	fetch->start_time = g_get_monotonic_time();
	fetch->cancelled = FALSE;

	/* Setup based on URL type */
	if (is_page_url) {
		/* Stage 1: Fetch HTML page */
		fetch->stage = FETCH_STAGE_HTML;
		fetch->original_url = g_strdup(url);
		fetch->html_buffer = g_string_new(NULL);
		fetch->fp = NULL;  /* No file output in stage 1 */
	} else {
		/* Direct image fetch */
		fetch->stage = FETCH_STAGE_IMAGE;
		fetch->original_url = NULL;
		fetch->html_buffer = NULL;

		/* Open output file */
		fetch->fp = fopen(cache_path, "wb");
		if (fetch->fp == NULL) {
			g_warning("image-fetch: Failed to open %s for writing: %s",
			          cache_path, strerror(errno));
			fetch_rec_free(fetch);
			return FALSE;
		}
	}

	/* Create curl easy handle */
	fetch->curl_handle = curl_easy_init();
	if (fetch->curl_handle == NULL) {
		g_warning("image-fetch: curl_easy_init failed");
		fetch_rec_free(fetch);
		return FALSE;
	}

	/* Configure curl */
	curl_easy_setopt(fetch->curl_handle, CURLOPT_URL, url);
	if (is_page_url) {
		curl_easy_setopt(fetch->curl_handle, CURLOPT_WRITEFUNCTION, write_callback_html);
	} else {
		curl_easy_setopt(fetch->curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
	}
	curl_easy_setopt(fetch->curl_handle, CURLOPT_WRITEDATA, fetch);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_HEADERFUNCTION, header_callback);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_HEADERDATA, fetch);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_PRIVATE, fetch);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_USERAGENT, "erssi-nc/1.0");
	curl_easy_setopt(fetch->curl_handle, CURLOPT_NOSIGNAL, 1L);

	/* Set timeout */
	timeout_ms = settings_get_time(IMAGE_PREVIEW_TIMEOUT);
	curl_easy_setopt(fetch->curl_handle, CURLOPT_TIMEOUT_MS, (long)timeout_ms);

	/* Add to multi handle */
	mc = curl_multi_add_handle(curl_multi, fetch->curl_handle);
	if (mc != CURLM_OK) {
		g_warning("image-fetch: curl_multi_add_handle failed: %s",
		          curl_multi_strerror(mc));
		fetch_rec_free(fetch);
		return FALSE;
	}

	/* Track active fetch - use original_url for page URLs so we can find it later */
	g_hash_table_insert(active_fetches, fetch->original_url ? fetch->original_url : fetch->url, fetch);

	/* Start processing timer */
	ensure_processing_timer();

	image_preview_debug_print("FETCH: started successfully, stage=%d timer running", fetch->stage);
	return TRUE;
}

/* Cancel fetch by URL */
void image_fetch_cancel(const char *url)
{
	IMAGE_FETCH_REC *fetch;

	if (active_fetches == NULL || url == NULL)
		return;

	fetch = g_hash_table_lookup(active_fetches, url);
	if (fetch != NULL) {
		fetch->cancelled = TRUE;
	}
}

/* Cancel all active fetches */
void image_fetch_cancel_all(void)
{
	GHashTableIter iter;
	gpointer key, value;

	if (active_fetches == NULL)
		return;

	g_hash_table_iter_init(&iter, active_fetches);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		IMAGE_FETCH_REC *fetch = value;
		fetch->cancelled = TRUE;
	}
}

/* Initialize fetch system */
void image_fetch_init(void)
{
	/* Initialize curl */
	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Create multi handle */
	curl_multi = curl_multi_init();
	if (curl_multi == NULL) {
		g_warning("image-fetch: curl_multi_init failed");
		return;
	}

	/* Create active fetches hash table */
	active_fetches = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                       NULL, (GDestroyNotify)fetch_rec_free);
}

/* Deinitialize fetch system */
void image_fetch_deinit(void)
{
	/* Stop processing timer */
	if (curl_timer_tag != 0) {
		g_source_remove(curl_timer_tag);
		curl_timer_tag = 0;
	}

	/* Clear active fetches */
	if (active_fetches != NULL) {
		g_hash_table_destroy(active_fetches);
		active_fetches = NULL;
	}

	/* Cleanup curl multi */
	if (curl_multi != NULL) {
		curl_multi_cleanup(curl_multi);
		curl_multi = NULL;
	}

	/* Global curl cleanup */
	curl_global_cleanup();
}
