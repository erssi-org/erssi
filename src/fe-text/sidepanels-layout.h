/*
 * sidepanels-layout.h : erssi
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

#ifndef IRSSI_FE_TEXT_SIDEPANELS_LAYOUT_H
#define IRSSI_FE_TEXT_SIDEPANELS_LAYOUT_H

#include <glib.h>
#include <irssi/src/common.h>
#include <irssi/src/fe-text/term.h>
#include <irssi/src/fe-text/mainwindows.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include "sidepanels-types.h"

/* Panel positioning and geometry */
void position_tw(MAIN_WINDOW_REC *mw, SP_MAINWIN_CTX *ctx);
void apply_reservations_all(void);
void setup_ctx_for(MAIN_WINDOW_REC *mw);
void apply_and_redraw(void);

/* Selection management */
void update_left_selection_to_active(void);

/* Window management */
void renumber_windows_by_position(void);

/* Main window border drawing */
void draw_main_window_borders(MAIN_WINDOW_REC *mw);

/* Signal handlers are now in sidepanels-core.c */

/* Initialization */
void sidepanels_layout_init(void);
void sidepanels_layout_deinit(void);

#endif
