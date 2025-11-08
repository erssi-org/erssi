# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**erssi** (Enhanced/Evolved IRC Client) is a modernized IRC client built on irssi's foundation, introducing features like advanced sidepanels, mouse gesture navigation, credential encryption, and web interface support while maintaining 100% compatibility with irssi Perl scripts and themes.

- **Website**: https://erssi.org
- **Version**: 1.0.0 (based on irssi 1.5)
- **Build System**: Meson + Ninja
- **Configuration**: Uses `~/.erssi/` (allows coexistence with standard irssi)

## Build Commands

### Initial Setup
```bash
# Configure build with full features
meson setup Build --prefix=/opt/erssi -Dwith-perl=yes -Dwith-otr=yes -Ddisable-utf8proc=no

# For local installation (instead of global /opt/erssi)
meson setup Build --prefix=$HOME/.local -Dwith-perl=yes -Dwith-otr=yes -Ddisable-utf8proc=no
```

### Building
```bash
# Compile (use -C to specify build directory)
ninja -C Build

# Install (may require sudo for global installation)
sudo ninja -C Build install
```

### Reconfiguration
```bash
# Reconfigure build options
meson configure Build -Dwith-perl=yes -Dwith-otr=yes

# Clean and rebuild
ninja -C Build clean
ninja -C Build
```

### Testing
```bash
# Run all tests
ninja -C Build test

# Run specific test
Build/tests/irc/core/test-irc
Build/tests/fe-text/test-paste-join-multiline
```

**IMPORTANT**: After making changes, check for compilation errors with `ninja -C Build` but do NOT start irssi - the user will do that.

## Development Workflow

### Quick Iteration Cycle
```bash
# 1. Make code changes
# 2. Recompile
ninja -C Build

# 3. Check for errors
ninja -C Build 2>&1 | grep -i error

# 4. User will test by running erssi
```

### Installation Script
For end-users, `install-erssi.sh` automates dependency installation and build:
- Detects system (macOS/Linux) and package manager
- Installs all dependencies (glib, openssl, ncurses, utf8proc, libgcrypt, libotr, perl)
- Configures with full feature support
- Offers global (`/opt/erssi`) or local (`~/.local`) installation

## High-Level Architecture

### Three-Layer Design

```
┌─────────────────────────────────────────────────────────┐
│                  IRSSI CORE (IRC Protocol)              │
│  [src/core] [src/irc] - IRC servers, channels, queries  │
└─────────────────────────────────────────────────────────┘
                            ↑
    ┌───────────────────────┼───────────────────────┐
    │                       │                       │
    ▼                       ▼                       ▼
┌──────────────┐  ┌──────────────────┐  ┌──────────────┐
│  FE-COMMON   │  │  FE-TEXT (TUI)   │  │  FE-WEB      │
│ [Core Logic] │  │   [Terminal UI]  │  │[WebSocket]   │
└──────────────┘  └──────────────────┘  └──────────────┘
```

### Key Directories

- **`src/core/`** - IRC protocol, server/channel management, credential system
- **`src/irc/`** - IRC-specific features (DCC, CTCP, flood control, proxy)
- **`src/fe-common/`** - Shared logic across all frontends (printtext, window management, themes)
- **`src/fe-text/`** - Primary terminal UI with sidepanels, mouse gestures, and enhanced rendering
- **`src/fe-web/`** - WebSocket-based web interface with real-time IRC communication
- **`src/perl/`** - Embedded Perl scripting (100% irssi compatible)
- **`src/otr/`** - Off-The-Record encryption
- **`src/lib-config/`** - Configuration file parsing

### Critical Component: Sidepanel System

**Most sophisticated feature** with complete separation of concerns across five modules:

```
sidepanels-core.c (Coordinator)
    ├→ sidepanels-signals.c (Signal Handling)
    │   └─ IRC events: join/part/quit/nick/kick/mode
    ├→ sidepanels-activity.c (Activity Tracking)
    │   └─ Window priorities (0-4 levels)
    ├→ sidepanels-layout.c (Geometry Management)
    │   └─ Panel positioning, window numbering
    └→ sidepanels-render.c (Rendering & Optimization)
        └─ Batched redraw system for performance
```

**Redraw Optimization**: Three-level strategy instead of full screen refresh:
- `redraw_left_panels_only()` - Window list changes
- `redraw_right_panels_only()` - Nicklist changes
- `schedule_batched_redraw()` - Defers updates for mass events (400+ user channels)

**Files**: `src/fe-text/sidepanels-{core,signals,activity,layout,render,types}.{c,h}`

### Mouse Gesture System

Intuitive window navigation via left/right swipes in chat area:

```
gui-mouse.c (SGR Protocol Parser)
    ↓
gui-gestures.c (Gesture Recognition)
    ├─ Area Validator (only active in chat area)
    ├─ State Machine (tracking, timing, coordinates)
    └─ Classification (LEFT_SHORT, LEFT_LONG, RIGHT_SHORT, RIGHT_LONG)
```

**Settings**:
- `/set gesture_left_short "/window prev"` - Map gestures to commands
- `/set gesture_sensitivity 10` - Pixel threshold
- `/set gesture_timeout 1000` - Milliseconds

**Integration**: Hooks into `gui key pressed` signal at input level, returns TRUE when gesture detected to prevent normal input processing.

**Files**: `src/fe-text/gui-gestures.{c,h}`, `src/fe-text/gui-mouse.{c,h}`

### Credential Encryption System

**Four orthogonal storage modes** (config/external × plaintext/encrypted):

```
credential.c (Main API)
    ├─ credential_set/get/remove
    ├─ credential_set_master_password (runtime-only)
    └─ credential_migrate_to_external/config

credential-crypto.c (OpenSSL Implementation)
    ├─ AES-256-CBC encryption
    ├─ PBKDF2-HMAC-SHA256 (100K iterations)
    └─ 32-byte random salt per credential
```

**Protected Fields**: Server passwords, SASL credentials, proxy passwords, OTR passwords, autosendcmd (NickServ identify), fe_web_password

**Format**: `salt_hex:iv_hex:base64_ciphertext`

**Files**: `src/core/credential.{c,h}`, `src/core/credential-crypto.c`

### Web Interface (fe-web)

WebSocket-based interface (~5,350 lines total) with real-time bidirectional communication:

```
fe-web.c (Coordinator)
fe-web-server.c (TCP server, client accept)
fe-web-client.c (Per-client state, authentication)
fe-web-websocket.c (RFC 6455 protocol)
fe-web-signals.c (IRC event forwarding - 55KB)
fe-web-ssl.c (TLS/SSL support)
fe-web-crypto.c (AES-256-GCM message encryption)
fe-web-json.c (Serialization)
fe-web-netserver.c (Network management API)
```

**50 message types**: AUTH_OK, MESSAGE, SERVER_STATUS, CHANNEL_*, NICKLIST_*, WHOIS, STATE_DUMP, etc.

**Security**: WebSocket TLS/SSL (wss://), AES-256-GCM per-message encryption, credential protection for web password

**Files**: `src/fe-web/fe-web*.{c,h}` (15 files)

## Critical Patterns & Conventions

### Signal System (Central to All Subsystems)

Gtk-like signal system for component communication:
```c
signal_emit("message public", ...);
signal_add("window changed", callback);
signal_add_last("hilight", callback);
```

**Examples**:
- Sidepanels register for: `window changed`, `window created`, `channel joined`, `message join`, `message part`
- Fe-web registers for: `message public/private`, `message join/part/quit`, `nicklist changed`, `hilight`
- Gestures hook into: `gui key pressed` (at input level)

### Module Initialization Order

**CRITICAL** - Dependencies must be respected (from `src/fe-text/irssi.c`):

```c
// textui_finish_init() order:
gui_printtext_init();
gui_readline_init();
gui_entry_init();
mainwindows_init();
mainwindow_activity_init();
gui_windows_init();
gui_mouse_init();
gui_gestures_init();      // BEFORE sidepanels!
sidepanels_init();        // Depends on mouse system
```

Each module has `*_init()` and `*_deinit()` pairs. Deinit in reverse order.

### Settings System

All configuration through `settings_add_*()`:
```c
settings_add_bool_module("fe-text", "lookandfeel", "mouse_gestures", TRUE);
settings_add_int_module("fe-text", "lookandfeel", "gesture_sensitivity", 10);
settings_add_str_module("fe-text", "lookandfeel", "gesture_left_short", "/window prev");
```

### Data Flow Example: Message from Server

```
IRC Server
    ↓
src/irc/core/ (IRC protocol parsing)
    ↓
signal_emit("message public", ...)
    ↓
    ├→ sidepanels-signals.c (update window priority)
    ├→ fe-web-signals.c (send to web clients)
    └→ fe-common (theme rendering, printtext)
```

### Memory Management

- GLib allocators: `g_malloc`, `g_new`, `g_strdup`, `g_free`
- Hash tables: `g_hash_table_new`, `g_hash_table_insert`, `g_hash_table_destroy`
- Lists: `GSList` for ordered collections
- Proper cleanup essential - no memory leaks

### Threading & Async

- Single-threaded event loop with GMainLoop
- Async operations tracked via `pending_requests` hash table (fe-web)
- WHOIS assembly: Multi-line events collected in `active_whois` hash table
- Timers for batched operations: `schedule_batched_redraw()`

## Code Locations by Feature

| Feature | Primary Files |
|---------|--------------|
| **Sidepanel System** | `fe-text/sidepanels-{core,signals,activity,layout,render}.c` |
| **Mouse Gestures** | `fe-text/gui-gestures.c`, `fe-text/gui-mouse.c` |
| **Credential Encryption** | `core/credential.c`, `core/credential-crypto.c` |
| **Web Interface** | `fe-web/fe-web*.c` (all 15 files) |
| **Nick Display** | `fe-common/core/fe-expandos.c` |
| **Window Management** | `fe-text/mainwindows.c`, `fe-common/core/gui-windows.c` |
| **IRC Protocol** | `irc/core/channels.c`, `irc/core/irc-servers.c`, `irc/core/nicklist.c` |
| **Configuration** | `lib-config/iconfig.c`, `core/settings.c` |
| **Terminal Rendering** | `fe-text/term.c`, `fe-text/gui-printtext.c` |

## Feature Flags in Build

Default enabled features:
- `-Dwith-perl=yes` - Embedded Perl scripting (100% compatible with irssi)
- `-Dwith-otr=yes` - Off-The-Record encryption
- `-Ddisable-utf8proc=no` - Enhanced Unicode/emoji support (grapheme clusters)
- `-Dwith-fe-web=yes` - WebSocket web interface (auto-enabled)

Optional:
- `-Dwith-proxy=yes` - IRC proxy support
- `-Dwith-bot=yes` - Headless bot mode
- `-Dwith-capsicum=yes` - Capsicum sandboxing (FreeBSD)

## Configuration & Coexistence

**erssi vs irssi**:
- **erssi**: Uses `~/.erssi/` configuration
- **irssi**: Uses `~/.irssi/` configuration
- Both can coexist on same system
- `erssi-convert.sh` script transforms paths and branding

**Conversion**:
```bash
./erssi-convert.sh  # Converts standard irssi to erssi variant
```

## Testing Additions for New Features

When adding features touching core systems:

1. **Sidepanel changes**: Test with multiple servers, mass join/part events, kicked channels
2. **Mouse gestures**: Verify area validation (chat vs sidepanel), all gesture types
3. **Credential system**: Test all 4 modes (config/external × plain/encrypted), migration
4. **Web interface**: Test authentication, multi-client, signal forwarding, encryption

Example test locations:
- `tests/fe-text/` - Terminal UI tests
- `tests/irc/core/` - IRC protocol tests
- `tests/fe-common/core/` - Format/theme tests

## Important Implementation Notes

### When Modifying Sidepanels
- Changes to sorting/layout → `sidepanels-layout.c`
- Activity tracking → `sidepanels-activity.c`
- Signal handlers → `sidepanels-signals.c`
- Rendering/optimization → `sidepanels-render.c`
- Use targeted redraws (`redraw_left_panels_only()` etc.) not full refreshes
- Consider batching for performance with `schedule_batched_redraw()`

### When Modifying Gestures
- `gui-gestures.c` handles recognition logic
- `gui-mouse.c` handles SGR protocol parsing
- Must return TRUE from handler when gesture detected (consumes input)
- Test area validation - gestures should NOT trigger in sidepanels

### When Modifying Credentials
- Never persist master password to disk
- Use `credential_set()` API, not direct config writes
- Test all 4 storage modes after changes
- Verify migration paths between modes

### When Modifying Web Interface
- Add new message types to `WEB_MESSAGE_TYPE` enum in `fe-web.h`
- Signal handlers go in `fe-web-signals.c`
- Per-client state tracked in `WEB_CLIENT_REC` structure
- All messages must be JSON serializable via `fe-web-json.c`
- Test with multiple concurrent clients

## Common Development Scenarios

### Adding a New IRC Signal Handler (Sidepanels)
```c
// In sidepanels-signals.c:
static void sig_your_event(void *arg1, void *arg2) {
    // Handle event, update state
    redraw_left_panels_only(); // Or appropriate redraw
}

// In sidepanels_signals_init():
signal_add("your event", (SIGNAL_FUNC) sig_your_event);
```

### Adding a New Setting
```c
// In your_module_init():
settings_add_bool_module("fe-text", "category", "your_setting", FALSE);

// Reading the setting:
gboolean enabled = settings_get_bool("your_setting");
```

### Adding a New Web Message Type
```c
// 1. Add to WEB_MESSAGE_TYPE enum in fe-web.h
// 2. Add handler in fe-web-signals.c
// 3. Add JSON serialization in fe-web-json.c
// 4. Test with web client
```

## Debugging

### Debug Output
```bash
# Sidepanel redraw events (when enabled in code)
/set debug_sidepanel_redraws on
tail -f /tmp/irssi_sidepanels.log

# Build with debug symbols
meson configure Build -Dbuildtype=debug
ninja -C Build
```

### GDB Debugging
```bash
gdb Build/src/fe-text/irssi
(gdb) break sidepanels_core.c:123
(gdb) run
```

### Common Issues
- **Sidepanel rendering**: Check redraw call sites, verify batching logic
- **Mouse not working**: Verify SGR protocol enabled (`/set mouse on`)
- **Credentials not encrypting**: Check master password set, verify storage mode
- **Web interface**: Check SSL certs in `~/.erssi/certs/`, verify port not in use

## Release & Versioning Workflow

erssi uses **Semantic Versioning** (MAJOR.MINOR.PATCH) with **Conventional Commits** for automated changelog generation and releases.

### Version Management

- **Version defined in**: `meson.build` line 2: `version : '1.0.0'`
- **NEWS file**: First line parsed for build metadata by `utils/irssi-version.sh`
  - Format: `erssi-v1.0.0 2025-11-08  erssi-org team <...>`
  - Script searches for `^erssi-v` first, falls back to `^v` (irssi upstream compatibility)
- **CHANGELOG.md**: Auto-generated from git commits using `git-cliff` (Keep a Changelog format)
- **Git tags**: `vX.Y.Z` format triggers automated release workflow

### Conventional Commits

All commits must follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:

```
<type>[optional scope]: <description>

[optional body]

[optional footer]
```

**Types**: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`

**Examples**:
```bash
feat(sidepanels): add support for custom panel width
fix(credentials): prevent race condition in master password prompt
docs: update CLAUDE.md with release workflow
```

### Automated Release Process

1. **Update version** in `meson.build`
2. **Add NEWS entry** (optional, for build metadata)
3. **Commit**: `git commit -m "chore(release): prepare for v1.1.0"`
4. **Create tag**: `git tag -a v1.1.0 -m "Release v1.1.0"`
5. **Push**: `git push && git push --tags`
6. **GitHub Actions automatically**:
   - Configures build (meson + dependencies)
   - Creates tarballs: `meson dist -C Build --formats=gztar,xztar`
   - Generates SHA256 checksums
   - Generates changelog from commits (git-cliff)
   - Creates GitHub Release with artifacts

**Workflow file**: `.github/workflows/release.yml`

### CI/CD Validation

- **Commit validation**: `.github/workflows/commitlint.yml` - Validates commit messages in PRs
- **PR title validation**: `.github/workflows/pr-title.yml` - Ensures PR titles follow conventional commits
- **Build & test**: `.github/workflows/check.yml` - Existing CI for build validation

### For Maintainers

See **[RELEASE.md](RELEASE.md)** for detailed release instructions and troubleshooting.

### For Contributors

See **[CONTRIBUTING.md](CONTRIBUTING.md)** for commit message guidelines and PR submission process.

## Documentation

- **README.md** - User-facing feature documentation
- **CONTRIBUTING.md** - Contributor guidelines and conventional commits
- **RELEASE.md** - Release process for maintainers
- **CHANGELOG.md** - Auto-generated changelog (Keep a Changelog format)
- **docs/MOUSE-GESTURES-*.md** - Gesture system documentation
- **INSTALL-SCRIPT.md** - Installation guide
- **src/*/README** - Component-specific notes (if present)

## Compatibility

- **Perl Scripts**: 100% compatible with standard irssi scripts
- **Themes**: All irssi themes work without modification
- **Configuration**: Import existing irssi config via `erssi-convert.sh`
- **Coexistence**: erssi and irssi can run side-by-side (different config dirs)
