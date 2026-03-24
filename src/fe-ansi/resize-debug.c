/*
 * resize-debug.c : erssi
 *
 * Comprehensive resize debugging system for terminal resize analysis.
 * Logs all stages of resize handling to help diagnose terminal-specific issues.
 *
 * Log file: ~/.erssi/resize-$TERM-$LC_TERMINAL.log
 *
 * Copyright (C) 2024-2025 erssi-org team
 */

#include "module.h"
#include "resize-debug.h"
#include <irssi/src/core/settings.h>
#include <irssi/src/core/special-vars.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/ioctl.h>

static FILE *debug_log = NULL;
static gboolean debug_enabled = FALSE;
static struct timeval sequence_start;
static int sequence_count = 0;
static char log_path[512] = {0};

/* Get current timestamp with microseconds */
static void get_timestamp(char *buf, size_t len)
{
	struct timeval tv;
	struct tm *tm_info;
	char time_buf[64];

	gettimeofday(&tv, NULL);
	tm_info = localtime(&tv.tv_sec);
	strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
	snprintf(buf, len, "%s.%06ld", time_buf, (long)tv.tv_usec);
}

/* Get elapsed time since sequence start in milliseconds */
static double get_elapsed_ms(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - sequence_start.tv_sec) * 1000.0 +
	       (now.tv_usec - sequence_start.tv_usec) / 1000.0;
}

/* Sanitize environment variable for filename */
static void sanitize_for_filename(char *str)
{
	while (*str) {
		if (*str == '/' || *str == '\\' || *str == ':' ||
		    *str == '*' || *str == '?' || *str == '"' ||
		    *str == '<' || *str == '>' || *str == '|' ||
		    *str == ' ') {
			*str = '_';
		}
		str++;
	}
}

/* Get terminal info and build log filename */
static void build_log_path(void)
{
	const char *home = g_get_home_dir();
	const char *term = g_getenv("TERM");
	const char *lc_terminal = g_getenv("LC_TERMINAL");
	const char *tmux = g_getenv("TMUX");
	const char *sty = g_getenv("STY");  /* screen session */
	const char *term_program = g_getenv("TERM_PROGRAM");
	const char *ghostty = g_getenv("GHOSTTY_RESOURCES_DIR");
	const char *kitty = g_getenv("KITTY_PID");
	const char *wezterm = g_getenv("WEZTERM_PANE");

	char term_safe[64] = "unknown";
	char terminal_safe[64] = "unknown";
	char multiplexer[32] = "none";

	/* Determine TERM */
	if (term && *term) {
		g_strlcpy(term_safe, term, sizeof(term_safe));
		sanitize_for_filename(term_safe);
	}

	/* Determine actual terminal (priority order) */
	if (ghostty) {
		g_strlcpy(terminal_safe, "ghostty", sizeof(terminal_safe));
	} else if (kitty) {
		g_strlcpy(terminal_safe, "kitty", sizeof(terminal_safe));
	} else if (wezterm) {
		g_strlcpy(terminal_safe, "wezterm", sizeof(terminal_safe));
	} else if (lc_terminal && *lc_terminal) {
		g_strlcpy(terminal_safe, lc_terminal, sizeof(terminal_safe));
		sanitize_for_filename(terminal_safe);
	} else if (term_program && *term_program) {
		g_strlcpy(terminal_safe, term_program, sizeof(terminal_safe));
		sanitize_for_filename(terminal_safe);
	}

	/* Determine multiplexer */
	if (tmux && *tmux) {
		g_strlcpy(multiplexer, "tmux", sizeof(multiplexer));
	} else if (sty && *sty) {
		g_strlcpy(multiplexer, "screen", sizeof(multiplexer));
	}

	snprintf(log_path, sizeof(log_path),
	         "%s/.erssi/resize-%s-%s-%s.log",
	         home, term_safe, terminal_safe, multiplexer);
}

/* Write terminal environment info to log */
static void log_environment_info(void)
{
	struct winsize ws;

	fprintf(debug_log, "========================================\n");
	fprintf(debug_log, "ERSSI RESIZE DEBUG LOG\n");
	fprintf(debug_log, "========================================\n\n");

	fprintf(debug_log, "=== ENVIRONMENT ===\n");
	fprintf(debug_log, "TERM=%s\n", g_getenv("TERM") ?: "(not set)");
	fprintf(debug_log, "LC_TERMINAL=%s\n", g_getenv("LC_TERMINAL") ?: "(not set)");
	fprintf(debug_log, "TERM_PROGRAM=%s\n", g_getenv("TERM_PROGRAM") ?: "(not set)");
	fprintf(debug_log, "TERM_PROGRAM_VERSION=%s\n", g_getenv("TERM_PROGRAM_VERSION") ?: "(not set)");
	fprintf(debug_log, "COLORTERM=%s\n", g_getenv("COLORTERM") ?: "(not set)");
	fprintf(debug_log, "TMUX=%s\n", g_getenv("TMUX") ? "yes" : "no");
	fprintf(debug_log, "STY (screen)=%s\n", g_getenv("STY") ? "yes" : "no");
	fprintf(debug_log, "GHOSTTY_RESOURCES_DIR=%s\n", g_getenv("GHOSTTY_RESOURCES_DIR") ? "yes" : "no");
	fprintf(debug_log, "KITTY_PID=%s\n", g_getenv("KITTY_PID") ?: "(not set)");
	fprintf(debug_log, "WEZTERM_PANE=%s\n", g_getenv("WEZTERM_PANE") ?: "(not set)");
	fprintf(debug_log, "ITERM_SESSION_ID=%s\n", g_getenv("ITERM_SESSION_ID") ?: "(not set)");

	/* Get current terminal size via ioctl */
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
		fprintf(debug_log, "\n=== INITIAL TERMINAL SIZE ===\n");
		fprintf(debug_log, "Columns: %d\n", ws.ws_col);
		fprintf(debug_log, "Rows: %d\n", ws.ws_row);
		fprintf(debug_log, "Pixel width: %d\n", ws.ws_xpixel);
		fprintf(debug_log, "Pixel height: %d\n", ws.ws_ypixel);
	}

	fprintf(debug_log, "\n=== RESIZE EVENTS ===\n");
	fprintf(debug_log, "(Format: [timestamp] +elapsed_ms STAGE: message)\n\n");
	fflush(debug_log);
}

void resize_debug_init(void)
{
	char *dir;

	settings_add_bool("lookandfeel", "resize_debug", FALSE);

	debug_enabled = settings_get_bool("resize_debug");

	if (!debug_enabled)
		return;

	build_log_path();

	/* Ensure directory exists */
	dir = g_path_get_dirname(log_path);
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);

	/* Open log file (append mode) */
	debug_log = fopen(log_path, "a");
	if (!debug_log) {
		g_warning("resize-debug: Cannot open log file: %s", log_path);
		debug_enabled = FALSE;
		return;
	}

	log_environment_info();

	resize_debug_log("INIT", "Resize debug logging started - log file: %s", log_path);
}

void resize_debug_deinit(void)
{
	if (debug_log) {
		resize_debug_log("DEINIT", "Resize debug logging stopped");
		fclose(debug_log);
		debug_log = NULL;
	}
	debug_enabled = FALSE;
}

gboolean resize_debug_enabled(void)
{
	return debug_enabled && debug_log != NULL;
}

void resize_debug_log(const char *stage, const char *fmt, ...)
{
	char timestamp[32];
	va_list args;

	if (!debug_enabled || !debug_log)
		return;

	get_timestamp(timestamp, sizeof(timestamp));

	fprintf(debug_log, "[%s] +%8.3fms %-20s: ",
	        timestamp, get_elapsed_ms(), stage);

	va_start(args, fmt);
	vfprintf(debug_log, fmt, args);
	va_end(args);

	fprintf(debug_log, "\n");
	fflush(debug_log);
}

void resize_debug_dimensions(const char *stage, int old_cols, int old_rows,
                              int new_cols, int new_rows)
{
	if (!debug_enabled || !debug_log)
		return;

	resize_debug_log(stage, "Dimensions: %dx%d -> %dx%d (delta: %+d cols, %+d rows)",
	                 old_cols, old_rows, new_cols, new_rows,
	                 new_cols - old_cols, new_rows - old_rows);
}

void resize_debug_cache(const char *operation, const char *panel, int window_id)
{
	if (!debug_enabled || !debug_log)
		return;

	resize_debug_log("CACHE", "%s %s cache for mainwindow %d",
	                 operation, panel, window_id);
}

void resize_debug_redraw(const char *type, int window_id)
{
	if (!debug_enabled || !debug_log)
		return;

	if (window_id >= 0) {
		resize_debug_log("REDRAW", "%s for mainwindow %d", type, window_id);
	} else {
		resize_debug_log("REDRAW", "%s (all windows)", type);
	}
}

void resize_debug_start_sequence(void)
{
	if (!debug_enabled || !debug_log)
		return;

	gettimeofday(&sequence_start, NULL);
	sequence_count++;

	fprintf(debug_log, "\n>>> RESIZE SEQUENCE #%d START >>>\n", sequence_count);
	fflush(debug_log);
}

void resize_debug_end_sequence(void)
{
	double elapsed;

	if (!debug_enabled || !debug_log)
		return;

	elapsed = get_elapsed_ms();

	fprintf(debug_log, "<<< RESIZE SEQUENCE #%d END (total: %.3f ms) <<<\n\n",
	        sequence_count, elapsed);
	fflush(debug_log);
}

void resize_debug_flush(void)
{
	if (debug_log) {
		fflush(debug_log);
	}
}
