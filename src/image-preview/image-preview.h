/*
 image-preview.h : Image preview support for erssi (Chafa-based)

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef IRSSI_IMAGE_PREVIEW_H
#define IRSSI_IMAGE_PREVIEW_H

#include <glib.h>

/* Forward declarations */
typedef struct _LINE_REC LINE_REC;
typedef struct _WINDOW_REC WINDOW_REC;
typedef struct _TEXT_BUFFER_VIEW_REC TEXT_BUFFER_VIEW_REC;

/* Image preview record - tracks preview state for a message line */
typedef struct {
	LINE_REC *line;              /* Associated text line */
	WINDOW_REC *window;          /* Target window */
	char *url;                   /* Original image URL */
	char *cache_path;            /* Local cached file path */
	GString *rendered;           /* Rendered escape sequences (from Chafa) */
	int height_rows;             /* Height in terminal rows */
	int y_position;              /* Y position when rendered */
	gboolean fetch_pending;      /* Fetch in progress */
	gboolean fetch_failed;       /* Fetch/render failed */
	gboolean show_on_complete;   /* Show popup when fetch completes (user clicked) */
	char *error_message;         /* Error description if failed */
} IMAGE_PREVIEW_REC;

/* Fetch stage for two-stage page URL handling */
typedef enum {
	FETCH_STAGE_IMAGE = 0,      /* Direct image fetch (default) */
	FETCH_STAGE_HTML = 1,       /* Fetching HTML page for og:image */
	FETCH_STAGE_OG_IMAGE = 2    /* Fetching extracted og:image URL */
} FetchStage;

/* URL type classification */
typedef enum {
	URL_TYPE_DIRECT_IMAGE,   /* Direct .jpg/.png etc */
	URL_TYPE_PAGE_IMGUR,     /* imgur.com/xxx page */
	URL_TYPE_PAGE_IMGBB,     /* ibb.co/xxx page */
	URL_TYPE_PAGE_KERMIT,    /* kermit.pw/xxx */
	URL_TYPE_PAGE_GENERIC    /* Other pages (try og:image) */
} ImageUrlType;

/* Image fetch state for async HTTP requests */
typedef struct _IMAGE_FETCH_REC IMAGE_FETCH_REC;

/* Module initialization/deinitialization */
void image_preview_init(void);
void image_preview_deinit(void);

/* Check if image preview is enabled */
gboolean image_preview_enabled(void);

/* Debug output (writes to ~/.erssi/image-preview-debug.log) */
void image_preview_debug_print(const char *fmt, ...);

/* Find image URLs in text, returns GSList of newly allocated strings */
GSList *image_preview_find_urls(const char *text);

/* Register an image URL for a line (no fetch, just tracking) */
gboolean image_preview_register_url(const char *url, LINE_REC *line, WINDOW_REC *window);

/* Queue an image fetch for the given URL (starts download immediately) */
gboolean image_preview_queue_fetch(const char *url, LINE_REC *line, WINDOW_REC *window);

/* Cancel pending fetch for URL */
void image_preview_cancel_fetch(const char *url);

/* Clear all rendered image previews */
void image_preview_clear_planes(void);

/* Clear all previews and cached data */
void image_preview_clear_all(void);

/* Get preview record for a line (or NULL) */
IMAGE_PREVIEW_REC *image_preview_get(LINE_REC *line);

/* Render visible image previews for a view */
void image_preview_render_view(TEXT_BUFFER_VIEW_REC *view, WINDOW_REC *window);

/* Cache management */
void image_cache_init(void);
void image_cache_deinit(void);
gboolean image_cache_has(const char *url);
char *image_cache_get(const char *url);
gboolean image_cache_store(const char *url, const char *source_path);
void image_cache_clear_all(void);
void image_cache_cleanup(void);
void image_cache_print_stats(void);

/* Async fetch management */
void image_fetch_init(void);
void image_fetch_deinit(void);
gboolean image_fetch_start(const char *url, const char *cache_path,
                           LINE_REC *line, WINDOW_REC *window,
                           gboolean is_page_url);
void image_fetch_cancel(const char *url);
void image_fetch_cancel_all(void);

/* URL classification */
ImageUrlType image_preview_classify_url(const char *url);

/* Chafa rendering */
GString *image_render_chafa(const char *image_path,
                            int max_cols,
                            int max_rows,
                            int *out_rows);
void image_render_popup(const char *image_path, int x, int y);
void image_render_popup_close(void);
void image_render_clear_graphics(void);

/* Settings names */
#define IMAGE_PREVIEW_SETTING           "image_preview"
#define IMAGE_PREVIEW_MAX_WIDTH         "image_preview_max_width"
#define IMAGE_PREVIEW_MAX_HEIGHT        "image_preview_max_height"
#define IMAGE_PREVIEW_CACHE_SIZE        "image_preview_cache_size"
#define IMAGE_PREVIEW_TIMEOUT           "image_preview_timeout"
#define IMAGE_PREVIEW_MAX_FILE_SIZE     "image_preview_max_file_size"
#define IMAGE_PREVIEW_BLITTER           "image_preview_blitter"
#define IMAGE_PREVIEW_DEBUG_SETTING     "image_preview_debug"

/* Default values */
#define IMAGE_PREVIEW_DEFAULT_MAX_WIDTH   40
#define IMAGE_PREVIEW_DEFAULT_MAX_HEIGHT  10
#define IMAGE_PREVIEW_DEFAULT_CACHE_SIZE  "100M"
#define IMAGE_PREVIEW_DEFAULT_TIMEOUT     "60s"
#define IMAGE_PREVIEW_DEFAULT_MAX_FILE_SIZE 10  /* MB */

/* Cache directory name under ~/.erssi/ */
#define IMAGE_CACHE_DIR "image_cache"

#endif /* IRSSI_IMAGE_PREVIEW_H */
