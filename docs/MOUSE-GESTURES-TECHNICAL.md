# Mouse Gesture System - Technical Reference
## Evolved Irssi (erssi) - Developer Documentation

### Overview

The mouse gesture system provides intuitive window navigation through horizontal mouse swipes in the chat area. This document covers the technical implementation, configuration system, and integration points for developers.

### System Architecture

#### Core Components

**Location**: `/src/fe-text/sidepanels.c` and `/src/fe-text/sidepanels.h`

1. **Gesture Types** (`MouseGestureType` enum):
   ```c
   typedef enum {
       GESTURE_NONE = 0,
       GESTURE_LEFT_SHORT,
       GESTURE_LEFT_LONG,
       GESTURE_RIGHT_SHORT,
       GESTURE_RIGHT_LONG
   } MouseGestureType;
   ```

2. **State Management** (`gesture_state_t` struct):
   ```c
   typedef struct {
       gboolean active;           /* gesture detection active */
       gboolean dragging;         /* mouse button currently pressed */
       int start_x, start_y;      /* gesture start coordinates */
       int current_x, current_y;  /* current mouse position */
       int start_time;            /* gesture start time (ms) */
       gesture_type_t detected;   /* detected gesture type */
       gboolean in_chat_area;     /* gesture started in main chat area */
   } gesture_state_t;
   ```

3. **Configuration Variables**:
   ```c
   static int mouse_gestures_enabled;
   static char *gesture_left_short_command;
   static char *gesture_left_long_command;
   static char *gesture_right_short_command;
   static char *gesture_right_long_command;
   static int gesture_sensitivity;
   static int gesture_timeout;
   ```

### Gesture Recognition Algorithm

#### 1. Activation Phase
```c
/* Mouse press in chat area starts gesture tracking */
if (press && !gesture_state.active) {
    gesture_state.active = TRUE;
    gesture_state.dragging = TRUE;
    gesture_state.start_x = gesture_state.current_x = x;
    gesture_state.start_y = gesture_state.current_y = y;
    gesture_state.start_time = g_get_monotonic_time() / 1000;
    gesture_state.in_chat_area = is_in_chat_area(x, y);
    gesture_state.detected = GESTURE_NONE;
}
```

#### 2. Tracking Phase
```c
/* Mouse drag events update current position */
if (gesture_state.active && gesture_state.dragging) {
    gesture_state.current_x = x;
    gesture_state.current_y = y;
}
```

#### 3. Classification Phase
```c
static gesture_type_t classify_gesture(int dx, int dy, int duration)
{
    int abs_dx = (dx < 0) ? -dx : dx;
    int abs_dy = (dy < 0) ? -dy : dy;
    
    /* Must meet minimum distance threshold */
    if (abs_dx < gesture_sensitivity && abs_dy < gesture_sensitivity) {
        return GESTURE_NONE;
    }
    
    /* Must be primarily horizontal */
    if (abs_dx <= abs_dy) {
        return GESTURE_NONE;
    }
    
    /* Determine length based on distance */
    gboolean is_long = (abs_dx > (gesture_sensitivity * 2));
    
    /* Classify direction and length */
    if (dx < 0) { /* Left swipe */
        return is_long ? GESTURE_LEFT_LONG : GESTURE_LEFT_SHORT;
    } else { /* Right swipe */
        return is_long ? GESTURE_RIGHT_LONG : GESTURE_RIGHT_SHORT;
    }
}
```

#### 4. Execution Phase
```c
/* Mouse release triggers gesture execution */
if (!press && gesture_state.active && gesture_state.dragging) {
    int duration = current_time - gesture_state.start_time;
    
    if (gesture_state.in_chat_area && duration <= gesture_timeout) {
        gesture_type_t detected = classify_gesture(dx, dy, duration);
        if (detected != GESTURE_NONE) {
            execute_gesture_command(detected);
            reset_gesture_state();
            return TRUE; /* Consume the event */
        }
    }
    
    reset_gesture_state();
}
```

### Configuration System

#### Settings Registration
```c
/* In sidepanels_init() */
settings_add_bool("lookandfeel", "mouse_gestures", TRUE);
settings_add_str("lookandfeel", "gesture_left_short", "/window prev");
settings_add_str("lookandfeel", "gesture_left_long", "/window 1");
settings_add_str("lookandfeel", "gesture_right_short", "/window next");
settings_add_str("lookandfeel", "gesture_right_long", "/window last");
settings_add_int("lookandfeel", "gesture_sensitivity", 20);
settings_add_int("lookandfeel", "gesture_timeout", 1000);
settings_add_bool("lookandfeel", "mouse_scroll_chat", TRUE);
```

#### Runtime Configuration Loading
```c
/* In read_settings() */
mouse_gestures_enabled = settings_get_bool("mouse_gestures");
gesture_left_short_command = g_strdup(settings_get_str("gesture_left_short"));
gesture_left_long_command = g_strdup(settings_get_str("gesture_left_long"));
gesture_right_short_command = g_strdup(settings_get_str("gesture_right_short"));
gesture_right_long_command = g_strdup(settings_get_str("gesture_right_long"));
gesture_sensitivity = settings_get_int("gesture_sensitivity");
gesture_timeout = settings_get_int("gesture_timeout");
```

### Integration with Mouse Event System

#### Event Flow
1. **Raw Mouse Event** → `term_read()` in terminal handling
2. **Mouse Coordinate Extraction** → X11/SGR format parsing
3. **Gesture Processing** → `handle_gesture_mouse_event()`
4. **Fallback Processing** → Normal click handling if gesture not consumed

#### Chat Area Detection
```c
static gboolean is_in_chat_area(int x, int y)
{
    /* Calculate boundaries of main chat area */
    int left_boundary = (sp_enable_left && sp_left_width > 0) ? sp_left_width : 0;
    int right_boundary = TERM_WIDTH - ((sp_enable_right && sp_right_width > 0) ? sp_right_width : 0);
    int bottom_boundary = TERM_HEIGHT - (statusbar_visible ? 1 : 0);
    
    /* Check if coordinates are within chat area */
    if (x < left_boundary || x >= right_boundary) {
        return FALSE;
    }
    if (y < 0 || y >= bottom_boundary) {
        return FALSE;
    }
    
    return TRUE;
}
```

### Command Execution

#### Command Processing
```c
static void execute_gesture_command(gesture_type_t gesture)
{
    const char *command = NULL;
    
    switch ((int)gesture) {
    case GESTURE_LEFT_SHORT:
        command = gesture_left_short_command;
        break;
    case GESTURE_LEFT_LONG:
        command = gesture_left_long_command;
        break;
    case GESTURE_RIGHT_SHORT:
        command = gesture_right_short_command;
        break;
    case GESTURE_RIGHT_LONG:
        command = gesture_right_long_command;
        break;
    }
    
    if (command && *command) {
        signal_emit("send command", 3, command, 
                    active_win->active_server, active_win->active);
    }
}
```

### Performance Considerations

#### Minimal Overhead
- Gesture tracking only active during left mouse button press
- No continuous polling or timer-based processing
- State reset immediately after gesture completion or timeout
- Event consumption prevents unnecessary processing by other handlers

#### Memory Management
- Gesture command strings allocated/freed in `read_settings()`
- No dynamic memory allocation during gesture processing
- Single global state structure, no per-window overhead

### Error Handling & Edge Cases

#### Timeout Protection
```c
if (duration <= gesture_timeout) {
    /* Process gesture */
} else {
    /* Ignore slow gestures */
}
```

#### Boundary Checking
- Gestures only processed if started in chat area
- Coordinate validation prevents out-of-bounds access
- Direction validation ensures horizontal movement priority

#### State Recovery
```c
static void reset_gesture_state(void)
{
    gesture_state.active = FALSE;
    gesture_state.dragging = FALSE;
    gesture_state.detected = GESTURE_NONE;
    gesture_state.in_chat_area = FALSE;
    /* Reset coordinates and timing */
}
```

### Debug Support

#### Debug Logging
```c
sp_logf("GESTURE: Started at (%d,%d) chat_area=%s", 
        x, y, gesture_state.in_chat_area ? "yes" : "no");
sp_logf("GESTURE: Detected gesture %d, dx=%d, dy=%d, duration=%dms", 
        detected, dx, dy, duration);
```

#### Troubleshooting
- Enable sidepanel debug: `/set sidepanel_debug on`
- Check log file: `/tmp/irssi_sidepanels.log`
- Verify chat area boundaries with coordinate logging

### Extension Points

#### Custom Gesture Types
To add new gesture types:
1. Extend `MouseGestureType` enum
2. Add classification logic in `classify_gesture()`
3. Add execution logic in `execute_gesture_command()`
4. Register new settings in `sidepanels_init()`

#### Integration with Scripts
Gestures can execute any irssi command, including script calls:
```c
/set gesture_right_long "script exec my_script.pl"
```

### Compatibility

#### C89/C90 Compliance
- All variable declarations at block start
- No mixed declarations and code
- Compatible with older compilers

#### Thread Safety
- No threading used in gesture system
- All processing in main event loop
- No shared state between different execution contexts

### Testing

#### Manual Testing
```c
# Test gesture recognition
/set sidepanel_debug on
# Perform gestures and check log output

# Test sensitivity
/set gesture_sensitivity 10  # Very sensitive
/set gesture_sensitivity 50  # Less sensitive

# Test timeout
/set gesture_timeout 500     # Fast gestures only
/set gesture_timeout 2000    # Slower gestures allowed
```

#### Automated Testing
The gesture system can be tested by:
1. Simulating mouse events programmatically
2. Verifying state transitions
3. Checking command execution
4. Validating boundary conditions

### Future Enhancements

#### Planned Features
- Visual feedback during gesture recognition
- Vertical gesture support for scrollback navigation
- Multi-touch gesture support
- Gesture macros (sequence of commands)
- Adaptive sensitivity based on usage patterns

#### API Extensions
- Gesture event signals for scripts
- Custom gesture callbacks
- Gesture recording/playback for automation

This technical reference provides the foundation for understanding, extending, and maintaining the mouse gesture system in evolved irssi.