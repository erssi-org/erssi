/*
 image-preview-cache.c : Image cache management for erssi-nc

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "image-preview.h"

#include <irssi/src/core/settings.h>
#include <irssi/src/core/misc.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/core/levels.h>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* Cache directory path */
static char *cache_dir = NULL;

/* Periodic cleanup timer */
static guint cleanup_timer_tag = 0;

/* Cache statistics */
static struct {
	gint64 total_size;
	int entry_count;
	int hits;
	int misses;
} cache_stats = {0};

/* Ensure cache directory exists */
static gboolean ensure_cache_dir(void)
{
	struct stat st;

	if (cache_dir == NULL) {
		cache_dir = g_strdup_printf("%s/%s", get_irssi_dir(), IMAGE_CACHE_DIR);
	}

	if (stat(cache_dir, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return TRUE;
		}
		g_warning("image-cache: %s exists but is not a directory", cache_dir);
		return FALSE;
	}

	/* Create directory */
	if (mkdir(cache_dir, 0700) != 0) {
		g_warning("image-cache: Failed to create cache directory %s: %s",
		          cache_dir, strerror(errno));
		return FALSE;
	}

	return TRUE;
}

/* Generate cache path for URL (SHA256 hash + extension) */
static char *generate_cache_path(const char *url)
{
	GChecksum *checksum;
	const char *hash;
	const char *ext;
	char *path;

	if (!ensure_cache_dir())
		return NULL;

	checksum = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(checksum, (guchar *)url, strlen(url));
	hash = g_checksum_get_string(checksum);

	/* Extract extension from URL */
	ext = strrchr(url, '.');
	if (ext != NULL) {
		/* Check if it's a valid image extension and not too long */
		if (strlen(ext) <= 6 && strchr(ext, '/') == NULL &&
		    (g_ascii_strcasecmp(ext, ".jpg") == 0 ||
		     g_ascii_strcasecmp(ext, ".jpeg") == 0 ||
		     g_ascii_strcasecmp(ext, ".png") == 0 ||
		     g_ascii_strcasecmp(ext, ".gif") == 0 ||
		     g_ascii_strcasecmp(ext, ".webp") == 0)) {
			/* Keep extension */
		} else {
			ext = ".img";
		}
	} else {
		ext = ".img";
	}

	path = g_strdup_printf("%s/%s%s", cache_dir, hash, ext);
	g_checksum_free(checksum);

	return path;
}

/* Check if URL is cached */
gboolean image_cache_has(const char *url)
{
	char *path;
	struct stat st;
	gboolean exists;

	if (url == NULL)
		return FALSE;

	path = generate_cache_path(url);
	if (path == NULL)
		return FALSE;

	exists = (stat(path, &st) == 0 && S_ISREG(st.st_mode));
	g_free(path);

	if (exists) {
		cache_stats.hits++;
	} else {
		cache_stats.misses++;
	}

	return exists;
}

/* Get cached file path (or NULL if not cached) */
char *image_cache_get(const char *url)
{
	char *path;
	struct stat st;

	if (url == NULL)
		return NULL;

	path = generate_cache_path(url);
	if (path == NULL)
		return NULL;

	if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
		cache_stats.hits++;
		return path;
	}

	cache_stats.misses++;
	g_free(path);
	return NULL;
}

/* Store image in cache (by moving/copying source file) */
gboolean image_cache_store(const char *url, const char *source_path)
{
	char *cache_path;
	struct stat st;
	gchar *contents = NULL;
	gsize length = 0;
	GError *error = NULL;

	if (url == NULL || source_path == NULL)
		return FALSE;

	cache_path = generate_cache_path(url);
	if (cache_path == NULL)
		return FALSE;

	/* Check if source exists */
	if (stat(source_path, &st) != 0) {
		g_free(cache_path);
		return FALSE;
	}

	/* If source and cache paths are the same, we're done */
	if (g_strcmp0(source_path, cache_path) == 0) {
		g_free(cache_path);
		cache_stats.total_size += st.st_size;
		cache_stats.entry_count++;
		return TRUE;
	}

	/* Try to rename (move) first */
	if (rename(source_path, cache_path) == 0) {
		cache_stats.total_size += st.st_size;
		cache_stats.entry_count++;
		g_free(cache_path);
		return TRUE;
	}

	/* Rename failed, try copy */
	if (!g_file_get_contents(source_path, &contents, &length, &error)) {
		g_warning("image-cache: Failed to read %s: %s", source_path, error->message);
		g_error_free(error);
		g_free(cache_path);
		return FALSE;
	}

	if (!g_file_set_contents(cache_path, contents, length, &error)) {
		g_warning("image-cache: Failed to write %s: %s", cache_path, error->message);
		g_error_free(error);
		g_free(contents);
		g_free(cache_path);
		return FALSE;
	}

	g_free(contents);
	g_free(cache_path);

	cache_stats.total_size += length;
	cache_stats.entry_count++;

	return TRUE;
}

/* Clear all cached images */
void image_cache_clear_all(void)
{
	DIR *dir;
	struct dirent *entry;
	char *path;

	if (cache_dir == NULL || !ensure_cache_dir())
		return;

	dir = opendir(cache_dir);
	if (dir == NULL)
		return;

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		path = g_strdup_printf("%s/%s", cache_dir, entry->d_name);
		unlink(path);
		g_free(path);
	}

	closedir(dir);

	cache_stats.total_size = 0;
	cache_stats.entry_count = 0;
}

/* Cleanup old cache entries */
void image_cache_cleanup(void)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char *path;
	time_t now;
	int max_age_seconds;
	gint64 max_size_bytes;
	GSList *files = NULL;
	GSList *l;

	if (cache_dir == NULL || !ensure_cache_dir())
		return;

	now = time(NULL);
	max_age_seconds = 7 * 24 * 60 * 60;  /* 7 days default */
	max_size_bytes = settings_get_size(IMAGE_PREVIEW_CACHE_SIZE);

	dir = opendir(cache_dir);
	if (dir == NULL)
		return;

	/* Collect all files with their stats */
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		path = g_strdup_printf("%s/%s", cache_dir, entry->d_name);
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
			/* Check age - remove if too old */
			if ((now - st.st_mtime) > max_age_seconds) {
				unlink(path);
				g_free(path);
				continue;
			}

			files = g_slist_prepend(files, path);
		} else {
			g_free(path);
		}
	}

	closedir(dir);

	/* Recalculate cache size */
	cache_stats.total_size = 0;
	cache_stats.entry_count = 0;

	for (l = files; l != NULL; l = l->next) {
		path = l->data;
		if (stat(path, &st) == 0) {
			cache_stats.total_size += st.st_size;
			cache_stats.entry_count++;
		}
	}

	/* If still over size limit, remove oldest files */
	if (cache_stats.total_size > max_size_bytes) {
		/* TODO: Sort by mtime and remove oldest until under limit */
		/* For now, just warn */
		g_warning("image-cache: Cache size (%" G_GINT64_FORMAT " bytes) exceeds limit (%" G_GINT64_FORMAT " bytes)",
		          cache_stats.total_size, max_size_bytes);
	}

	g_slist_free_full(files, g_free);
}

/* Periodic cleanup callback */
static gboolean cleanup_timer_cb(gpointer data)
{
	image_cache_cleanup();
	return TRUE;  /* Continue timer */
}

/* Print cache statistics */
void image_cache_print_stats(void)
{
	char *size_str;

	if (cache_stats.total_size >= 1024 * 1024) {
		size_str = g_strdup_printf("%.1f MB", cache_stats.total_size / (1024.0 * 1024.0));
	} else if (cache_stats.total_size >= 1024) {
		size_str = g_strdup_printf("%.1f KB", cache_stats.total_size / 1024.0);
	} else {
		size_str = g_strdup_printf("%" G_GINT64_FORMAT " bytes", cache_stats.total_size);
	}

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "Image cache statistics:");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "  Directory: %s", cache_dir ? cache_dir : "(not initialized)");
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "  Entries: %d", cache_stats.entry_count);
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "  Total size: %s", size_str);
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "  Cache hits: %d", cache_stats.hits);
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "  Cache misses: %d", cache_stats.misses);

	g_free(size_str);
}

/* Initialize cache system */
void image_cache_init(void)
{
	if (!ensure_cache_dir()) {
		g_warning("image-cache: Failed to initialize cache directory");
		return;
	}

	/* Initial cleanup */
	image_cache_cleanup();

	/* Start periodic cleanup timer (every 30 minutes) */
	cleanup_timer_tag = g_timeout_add(30 * 60 * 1000, cleanup_timer_cb, NULL);
}

/* Deinitialize cache system */
void image_cache_deinit(void)
{
	if (cleanup_timer_tag != 0) {
		g_source_remove(cleanup_timer_tag);
		cleanup_timer_tag = 0;
	}

	g_free(cache_dir);
	cache_dir = NULL;
}
