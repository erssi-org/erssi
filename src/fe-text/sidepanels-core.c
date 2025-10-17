/*
 * sidepanels-core.c : erssi
 *
 * Copyright (C) 2024-2025 erssi-org team
 * Lead Developer: Jerzy (kofany) Dąbrowski <https://github.com/kofany>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "module.h"
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/fe-text/mainwindows.h>
#include <irssi/src/fe-text/sidepanels.h>
#include <irssi/src/fe-text/sidepanels-render.h>
#include <irssi/src/fe-text/sidepanels-activity.h>
#include <irssi/src/fe-text/sidepanels-signals.h>
#include <irssi/src/fe-text/sidepanels-layout.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-text/module-formats.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-text/textbuffer-view.h>
#include <stdarg.h>
#include <stdlib.h>
#include "gui-mouse.h"
#include "gui-gestures.h"

/* Settings */
static int sp_left_width;
static int sp_right_width;
static int sp_enable_left;
static int sp_enable_right;
static int sp_auto_hide_right;
static int sp_enable_mouse;
static int sp_debug;
static int sp_auto_create_separators;

/* Mouse scroll setting - still needed for sidepanel scrolling */
static int mouse_scroll_chat;

/* SP_MAINWIN_CTX is now defined in sidepanels-types.h */

static GHashTable *mw_to_ctx;

/* Logging system */
static FILE *sp_log;

static void sp_log_open(void)
{
	if (!sp_log)
		sp_log = fopen("/tmp/irssi_sidepanels.log", "a");
}

static void sp_log_close(void)
{
	if (sp_log) {
		fclose(sp_log);
		sp_log = NULL;
	}
}

void sp_logf(const char *fmt, ...)
{
	va_list args;
	time_t now;
	struct tm *tm_info;
	char timestamp[32];

	if (!sp_debug)
		return;

	sp_log_open();
	if (!sp_log)
		return;

	/* Add timestamp */
	time(&now);
	tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
	fprintf(sp_log, "[%s] ", timestamp);

	/* Add the actual message */
	va_start(args, fmt);
	vfprintf(sp_log, fmt, args);
	va_end(args);
	fprintf(sp_log, "\n");
	fflush(sp_log);
}

SP_MAINWIN_CTX *get_ctx(MAIN_WINDOW_REC *mw, gboolean create)
{
	SP_MAINWIN_CTX *ctx = g_hash_table_lookup(mw_to_ctx, mw);
	if (!ctx && create) {
		ctx = g_new0(SP_MAINWIN_CTX, 1);
		g_hash_table_insert(mw_to_ctx, mw, ctx);
	}
	return ctx;
}

static void destroy_ctx(MAIN_WINDOW_REC *mw)
{
	SP_MAINWIN_CTX *ctx = g_hash_table_lookup(mw_to_ctx, mw);
	if (!ctx)
		return;
	if (ctx->left_tw) {
		term_window_destroy(ctx->left_tw);
		ctx->left_tw = NULL;
	}
	if (ctx->right_tw) {
		term_window_destroy(ctx->right_tw);
		ctx->right_tw = NULL;
	}
	if (ctx->right_order) {
		g_slist_free(ctx->right_order);
		ctx->right_order = NULL;
	}
	g_hash_table_remove(mw_to_ctx, mw);
	g_free(ctx);
}

static void read_settings(void)
{
	int old_mouse = sp_enable_mouse;

	sp_left_width = settings_get_int("sidepanel_left_width");
	sp_right_width = settings_get_int("sidepanel_right_width");
	sp_enable_left = settings_get_bool("sidepanel_left");
	sp_enable_right = settings_get_bool("sidepanel_right");
	sp_auto_hide_right = settings_get_bool("sidepanel_right_auto_hide");
	sp_enable_mouse = TRUE; /* always on natively */
	sp_debug = settings_get_bool("sidepanel_debug");
	sp_auto_create_separators = settings_get_bool("auto_create_separators");

	/* Mouse Gesture Settings - delegate to gesture system */
	gui_gestures_reload_settings();
	mouse_scroll_chat = settings_get_bool("mouse_scroll_chat");

	/* Nick mention color is now handled through theme formats */

	apply_reservations_all();
	apply_and_redraw();
	if (!old_mouse)
		gui_mouse_enable_tracking();
}

/* Forward declarations for mouse handling */
static gboolean is_in_chat_area(int x, int y);
static gboolean chat_area_validator(int x, int y, gpointer user_data);
static gboolean handle_application_mouse_event(const GuiMouseEvent *event, gpointer user_data);
static gboolean handle_click_at(int x, int y, int button);

/* Wrapper function to maintain public API - now delegates to gui-mouse */
gboolean sidepanels_try_parse_mouse_key(unichar key)
{
	if (!sp_enable_mouse)
		return FALSE;
	
	/* Delegate to gui-mouse system */
	return gui_mouse_try_parse_key(key);
}

static void sig_mainwindow_created(MAIN_WINDOW_REC *mw)
{
	/* Panel reservations are now handled dynamically in position_tw() for better control */
	(void) mw;
}

static void sig_irssi_init_finished(void)
{
	/* Initialize Notices window programmatically before renumbering */
	if (get_auto_create_separators()) {
		initialize_notices_window();
	}
	
	/* Renumber windows to ensure proper order */
	renumber_windows_by_position();
	apply_reservations_all();
	apply_and_redraw();
}

void sidepanels_init(void)
{
	settings_add_bool("lookandfeel", "sidepanel_left", TRUE);
	settings_add_bool("lookandfeel", "sidepanel_right", TRUE);
	settings_add_int("lookandfeel", "sidepanel_left_width", 15);
	settings_add_int("lookandfeel", "sidepanel_right_width", 15);
	settings_add_bool("lookandfeel", "sidepanel_right_auto_hide", TRUE);

	settings_add_bool("lookandfeel", "sidepanel_debug", FALSE);
	settings_add_bool("lookandfeel", "mouse_scroll_chat", TRUE);
	settings_add_bool("lookandfeel", "auto_create_separators", TRUE);
	
	sp_enable_mouse = TRUE; /* force native */
	
	/* Initialize subsystems */
	sidepanels_render_init();
	sidepanels_activity_init();
	sidepanels_signals_init();
	sidepanels_layout_init();
	
	/* Read settings - mouse and gesture systems are already initialized */
	read_settings();
	
	/* Register event handlers */
	gui_mouse_add_handler(gui_gestures_handle_mouse_event, NULL);
	gui_mouse_add_handler(handle_application_mouse_event, NULL);
	
	/* Set up gesture area validator */
	gui_gestures_set_area_validator(chat_area_validator, NULL);
	
	/* Enable mouse tracking if gestures or scroll are enabled */
	if (gui_gestures_is_enabled() || mouse_scroll_chat) {
		gui_mouse_enable_tracking();
	}
	
	mw_to_ctx = g_hash_table_new(g_direct_hash, g_direct_equal);
	/* Apply reservations but don't redraw yet - wait for irssi init finished */
	apply_reservations_all();
	signal_add("irssi init finished", (SIGNAL_FUNC) sig_irssi_init_finished);
	signal_add("mainwindow created", (SIGNAL_FUNC) sig_mainwindow_created);
	signal_add("setup changed", (SIGNAL_FUNC) read_settings);
	
	/* Register all signal handlers */
	sidepanels_signals_register();
}

void sidepanels_deinit(void)
{
	GSList *tmp;
	
	/* Unregister all signal handlers */
	sidepanels_signals_unregister();
	
	signal_remove("irssi init finished", (SIGNAL_FUNC) sig_irssi_init_finished);
	signal_remove("mainwindow created", (SIGNAL_FUNC) sig_mainwindow_created);
	signal_remove("setup changed", (SIGNAL_FUNC) read_settings);
	
	/* Remove reservations */
	for (tmp = mainwindows; tmp != NULL; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		destroy_ctx(mw);
		if (mw->statusbar_columns_left)
			mainwindow_set_statusbar_columns(mw, -mw->statusbar_columns_left, 0);
		if (mw->statusbar_columns_right)
			mainwindow_set_statusbar_columns(mw, 0, -mw->statusbar_columns_right);
	}
	if (mw_to_ctx) {
		g_hash_table_destroy(mw_to_ctx);
		mw_to_ctx = NULL;
	}
	
	gui_mouse_disable_tracking();
	
	/* Deinitialize subsystems */
	sidepanels_layout_deinit();
	sidepanels_signals_deinit();
	sidepanels_activity_deinit();
	sidepanels_render_deinit();
	
	/* Mouse and gesture systems are cleaned up by main irssi.c */
	
	sp_log_close();
}

/* Settings accessors */
int get_sp_left_width(void) { return sp_left_width; }
int get_sp_right_width(void) { return sp_right_width; }
int get_sp_enable_left(void) { return sp_enable_left; }
int get_sp_enable_right(void) { return sp_enable_right; }
int get_sp_auto_hide_right(void) { return sp_auto_hide_right; }
int get_sp_enable_mouse(void) { return sp_enable_mouse; }
int get_sp_debug(void) { return sp_debug; }
int get_mouse_scroll_chat(void) { return mouse_scroll_chat; }
int get_auto_create_separators(void) { return sp_auto_create_separators; }

/* =============================================================================
 * MOUSE HANDLING IMPLEMENTATION
 * =============================================================================
 */

static gboolean is_in_chat_area(int x, int y)
{
	GSList *tmp;

	for (tmp = mainwindows; tmp; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
		int chat_x, chat_y, chat_w, chat_h;

		if (!ctx)
			continue;

		/* Calculate chat area bounds (main window minus sidepanels) */
		chat_x = mw->first_column;
		if (ctx->left_tw && ctx->left_h > 0) {
			chat_x += ctx->left_w;
		}
		chat_y = mw->first_line + mw->statusbar_lines_top;
		chat_w = mw->width - mw->statusbar_columns;
		if (ctx->left_tw && ctx->left_h > 0) {
			chat_w -= ctx->left_w;
		}
		if (ctx->right_tw && ctx->right_h > 0) {
			chat_w -= ctx->right_w;
		}
		chat_h = mw->height - mw->statusbar_lines;

		/* Check if coordinates are within chat area */
		if (x >= chat_x && x < chat_x + chat_w &&
		    y >= chat_y && y < chat_y + chat_h) {
			sp_logf("MOUSE: is_in_chat_area(%d,%d) = TRUE (chat area: %d,%d %dx%d)",
			        x, y, chat_x, chat_y, chat_w, chat_h);
			return TRUE;
		}
	}

	sp_logf("MOUSE: is_in_chat_area(%d,%d) = FALSE", x, y);
	return FALSE;
}

static gboolean chat_area_validator(int x, int y, gpointer user_data)
{
	(void) user_data;
	return is_in_chat_area(x, y);
}

static gboolean handle_click_at(int x, int y, int button)
{
	GSList *mt;
	/* Debug: Mouse clicks - useful for mouse interaction debugging */
	sp_logf("MOUSE_CLICK: at x=%d y=%d button=%d", x, y, button);

	/* Check if this is a chat area click and handle scroll */
	if (mouse_scroll_chat && button == 1 && is_in_chat_area(x, y)) {
		/* For now, just log that we detected a chat area click */
		sp_logf("MOUSE_CLICK: Chat area click detected - scroll support ready");
	}
	for (mt = mainwindows; mt; mt = mt->next) {
		MAIN_WINDOW_REC *mw = mt->data;
		SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
		if (!ctx)
			continue;
		if (ctx->left_tw) {
			int px = ctx->left_x, py = ctx->left_y, pw = ctx->left_w, ph = ctx->left_h;
			if (x >= px && x < px + pw && y >= py && y < py + ph) {
				int row = y - py;
				int target_index = row + ctx->left_scroll_offset;
				int idx = 0;
				GSList *sort_list, *s;

				/* Get sorted list using shared function */
				sort_list = build_sorted_window_list();

				/* Find the window at target_index */
				for (s = sort_list; s; s = s->next) {
					if (idx++ == target_index) {
						WINDOW_SORT_REC *sort_rec = s->data;
						WINDOW_REC *win = sort_rec->win;

						/* Debug: Window switches via mouse - tracks click
						 * navigation */
						sp_logf(
						    "MOUSE_CLICK: switching to window %d (was %d)",
						    win ? win->refnum : -1,
						    active_win ? active_win->refnum : -1);
						ctx->left_selected_index = target_index;
						if (win)
							window_set_active(win);
						redraw_one(mw);
						irssi_set_dirty();
						free_sorted_window_list(sort_list);
						return TRUE;
					}
				}

				free_sorted_window_list(sort_list);
			}
		}
		if (ctx->right_tw) {
			int px = ctx->right_x, py = ctx->right_y, pw = ctx->right_w,
			    ph = ctx->right_h;
			if (x >= px && x < px + pw && y >= py && y < py + ph) {
				int row = y - py;
				WINDOW_REC *aw = mw->active;
				if (aw && IS_CHANNEL(aw->active)) {
					CHANNEL_REC *ch = CHANNEL(aw->active);
					int target_index = ctx->right_scroll_offset + row;
					int count = g_slist_length(ctx->right_order);
					if (target_index >= 0 && target_index < count) {
						NICK_REC *nick = g_slist_nth_data(ctx->right_order,
						                                  target_index);
						ctx->right_selected_index = target_index;
						if (nick && nick->nick)
							signal_emit("command query", 3, nick->nick,
							            ch->server, ch);
						redraw_one(mw);
						irssi_set_dirty();
						return TRUE;
					}
				}
			}
		}
	}
	return FALSE;
}

/* Handle application-specific mouse events (scroll, clicks) */
static gboolean handle_application_mouse_event(const GuiMouseEvent *event, gpointer user_data)
{
	GSList *mt;

	(void) user_data;

	/* Handle scroll events */
	if (gui_mouse_is_scroll_event(event->raw_button) && event->press) {
		int dir = event->raw_button - 64;
		int delta = (dir == 0) ? -3 : 3;
		int px, py, pw, ph;

		/* Handle chat area scrolling first if enabled */
		if (mouse_scroll_chat && is_in_chat_area(event->x, event->y)) {
			const char *str;
			double count;
			int scroll_lines;

			sp_logf("MOUSE_SCROLL: Chat area scroll, delta=%d", delta);

			/* Calculate scroll count like gui-readline.c does */
			str = settings_get_str("scroll_page_count");
			count = atof(str + (*str == '/'));
			if (count == 0)
				count = 1;
			else if (count < 0)
				count = active_mainwin->height - active_mainwin->statusbar_lines + count;
			else if (count < 1)
				count = 1.0 / count;

			if (*str == '/' || *str == '.') {
				count = (active_mainwin->height - active_mainwin->statusbar_lines) / count;
			}
			scroll_lines = (int) count;

			/* Scroll active window using same function as PageUp/PageDown */
			gui_window_scroll(active_win, delta < 0 ? -scroll_lines : scroll_lines);
			return TRUE;
		}

		/* Handle sidepanel scrolling */
		for (mt = mainwindows; mt; mt = mt->next) {
			MAIN_WINDOW_REC *mw = mt->data;
			SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
			if (!ctx)
				continue;
			if (ctx->left_tw) {
				px = ctx->left_x;
				py = ctx->left_y;
				pw = ctx->left_w;
				ph = ctx->left_h;
				if (event->x >= px && event->x < px + pw &&
				    event->y >= py && event->y < py + ph) {
					ctx->left_scroll_offset = MAX(0, ctx->left_scroll_offset + delta);
					redraw_one(mw);
					irssi_set_dirty();
					return TRUE;
				}
			}
			if (ctx->right_tw) {
				px = ctx->right_x;
				py = ctx->right_y;
				pw = ctx->right_w;
				ph = ctx->right_h;
				if (event->x >= px && event->x < px + pw &&
				    event->y >= py && event->y < py + ph) {
					ctx->right_scroll_offset = MAX(0, ctx->right_scroll_offset + delta);
					redraw_one(mw);
					irssi_set_dirty();
					return TRUE;
				}
			}
		}
		return TRUE; /* Consume scroll events */
	}

	/* Handle click events */
	if (event->button == MOUSE_BUTTON_LEFT && event->press) {
		return handle_click_at(event->x, event->y, event->button);
	}

	return FALSE; /* Don't consume other events */
}
