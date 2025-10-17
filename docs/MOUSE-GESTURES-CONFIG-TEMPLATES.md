# Mouse Gesture Configuration Templates
## Evolved Irssi (erssi) - Configuration Examples

### Default Configuration (Recommended)

Optimized for general IRC usage with intuitive navigation:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Default mappings (optimal for most users)
/set gesture_left_short "//window prev"     # Most common action
/set gesture_left_long "//window 1"         # Jump to network status  
/set gesture_right_short "//window next"    # Sequential navigation
/set gesture_right_long "//window last"     # Jump to recent activity

# Standard sensitivity (20 pixels, 1 second timeout)
/set gesture_sensitivity 20
/set gesture_timeout 1000
```

### Beginner Configuration

Simplified two-gesture setup for new users:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Simple left/right navigation only
/set gesture_left_short "/window prev"
/set gesture_left_long "/window prev"       # Same as short gesture
/set gesture_right_short "/window next"
/set gesture_right_long "/window next"      # Same as short gesture

# More forgiving sensitivity
/set gesture_sensitivity 15               # Shorter swipes needed
/set gesture_timeout 1200                 # Allow slower movements
```

### Power User Configuration

Advanced setup for heavy IRC users:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Activity-focused navigation
/set gesture_left_short "/window prev"
/set gesture_left_long "/window last"       # Jump to recent activity
/set gesture_right_short "/window next"
/set gesture_right_long "/window next_act"  # Next window with activity

# Precise control
/set gesture_sensitivity 25               # Require more deliberate movements
/set gesture_timeout 800                  # Fast gestures only
```

### Network-Focused Configuration

Optimized for users managing multiple IRC networks:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Network-based navigation
/set gesture_left_short "/window prev"
/set gesture_left_long "window goto IRCnet"    # Jump to network status window
/set gesture_right_short "/window next"
/set gesture_right_long "window goto Libera"   # Jump to other network

# Standard sensitivity
/set gesture_sensitivity 20
/set gesture_timeout 1000
```

### Script Integration Configuration

For users with custom Perl scripts:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Script-enhanced navigation
/set gesture_left_short "/window prev"
/set gesture_left_long "script exec network_status.pl"  # Custom script
/set gesture_right_short "/window next"
/set gesture_right_long "script exec jump_active.pl"    # Custom activity jumper

# Standard sensitivity
/set gesture_sensitivity 20
/set gesture_timeout 1000
```

### Accessibility Configuration

Optimized for users with motor difficulties:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Standard navigation
/set gesture_left_short "/window prev"
/set gesture_left_long "/window 1"
/set gesture_right_short "/window next"
/set gesture_right_long "/window last"

# More forgiving settings
/set gesture_sensitivity 12               # Shorter distance required
/set gesture_timeout 1500                 # Allow slower movements
```

### Touchpad-Optimized Configuration

For users primarily using touchpads:

```bash
# Enable mouse gestures
/set mouse_gestures on
/set mouse_scroll_chat on

# Standard navigation
/set gesture_left_short "/window prev"
/set gesture_left_long "/window 1"
/set gesture_right_short "/window next"
/set gesture_right_long "/window last"

# Touchpad-friendly sensitivity
/set gesture_sensitivity 30               # Larger movements to avoid accidents
/set gesture_timeout 1200                 # Account for touchpad precision
```

### Disabled Configuration

To completely disable mouse gestures:

```bash
# Disable mouse gestures but keep scrolling
/set mouse_gestures off
/set mouse_scroll_chat on

# Gesture commands are ignored when disabled
```

## Configuration Tips

### Finding Your Optimal Settings

1. **Start with defaults** - they work well for most users
2. **Adjust sensitivity first** - if you get accidental triggers, increase it
3. **Modify timeout second** - if gestures feel unresponsive, increase timeout
4. **Customize commands last** - once comfortable with gestures, tailor actions

### Testing Your Configuration

```bash
# Enable debug logging
/set sidepanel_debug on

# Perform test gestures and check the log
# Log location: /tmp/irssi_sidepanels.log

# View log in real-time
/exec -interactive tail -f /tmp/irssi_sidepanels.log
```

### Common Adjustments

**Gestures too sensitive (accidental triggers)**:
```bash
/set gesture_sensitivity 30      # Increase from default 20
```

**Gestures not registering**:
```bash
/set gesture_sensitivity 15      # Decrease from default 20
/set gesture_timeout 1500        # Increase from default 1000
```

**Want faster response**:
```bash
/set gesture_timeout 600         # Decrease from default 1000
```

## Advanced Customizations

### Command Chaining

Execute multiple commands with one gesture:

```bash
/set gesture_right_long "/window last; scrollback end"
/set gesture_left_long "/window 1; scrollback end"
```

### Conditional Commands

Use irssi's conditional syntax:

```bash
# Only jump to /window 1 if it exists, otherwise stay
/set gesture_left_long "eval window goto 1 || echo No /window 1"
```

### Variable References

Use irssi variables in gesture commands:

```bash
/set gesture_right_long "window goto $[winref]"    # Reference saved window
```

## Migrating Between Configurations

### Backup Current Settings
```bash
/save    # Save current configuration to file
```

### Apply New Template
Copy desired configuration from above, paste into irssi one line at a time.

### Test and Adjust
1. Try each gesture type
2. Monitor debug output if needed
3. Fine-tune sensitivity/timeout
4. Save when satisfied: `/save`

## Troubleshooting

### Gestures Not Working At All
1. Check: `/set mouse_gestures` (should be ON)
2. Check: Are you dragging in chat area, not sidepanels?
3. Check: Is your terminal sending mouse events correctly?

### Specific Gestures Missing
1. Enable debug: `/set sidepanel_debug on`
2. Check log: `/exec tail -f /tmp/irssi_sidepanels.log`
3. Verify your gesture meets distance/direction requirements

### Performance Issues
1. Lower sensitivity if gestures feel laggy
2. Check for conflicting scripts
3. Verify no infinite loops in gesture commands

Remember: Gesture commands can be any valid irssi command, so you can create powerful custom navigation workflows that match your specific IRC usage patterns!