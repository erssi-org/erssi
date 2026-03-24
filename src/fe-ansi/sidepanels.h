/*
 * sidepanels.h : erssi
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

#ifndef IRSSI_FE_TEXT_SIDEPANELS_H
#define IRSSI_FE_TEXT_SIDEPANELS_H

#include <glib.h>
#include <irssi/src/common.h>

/* Custom data levels for activity tracking - restored from v0.0.4 */
#define DATA_LEVEL_NONE 0
#define DATA_LEVEL_TEXT 1
#define DATA_LEVEL_MSG 2
#define DATA_LEVEL_HILIGHT 3
#define DATA_LEVEL_EVENT 10

/* Main sidepanels API - implemented in sidepanels-core.c */
void sidepanels_init(void);
void sidepanels_deinit(void);

/* Settings accessors */
int get_sp_left_width(void);
int get_sp_right_width(void);
int get_sp_enable_left(void);
int get_sp_enable_right(void);
int get_sp_auto_hide_right(void);
int get_sp_enable_mouse(void);
int get_sp_debug(void);
int get_mouse_scroll_chat(void);
int get_auto_create_separators(void);

/* Feed one key (gunichar) from sig_gui_key_pressed; returns TRUE if consumed by mouse parser. */
gboolean sidepanels_try_parse_mouse_key(gunichar key);

/* Include all subsystem headers for convenience */
#include "sidepanels-render.h"
#include "sidepanels-activity.h"
#include "sidepanels-signals.h"
#include "sidepanels-layout.h"

#endif