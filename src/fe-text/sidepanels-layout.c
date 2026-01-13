/*
 * sidepanels-layout.c : erssi
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
#include <irssi/src/fe-text/sidepanels-layout.h>
#include <irssi/src/fe-text/sidepanels-render.h>
#include <irssi/src/fe-text/resize-debug.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-text/module-formats.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <stdarg.h>
#include <stdlib.h>

/* Minimum width for main window content - if terminal is smaller,
 * sidepanels will be hidden to prevent UI freeze */
#define MIN_MAIN_WINDOW_WIDTH 20

/* SP_MAINWIN_CTX is now defined in sidepanels-types.h */

/* External functions we need */
extern void sp_logf(const char *fmt, ...);
extern SP_MAINWIN_CTX *get_ctx(MAIN_WINDOW_REC *mw, gboolean create);
extern void clear_window_full(TERM_WINDOW *tw, int width, int height);
extern void sp_cache_clear(SP_PANEL_CACHE *cache);
/* Settings are accessed through functions from sidepanels.h */

void apply_reservations_all(void)
{
	GSList *tmp;
	for (tmp = mainwindows; tmp != NULL; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		/* reset previous reservations if any by setting negative, then apply new */
		if (mw->statusbar_columns_left)
			mainwindow_set_statusbar_columns(mw, -mw->statusbar_columns_left, 0);
		if (mw->statusbar_columns_right)
			mainwindow_set_statusbar_columns(mw, 0, -mw->statusbar_columns_right);
		/* Left panel reservations are now handled in position_tw() for better control */
		/* Don't reserve right space here - let auto-hide logic in position_tw() decide */
	}
}

void sig_mainwindow_created(MAIN_WINDOW_REC *mw)
{
	/* Panel reservations are now handled dynamically in position_tw() for better control */
	(void) mw;
}

void setup_ctx_for(MAIN_WINDOW_REC *mw)
{
	SP_MAINWIN_CTX *ctx;
	ctx = get_ctx(mw, TRUE);
	ctx->left_w = (get_sp_enable_left() ? get_sp_left_width() : 0);
	ctx->right_w = (get_sp_enable_right() ? get_sp_right_width() : 0);
	position_tw(mw, ctx);
}

void update_left_selection_to_active(void)
{
	GSList *tmp;

	for (tmp = mainwindows; tmp; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
		WINDOW_REC *aw = mw->active;

		if (!ctx || !aw)
			continue;

		/* Simple: selection index = active window refnum - 1 (0-based indexing) */
		ctx->left_selected_index = aw->refnum - 1;
	}
}

void apply_and_redraw(void)
{
	GSList *tmp;
	for (tmp = mainwindows; tmp; tmp = tmp->next) {
		MAIN_WINDOW_REC *mw = tmp->data;
		setup_ctx_for(mw);
	}
	redraw_all();
}

void position_tw(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx)
{
	int y;
	int h;
	int x;
	int w;
	gboolean show_right;
	gboolean show_left;
	WINDOW_REC *aw;
	int total_sidepanel_width;
	int available_for_main;

	y = mw->first_line + mw->statusbar_lines_top;
	h = mw->height - mw->statusbar_lines;

	/* Calculate total sidepanel width that WOULD BE reserved if we create/keep them.
	 * Use ACTUAL panel state (ctx->*_tw exists) for existing panels,
	 * and desired width (ctx->*_w from settings) for panels we might create. */
	total_sidepanel_width = 0;

	/* For left panel: if it exists, it's already reserved; if not but enabled, we'd want to create it */
	if (ctx->left_tw) {
		/* Panel exists - it has reserved space */
		total_sidepanel_width += ctx->left_w;
	} else if (get_sp_enable_left() && ctx->left_w > 0) {
		/* Panel doesn't exist but we'd want to create it - count potential space */
		total_sidepanel_width += ctx->left_w;
	}

	/* Same for right panel */
	if (ctx->right_tw) {
		total_sidepanel_width += ctx->right_w;
	} else if (get_sp_enable_right() && ctx->right_w > 0) {
		total_sidepanel_width += ctx->right_w;
	}

	/* Check if terminal is wide enough for sidepanels + minimum main window */
	available_for_main = term_width - total_sidepanel_width;

	resize_debug_log("POSITION_TW", "term_width=%d, total_sidepanel=%d (left=%d/%s, right=%d/%s), available_for_main=%d, min_required=%d",
	                 term_width, total_sidepanel_width,
	                 ctx->left_w, ctx->left_tw ? "exists" : "new",
	                 ctx->right_w, ctx->right_tw ? "exists" : "new",
	                 available_for_main, MIN_MAIN_WINDOW_WIDTH);

	/* Collapsed mode: terminal too small for any sidepanels */
	if (available_for_main < MIN_MAIN_WINDOW_WIDTH) {
		/* If panels don't exist and we can't create them, just return */
		if (!ctx->left_tw && !ctx->right_tw) {
			resize_debug_log("POSITION_TW", "COLLAPSED: no space, no panels - nothing to do");
			return;
		}

		resize_debug_log("POSITION_TW", "COLLAPSED MODE: terminal too small, hiding all sidepanels");

		/* Destroy left panel if it exists */
		if (ctx->left_tw) {
			resize_debug_log("POSITION_TW", "destroying left panel");
			clear_window_full(ctx->left_tw, ctx->left_w, ctx->left_h);
			term_window_destroy_left_panel(ctx->left_tw);
			ctx->left_tw = NULL;
			ctx->left_h = 0;
			mainwindows_reserve_columns(-ctx->left_w, 0);
			if (ctx->left_cache)
				sp_cache_clear(ctx->left_cache);
		}

		/* Destroy right panel if it exists */
		if (ctx->right_tw) {
			resize_debug_log("POSITION_TW", "destroying right panel");
			clear_window_full(ctx->right_tw, ctx->right_w, ctx->right_h);
			term_window_destroy_right_panel(ctx->right_tw);
			ctx->right_tw = NULL;
			ctx->right_h = 0;
			mainwindows_reserve_columns(0, -ctx->right_w);
			if (ctx->right_cache)
				sp_cache_clear(ctx->right_cache);
		}

		/* Note: Do NOT call mainwindows_recreate() or signal_emit() here!
		 * That would cause infinite recursion via sig_mainwindow_resized -> redraw_one -> position_tw */
		resize_debug_log("POSITION_TW", "COLLAPSED MODE complete, sidepanels hidden");
		return;
	}

	/* Semi-collapsed mode: only enough space for left panel OR no sidepanels
	 * Priority: hide right panel first (nicklist is less critical than window list) */
	show_left = get_sp_enable_left() && ctx->left_w > 0;
	if (show_left && (term_width - ctx->left_w) < MIN_MAIN_WINDOW_WIDTH) {
		/* Even with just left panel, not enough space - hide it too */
		show_left = FALSE;
		resize_debug_log("POSITION_TW", "SEMI-COLLAPSED: left panel alone would leave %d cols, hiding left panel too",
		                 term_width - ctx->left_w);
	}

	if (show_left) {
		/* Left panel is always at x=0, regardless of main window position */
		x = 0;
		w = ctx->left_w;
		if (ctx->left_tw) {
			/* Panel already exists, just move to correct position */
			term_window_move(ctx->left_tw, x, y, w, h);
		} else {
			/* Reserve space for left panel - this shifts main window right */
			mainwindows_reserve_columns(ctx->left_w, 0);
			/* Use explicit left panel creation - handles ncplane properly */
			ctx->left_tw = term_window_create_left_panel(w);
			/* Force statusbar redraw to fix input box positioning */
			signal_emit("mainwindow resized", 1, mw);
		}
		/* Cache geometry for hit-test */
		ctx->left_x = x;
		ctx->left_y = y;
		ctx->left_h = h;
	} else if (ctx->left_tw) {
		/* Clear the left panel area before destroying */
		clear_window_full(ctx->left_tw, ctx->left_w, ctx->left_h);
		/* Use explicit left panel destruction */
		term_window_destroy_left_panel(ctx->left_tw);
		ctx->left_tw = NULL;
		ctx->left_h = 0;
		/* Free reserved space - this shifts main window back left */
		mainwindows_reserve_columns(-ctx->left_w, 0);
		/* Force complete recreation of mainwindows to clear artifacts */
		mainwindows_recreate();
		/* Force statusbar redraw to fix input box positioning */
		signal_emit("mainwindow resized", 1, mw);
	}

	/* Right panel auto-hide logic */
	aw = mw->active;
	show_right = get_sp_enable_right() && ctx->right_w > 0;
	if (get_sp_auto_hide_right() && show_right) {
		/* Auto-hide: only show if active window is a channel (any type: #, &, !, +) */
		show_right = (aw && aw->active && IS_CHANNEL(aw->active));
	}

	/* Space-based auto-hide: check if there's enough space for right panel
	 * Given current left panel state (show_left and ctx->left_tw) */
	if (show_right) {
		int left_reserved = (show_left || ctx->left_tw) ? ctx->left_w : 0;
		int space_with_right = term_width - left_reserved - ctx->right_w;
		if (space_with_right < MIN_MAIN_WINDOW_WIDTH) {
			resize_debug_log("POSITION_TW", "SPACE-HIDE RIGHT: with right panel, main window would have %d cols (min=%d), hiding right",
			                 space_with_right, MIN_MAIN_WINDOW_WIDTH);
			show_right = FALSE;
		}
	}

	/* DEBUG: Log position_tw auto-hide decision */
	sp_logf("DEBUG position_tw: aw=%p, aw->active=%p, name='%s', IS_CHANNEL=%d, show_right=%d, ctx->right_tw=%p",
	        (void*)aw,
	        aw ? (void*)aw->active : NULL,
	        aw && aw->active ? aw->active->visible_name : "NULL",
	        aw && aw->active ? IS_CHANNEL(aw->active) : -1,
	        show_right,
	        (void*)ctx->right_tw);

	if (show_right) {
		w = ctx->right_w;
		if (ctx->right_tw) {
			/* Panel already exists, space already reserved, use current last_column */
			x = mw->last_column + 1;
			term_window_move(ctx->right_tw, x, y, w, h);
		} else {
			/* Reserve space for right panel - this shrinks main window */
			mainwindows_reserve_columns(0, ctx->right_w);
			/* After reservation, right panel should be at the new last_column + 1 */
			x = mw->last_column + 1;
			/* Use explicit right panel creation - handles ncplane properly */
			ctx->right_tw = term_window_create_right_panel(w);
			/* Force statusbar redraw to fix input box positioning */
			signal_emit("mainwindow resized", 1, mw);
		}
		/* Cache geometry for hit-test */
		ctx->right_x = x;
		ctx->right_y = y;
		ctx->right_h = h;
	} else if (ctx->right_tw) {
		/* Clear the right panel area before destroying */
		clear_window_full(ctx->right_tw, ctx->right_w, ctx->right_h);
		/* Use explicit right panel destruction */
		term_window_destroy_right_panel(ctx->right_tw);
		ctx->right_tw = NULL;
		ctx->right_h = 0;
		/* CRITICAL: Clear the cache when panel is destroyed!
		 * Otherwise, when returning to a channel, the cache has stale data
		 * and differential rendering doesn't redraw the nicklist. */
		if (ctx->right_cache) {
			sp_cache_clear(ctx->right_cache);
		}
		/* Free reserved space - this expands main window */
		mainwindows_reserve_columns(0, -ctx->right_w);
		/* Force complete recreation of mainwindows to clear artifacts */
		mainwindows_recreate();
		/* Force statusbar redraw to fix input box positioning */
		signal_emit("mainwindow resized", 1, mw);
	}
}

void renumber_windows_by_position(void)
{
	GSList *sort_list, *s;
	int position = 1;

	/* Get sorted list using shared function */
	sort_list = build_sorted_window_list();

	/* Renumber all windows according to sorted order */
	for (s = sort_list; s; s = s->next) {
		WINDOW_SORT_REC *sort_rec = s->data;
		WINDOW_REC *win = sort_rec->win;

		if (win->refnum != position) {
			window_set_refnum(win, position);
		}
		position++;
	}

	/* Clean up */
	free_sorted_window_list(sort_list);
}

void draw_main_window_borders(MAIN_WINDOW_REC *mw)
{
#ifdef USE_NOTCURSES
	/* Notcurses handles borders differently - skip terminfo border drawing */
	(void) mw;
	return;
#else
	SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
	if (!ctx)
		return;

	/* Draw left border (between left panel and main window) */
	if (ctx->left_tw && ctx->left_h > 0) {
		int border_x = mw->first_column + mw->statusbar_columns_left - 1;
		for (int y = 0; y < ctx->left_h; y++) {
			gui_printtext_window_border(border_x,
			                            mw->first_line + mw->statusbar_lines_top + y);
		}
	}

	/* Draw right border (between main window and right panel) */
	if (ctx->right_tw && ctx->right_h > 0) {
		int border_x = mw->last_column + 1;
		for (int y = 0; y < ctx->right_h; y++) {
			gui_printtext_window_border(border_x,
			                            mw->first_line + mw->statusbar_lines_top + y);
		}
	}
#endif
}

void sidepanels_layout_init(void)
{
	/* Nothing to initialize */
}

void sidepanels_layout_deinit(void)
{
	/* Nothing to clean up */
}
