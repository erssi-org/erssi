# Mouse Gesture Debug Status - erssi v0.0.4

## 🎯 CURRENT SITUATION

**Date**: 2025-01-09
**Status**: Mouse gestures partially working - scroll fixed, gestures need drag event fix

## ✅ WORKING FEATURES

### Mouse Scroll in Chat Area - FIXED ✅
- **File**: `src/fe-text/sidepanels.c` lines ~2113-2139
- **Fix**: Replaced wrong signals with `gui_window_scroll()` function
- **Works**: Mouse wheel in chat area now scrolls like PageUp/PageDown
- **Implementation**: Copied `get_scroll_count()` logic from `gui-readline.c`

## ❌ BROKEN FEATURES

### Mouse Gestures - DRAG EVENTS NOT DETECTED ❌

**Problem**: Gestures start on click but never detect drag/motion events

### Debug Output Shows:
```
GESTURE: Started at (146,23) chat_area=yes  ← Good
MOUSE_CLICK: at x=146 y=23 button=1         ← Good
MOUSE_CLICK: Chat area click detected       ← Good
```

**Missing**: NO `GESTURE: Drag to (x,y)` messages during swipe attempts

## 🐛 ROOT CAUSE IDENTIFIED

**File**: `src/fe-text/sidepanels.c` line **2181**

**Current (WRONG) Code**:
```c
if (!press && (braw & 32)) { /* Mouse drag/motion event */
```

**Problem**: Condition `!press && (braw & 32)` is INCORRECT for SGR mouse format

**SGR Mouse Events**:
- Press: `press=TRUE, braw=0`
- **Drag: `press=TRUE, braw=32`** ← This should trigger drag detection
- Release: `press=FALSE, braw=0`

**Current condition requires `!press` (FALSE) but drag has `press=TRUE`**

## 🛠️ REQUIRED FIX

**Change line 2181 from**:
```c
if (!press && (braw & 32)) { /* Mouse drag/motion event */
```

**To**:
```c
if ((braw & 32)) { /* Mouse drag/motion event regardless of press state */
```

**OR**:
```c
if (press && (braw & 32)) { /* Mouse drag/motion event */
```

## 📋 GESTURE SETTINGS STATUS

All settings are correctly configured:
```
gesture_left_long = window 1
gesture_left_short = window prev  
gesture_right_long = window last
gesture_right_short = window next
gesture_sensitivity = 20
gesture_timeout = 1000
mouse_gestures = ON
```

## 🎯 TESTING PLAN

After fix, expect to see in debug:
```
GESTURE: Started at (x,y) chat_area=yes    ← Already working
GESTURE: Drag to (x,y)                     ← Should appear after fix
GESTURE: Detected gesture X, dx=Y, dy=Z    ← Should appear on release
```

## 📁 KEY FILES

- **sidepanels.c**: Main mouse handling (~2200 lines)
- **gui-readline.c**: Reference for scroll implementation (working)
- **fe-messages.c**: Gesture settings registration

## 🚀 NEXT STEPS

1. Fix drag detection condition in line 2181
2. Test gesture with simple command: `/set gesture_left_short "echo TEST"`
3. Verify debug shows drag events
4. Test with default window navigation commands

## 💡 NOTES

- User handles all compilation/building
- Scroll implementation is solid reference for PageUp/PageDown behavior
- Mouse detection architecture is good, just drag condition wrong
- All gesture logic exists and looks correct, just needs proper drag events