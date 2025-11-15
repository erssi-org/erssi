# irssi-fe-web

**WebSocket-based web frontend module for irssi IRC client**

This module provides a modern WebSocket interface with real-time bidirectional communication, allowing you to connect to your irssi instance from any web browser.

## Features

- 🔒 **Secure by default**: TLS/SSL (wss://) + AES-256-GCM encryption
- 🚀 **Real-time**: WebSocket (RFC 6455) with instant updates
- 📱 **Multi-client**: Multiple web clients can connect simultaneously
- 🎯 **Highlight detection**: Automatic detection of mentions
- 💬 **Full IRC support**: Messages, joins, parts, quits, kicks, modes, WHOIS, etc.
- ⚡ **ACTION messages**: Full /me support
- 🌐 **Network management**: Add/remove networks and servers via web interface
- 📊 **State synchronization**: Complete state dump on connect
- 🔔 **Activity tracking**: Unread markers and activity levels

## Requirements

### Build Dependencies
- **irssi >= 1.2** (headers required)
- **glib-2.0**
- **openssl >= 1.1**
- **pkg-config**
- **gcc** or **clang**

### Installation

#### Debian/Ubuntu
```bash
sudo apt install irssi-dev libglib2.0-dev libssl-dev pkg-config build-essential
```

#### Arch Linux
```bash
sudo pacman -S irssi glib2 openssl pkg-config base-devel
```

#### macOS (Homebrew)
```bash
brew install irssi glib openssl pkg-config
```

## Building

### Method 1: Makefile (Quick & Simple)

```bash
# Clone or download this repository
cd irssi-fe-web

# Build
make

# Install to ~/.irssi/modules
make install
```

### Method 2: Meson (Recommended for packaging)

```bash
# Configure
meson setup build

# Build
ninja -C build

# Install
ninja -C build install
```

### Build Configuration

If irssi headers are in a non-standard location:

```bash
export IRSSI_INCLUDE=/path/to/irssi/include
make
```

Or for Meson:

```bash
meson setup build -Dirssi_include=/path/to/irssi/include
```

## Usage

### 1. Load the module in irssi

```
/LOAD fe-web
```

### 2. Configure (optional)

```
/SET fe_web_enabled ON
/SET fe_web_port 9001
/SET fe_web_password your_secure_password_here
/SET fe_web_bind_address 127.0.0.1  # Localhost only (recommended)
```

### 3. Start the server

```
/SET fe_web_enabled ON
```

### 4. Check status

```
/FE_WEB STATUS
```

### 5. Connect from web browser

- Default URL: `wss://localhost:9001`
- Authenticate with your password
- Enjoy real-time IRC in your browser!

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `fe_web_enabled` | OFF | Enable/disable web server |
| `fe_web_port` | 9001 | WebSocket server port |
| `fe_web_password` | (none) | Authentication password (REQUIRED) |
| `fe_web_bind_address` | 0.0.0.0 | Bind address (use 127.0.0.1 for localhost only) |
| `fe_web_ssl_cert` | (auto) | Path to SSL certificate (auto-generated if not set) |
| `fe_web_ssl_key` | (auto) | Path to SSL private key (auto-generated if not set) |

## Security

### Encryption Layers

1. **TLS/SSL (wss://)**: WebSocket Secure connection
2. **AES-256-GCM**: Per-message encryption (always enabled)
3. **Password authentication**: Required for all connections

### Best Practices

- **Use strong password**: At least 16 characters, random
- **Bind to localhost**: Use `127.0.0.1` if accessing locally only
- **Firewall**: Block port 9001 from external access if not needed
- **SSL certificates**: Use proper certificates for production (not self-signed)

## Protocol

The module implements a JSON-based protocol over WebSocket with 50+ message types:

- `AUTH`, `AUTH_OK` - Authentication
- `MESSAGE` - Chat messages (with highlight detection)
- `SERVER_STATUS` - Server connection status
- `CHANNEL_JOIN`, `CHANNEL_PART`, `CHANNEL_KICK` - Channel events
- `NICKLIST`, `NICKLIST_UPDATE` - Nicklist management
- `WHOIS` - User information
- `ACTIVITY_UPDATE`, `MARK_READ` - Activity tracking
- `NETWORK_LIST`, `SERVER_LIST` - Configuration management
- And many more...

See `PROTOCOL.md` for full protocol specification.

## Message Types

### Supported IRC Events

- Public messages (with highlight detection)
- Private messages (queries)
- ACTION messages (/me)
- Joins, parts, quits
- Kicks, bans
- Nick changes (others and own)
- Topic changes
- Mode changes
- WHOIS responses
- Channel lists
- Away status

## Differences from erssi version

This is the **standard irssi** version. The erssi version includes additional features:

- **Sidepanel integration**: Activity markers and redraw optimization
- **Enhanced window management**: Priority tracking

Both versions share the same core functionality and protocol.

## Improvements over original

This version includes several bugfixes and improvements:

### Bugfixes
- ✅ **JSON unescape**: Proper handling of escaped characters (`\n`, `\"`, `\\`, `\uXXXX`)
- ✅ **TCP buffer**: Increased send buffer to 2MB for large state dumps
- ✅ **Error handling**: Improved connection error detection

### New Features
- ✅ **Highlight detection**: Automatic detection of mentions in messages
- ✅ **ACTION messages**: Full support for /me commands
- ✅ **Own nick changes**: Proper handling of user's own nick changes
- ✅ **Network management**: Add/remove networks and servers via web

### Improvements
- ✅ Cleaner logging (removed debug spam)
- ✅ Better ABI compatibility checking
- ✅ Optimized JSON serialization

## Troubleshooting

### Module won't load
```
Error loading module fe-web: fe-web is ABI version X but irssi is version Y
```

**Solution**: Rebuild the module against your irssi version:
```bash
make clean
make
make install
```

### Connection refused
```
WebSocket connection failed
```

**Checklist**:
1. Is `fe_web_enabled` set to ON?
2. Is the port correct? (`/SET fe_web_port`)
3. Is firewall blocking the port?
4. Are you using `wss://` (not `ws://`)?

### SSL certificate errors

For development, browsers may complain about self-signed certificates. Options:
1. Accept the certificate in browser
2. Generate proper certificates with Let's Encrypt
3. Use `mkcert` for local development certificates

### No messages appearing

1. Check if you're authenticated: `/FE_WEB STATUS`
2. Check if client is connected to the right server
3. Verify `wants_all_servers` flag in client configuration

## Development

### Project Structure

```
src/
├── fe-web.c              # Main module initialization
├── fe-web.h              # Public API and types
├── fe-web-client.c       # Client connection handling
├── fe-web-server.c       # WebSocket server
├── fe-web-signals.c      # IRC event handlers (55KB!)
├── fe-web-websocket.c    # WebSocket protocol (RFC 6455)
├── fe-web-ssl.c/h        # TLS/SSL support
├── fe-web-crypto.c/h     # AES-256-GCM encryption
├── fe-web-json.c         # JSON serialization
├── fe-web-netserver.c    # Network server API
├── fe-web-utils.c        # Utilities
└── module.h              # Module header
```

### Building with Debug Symbols

```bash
make CFLAGS="-g -O0 -DDEBUG"
```

### Running under GDB

```bash
gdb irssi
(gdb) run
# In irssi:
/LOAD fe-web
```

## Contributing

Contributions welcome! This module is maintained separately for standard irssi.

## License

GPL-2.0 (same as irssi)

## Credits

Original development by the irssi community.
Enhanced version with bugfixes and new features by the erssi team.

## Links

- **irssi**: https://irssi.org
- **erssi**: https://erssi.org
- **Original irssi repo**: https://github.com/irssi/irssi
- **This version**: https://github.com/kofany/irssi/tree/fe-web

---

**Made with ❤️ for the IRC community**
