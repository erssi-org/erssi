# Advanced Irssi/Erssi Installation Script

This repository contains advanced installation scripts for building and installing irssi or erssi (evolved irssi) with full feature support.

## Features

- **Cross-platform support**: macOS and Linux
- **Automatic dependency detection and installation**
- **Two installation modes**:
  - Standard `irssi` (replaces system installation)
  - Independent `erssi` (evolved irssi with separate config)
- **Flexible installation locations**:
  - Global: `/opt/irssi` or `/opt/erssi`
  - Local: `~/.local`
- **Full feature support**: Perl, OTR, UTF8proc, SSL/TLS
- **KISS principle**: Simple, reliable, minimal complexity

## Quick Start

```bash
# Make scripts executable
chmod +x install-irssi.sh check-installation.sh

# Run installation
./install-irssi.sh

# Check installation status
./check-installation.sh
```

## Prerequisites

### macOS
- Xcode Command Line Tools: `xcode-select --install`
- Homebrew: https://brew.sh/

### Linux (Debian/Ubuntu)
- Build tools available via package manager
- sudo access for system package installation

### Linux (Other Distributions)
- Fedora/RHEL: `dnf` package manager
- Arch Linux: `pacman` package manager

## Installation Options

### 1. Application Type

**Option 1: Install as `irssi`**
- Replaces any existing system irssi installation
- Uses standard `~/.irssi/` configuration directory
- Binary name: `irssi`
- ⚠️ **Warning**: Will replace existing irssi installation

**Option 2: Install as `erssi` (Recommended for testing)**
- Independent installation alongside existing irssi
- Uses separate `~/.erssi/` configuration directory
- Binary name: `erssi`
- Includes all evolved features from erssi-convert.sh

### 2. Installation Location

**Global Installation (`/opt/app_name`)**
- Requires sudo for installation
- Available system-wide
- Automatic symlink creation to `~/.local/bin`
- Recommended for single-user systems

**Local Installation (`~/.local`)**
- No sudo required
- User-specific installation
- Requires `~/.local/bin` in PATH
- Recommended for shared systems

## Dependencies Installed

### macOS (via Homebrew)
```bash
brew install meson ninja pkg-config glib openssl@3 ncurses \
  utf8proc libgcrypt libotr perl
```

### Debian/Ubuntu
```bash
sudo apt-get install meson ninja-build build-essential pkg-config perl \
  libglib2.0-dev libssl-dev libncurses-dev libperl-dev libutf8proc-dev \
  libgcrypt20-dev libotr5-dev libattr1-dev
```

### Fedora/RHEL
```bash
sudo dnf install meson ninja-build gcc pkg-config perl \
  glib2-devel openssl-devel ncurses-devel perl-devel utf8proc-devel \
  libgcrypt-devel libotr-devel libattr-devel
```

### Arch Linux
```bash
sudo pacman -S meson ninja gcc pkg-config perl \
  glib2 openssl ncurses utf8proc libgcrypt libotr
```

## Build Configuration

The script automatically configures the build with:
- `--prefix=<chosen_location>`
- `-Dwith-perl=yes` (Perl scripting support)
- `-Dwith-otr=yes` (Off-The-Record messaging)
- `-Ddisable-utf8proc=no` (Enhanced UTF-8 support)

This ensures maximum feature compatibility and support.

## Usage Examples

### Standard Installation
```bash
./install-irssi.sh
# Choose: 1 (irssi), 1 (global /opt/irssi)
```

### Safe Testing Installation
```bash
./install-irssi.sh
# Choose: 2 (erssi), 2 (local ~/.local)
```

### Check Status
```bash
./check-installation.sh          # Full check
./check-installation.sh --quiet  # Minimal output
./check-installation.sh --verbose # Extra details
```

## Post-Installation

### Adding to PATH (if needed)
```bash
# Add to ~/.bashrc, ~/.zshrc, or equivalent
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### First Run
```bash
# For irssi
irssi

# For erssi
erssi
```

### Configuration
- **irssi**: Uses `~/.irssi/` directory
- **erssi**: Uses `~/.erssi/` directory (automatically created)

## Troubleshooting

### macOS Issues

**ncurses linking problems:**
```bash
export PKG_CONFIG_PATH="/opt/homebrew/opt/ncurses/lib/pkgconfig:$PKG_CONFIG_PATH"
```

**OpenSSL not found:**
```bash
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig:$PKG_CONFIG_PATH"
```

### Linux Issues

**Missing development packages:**
```bash
# Install development tools
sudo apt-get install build-essential  # Debian/Ubuntu
sudo dnf groupinstall "Development Tools"  # Fedora/RHEL
```

**Permission denied on global install:**
- Ensure you have sudo access
- Or choose local installation option

### General Issues

**Binary not found after installation:**
1. Check if `~/.local/bin` is in PATH
2. Run `./check-installation.sh` for diagnosis
3. Manual symlink: `ln -s /opt/app_name/bin/app_name ~/.local/bin/`

**Build failures:**
1. Ensure all dependencies are installed
2. Check `./check-installation.sh` output
3. Try cleaning build: `rm -rf Build` and retry

## Advanced Usage

### Manual Build (without script)
```bash
# For erssi
./erssi-convert.sh
meson setup Build --prefix=/opt/erssi -Dwith-perl=yes -Dwith-otr=yes
ninja -C Build
sudo ninja -C Build install

# For standard irssi
meson setup Build --prefix=/opt/irssi -Dwith-perl=yes -Dwith-otr=yes
ninja -C Build
sudo ninja -C Build install
```

### Environment Variables
```bash
# macOS Homebrew paths
export PATH="/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"

# Custom installation paths
export PATH="$HOME/.local/bin:$PATH"
```

## File Overview

- `install-irssi.sh`: Main installation script
- `check-installation.sh`: Status checker and diagnostics
- `erssi-convert.sh`: Converts irssi to erssi (used internally)
- `INSTALL-SCRIPT.md`: This documentation

## Contributing

When modifying the scripts, follow these principles:
- **KISS**: Keep It Simple, Stupid
- Test on both macOS and Linux
- Maintain backward compatibility
- Add appropriate error handling
- Update documentation

## License

Same license as the irssi project (GPL-2.0-or-later).