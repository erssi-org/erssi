# Mouse Gesture System UX Design Guide
## Evolved Irssi (erssi) - User Experience Documentation

### Overview

The mouse gesture system in evolved irssi introduces intuitive window navigation through simple mouse swipes in the chat area. This system enables rapid channel/query switching without keyboard shortcuts, making IRC navigation more fluid and accessible.

## Design Principles

### 1. Chat Area Focus
- **Gestures only work in the main chat area** - prevents accidental triggers in sidepanels
- **Visual boundaries are respected** - left/right sidepanels remain gesture-free zones
- **Content-first approach** - gestures complement, don't interrupt text interaction

### 2. Intuitive Direction Mapping
- **Left swipe = Previous** - follows natural reading direction (go back)
- **Right swipe = Next** - follows natural progression (go forward)
- **Long swipes = Jump actions** - more distance equals bigger navigation jumps

### 3. Forgiving Recognition
- **Directional priority** - horizontal movement dominates over accidental vertical drift
- **Minimum distance threshold** - prevents tiny mouse movements from triggering gestures
- **Timeout protection** - very slow drags don't register as gestures

## Current Default Mappings

### Gesture Commands (Optimized for IRC Workflow)

| Gesture | Command | Purpose | UX Rationale |
|---------|---------|---------|--------------|
| **Left Short** | `/window prev` | Previous window | Most frequent IRC action - quick channel switching |
| **Left Long** | `/window 1` | First window | Jump to status/notices window (network home) |
| **Right Short** | `/window next` | Next window | Natural complement to "prev" - sequential navigation |
| **Right Long** | `/window last` | Last window | Jump to most recent active window (where action is) |

### Why These Defaults Work

**Left Short (window prev)**
- Most frequently used gesture in IRC workflow
- Matches mental model: "go back" = left movement
- Provides instant access to previously active conversation

**Left Long (window 1)**  
- Emergency "home" gesture - returns to network status
- Window 1 typically contains server notices and network information
- Long gesture = big jump, conceptually consistent

**Right Short (window next)**
- Natural pair with "prev" - creates navigation cycle
- Enables sequential window browsing
- Familiar behavior from tab switching in browsers

**Right Long (window last)**
- Brilliant for IRC workflow - jumps to where activity is happening  
- Matches user intent: "take me to the action"
- Long gesture = significant navigation jump

## Settings Configuration

### Current Settings with UX Justification

```bash
# Core gesture system
/set mouse_gestures on           # Enable gesture recognition
/set mouse_scroll_chat on        # Enable chat area scrolling

# Gesture command mappings  
/set gesture_left_short "/window prev"   # Most common action
/set gesture_left_long "/window 1"       # Jump to home/status
/set gesture_right_short "/window next"  # Sequential navigation
/set gesture_right_long "/window last"   # Jump to active window

# Sensitivity and timing (UX-optimized defaults)
/set gesture_sensitivity 20      # 20 pixels minimum - prevents accidents
/set gesture_timeout 1000        # 1 second max - natural gesture speed
```

### Sensitivity Analysis

**gesture_sensitivity = 20 pixels**
- **Too low (< 15)**: Accidental triggers from tiny mouse movements
- **Optimal (15-25)**: Clear intentional gestures, forgiving of slight drift  
- **Too high (> 30)**: Requires exaggerated movements, feels unresponsive

**gesture_timeout = 1000ms**
- **Too short (< 500ms)**: Misses deliberate slow gestures
- **Optimal (800-1200ms)**: Natural gesture timing for most users
- **Too long (> 1500ms)**: Slow drags feel disconnected from intent

## User Experience Recommendations

### 1. Visual Feedback Enhancements

**Current State**: No visual feedback during gesture recognition
**Recommended Improvements**:

- **Gesture Preview**: Show faint arrow indicator during drag
- **Threshold Indicator**: Subtle highlight when gesture distance threshold is met
- **Command Preview**: Brief status line preview of action before release

### 2. Progressive Disclosure

**Beginner Mode**:
```bash
# Simple left/right only
/set gesture_left_short "/window prev"
/set gesture_left_long "window prev"      # Duplicate for consistency
/set gesture_right_short "/window next"  
/set gesture_right_long "window next"     # Duplicate for consistency
```

**Advanced Mode** (current defaults):
```bash
# Full four-gesture system with jump actions
/set gesture_left_short "/window prev"
/set gesture_left_long "/window 1"         # Jump to home
/set gesture_right_short "/window next"
/set gesture_right_long "/window last"     # Jump to active
```

### 3. Accessibility Considerations

**Motor Accessibility**:
- Current 20-pixel threshold accommodates slight hand tremor
- 1-second timeout allows for slower deliberate movements
- Horizontal-only gestures reduce precision requirements

**Cognitive Accessibility**:
- Consistent direction mapping (left = back, right = forward)
- Logical short/long progression (small movement = small action)
- Familiar navigation patterns from other applications

## Usage Examples & Customization

### Common Customizations

**Power User Setup** (Network-focused):
```bash
/set gesture_left_short "/window prev"
/set gesture_left_long "/window goto IRCnet"     # Jump to network status
/set gesture_right_short "/window next"
/set gesture_right_long "/window goto #main"     # Jump to main channel
```

**Minimal Setup** (Two-gesture only):
```bash
/set gesture_left_short "/window prev"
/set gesture_left_long "window prev"            # Same as short
/set gesture_right_short "/window next" 
/set gesture_right_long "window next"           # Same as short
```

**Activity-focused Setup**:
```bash
/set gesture_left_short "/window prev"
/set gesture_left_long "window last"            # Recent activity
/set gesture_right_short "/window next"
/set gesture_right_long "/window next_act"       # Next with activity
```

### Integration Examples

**Script Integration**:
```bash
# Custom gesture commands can call Perl scripts
/set gesture_right_long "script exec toggle_away.pl"
/set gesture_left_long "script exec network_status.pl"
```

**Multi-action Gestures**:
```bash
# Chain commands with semicolons
/set gesture_right_long "window last; scrollback end"
```

## Best Practices for Users

### 1. Learning the System
- **Start with short gestures only** - master left/right navigation first
- **Practice in quiet channels** - avoid accidentally switching during conversations  
- **Use consistent gesture speed** - develop muscle memory for timing

### 2. Customization Strategy
- **Map most frequent actions to short gestures** - these are easiest to perform
- **Reserve long gestures for "power" actions** - jumping, shortcuts, scripts
- **Match gestures to mental models** - left = back/previous, right = forward/next

### 3. Troubleshooting
- **Gestures not working**: Check you're dragging in chat area, not sidepanels
- **Accidental triggers**: Increase `gesture_sensitivity` from 20 to 25-30
- **Missing gestures**: Decrease `gesture_timeout` if you gesture quickly
- **Wrong direction**: Remember horizontal distance must exceed vertical

## Technical Implementation Details

### Gesture Recognition Logic

1. **Detection Area**: Only main chat area (between left/right sidepanels)
2. **Activation**: Left mouse button press begins tracking
3. **Classification**: 
   - Horizontal distance > vertical distance (directional priority)
   - Minimum threshold: `gesture_sensitivity` pixels
   - Long gesture: 2x sensitivity threshold
4. **Execution**: Command triggered on mouse button release
5. **Timeout**: Gestures exceeding `gesture_timeout` are ignored

### Performance Impact
- **Minimal CPU overhead**: Only processes mouse events in chat area
- **No memory leaks**: Gesture state properly reset after each gesture
- **Thread-safe**: Integrates cleanly with irssi's event system

## Future Enhancement Opportunities

### 1. Gesture Feedback System
- **Visual arrows** during gesture recognition
- **Command preview** in status bar
- **Haptic feedback** for supported terminals

### 2. Advanced Gesture Types
- **Up/down swipes** for scrollback navigation
- **Circular gestures** for window list popup
- **Multi-finger support** for additional actions

### 3. Adaptive Learning
- **Usage analytics** to suggest optimal mappings
- **Automatic sensitivity adjustment** based on user behavior
- **Context-aware gestures** that change based on current window type

## Conclusion

The mouse gesture system in evolved irssi represents a carefully designed enhancement to IRC navigation that prioritizes:

- **Intuitive interaction** through natural directional mapping
- **Reliable recognition** with forgiving thresholds and timing
- **Customizable behavior** that adapts to different user workflows
- **Minimal disruption** to existing irssi interaction patterns

The current default mappings provide an excellent starting point for most IRC users, while the configuration system allows for extensive customization to match individual preferences and workflows.

The system successfully bridges the gap between keyboard-driven IRC clients and modern mouse-driven interfaces, making IRC more accessible to users who prefer hybrid interaction models while maintaining the efficiency that makes irssi legendary among power users.