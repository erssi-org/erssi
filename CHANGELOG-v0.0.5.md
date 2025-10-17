# Evolved Irssi (erssi) v1.5-erssi-v0.0.5 - Milestone Release

## 🎉 Major Milestone: Complete Modular Architecture

This release represents a significant architectural achievement - the complete transformation of erssi into a fully modular, scalable IRC client while maintaining 100% compatibility with classic irssi.

## 🏗️ Complete Modular Architecture

### Separated Components
- **`sidepanels-core.c`**: Central coordination, settings management, and main initialization
- **`sidepanels-layout.c`**: Window positioning, resize handling, and layout calculations  
- **`sidepanels-render.c`**: Theme integration, rendering logic, and display formatting
- **`sidepanels-activity.c`**: Activity tracking, window sorting, and priority management
- **`sidepanels-signals.c`**: IRC event handling, signal processing, and auto-features

### Clean APIs & Interfaces
- Well-defined interfaces between all components
- Proper separation of concerns for maintainability
- Extensible architecture for future enhancements

## 🌐 Enhanced Multi-Server Support

### Intelligent Server Management
- **Alphabetical Server Sorting**: Servers automatically grouped and sorted alphabetically
- **Server Tag Labels**: Display actual server tags (e.g., "irc.libera.chat") instead of connection strings
- **Scalable Architecture**: Supports unlimited number of IRC networks simultaneously
- **Perfect Window Positioning**: Server status windows and channels properly grouped per server

### Smart Window Sorting Logic
```
1. Notices (always first)
2. Server Status Windows (alphabetically by server tag)
3. Channels (alphabetically within each server)  
4. Queries (alphabetically within each server)
5. Orphaned windows (if any)
```

## 💪 Kicked Channel Resilience

### Advanced Channel Management
- **Label Preservation**: Channel names remain visible even after being kicked from channels
- **Priority Highlighting**: Kicked channels receive maximum priority (level 4) for immediate attention in themes
- **Position Maintenance**: Kicked channels maintain their position in window sorting instead of falling to bottom
- **Smart Detection**: Automatic recognition of kick events with proper window state preservation

### Technical Implementation
- Intercepts `"message kick"` signal to detect own nick being kicked
- Preserves channel name in `window->name` for continued display
- Sets highest activity priority for theme highlighting
- Updates both `/window list` and sidepanel display consistently

## ⚡ Smart Window Management

### Auto-Separator Creation
- **Automatic Generation**: Server status windows created automatically when connecting to new servers
- **Proper Message Filtering**: Server windows get all traffic except client messages and notices
- **Level Configuration**: Notices window receives `NOTICES + CLIENTNOTICE + CLIENTCRAP + CLIENTERROR`
- **Setting Control**: Controlled by `auto_create_separators` setting (default: enabled)

### Synchronized Sorting
- **Perfect Synchronization**: `/window list` output perfectly matches sidepanel sorting
- **Real-time Updates**: Window renumbering occurs automatically on window creation/destruction
- **Consistent Experience**: Users see identical ordering in both command output and visual interface

### Smart Notices Window
- **Comprehensive Filtering**: Receives all client messages (help output, command results, errors)
- **Server Independence**: Uses `servertag = "*"` for proper isolation
- **Immortal Configuration**: Automatically configured as immortal window
- **Proper Positioning**: Always maintains position 1 in window list

## 🔧 Technical Improvements

### Terminal Resize Fix
- **Root Cause Resolution**: Fixed issue where sidepanel content disappeared during terminal resize
- **Screen Clear Handling**: Added proper sidepanel redraw after `term_clear()` operations
- **Reliable Rendering**: Consistent display across all terminal resize scenarios

### Code Quality & Performance
- **Debug Cleanup**: Removed development debugging statements for production readiness
- **Memory Management**: Proper cleanup and resource management throughout
- **C89/C90 Compliance**: All code follows strict C standards for maximum compatibility
- **Performance Optimization**: Cleaner code paths and reduced logging overhead

### Enhanced Signal Handling
- **Comprehensive Coverage**: Handles all relevant IRC events (join, part, quit, kick, nick, mode changes)
- **Batched Processing**: Intelligent batching for mass events like netsplits
- **Real-time Updates**: Immediate UI updates for important events
- **Window Renumbering**: Automatic window renumbering on structural changes

## 📁 File Structure Changes

### New Modular Files
- `src/fe-text/sidepanels-activity.c/.h` - Activity tracking and window sorting
- `src/fe-text/sidepanels-layout.c/.h` - Layout management and positioning
- `src/fe-text/sidepanels-render.c/.h` - Rendering and theme integration  
- `src/fe-text/sidepanels-signals.c/.h` - Signal handling and IRC events
- `src/fe-text/sidepanels-types.h` - Shared type definitions
- `src/fe-text/sidepanels-core.c` - Central coordination (refactored)

### Removed Files
- `sidepanels_old.c` - Legacy implementation removed

## 🎯 User Experience Improvements

### Visual Enhancements
- **Consistent Labeling**: Server windows show meaningful names instead of connection strings
- **Kicked Channel Visibility**: Kicked channels clearly marked with high priority highlighting
- **Perfect Sorting**: Logical, predictable window ordering across all interfaces

### Reliability Improvements  
- **Terminal Compatibility**: Works reliably across all terminal resize scenarios
- **Multi-Server Stability**: Robust handling of multiple simultaneous IRC connections
- **Event Processing**: Reliable processing of all IRC events without conflicts

## 🧪 Testing & Validation

This release has been extensively tested with:
- Multiple IRC networks simultaneously (4+ servers)
- Terminal resize scenarios on macOS
- Channel kicks and rejoins
- Mass join/part events
- Window creation/destruction cycles
- Theme integration and display formatting

## 📈 Development Metrics

- **Files Modified**: 25+ core files
- **Architecture**: Complete modular separation
- **Compatibility**: 100% backward compatible with irssi
- **Standards**: Full C89/C90 compliance
- **Performance**: Optimized code paths with reduced overhead

## 🚀 What's Next

This milestone establishes the foundation for:
- Advanced theme integration features
- Enhanced mouse gesture capabilities  
- Extended multi-server management tools
- Additional IRC network support features

---

**Full Changelog**: [v0.0.4...v0.0.5](https://github.com/kofany/irssi/compare/evolved-irssi...evolved-dev)

**Installation**: Use `./install-irssi.sh` and select option 2 (erssi) for the evolved experience.

**Compatibility**: This release maintains 100% compatibility with existing irssi configurations, themes, and Perl scripts.