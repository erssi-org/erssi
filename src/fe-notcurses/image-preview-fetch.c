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

/* Write callback for curl */
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	IMAGE_FETCH_REC *fetch = userdata;
	size_t total = size * nmemb;

	if (fetch->cancelled) {
		return 0;  /* Abort transfer */
	}

	if (fetch->fp == NULL) {
		return 0;
	}

	return fwrite(ptr, size, nmemb, fetch->fp);
}

/* Header callback for curl - check Content-Length */
static size_t header_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	IMAGE_FETCH_REC *fetch = userdata;
	size_t total = size * nmemb;
	char *header = ptr;
	gint64 max_size;

	/* Check for Content-Length header */
	if (g_ascii_strncasecmp(header, "Content-Length:", 15) == 0) {
		fetch->content_length = g_ascii_strtoll(header + 15, NULL, 10);

		/* Check against max file size */
		max_size = settings_get_int(IMAGE_PREVIEW_MAX_FILE_SIZE) * 1024 * 1024;
		if (fetch->content_length > max_size) {
			fetch->cancelled = TRUE;
			return 0;  /* Abort */
		}
	}

	return total;
}

/* Process curl events - called from GMainLoop */
static gboolean curl_process(gpointer data)
{
	int still_running;
	CURLMcode mc;
	CURLMsg *msg;
	int msgs_left;

	if (curl_multi == NULL)
		return FALSE;

	/* Perform transfers */
	mc = curl_multi_perform(curl_multi, &still_running);
	if (mc != CURLM_OK) {
		image_preview_debug_print("FETCH: curl_multi_perform failed: %s",
		                          curl_multi_strerror(mc));
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
		/* Poll every 100ms */
		curl_timer_tag = g_timeout_add(100, curl_process, NULL);
	}
}

/* Handle fetch completion */
static void fetch_complete(IMAGE_FETCH_REC *fetch, gboolean success, const char *error)
{
	IMAGE_PREVIEW_REC *preview;

	if (fetch == NULL)
		return;

	image_preview_debug_print("FETCH: complete url=%s success=%d error=%s",
	                          fetch->url, success, error ? error : "none");

	/* Close file */
	if (fetch->fp != NULL) {
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

	/* Remove from active fetches */
	if (active_fetches != NULL) {
		g_hash_table_remove(active_fetches, fetch->url);
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

	g_free(fetch->url);
	g_free(fetch->cache_path);
	g_free(fetch);
}

/* Start async fetch */
gboolean image_fetch_start(const char *url, const char *cache_path,
                           LINE_REC *line, WINDOW_REC *window)
{
	IMAGE_FETCH_REC *fetch;
	CURLMcode mc;
	int timeout_ms;

	image_preview_debug_print("FETCH: start url=%s cache=%s", url, cache_path);

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

	/* Open output file */
	fetch->fp = fopen(cache_path, "wb");
	if (fetch->fp == NULL) {
		g_warning("image-fetch: Failed to open %s for writing: %s",
		          cache_path, strerror(errno));
		fetch_rec_free(fetch);
		return FALSE;
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

	/* Add to multi handle */
	mc = curl_multi_add_handle(curl_multi, fetch->curl_handle);
	if (mc != CURLM_OK) {
		g_warning("image-fetch: curl_multi_add_handle failed: %s",
		          curl_multi_strerror(mc));
		fetch_rec_free(fetch);
		return FALSE;
	}

	/* Track active fetch */
	g_hash_table_insert(active_fetches, fetch->url, fetch);

	/* Start processing timer */
	ensure_processing_timer();

	image_preview_debug_print("FETCH: started successfully, timer running");
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
