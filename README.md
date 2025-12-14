# erssi 🚀
**Enhanced/Evolved IRC Client**

[![Latest Release](https://img.shields.io/github/v/release/erssi-org/erssi)](https://github.com/erssi-org/erssi/releases/latest)
[![GitHub stars](https://img.shields.io/github/stars/erssi-org/erssi.svg?style=social&label=Stars)](https://github.com/erssi-org/erssi)
[![License](https://img.shields.io/badge/License-GPL--2.0--or--later-blue.svg)](https://opensource.org/licenses/GPL-2.0)
[![IRC Network](https://img.shields.io/badge/Chat-IRC-green.svg)](irc://irc.ircnet.com)
[![Build Status](https://img.shields.io/badge/Build-Meson%2BNinja-orange.svg)](https://mesonbuild.com/)
[![Website](https://img.shields.io/badge/Website-erssi.org-blue.svg)](https://erssi.org)

## What is erssi?

**erssi v1.2.5** is a next-generation IRC client that builds upon the robust foundation of classic irssi, introducing modern features and enhanced user experience without sacrificing the simplicity and power that made irssi legendary.

🎯 **Mission**: Modernizing IRC, one feature at a time, while preserving the soul of irssi.

🌐 **Website**: https://erssi.org

## 🌟 Key Features

### 🎨 Advanced Sidepanel System
- **Differential Rendering**: Flicker-free updates - only changed lines are redrawn, similar to WeeChat/modern TUI approach
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
- **Flicker-Free Sidepanels**: Differential rendering with line-by-line caching for smooth, flicker-free panel updates
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

---

## ✨ Latest Release - v1.2.4

### 🎨 Flicker-Free Sidepanels

The v1.2.4 release introduces differential rendering for smooth, flicker-free sidepanel updates:

- **Differential Rendering**: Only changed lines are redrawn instead of entire panel refresh
- **Line-by-Line Caching**: Stores text, prefix, format, and refnum for intelligent comparison
- **Terminal Buffering**: Uses term_refresh_freeze/thaw to batch all terminal operations
- **Smart Cache Invalidation**: Full redraw only when dimensions or scroll offset change
- **Modern TUI Approach**: Similar rendering strategy to WeeChat and other modern terminal UIs

**See [CHANGELOG.md](CHANGELOG.md) for complete version history and detailed changes.**

## 🚀 Quick Start

### One-Line Installation

```bash
# Clone and run the installation script
git clone https://github.com/erssi-org/erssi.git
cd erssi
./install.sh
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
./install.sh
# Choose: 1 (Global) → installs to /opt/erssi with symlink in /usr/local/bin
```

**Option 2: Local installation**
```bash
./install.sh
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

## 🏆 Acknowledgments

- **[Irssi Core Team](https://irssi.org/)**: Original irssi project and continued excellence
- **[Open Source Community](https://github.com/irssi/irssi)**: Inspiration and collaborative spirit
- **Contributors**: Everyone who made evolved irssi possible

## 📞 Contact & Support

- **🐛 Issues**: [GitHub Issues](https://github.com/kofany/irssi/issues)
- **💬 Discussion**: [GitHub Discussions](https://github.com/kofany/irssi/discussions)
- **📧 IRC**: `#erssi` on IRCnet
- **📖 Documentation**: [Installation Guide](INSTALL-SCRIPT.md)

## 🤝 Contributing

We welcome contributions from the community! erssi uses **Conventional Commits** for changelog generation and automated releases.

- **📋 Contributing Guide**: [CONTRIBUTING.md](CONTRIBUTING.md) - Learn about our development workflow, commit message format, and how to submit pull requests
- **🚀 Release Process**: [RELEASE.md](RELEASE.md) - For maintainers: how to create releases
- **📝 Changelog**: [CHANGELOG.md](CHANGELOG.md) - See what's new in each version
- **🏗️ Architecture**: [CLAUDE.md](CLAUDE.md) - Detailed architecture and development guide

**Quick Start for Contributors:**

```bash
# Fork and clone the repository
git clone https://github.com/YOUR_USERNAME/erssi.git
cd erssi

# Install dependencies and build
./install-dev.sh
meson setup Build -Dwith-perl=yes -Dwith-otr=yes
ninja -C Build

# Make your changes using conventional commits
git commit -m "feat: add new feature"
git commit -m "fix: resolve issue"

# Submit a pull request
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines.

## 📄 License

Evolved Irssi is released under the **GNU General Public License v2.0 or later** (GPL-2.0-or-later), consistent with the original irssi project.

---

<div align="center">

**🎯 Evolved Irssi: Where Classic Meets Modern** 

*Preserving the power of irssi while embracing the future of IRC*

[⭐ Star us on GitHub](https://github.com/kofany/irssi/tree/evolved-irssi) • [📚 Read the Docs](INSTALL-SCRIPT.md) • [🚀 Get Started](#quick-start)

</div>