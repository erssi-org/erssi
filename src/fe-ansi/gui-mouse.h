#ifndef IRSSI_FE_TEXT_GUI_MOUSE_H
#define IRSSI_FE_TEXT_GUI_MOUSE_H

#include <glib.h>
#include <irssi/src/common.h>

/* Mouse button constants */
#define MOUSE_BUTTON_LEFT    1
#define MOUSE_BUTTON_MIDDLE  2
#define MOUSE_BUTTON_RIGHT   3
#define MOUSE_WHEEL_UP       4
#define MOUSE_WHEEL_DOWN     5

/* Mouse event structure */
typedef struct {
	int x;
	int y;
	int button;
	gboolean press;  /* TRUE for press, FALSE for release */
	gboolean valid;  /* TRUE if event was successfully parsed */
	int raw_button;  /* Raw button value from SGR protocol */
} GuiMouseEvent;

/* Mouse event callback function type - return TRUE to consume event */
typedef gboolean (*GuiMouseEventCallback)(const GuiMouseEvent *event, gpointer user_data);

/* Core mouse system functions */
void gui_mouse_init(void);
void gui_mouse_deinit(void);

/* Mouse tracking control */
void gui_mouse_enable_tracking(void);
void gui_mouse_disable_tracking(void);
gboolean gui_mouse_is_tracking_enabled(void);

/* Main parsing function - returns TRUE if key was consumed */
gboolean gui_mouse_try_parse_key(gunichar key);

/* Event handler registration */
void gui_mouse_add_handler(GuiMouseEventCallback callback, gpointer user_data);
void gui_mouse_remove_handler(GuiMouseEventCallback callback, gpointer user_data);

/* Utility functions */
gboolean gui_mouse_is_scroll_event(int raw_button);
gboolean gui_mouse_is_drag_event(int raw_button);

#endif