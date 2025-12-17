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
	WINDOW_REC *aw;
	y = mw->first_line + mw->statusbar_lines_top;
	h = mw->height - mw->statusbar_lines;
	if (get_sp_enable_left() && ctx->left_w > 0) {
		/* Left panel is always at x=0, regardless of main window position */
		x = 0;
		w = ctx->left_w;
		if (ctx->left_tw) {
			/* Panel already exists, just move to correct position */
			term_window_move(ctx->left_tw, x, y, w, h);
		} else {
			/* Reserve space for left panel - this shifts main window right */
			mainwindows_reserve_columns(ctx->left_w, 0);
			ctx->left_tw = term_window_create(x, y, w, h);
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
		term_window_destroy(ctx->left_tw);
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
			ctx->right_tw = term_window_create(x, y, w, h);
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
		term_window_destroy(ctx->right_tw);
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
}

void sidepanels_layout_init(void)
{
	/* Nothing to initialize */
}

void sidepanels_layout_deinit(void)
{
	/* Nothing to clean up */
}
