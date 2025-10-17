# Mouse Gestures Quick Reference
## Evolved Irssi (erssi) - User Guide

### What are Mouse Gestures?

Mouse gestures let you navigate between IRC windows by simply dragging your mouse in the chat area. Think of it as "swiping" between conversations - similar to mobile apps, but for your IRC client.

### How to Use Gestures

1. **Click and hold** left mouse button in the chat area (the main text area where messages appear)
2. **Drag** horizontally (left or right) 
3. **Release** mouse button to execute the action

**Important**: Gestures only work in the main chat area, not in the sidepanels (window list/nicklist).

### Default Gestures

| 👈 **Left Swipe** | 👉 **Right Swipe** |
|-------------------|-------------------|
| **Short**: Previous window | **Short**: Next window |
| **Long**: Jump to window 1 | **Long**: Jump to last active window |

### Quick Start

**Most Common Usage**:
- **Short left swipe** → Go to previous window (most used!)
- **Short right swipe** → Go to next window  
- **Long right swipe** → Jump to where activity is happening

### Visual Guide

```
Chat Area (gestures work here)
┌─────────────────────────────┐
│ [10:30] <user> hello        │ ← Drag mouse left/right here
│ [10:31] <you> hi there      │
│ [10:32] * user waves        │
│                             │
└─────────────────────────────┘
```

### Settings

**Enable/Disable**:
```
/set mouse_gestures on         # Enable gestures (default: on)
/set mouse_gestures off        # Disable if you don't want them
```

**Adjust Sensitivity**:
```  
/set gesture_sensitivity 5    # More sensitive (shorter swipes)
/set gesture_sensitivity 20   # Less sensitive (longer swipes needed)
```

### Tips for New Users

✅ **Do**:
- Practice in quiet channels first
- Use consistent, deliberate movements
- Start with just short left/right swipes

❌ **Don't**:
- Try to gesture in sidepanels (window list/nicklist)
- Make extremely slow movements (has 1-second timeout)
- Drag vertically - horizontal movement is key

### Troubleshooting

**Gestures not working?**
- Check you're dragging in the chat area, not sidepanels
- Make sure `mouse_gestures` is set to `on`
- Try a slightly longer swipe motion

**Accidental gestures?**  
- Increase sensitivity: `/set gesture_sensitivity 20`
- Be more deliberate with mouse movements

**Missing some gestures?**
- Make sure you're dragging horizontally, not vertically
- Try slightly faster movements (under 1 second)

### Advanced Customization

You can change what each gesture does:

```bash
# Navigation focused (default)
/set gesture_left_short "/window prev"
/set gesture_right_short "/window next"

# Activity focused  
/set gesture_right_long "/window next_act"  # Next window with activity
/set gesture_left_long "/window last"       # Most recent active window
```

### Integration with Scroll

Mouse gestures work alongside chat scrolling:
```
/set mouse_scroll_chat on      # Enable mouse wheel scrolling in chat
```

Both features complement each other - scroll with wheel, navigate with gestures.

### Why These Gestures Work Well

The default gesture mappings are designed around common IRC usage patterns:

- **Left = Previous**: Natural "go back" mental model
- **Short swipes**: Frequent actions (prev/next window)  
- **Long swipes**: Jump actions (home base, recent activity)
- **Right long → last window**: Brilliant for IRC - takes you where action is happening

This creates an intuitive navigation system that feels natural and speeds up your IRC workflow.

---

**Need more details?** See the complete [Mouse Gestures UX Design Guide](MOUSE-GESTURES-UX.md) for comprehensive information about customization, technical details, and advanced usage patterns.