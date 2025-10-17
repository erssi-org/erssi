# erssi 🚀
**Enhanced/Evolved IRC Client**

[![GitHub stars](https://img.shields.io/github/stars/erssi-org/erssi.svg?style=social&label=Stars)](https://github.com/erssi-org/erssi)
[![License](https://img.shields.io/badge/License-GPL--2.0--or--later-blue.svg)](https://opensource.org/licenses/GPL-2.0)
[![IRC Network](https://img.shields.io/badge/Chat-IRC-green.svg)](irc://irc.ircnet.com)
[![Build Status](https://img.shields.io/badge/Build-Meson%2BNinja-orange.svg)](https://mesonbuild.com/)
[![Website](https://img.shields.io/badge/Website-erssi.org-blue.svg)](https://erssi.org)

## What is erssi?

**erssi v1.0.0** is a next-generation IRC client that builds upon the robust foundation of classic irssi, introducing modern features and enhanced user experience without sacrificing the simplicity and power that made irssi legendary.

🎯 **Mission**: Modernizing IRC, one feature at a time, while preserving the soul of irssi.

🌐 **Website**: https://erssi.org

## 🌟 Key Features

### 🎨 Advanced Sidepanel System
- **Modular Architecture**: Complete separation of concerns with dedicated modules for core, layout, rendering, activity tracking, and signal handling
- **Smart Window Sorting**: Multi-server support with alphabetical server grouping and intelligent window positioning
- **Kicked Channel Preservation**: Maintains channel labels and highlights with maximum priority when kicked from channels
- **Auto-Separator Windows**: Automatic creation of server status windows with proper message level filtering
- **Smart Redraw Logic**: Separate functions for left panel, right panel, and both panels with event-specific updates
- **Batched Mass Events**: Hybrid batching system for mass joins/parts with timer fallback and immediate sync triggers

### 🎯 Enhanced Nick Display
- **Dynamic Nick Alignment**: Intelligent padding and truncation with `+` indicator for long nicks
- **Hash-Based Nick Coloring**: Consistent colors per nick per channel with configurable palette and reset events
- **Color Reset System**: Configurable events (quit/part/nickchange) that reassign colors, plus manual `/nickhash shift` command
- **Real-Time Updates**: All formatting applied dynamically as messages appear

### 🖱️ Mouse Gesture Navigation System
- **Intuitive Window Switching**: Navigate between IRC windows with simple left/right mouse swipes in the chat area
- **Four Gesture Types**: Short/long swipes in both directions for comprehensive navigation control
- **Smart Recognition**: Only active in chat area, prevents accidental triggers in sidepanels
- **Configurable Actions**: Map any irssi command to gestures, with intelligent defaults for IRC workflow
- **Optimized for IRC**: Default mappings designed around common usage patterns (prev/next/home/last active)

### Enhanced User Experience
- **Whois in Active Window**: Say goodbye to context switching! Whois responses appear directly in your current chat window
- **Optimized Sidepanels with Mouse Support**: Intelligent redraw system for left (window list) and right (nicklist) panels with batching for mass events
- **Enhanced Nick Display**: Advanced nick alignment, intelligent truncation, and hash-based nick coloring system with separate mode colors
- **Performance Optimizations**: Granular panel redraws instead of full refreshes, batched updates for mass join/part events
- **Separate Configuration**: Uses `~/.erssi/` directory, allowing coexistence with standard irssi

### 🔐 Secure Credential Management
- **Integrated Encryption**: Passwords automatically encrypted with AES-256-CBC
- **Master Password Protection**: PBKDF2 key derivation with 100,000 iterations
- **External Storage Support**: Store credentials in separate encrypted file
- **Universal Coverage**: Automatically handles all sensitive fields including fe-web password
- **Transparent Operation**: No code changes needed - works through settings system
- **Migration Tools**: Easy switching between storage modes and encryption states

### 🌐 Web Interface (fe-web)
- **WebSocket Server**: Real-time bidirectional communication (RFC 6455 compliant)
- **SSL/TLS Support**: Secure connections with auto-generated certificates (wss://)
- **Application Encryption**: AES-256-GCM for message-level security
- **Multi-Client Support**: Multiple web clients with independent state management
- **Credential Integration**: Web password automatically protected by credential system
- **Network Management**: Configure servers and networks through web API
- **Full IRC Events**: Complete IRC protocol event handling and forwarding

### Full Compatibility
- **100% Perl Script Compatible**: All existing irssi Perl scripts work without modification
- **Theme Compatible**: Use any irssi theme seamlessly
- **Configuration Compatible**: Import your existing irssi configuration effortlessly

### 🎨 Premium Themes Collection
- **Nexus Steel Theme**: Modern cyberpunk-inspired theme with advanced statusbar configurations
- **Default Theme**: Enhanced version of classic irssi theme with improved readability
- **Colorless Theme**: Clean, minimalist theme for terminal environments with limited color support
- **iTerm2 Color Scheme**: Premium `material-erssi.itermcolors` terminal color scheme optimized for erssi
- **Auto-Installation**: Themes are automatically copied to `~/.erssi/` on first startup
- **Custom Location**: All themes located in `themes/` directory for easy customization

### Modern Build System
- **Meson + Ninja**: Fast, reliable builds with comprehensive dependency management
- **Full Feature Support**: Perl scripting, OTR messaging, UTF8proc, SSL/TLS out of the box
- **Cross-Platform**: Native support for macOS and Linux distributions

## 🎉 What's New in v0.0.7 - Perfect UTF-8 Display

### 🎨 Critical Chat Window Fixes
- **Emoji Overflow Eliminated**: Completely resolved text overflow from chat window into sidepanels
- **Unified Display Logic**: Chat window and input field now use identical emoji width calculation
- **Variation Selector Mastery**: Perfect handling of emoji with variation selectors (❣️, ♥️, etc.)
- **Modern Terminal Excellence**: Flawless rendering in Ghostty without requiring legacy mode
- **Display Precision**: Accurate text measurement prevents any layout corruption

### 🔧 Enhanced Implementation
- **Consistent Grapheme Logic**: Both input and display systems use same advanced UTF-8 processing
- **Special Emoji Handling**: Proper width calculation for base emoji + variation selector combinations
- **Zero Breaking Changes**: All existing functionality preserved and enhanced
- **Cross-Platform Stability**: Reliable operation across all supported terminals and systems

## 📚 Previous Release - v0.0.6 - Emoji & Unicode Milestone

### 🎨 Advanced Emoji Support
- **Grapheme Cluster Detection**: Full Unicode Standard Annex #29 compliance for proper emoji rendering
- **Multi-Codepoint Emoji**: Handles complex emoji like 👨‍👩‍👧‍👦 (family), 🧑🏻‍💻 (person with skin tone), ♥️ (heart with variation selector)
- **Initial Sidepanel Fixes**: Major improvements to emoji handling in modern terminals
- **Transparent Implementation**: Automatic detection with fallback to legacy mode when utf8proc unavailable
- **Zero Configuration**: Always-on improvement with no settings required

## 📚 Previous Release - v0.0.5 Milestone

### 🏗️ Complete Modular Architecture
- **Separated Components**: Dedicated modules for core logic, layout management, rendering, activity tracking, and signal handling
- **Clean APIs**: Well-defined interfaces between components for maintainability and extensibility
- **Terminal Resize Fix**: Resolved issue where sidepanel content disappeared during terminal resize

### 🌐 Enhanced Multi-Server Support  
- **Intelligent Server Sorting**: Alphabetical grouping of servers with proper window positioning
- **Server Tag Labels**: Display actual server tags instead of connection strings for clarity
- **Scalable Architecture**: Supports unlimited number of IRC networks simultaneously

### 💪 Kicked Channel Resilience
- **Label Preservation**: Channel names remain visible even after being kicked from channels
- **Priority Highlighting**: Kicked channels receive maximum priority (level 4) for immediate attention
- **Position Maintenance**: Kicked channels maintain their position in window sorting instead of falling to bottom

### ⚡ Smart Window Management
- **Auto-Separator Creation**: Automatic generation of server status windows with proper message level filtering
- **Synchronized Sorting**: Window list (`/window list`) perfectly matches sidepanel sorting
- **Message Level Filtering**: Notices window receives all client messages (help, errors, command output) while server windows get relevant IRC traffic

### 🧹 Code Quality Improvements
- **Debug Code Cleanup**: Removed development debugging statements for production readiness
- **Performance Optimization**: Cleaner code paths and reduced logging overhead
- **Maintainability**: Better code organization and documentation

## 🚀 Quick Start

### One-Line Installation

```bash
# Clone and run the installation script
git clone https://github.com/erssi-org/erssi.git
cd erssi
./install-erssi.sh
```

The installation script will:
- ✅ Detect your system (macOS/Linux)
- ✅ Install all required dependencies automatically
- ✅ Choose global (`/opt/erssi`) or local (`~/.local`) installation
- ✅ Build with full feature support (Perl, OTR, UTF8proc)
- ✅ Create symlinks for easy access

### Installation Options

**Option 1: Global installation (Recommended)**
```bash
./install-erssi.sh
# Choose: 1 (Global) → installs to /opt/erssi with symlink in /usr/bin
```

**Option 2: Local installation**
```bash
./install-erssi.sh
# Choose: 2 (Local) → installs to ~/.local with symlink in ~/.local/bin
```

## 📦 Manual Installation

For advanced users who prefer manual control:

```bash
# Install dependencies (varies by system)
# See docs/INSTALL for complete package lists

meson setup Build --prefix=/opt/erssi -Dwith-perl=yes -Dwith-otr=yes -Ddisable-utf8proc=no
ninja -C Build
sudo ninja -C Build install  
sudo ninja -C Build install
```

## 🔧 Dependencies

### Automatically Installed
The installation script handles all dependencies:

**macOS (Homebrew):**
- meson, ninja, pkg-config, glib, openssl@3, ncurses
- utf8proc, libgcrypt, libotr, perl

**Linux (APT/DNF/Pacman):**
- Build tools, meson, ninja, pkg-config
- libglib2.0-dev, libssl-dev, libncurses-dev
- libperl-dev, libutf8proc-dev, libgcrypt-dev, libotr-dev

### Build Features Enabled
- ✅ **Perl Scripting**: Full embedded Perl support
- ✅ **OTR Messaging**: Off-The-Record encrypted messaging
- ✅ **UTF8proc**: Enhanced Unicode handling
- ✅ **SSL/TLS**: Secure connections
- ✅ **Terminal UI**: Full ncurses support with mouse interaction

## 📋 System Requirements

### Supported Systems
- **macOS**: 10.15+ (Catalina and later)
- **Linux**: Ubuntu 18.04+, Debian 10+, Fedora 30+, Arch Linux

### Build Requirements
- Meson ≥ 0.53
- Ninja ≥ 1.5
- GLib ≥ 2.32
- Modern C compiler (GCC/Clang)

## 🏃‍♂️ Running Evolved Irssi

```bash
# For erssi installation
erssi

# For irssi replacement
irssi

```

### First Run
- **erssi**: Creates `~/.erssi/` configuration directory
- **irssi**: Uses existing `~/.irssi/` or creates new one

## ⚙️ Configuration

### 🖱️ Mouse Gesture Settings

Navigate between IRC windows with intuitive mouse swipes:

```bash
# Enable mouse gestures (default: on)
/set mouse_gestures on
/set mouse_scroll_chat on

# Default gesture mappings (optimized for IRC workflow)
/set gesture_left_short "/window prev"    # Most common: previous window
/set gesture_left_long "/window 1"        # Jump to network status  
/set gesture_right_short "/window next"   # Next window in sequence
/set gesture_right_long "/window last"    # Jump to last active window

# Sensitivity and timing
/set gesture_sensitivity 10               # Minimum swipe distance (pixels)
/set gesture_timeout 1000                 # Maximum gesture time (ms)
```

**Quick Guide**: Drag mouse left/right in chat area to switch windows. See [Mouse Gestures Guide](docs/MOUSE-GESTURES-QUICK-GUIDE.md) for details.

### 🎨 Nick Display Settings

Evolved Irssi includes advanced nick formatting features that can be configured:

```bash
# Enable nick column alignment and truncation
/set nick_column_enabled on
/set nick_column_width 12

# Enable hash-based nick coloring (coming soon)
/set nick_color_enabled on
/set nick_mode_color_enabled on
```

### � Credential Management Settings

Secure your passwords and sensitive data:

```bash
# Set master password for encryption
/credential passwd <your-master-password>

# Enable config encryption (encrypts passwords in config file)
/set credential_config_encrypt on

# Or use external encrypted file (recommended)
/set credential_storage_mode external
/set credential_external_file .credentials

# Check credential status
/credential status

# List all stored credentials
/credential list
```

**Protected Fields**:
- Server passwords
- SASL username/password
- Proxy passwords
- OTR passwords
- TLS certificate passwords
- Autosendcmd (NickServ identify, Q AUTH, etc.)
- **fe_web_password** - Web interface password

### 🌐 Web Interface Settings

Configure the built-in web interface:

```bash
# Enable web interface
/set fe_web_enabled on
/set fe_web_port 9001
/set fe_web_bind 127.0.0.1

# Set web password (automatically encrypted by credential system)
/set fe_web_password <strong-password>

# Generate strong random password
/set fe_web_password $(openssl rand -base64 32)

# Check web server status
/fe_web status
```

**Security Notes**:
- Web password is automatically encrypted when `credential_config_encrypt` is enabled
- In external storage mode, web password is moved to `.credentials` file
- SSL/TLS is always enabled for web connections
- Application-level AES-256-GCM encryption protects all web traffic

### �🔧 Sidepanel Debug (Advanced)

For troubleshooting sidepanel performance:

```bash
# Enable debug logging to /tmp/irssi_sidepanels.log
/set debug_sidepanel_redraws on

# View real-time redraw events
tail -f /tmp/irssi_sidepanels.log
```

### Left Sidepanel Setup

For optimal experience with the left sidepanel feature, we recommend configuring separate status windows for each IRC network. This creates clean visual separators in the sidepanel and improves navigation.

Add this to your `~/.erssi/config` (or `~/.irssi/config`):

```
windows = {
  1 = {
    immortal = "yes";
    name = "Notices";
    level = "NOTICES";
    servertag = "Notices";
  };
  2 = {
    name = "IRCnet";
    level = "ALL -NOTICES";
    servertag = "IRCnet";
  };
  3 = {
    name = "IRCnet2";
    level = "ALL -NOTICES";
    servertag = "IRCnet2";
  };
};
```

**Key benefits:**
- **Network Separation**: Each IRC network gets its own status window
- **Visual Organization**: Sidepanel shows clear network boundaries
- **Better Navigation**: Easy switching between different networks
- **Servertag Matching**: The `servertag` field should match your server connection names

**Best Practices:**
- Create one status window per IRC network
- Use descriptive names matching your server tags
- Keep the "Notices" window for general system messages
- Adjust `level` settings based on what you want to see in each window

## 🔍 Verification & Troubleshooting

Check your installation:
```bash
./check-installation.sh          # Full system check
./check-installation.sh --quiet  # Minimal output
./check-installation.sh --verbose # Detailed diagnostics
```

For detailed troubleshooting, see [INSTALL-SCRIPT.md](INSTALL-SCRIPT.md).

## 📁 Project Structure

```
irssi/
├── install-irssi.sh       # Main installation script
├── check-installation.sh  # Installation checker
├── erssi-convert.sh      # Irssi → Erssi converter
├── INSTALL-SCRIPT.md     # Detailed installation guide
├── src/                  # Source code
│   ├── fe-text/
│   │   ├── sidepanels.c  # Enhanced sidepanel system with optimized redraws
│   │   └── sidepanels.h  # Sidepanel definitions
│   └── fe-common/core/
│       └── fe-expandos.c # Nick formatting expandos (alignment, truncation, coloring)
├── themes/              # Premium theme collection
│   ├── nexus.theme      # Modern cyberpunk theme (recommended)
│   ├── default.theme    # Enhanced classic irssi theme
│   └── colorless.theme  # Minimalist theme for limited color terminals
├── startup              # Evolved banner displayed on erssi startup
├── material-erssi.itermcolors # Premium iTerm2 color scheme for optimal erssi experience
└── docs/               # Documentation
```

## 🤝 Contributing

We welcome contributions of all kinds!

### Quick Start
```bash
git clone https://github.com/kofany/irssi.git -b evolved-irssi
cd irssi
# Make your changes
git add .
git commit -m "feat: your awesome feature"
git push origin feature/your-feature
```

### Areas for Contribution
- 🐛 **Bug Reports**: Found an issue? Let us know!
- ✨ **New Features**: Ideas for enhancing the IRC experience
- 📚 **Documentation**: Help improve guides and tutorials
- 🎨 **Themes**: Create beautiful themes for the community
- 🔧 **Platform Support**: Extend support to more systems

## 📈 Performance

Evolved Irssi maintains the legendary performance of classic irssi:
- **Memory**: Minimal additional footprint (~2-5MB)
- **CPU**: Negligible performance overhead
- **Startup**: Fast boot times maintained
- **Network**: Efficient IRC protocol handling

## 🆚 irssi vs erssi

| Feature | Standard irssi | Evolved erssi |
|---------|---------------|---------------|
| **Configuration** | `~/.irssi/` | `~/.erssi/` |
| **Binary Name** | `irssi` | `erssi` |
| **Whois Display** | Separate window | Active window |
| **Mouse Support** | Limited | Full sidepanel + gestures |
| **Window Navigation** | Keyboard only | Keyboard + mouse gestures |
| **Gesture System** | None | 4 configurable swipe gestures |
| **Nick Alignment** | Basic | Enhanced alignment |
| **Perl Scripts** | ✅ Compatible | ✅ Compatible |
| **Themes** | ✅ Compatible | ✅ Compatible |
| **Coexistence** | N/A | ✅ Runs alongside irssi |

## 📜 Version History

### v1.5-erssi-v0.0.7 (Current)
- **🎯 CRITICAL FIX**: Completely eliminated emoji overflow from chat window into sidepanels
- **🔧 Unified UTF-8 Logic**: Chat window and input field now use identical grapheme cluster processing
- **✨ Perfect Emoji Handling**: Flawless rendering of variation selector emoji (❣️, ♥️) in all contexts
- **🌟 Modern Terminal Excellence**: Native support for Ghostty without legacy mode requirement
- **🔧 Technical Implementation**:
  - Enhanced `string_advance_with_grapheme_support()` with special emoji variation selector logic
  - Consistent width calculation between input and display systems
  - Fixed base emoji + variation selector combinations to properly display as width 2
  - Zero breaking changes - all existing functionality preserved and enhanced

### v1.5-erssi-v0.0.6 (Previous)
- **🎨 Advanced Emoji Support**: Full Unicode Standard Annex #29 grapheme cluster detection for proper emoji rendering
- **🔧 Initial Sidepanel Fixes**: Major improvements to emoji handling in modern terminals
- **⚡ Performance Optimized**: Efficient utf8proc integration with automatic fallback to legacy mode
- **🌍 Cross-Platform Unicode**: Enhanced support for complex emoji across macOS and Linux terminals

### v1.5-erssi-v0.0.5
- **🏗️ Complete Modular Architecture**: Separated components with dedicated modules for core, layout, rendering, activity tracking
- **🌐 Enhanced Multi-Server Support**: Intelligent server sorting with alphabetical grouping and proper positioning
- **💪 Kicked Channel Resilience**: Label preservation and priority highlighting for kicked channels
- **⚡ Smart Window Management**: Auto-separator creation with synchronized sorting
- **🧹 Code Quality**: Debug cleanup and performance optimizations

### v1.5-erssi-v0.0.4
- **🖱️ Mouse Gesture System**: Fixed drag detection and motion tracking for reliable gesture recognition
- **⚡ Enhanced Mouse Protocol**: Added SGR button event tracking (1002h) for precise motion detection
- **🎯 Improved Gesture Sensitivity**: Optimized default sensitivity from 20 to 10 pixels for better responsiveness
- **🔧 Technical Fixes**:
  - Fixed drag event condition that prevented motion tracking
  - Added proper button event tracking for gesture system
  - Cleaned up debug logging for production use
  - Improved gesture classification for short/long swipes

### v1.5-evolved (Previous)
- **🎨 Advanced Sidepanel System**: Granular redraw optimizations with event-specific panel updates
- **⚡ Performance Improvements**: Batched mass event handling for channels with 400+ users
- **🎯 Enhanced Nick Display**: Hash-based coloring system with separate mode colors and intelligent truncation
- **🔧 Technical Enhancements**:
  - Replaced global `redraw_all()` with targeted `redraw_*_only()` functions
  - Hybrid batching system (timer + event marker) for mass joins/parts
  - Safety checks for window creation/destruction events
  - C89/C90 compliance for broader compatibility
  - Debug logging system with file output
- **📦 Installation & Build**:
  - Enhanced installation system with cross-platform support
  - Automatic dependency management
  - Improved build configuration
  - Comprehensive documentation

### Previous Versions
- Based on irssi current from irssi master branch
- Incremental feature additions
- Community-driven enhancements

## 🏆 Acknowledgments

- **[Irssi Core Team](https://irssi.org/)**: Original irssi project and continued excellence
- **[Open Source Community](https://github.com/irssi/irssi)**: Inspiration and collaborative spirit
- **Contributors**: Everyone who made evolved irssi possible

## 📞 Contact & Support

- **🐛 Issues**: [GitHub Issues](https://github.com/kofany/irssi/issues)
- **💬 Discussion**: [GitHub Discussions](https://github.com/kofany/irssi/discussions)
- **📧 IRC**: `#erssi` on IRCnet
- **📖 Documentation**: [Installation Guide](INSTALL-SCRIPT.md)

## 📄 License

Evolved Irssi is released under the **GNU General Public License v2.0 or later** (GPL-2.0-or-later), consistent with the original irssi project.

---

<div align="center">

**🎯 Evolved Irssi: Where Classic Meets Modern** 

*Preserving the power of irssi while embracing the future of IRC*

[⭐ Star us on GitHub](https://github.com/kofany/irssi/tree/evolved-irssi) • [📚 Read the Docs](INSTALL-SCRIPT.md) • [🚀 Get Started](#quick-start)

</div>