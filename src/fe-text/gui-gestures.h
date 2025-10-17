#ifndef IRSSI_FE_TEXT_GUI_GESTURES_H
#define IRSSI_FE_TEXT_GUI_GESTURES_H

#include <glib.h>
#include "gui-mouse.h"

/* Gesture types */
typedef enum {
	GESTURE_NONE = 0,
	GESTURE_LEFT_SHORT,
	GESTURE_LEFT_LONG,
	GESTURE_RIGHT_SHORT,
	GESTURE_RIGHT_LONG
} GuiGestureType;

/* Area validation callback - determines if coordinates are in valid gesture area */
typedef gboolean (*GuiGestureAreaValidator)(int x, int y, gpointer user_data);

/* Core gesture system functions */
void gui_gestures_init(void);
void gui_gestures_deinit(void);

/* Settings management */
void gui_gestures_reload_settings(void);
gboolean gui_gestures_is_enabled(void);

/* Area validator registration - call this to set which area accepts gestures */
void gui_gestures_set_area_validator(GuiGestureAreaValidator validator, gpointer user_data);

/* Mouse event handler - called by gui-mouse system */
gboolean gui_gestures_handle_mouse_event(const GuiMouseEvent *event, gpointer user_data);

/* Utility functions for gesture configuration */
const char* gui_gestures_get_command(GuiGestureType gesture);
void gui_gestures_execute_command(GuiGestureType gesture);

#endif