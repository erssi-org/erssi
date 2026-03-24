/*
 * gui-gestures.c : Mouse gesture detection and handling for irssi
 *
 * Copyright (C) 2025 Evolved Irssi Team
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <irssi/src/common.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include "gui-gestures.h"

/* Gesture detection state */
typedef struct {
	gboolean active;           /* gesture detection active */
	gboolean dragging;         /* mouse button currently pressed */
	int start_x, start_y;      /* gesture start coordinates */
	int current_x, current_y;  /* current mouse position */
	int start_time;            /* gesture start time (ms) */
	GuiGestureType detected;   /* detected gesture type */
	gboolean in_valid_area;    /* gesture started in valid area */
} GestureState;

static GestureState gesture_state = {0};

/* Gesture settings */
static gboolean gestures_enabled = FALSE;
static char *gesture_left_short_command = NULL;
static char *gesture_left_long_command = NULL;
static char *gesture_right_short_command = NULL;
static char *gesture_right_long_command = NULL;
static int gesture_sensitivity = 10;
static int gesture_timeout = 1000;

/* Area validator callback */
static GuiGestureAreaValidator area_validator = NULL;
static gpointer area_validator_data = NULL;

/* Forward declarations */
static GuiGestureType classify_gesture(int dx, int dy, int duration);
static void execute_gesture_command(GuiGestureType gesture);
static void reset_gesture_state(void);

/* Initialize gesture system */
void gui_gestures_init(void)
{
	/* Initialize state */
	memset(&gesture_state, 0, sizeof(gesture_state));
	
	/* Register settings */
	settings_add_bool_module("fe-text", "lookandfeel", "mouse_gestures", TRUE);
	settings_add_str_module("fe-text", "lookandfeel", "gesture_left_short", "/window prev");
	settings_add_str_module("fe-text", "lookandfeel", "gesture_left_long", "/window 1");
	settings_add_str_module("fe-text", "lookandfeel", "gesture_right_short", "/window next");
	settings_add_str_module("fe-text", "lookandfeel", "gesture_right_long", "/window lastone");
	settings_add_int_module("fe-text", "lookandfeel", "gesture_sensitivity", 10);
	settings_add_int_module("fe-text", "lookandfeel", "gesture_timeout", 1000);
	
	/* Load settings */
	gui_gestures_reload_settings();
}

/* Cleanup gesture system */
void gui_gestures_deinit(void)
{
	/* Free command strings */
	g_free(gesture_left_short_command);
	g_free(gesture_left_long_command);
	g_free(gesture_right_short_command);
	g_free(gesture_right_long_command);
	gesture_left_short_command = NULL;
	gesture_left_long_command = NULL;
	gesture_right_short_command = NULL;
	gesture_right_long_command = NULL;
	
	/* Reset state */
	reset_gesture_state();
}

/* Reload settings from configuration */
void gui_gestures_reload_settings(void)
{
	gestures_enabled = settings_get_bool("mouse_gestures");
	
	/* Free old command strings */
	g_free(gesture_left_short_command);
	g_free(gesture_left_long_command);
	g_free(gesture_right_short_command);
	g_free(gesture_right_long_command);
	
	/* Load new command strings */
	gesture_left_short_command = g_strdup(settings_get_str("gesture_left_short"));
	gesture_left_long_command = g_strdup(settings_get_str("gesture_left_long"));
	gesture_right_short_command = g_strdup(settings_get_str("gesture_right_short"));
	gesture_right_long_command = g_strdup(settings_get_str("gesture_right_long"));
	
	gesture_sensitivity = settings_get_int("gesture_sensitivity");
	gesture_timeout = settings_get_int("gesture_timeout");
}

/* Check if gestures are enabled */
gboolean gui_gestures_is_enabled(void)
{
	return gestures_enabled;
}

/* Set area validator for gesture detection */
void gui_gestures_set_area_validator(GuiGestureAreaValidator validator, gpointer user_data)
{
	area_validator = validator;
	area_validator_data = user_data;
}

/* Get command for gesture type */
const char* gui_gestures_get_command(GuiGestureType gesture)
{
	switch (gesture) {
	case GESTURE_LEFT_SHORT:
		return gesture_left_short_command;
	case GESTURE_LEFT_LONG:
		return gesture_left_long_command;
	case GESTURE_RIGHT_SHORT:
		return gesture_right_short_command;
	case GESTURE_RIGHT_LONG:
		return gesture_right_long_command;
	case GESTURE_NONE:
	default:
		return NULL;
	}
}

/* Execute command for detected gesture */
void gui_gestures_execute_command(GuiGestureType gesture)
{
	const char *command = gui_gestures_get_command(gesture);
	
	if (command && *command) {
		signal_emit("send command", 3, command, active_win->active_server, active_win->active);
	}
}

/* Execute command for detected gesture (internal version with logging) */
static void execute_gesture_command(GuiGestureType gesture)
{
	const char *command = gui_gestures_get_command(gesture);
	
	if (command && *command) {
		/* Note: sp_logf would need to be provided by caller or made available */
		signal_emit("send command", 3, command, active_win->active_server, active_win->active);
	}
}

/* Classify gesture based on distance and duration */
static GuiGestureType classify_gesture(int dx, int dy, int duration)
{
	int abs_dx = (dx < 0) ? -dx : dx;
	int abs_dy = (dy < 0) ? -dy : dy;
	gboolean is_horizontal, is_long;
	
	/* Must meet minimum distance threshold */
	if (abs_dx < gesture_sensitivity && abs_dy < gesture_sensitivity) {
		return GESTURE_NONE;
	}
	
	/* Determine if primarily horizontal or vertical */
	is_horizontal = (abs_dx > abs_dy);
	
	/* Only process horizontal gestures for now */
	if (!is_horizontal) {
		return GESTURE_NONE;
	}
	
	/* Determine length based on distance */
	is_long = (abs_dx > (gesture_sensitivity * 2));
	
	/* Classify direction and length */
	if (dx < 0) { /* Left swipe */
		return is_long ? GESTURE_LEFT_LONG : GESTURE_LEFT_SHORT;
	} else { /* Right swipe */
		return is_long ? GESTURE_RIGHT_LONG : GESTURE_RIGHT_SHORT;
	}
}

/* Reset gesture tracking state */
static void reset_gesture_state(void)
{
	gesture_state.active = FALSE;
	gesture_state.dragging = FALSE;
	gesture_state.detected = GESTURE_NONE;
	gesture_state.in_valid_area = FALSE;
	gesture_state.start_x = gesture_state.start_y = 0;
	gesture_state.current_x = gesture_state.current_y = 0;
	gesture_state.start_time = 0;
}

/* Handle mouse events for gesture detection */
gboolean gui_gestures_handle_mouse_event(const GuiMouseEvent *event, gpointer user_data)
{
	(void) user_data;
	
	if (!gestures_enabled || event->button != MOUSE_BUTTON_LEFT) {
		return FALSE; /* Let other handlers process */
	}
	
	if (event->press) {
		/* Mouse press - start gesture tracking */
		if (!gesture_state.active) {
			gesture_state.active = TRUE;
			gesture_state.dragging = TRUE;
			gesture_state.start_x = gesture_state.current_x = event->x;
			gesture_state.start_y = gesture_state.current_y = event->y;
			gesture_state.start_time = g_get_monotonic_time() / 1000; /* Convert to ms */
			gesture_state.detected = GESTURE_NONE;
			
			/* Check if gesture started in valid area */
			if (area_validator) {
				gesture_state.in_valid_area = area_validator(event->x, event->y, area_validator_data);
			} else {
				gesture_state.in_valid_area = TRUE; /* Default to valid if no validator */
			}
		}
	} else {
		/* Mouse release - end gesture and classify */
		if (gesture_state.active && gesture_state.dragging) {
			int current_time = g_get_monotonic_time() / 1000;
			int duration = current_time - gesture_state.start_time;
			int dx = gesture_state.current_x - gesture_state.start_x;
			int dy = gesture_state.current_y - gesture_state.start_y;
			GuiGestureType detected;
			
			/* Only process gestures that started in valid area */
			if (gesture_state.in_valid_area && duration <= gesture_timeout) {
				detected = classify_gesture(dx, dy, duration);
				if (detected != GESTURE_NONE) {
					execute_gesture_command(detected);
					reset_gesture_state();
					return TRUE; /* Consume the event */
				}
			}
			
			reset_gesture_state();
		}
	}
	
	/* Handle drag events for gesture tracking */
	if (gui_mouse_is_drag_event(event->raw_button) && gesture_state.active && gesture_state.dragging) {
		gesture_state.current_x = event->x;
		gesture_state.current_y = event->y;
	}
	
	return FALSE; /* Let other handlers process */
}