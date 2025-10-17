# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is **Evolved Irssi (erssi)** - a modernized IRC client based on the classic irssi with enhanced features while maintaining full compatibility. The project version is `1.5-erssi-v0.0.5` and uses the Meson build system.

## Build Commands

### Development Build
```bash
meson setup Build --prefix=/opt/erssi -Dwith-perl=yes -Dwith-otr=yes -Ddisable-utf8proc=no
ninja -C Build
```

### Installation (requires sudo for global install)
```bash
sudo ninja -C Build install
```

### Testing
```bash
# Run tests from build directory
ninja -C Build test
```

### Clean Build
```bash
rm -rf Build
```

## Key Architecture Components

### Core Systems
- **Sidepanel System** (`src/fe-text/sidepanels*`): Modular architecture with separate left (window list) and right (nicklist) panels
  - `sidepanels-core.c`: Main coordination and settings management
  - `sidepanels-render.c`: Rendering logic with optimized redraw functions
  - `sidepanels-activity.c`: Activity tracking and batch processing
  - `sidepanels-signals.c`: IRC event signal handling
  - `sidepanels-layout.c`: Panel layout and positioning

### Enhanced Features
- **Mouse Gesture System** (`src/fe-text/gui-mouse.c`): SGR protocol parsing with configurable swipe gestures for window navigation
- **Nick Display Enhancement** (`src/fe-common/core/fe-expandos.c`): Hash-based coloring, alignment, and truncation
- **Performance Optimizations**: Granular panel redraws instead of full screen refreshes

### Build Configuration
- **Primary Build Tool**: Meson + Ninja (not autotools/make)
- **Key Features Enabled**: Perl scripting, OTR messaging, UTF8proc, SSL/TLS, terminal UI with mouse support
- **Dependency Management**: Automatic GLib download/build if system version unavailable

### Installation Variants
- **Standard Mode**: Installs as `irssi`, uses `~/.irssi/` config
- **Evolved Mode**: Installs as `erssi`, uses `~/.erssi/` config (independent coexistence)
- **Conversion Script**: `erssi-convert.sh` transforms branding and config paths

## Common Development Tasks

### Adding New Sidepanel Features
- Implement in appropriate `sidepanels-*.c` file
- Add settings to `sidepanels-core.c` settings system
- Update header declarations in corresponding `.h` files
- Use targeted redraw functions (`redraw_left_panels_only`, `redraw_right_panels_only`) instead of global redraws

### Mouse Event Handling
- Mouse events flow through `gui-mouse.c` SGR parser
- Gesture system uses drag detection with configurable sensitivity/timeout
- Events forwarded to sidepanel system via callback chain

### Performance Considerations
- **Batch Operations**: Mass join/part events use hybrid batching (timer + event marker)
- **Debug Logging**: File-based debug output to `/tmp/irssi_sidepanels.log` when enabled
- **Memory Management**: C89/C90 compliant code with careful pointer handling

## Configuration and Settings

### Key Settings Locations
- **Build Options**: `meson_options.txt` - configure features like Perl, OTR, UTF8proc
- **Default Config**: `irssi.conf` - base configuration template
- **Themes**: `themes/` directory with multiple theme options
- **Startup Banner**: `startup` file for erssi branding

### Debug Settings
```bash
/set debug_sidepanel_redraws on  # Enable debug logging
/set mouse_gestures on           # Enable mouse gesture system
/set gesture_sensitivity 10      # Mouse swipe sensitivity (pixels)
```

## Important Files for Development
- `src/fe-text/sidepanels-core.c` - Main sidepanel coordination
- `src/fe-text/gui-mouse.c` - Mouse event handling and gestures  
- `src/fe-common/core/fe-expandos.c` - Nick display formatting
- `meson.build` - Primary build configuration
- `install-irssi.sh` - Main installation script with cross-platform support
- `erssi-convert.sh` - Transforms irssi into erssi variant

## Testing and Debugging

### Debug Output
- Sidepanel debug: `/tmp/irssi_sidepanels.log`
- Build output: Check `Build/` directory for compilation artifacts
- Installation verification: Use `check-installation.sh` script

### Common Issues
- **Build Failures**: Ensure all dependencies installed via install script
- **Mouse Issues**: Verify terminal supports SGR mouse protocol (1002h mode)
- **Panel Glitches**: Check debug output for batch processing timing
- TEST AND BUILD CAN BE DONE ONLY BY USER!!