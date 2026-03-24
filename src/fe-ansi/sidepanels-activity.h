/*
 * sidepanels-activity.h : erssi
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

#ifndef IRSSI_FE_TEXT_SIDEPANELS_ACTIVITY_H
#define IRSSI_FE_TEXT_SIDEPANELS_ACTIVITY_H

#include <glib.h>
#include <irssi/src/common.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/printtext.h>
#include "sidepanels-types.h"

/* Data levels for activity tracking - DATA_LEVEL_* constants are already defined in fe-windows.h */
#define DATA_LEVEL_EVENT 10

/* Global activity tracking */
extern GHashTable *window_priorities;

/* Activity management functions */
void handle_new_activity(WINDOW_REC *window, int data_level);

/* Window list management */
GSList *build_sorted_window_list(void);
void free_sorted_window_list(GSList *list);

/* Priority state management */
int get_window_current_priority(WINDOW_REC *win);
void reset_window_priority(WINDOW_REC *win);

/* Nick comparison for sorting */
int ci_nick_compare(gconstpointer a, gconstpointer b);

/* Signal handlers for activity tracking */
void sig_print_text(TEXT_DEST_REC *dest, const char *msg);

/* Initialization */
void sidepanels_activity_init(void);
void sidepanels_activity_deinit(void);

#endif
