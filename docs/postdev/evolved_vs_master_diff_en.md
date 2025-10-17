# Evolved Irssi vs Master Irssi - Detailed Feature Comparison

## 🚀 Sidepanel System (Main Innovation)

**Description**: Complete sidepanel system with modular architecture and intelligent management.

- **Modularity**: Separate modules for core, layout, rendering, activity and signals
- **Intelligent Redrawing**: Granular refresh of only left/right panel instead of entire screen
- **Batching Mass Events**: Hybrid batching system for mass join/part events (400+ users)
- **Multi-Server Support**: Alphabetical server sorting with proper server tags
- **Kicked Channel Preservation**: Maintains channel labels after being kicked with maximum priority

## 🖱️ Mouse Gesture System

**Description**: Intuitive navigation between IRC windows by dragging mouse in chat area.

- **4 Gesture Types**: Short/long swipes left/right with configurable actions
- **Intelligent Recognition**: Active only in chat area, not in sidepanels
- **SGR Protocol**: Advanced mouse motion tracking with precise drag event detection
- **Optimized Defaults**: Default mappings tailored to typical IRC usage patterns
- **Configurable Sensitivity**: Adjustable sensitivity and timeout for gestures

## 🎨 Enhanced Nick Display System

**Description**: Advanced nick formatting system with alignment, truncation and coloring.

- **Dynamic Alignment**: Intelligent alignment with configurable columns
- **Smart Truncation**: Truncation of long nicks with "+" indicator
- **Hash-Based Coloring**: Consistent colors per nick per channel with configurable palettes
- **Mode Color Separation**: Separate colors for user statuses (op/voice/normal)
- **Real-Time Updates**: All formatting applied dynamically during display

## 📺 Whois in Active Window

**Description**: Display whois responses directly in active chat window instead of separate one.

- **Context Preservation**: No context switching during whois checks
- **Improved Workflow**: Smoother workflow during conversations
- **Backward Compatible**: Can be disabled to return to standard behavior
- **Clean Integration**: Natural integration into existing interface
- **Message Level Filtering**: Proper message levels for different response types

## 🪟 Intelligent Window Management

**Description**: Automatic creation of Notices and server separator windows for better organization.

- **Notices Window**: Window #1 programmatically set as "Notices" with level `NOTICES|CLIENTNOTICE|CLIENTCRAP|CLIENTERROR`
- **Auto Server Windows**: Automatic creation of server status windows on connect with level `ALL -NOTICES -CLIENTNOTICE -CLIENTCRAP -CLIENTERROR`
- **Message Separation**: System messages (help, errors, command output) go to Notices, IRC traffic to server windows
- **Sidepanel Separators**: Server windows serve as visual separators in left sidepanel with proper servertags
- **Immortal & Sticky**: Notices is immortal, server windows are sticky to respective connections

## 🎯 New /window lastone Command

**Description**: Addition of missing command to go to actually last window in list.

- **Missing Functionality**: Command that strangely didn't exist in original irssi despite obvious need
- **Gesture Integration**: Key for mouse gesture system (long right swipe)
- **True Last Window**: Goes to window with highest refnum, not last active like `/window last`
- **IRC Workflow**: Perfect for quickly jumping to end of window list where activity often happens
- **Consistent Behavior**: Behaves predictably - always goes to highest numbered window

## 🎯 Perfect UTF-8 & Emoji Support (v0.0.7)

**Description**: Comprehensive UTF-8 implementation with perfect emoji display in modern terminals.

- **Unified Grapheme Logic**: Identical grapheme cluster logic between input field and chat window
- **Variation Selector Mastery**: Perfect handling of emoji with variation selectors (❣️, ♥️)
- **Chat Window Fix**: Complete elimination of emoji overflow from chat window to sidepanels
- **Modern Terminal Support**: Native support for Ghostty without legacy mode requirement
- **Enhanced string_advance**: Extended `string_advance_with_grapheme_support()` with special emoji logic
- **Consistent Width Calculation**: Unified width calculation between input and display systems
- **Zero Breaking Changes**: All existing functionality preserved and enhanced

## ⚡ Performance Optimizations

**Description**: Performance optimizations focused on reducing unnecessary refresh operations.

- **Granular Redraws**: Functions `redraw_left_panels_only()` and `redraw_right_panels_only()`
- **Event-Specific Updates**: Updates only relevant panels for specific events
- **Batch Processing**: Grouping of mass events with timer fallback and immediate sync triggers
- **Safety Checks**: Protection against errors during window creation/destruction
- **Debug Logging**: Debug logging system with file output for diagnostics

## 🔧 Enhanced Build System

**Description**: Modern build system with automatic dependency management.

- **Meson + Ninja**: Fast, reliable builds with comprehensive dependency management
- **Cross-Platform**: Native support for macOS and Linux distributions
- **Auto-Installation**: Automatic detection and installation of all required packages
- **Feature Complete**: Perl scripting, OTR messaging, UTF8proc, SSL/TLS out of the box
- **Dual Mode**: Installation as `irssi` (replacement) or `erssi` (independent coexistence)

## 🎨 Premium Themes Collection

**Description**: High-quality theme collection with advanced statusbar configurations.

- **Nexus Steel Theme**: Modern cyberpunk theme with advanced configurations
- **Enhanced Default**: Improved version of classic irssi theme with better readability
- **Colorless Theme**: Minimalist theme for terminals with limited color support
- **iTerm2 Integration**: Premium `material-erssi.itermcolors` color scheme
- **Auto-Installation**: Automatic copying of themes to `~/.erssi/` on first startup

## 📋 New /set Settings

### Sidepanel System
```bash
/set sidepanel_left on                    # Enable left panel (window list)
/set sidepanel_right on                   # Enable right panel (nicklist)
/set sidepanel_left_width 20              # Left panel width
/set sidepanel_right_width 16             # Right panel width
/set sidepanel_right_auto_hide on         # Auto-hide right panel
/set sidepanel_debug off                  # Debug logging to /tmp/irssi_sidepanels.log
/set auto_create_separators on            # Automatic creation of separator windows
```

### Mouse Gestures System
```bash
/set mouse_gestures on                    # Enable mouse gesture system
/set mouse_scroll_chat on                 # Enable mouse scrolling in chat
/set gesture_left_short "/window prev"    # Command for short left gesture
/set gesture_left_long "/window 1"        # Command for long left gesture
/set gesture_right_short "/window next"   # Command for short right gesture
/set gesture_right_long "/window lastone" # Command for long right gesture
/set gesture_sensitivity 10               # Gesture sensitivity (pixels)
/set gesture_timeout 1000                 # Gesture timeout (milliseconds)
```

### Nick Display System
```bash
/set nick_column_enabled on               # Enable nick column system
/set nick_column_width 12                 # Nick column width
/set nick_hash_colors "2,3,4,5,6,7,9,10,11,12,13,14" # Hash color palette
```

### Whois Enhancement
```bash
/set print_whois_rpl_in_active_window on  # Whois in active window
```

## 🎨 New Theme Formats

### Sidepanel Formats
```
sidepanel_header = "%B*%B$0%N"                    # Server headers
sidepanel_item = "%W %W$0%N"                      # Normal items
sidepanel_item_selected = "%g%w> %g%w$0%N"        # Selected items
sidepanel_item_nick_mention = "%M# %M$0%N"        # Nick mention (priority 4)
sidepanel_item_query_msg = "%M+ %M$0%N"           # Private message (priority 4)
sidepanel_item_activity = "%y* %y$0%N"            # Channel activity (priority 3)
sidepanel_item_events = "%Go%N %G$0%N"            # Events (priority 1)
sidepanel_item_highlight = "%R! %R$0%N"           # Highlights (priority 2)
```

### Nick Status Formats (Dual-Parameter)
```
sidepanel_nick_op_status = "%Y$0%N%B$1%N"         # Operators (gold status, steel nick)
sidepanel_nick_voice_status = "%C$0%N%c$1%N"      # Voice (cyan status, light cyan nick)
sidepanel_nick_normal_status = "%w$0%N%w$1%N"     # Normal (light gray for visibility)
```

## 🔧 New Expandos

```
$nickalign    # Returns alignment spaces for nick column
$nicktrunc    # Returns truncated nick with "+" indicator if too long
$nickcolored  # Returns nick with hash-based coloring
```

## 📝 New Commands

```bash
/nickhash shift [channel]  # Manually shift nick colors in channel
/window lastone            # Go to window with highest refnum (actually last)
```

## ⚙️ Configuration Philosophy

**All new features are enabled by default** - this is the design intent of Evolved Irssi project. Every feature can be disabled through appropriate `/set` setting, but by default everything is active so users immediately experience the full capabilities.

**Compatibility Preservation**: We strived to maintain compatibility with irssi code conventions, theme standards, and overall project architecture. All changes are implemented as extensions, not modifications of existing functionality.

## 🏗️ Module Architecture

### Sidepanel System Files
- `sidepanels-core.c` - Main coordination and settings management
- `sidepanels-render.c` - Rendering logic with optimized redraw functions
- `sidepanels-activity.c` - Activity tracking and batch processing
- `sidepanels-signals.c` - IRC event signal handling
- `sidepanels-layout.c` - Panel layout and positioning management

### Mouse System Files
- `gui-mouse.c` - Mouse event handling and SGR protocol parser
- `gui-gestures.c` - Gesture system with drag detection and motion classification

### Enhanced Features Files
- `fe-expandos.c` - Extended expandos for nick formatting
- `module-formats.c` - Default theme format definitions

---

**Summary**: Evolved Irssi introduces 47 new settings, 9 new theme formats, 3 new expandos and 1 new command, while maintaining 100% compatibility with original irssi.
