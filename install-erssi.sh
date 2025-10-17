#!/bin/bash

# erssi Installation Script
# Enhanced/Evolved IRC Client
# https://erssi.org

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Global variables
install_path=""
system=""
pkg_mgr=""

# Print functions
print_info() { echo -e "${BLUE}ℹ️  $1${NC}"; }
print_success() { echo -e "${GREEN}✅ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠️  $1${NC}"; }
print_error() { echo -e "${RED}❌ $1${NC}"; }

detect_system() {
   case "$(uname -s)" in
       Darwin*) system="macos" ;;
       Linux*) system="linux" ;;
       *) print_error "Unsupported system: $(uname -s)"; exit 1 ;;
   esac
}

detect_package_manager() {
   if [[ "$system" == "macos" ]]; then
       if command -v brew >/dev/null 2>&1; then
           pkg_mgr="brew"
       else
           print_error "Homebrew not found. Please install Homebrew first: https://brew.sh/"
           exit 1
       fi
   else
       if command -v apt-get >/dev/null 2>&1; then
           pkg_mgr="apt"
       elif command -v dnf >/dev/null 2>&1; then
           pkg_mgr="dnf"
       elif command -v pacman >/dev/null 2>&1; then
           pkg_mgr="pacman"
       else
           print_error "Unsupported package manager. Please install dependencies manually."
           exit 1
       fi
   fi
}

install_dependencies() {
   print_info "Installing dependencies for $system using $pkg_mgr..."

   case "$pkg_mgr" in
       "brew")
           brew install meson ninja pkg-config glib openssl@3 ncurses utf8proc libgcrypt libotr perl || {
               print_error "Failed to install dependencies with brew"
               exit 1
           }
           ;;
       "apt")
           sudo apt-get update || {
               print_error "Failed to update package list"
               exit 1
           }
           sudo apt-get install -y meson ninja-build build-essential pkg-config perl \
               libglib2.0-dev libssl-dev libncurses-dev libperl-dev libutf8proc-dev \
               libgcrypt20-dev libotr5-dev libattr1-dev || {
               print_error "Failed to install dependencies with apt"
               exit 1
           }
           ;;
       "dnf")
           sudo dnf install -y meson ninja-build gcc pkg-config perl \
               glib2-devel openssl-devel ncurses-devel perl-devel utf8proc-devel \
               libgcrypt-devel libotr-devel libattr-devel || {
               print_error "Failed to install dependencies with dnf"
               exit 1
           }
           ;;
       "pacman")
           sudo pacman -S --needed meson ninja gcc pkg-config perl \
               glib2 openssl ncurses utf8proc libgcrypt libotr || {
               print_error "Failed to install dependencies with pacman"
               exit 1
           }
           ;;
       *)
           print_error "Unknown package manager: $pkg_mgr"
           exit 1
           ;;
   esac

   print_success "Dependencies installed successfully"
}

check_existing_erssi() {
   if command -v erssi >/dev/null 2>&1; then
       local erssi_path=$(which erssi)
       print_warning "Existing erssi installation found at: $erssi_path"
       return 0
   else
       print_info "No existing erssi installation found"
       return 1
   fi
}

ask_dependencies_installation() {
   echo ""
   print_info "Choose dependencies installation:"
   echo "1) Install dependencies automatically (requires sudo for system packages)"
   echo "2) Skip dependency installation (continue with existing packages)"
   echo "3) Exit and install dependencies manually"
   echo ""
   print_warning "Option 2 requires that you have already installed all required dependencies"
   print_info "Required: meson, ninja, pkg-config, glib, openssl, ncurses, perl, utf8proc, libgcrypt, libotr"
   echo ""

   while true; do
       echo -e "${CYAN}Enter your choice (1, 2, or 3): \c"
       read -n 1 choice
       echo -e "\n"
       case "$choice" in
           1) 
               print_info "Installing dependencies..."
               install_dependencies
               break 
               ;;
           2) 
               print_warning "Skipping dependency installation - assuming packages are already installed"
               print_info "If build fails, install dependencies manually and try again"
               break 
               ;;
           3) 
               print_info "Installation cancelled. Install dependencies manually and run script again."
               print_info "See docs/INSTALL for complete dependency lists for your system."
               exit 0 
               ;;
           *) print_error "Please enter 1, 2, or 3" ;;
       esac
   done
}

yes_or_no() {
   while true; do
       echo -e "${CYAN}$* [y/n]? \c"
       read -n 1 REPLY
       echo -e "\n"
       case "$REPLY" in
           Y|y) return 0 ;;
           N|n) 
               print_warning "Operation cancelled by user"
               return 1 
               ;;
       esac
   done
}

ask_installation_location() {
   echo ""
   print_info "Choose installation location:"
   echo "1) Global installation to /opt/erssi (requires sudo)"
   echo "2) Local installation to ~/.local (user only)"
   echo ""

   if check_existing_erssi; then
       print_warning "WARNING: Existing erssi installation will be replaced!"
   fi

   while true; do
       echo -e "${CYAN}Enter your choice (1 or 2): \c"
       read -n 1 choice
       echo -e "\n"
       case "$choice" in
           1) install_path="/opt/erssi"; break ;;
           2) install_path="$HOME/.local"; break ;;
           *) print_error "Please enter 1 or 2" ;;
       esac
   done
}

setup_build_environment() {
   # Set up environment for macOS if needed
   if [[ "$system" == "macos" ]]; then
       if [[ -d "/opt/homebrew" ]]; then
           export PATH="/opt/homebrew/bin:$PATH"
           export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/ncurses/lib/pkgconfig:$PKG_CONFIG_PATH"
       elif [[ -d "/usr/local/Homebrew" ]]; then
           export PATH="/usr/local/bin:$PATH"
           export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/opt/ncurses/lib/pkgconfig:$PKG_CONFIG_PATH"
       fi
   fi
}

convert_to_erssi() {
   print_info "Converting irssi to erssi..."
   
   # Check if required files exist
   if [[ ! -f "meson.build" ]]; then
       print_error "meson.build not found for erssi conversion"
       exit 1
   fi
   
   if [[ ! -f "src/common.h" ]]; then
       print_error "src/common.h not found for erssi conversion"
       exit 1
   fi
   
   # 1. w meson.build: incdir = 'irssi' -> incdir = 'erssi'
   if [[ "$system" == "macos" ]]; then
       sed -i '' "s/incdir              = 'irssi'/incdir              = 'erssi'/" meson.build || {
           print_error "Failed to update meson.build"
           exit 1
       }
   else
       sed -i "s/incdir              = 'irssi'/incdir              = 'erssi'/" meson.build || {
           print_error "Failed to update meson.build"
           exit 1
       }
   fi

   # 2. w src/common.h: .irssi -> .erssi
   if [[ "$system" == "macos" ]]; then
       sed -i '' 's/\.irssi/\.erssi/g' src/common.h || {
           print_error "Failed to update src/common.h"
           exit 1
       }
   else
       sed -i 's/\.irssi/\.erssi/g' src/common.h || {
           print_error "Failed to update src/common.h"
           exit 1
       }
   fi

   # 3. GLOBALNA zmiana: #include <irssi/ -> #include <erssi/
   # Omijamy katalogi build*, .git i scripts
   if [[ "$system" == "macos" ]]; then
       export LC_ALL=C
       find . -type f -not -path "./build*/*" -not -path "./Build*/*" -not -path "./.git/*" -not -path "./scripts/*" | \
           xargs sed -i '' 's|<irssi/|<erssi/|g' || {
           print_error "Failed to update include paths"
           exit 1
       }
   else
       find . -type f -not -path "./build*/*" -not -path "./Build*/*" -not -path "./.git/*" -not -path "./scripts/*" | \
           xargs sed -i 's|<irssi/|<erssi/|g' || {
           print_error "Failed to update include paths"
           exit 1
       }
   fi

   # 4. GLOBALNA zmiana: ".irssi/" -> ".erssi/" (tylko z slashem)
   # Omijamy katalogi build*, .git i scripts
   if [[ "$system" == "macos" ]]; then
       export LC_ALL=C
       find . -type f -not -path "./build*/*" -not -path "./Build*/*" -not -path "./.git/*" -not -path "./scripts/*" | \
           xargs sed -i '' 's|\.irssi/|\.erssi/|g' || {
           print_error "Failed to update directory paths"
           exit 1
       }
   else
       find . -type f -not -path "./build*/*" -not -path "./Build*/*" -not -path "./.git/*" -not -path "./scripts/*" | \
           xargs sed -i 's|\.irssi/|\.erssi/|g' || {
           print_error "Failed to update directory paths"
           exit 1
       }
   fi

   # 5. w src/fe-text/meson.build: executable('irssi', -> executable('erssi',
   if [[ -f "src/fe-text/meson.build" ]]; then
       if [[ "$system" == "macos" ]]; then
           sed -i '' "s/executable('irssi',/executable('erssi',/" src/fe-text/meson.build || {
               print_error "Failed to update executable name"
               exit 1
           }
       else
           sed -i "s/executable('irssi',/executable('erssi',/" src/fe-text/meson.build || {
               print_error "Failed to update executable name"
               exit 1
           }
       fi
   else
       print_warning "src/fe-text/meson.build not found - skipping executable name change"
   fi

   print_success "Successfully converted to erssi - 5 changes made"
}

build_and_install() {
   print_info "Building erssi..."

   # Clean previous builds
   if [[ -d "Build" ]]; then
       rm -rf Build
   fi

   # Setup meson build
   local meson_args=(
       "--prefix=$install_path"
       "-Dwith-perl=yes"
       "-Dwith-otr=yes"
       "-Dwith-proxy=yes"
       "-Ddisable-utf8proc=no"
   )

   print_info "Setting up build with: meson setup Build ${meson_args[*]}"
   meson setup Build "${meson_args[@]}" || {
       print_error "Meson setup failed"
       exit 1
   }

   print_info "Building with ninja..."
   ninja -C Build || {
       print_error "Build failed"
       exit 1
   }

   print_info "Installing erssi to $install_path..."

   if [[ "$install_path" == "/opt/"* ]]; then
       sudo ninja -C Build install || {
           print_error "Installation failed"
           exit 1
       }
   else
       ninja -C Build install || {
           print_error "Installation failed"
           exit 1
       }
   fi

   print_success "erssi built and installed successfully"
}

create_symlinks() {
   local bin_path="$install_path/bin/erssi"

   if [[ ! -f "$bin_path" ]]; then
       print_error "Binary not found at $bin_path"
       return 1
   fi

   # For global install, create symlink in /usr/bin
   if [[ "$install_path" == "/opt/erssi" ]]; then
       print_info "Creating symlink in /usr/bin..."
       
       # Remove old symlink if exists
       if [[ -L "/usr/bin/erssi" ]]; then
           sudo rm -f /usr/bin/erssi
       fi
       
       sudo ln -sf "$bin_path" /usr/bin/erssi || {
           print_error "Failed to create symlink in /usr/bin"
           print_warning "You may need to manually add $install_path/bin to your PATH"
           return 1
       }
       
       print_success "Created symlink: /usr/bin/erssi -> $bin_path"
   else
       # For local install, create symlink in ~/.local/bin
       mkdir -p "$HOME/.local/bin" || {
           print_error "Failed to create $HOME/.local/bin"
           return 1
       }
       
       ln -sf "$bin_path" "$HOME/.local/bin/erssi" || {
           print_error "Failed to create symlink"
           return 1
       }
       
       print_success "Created symlink: $HOME/.local/bin/erssi -> $bin_path"

       # Add to PATH if not already there
       if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
           print_info "Add to your shell profile: export PATH=\"\$HOME/.local/bin:\$PATH\""
       fi
   fi
}

show_completion_message() {
   echo ""
   print_success "🎉 erssi installation completed!"
   echo ""
   print_info "Installation details:"
   echo "  • Application: erssi v1.0.0"
   echo "  • Location: $install_path"
   echo "  • Binary: $install_path/bin/erssi"
   
   if [[ "$install_path" == "/opt/erssi" ]]; then
       echo "  • Symlink: /usr/bin/erssi"
   else
       echo "  • Symlink: $HOME/.local/bin/erssi"
   fi
   
   echo ""
   print_info "erssi-specific features:"
   echo "  • Configuration directory: ~/.erssi/"
   echo "  • Secure credential management with AES-256 encryption"
   echo "  • Advanced sidepanels with mouse gesture support"
   echo "  • Full Unicode/emoji grapheme cluster support"
   echo "  • All RFC 2812 compliant channel types (#, &, !, +)"
   echo ""
   print_info "To run: erssi"
   print_info "Website: https://erssi.org"

   if [[ "$install_path" != "/opt/erssi" ]] && [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
       echo ""
       print_warning "Note: Add ~/.local/bin to your PATH to run erssi from anywhere:"
       echo "  echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.bashrc"
       echo "  source ~/.bashrc"
   fi
}

# Cleanup function
cleanup() {
   if [[ -d "Build" ]]; then
       print_info "Cleaning up temporary files..."
       rm -rf Build
   fi
}

# Setup trap for cleanup
trap cleanup EXIT INT TERM

main() {
   echo "🚀 erssi Installation Script"
   echo "====================================="
   echo "Enhanced/Evolved IRC Client v1.0.0"
   echo "https://erssi.org"
   echo ""

   # Detect system and package manager
   detect_system
   detect_package_manager

   print_info "Detected system: $system"
   print_info "Package manager: $pkg_mgr"

   # Check if we're in the right directory
   if [[ ! -f "meson.build" ]]; then
       print_error "meson.build not found. Please run this script from the erssi source directory."
       exit 1
   fi

   # Ask for confirmation
   if ! yes_or_no "Do you want to proceed with installation?"; then
       exit 1
   fi

   # Ask about dependencies installation
   ask_dependencies_installation

   # Setup build environment
   setup_build_environment

   # Convert to erssi
   convert_to_erssi

   # Ask installation location
   ask_installation_location

   # Build and install
   build_and_install

   # Create symlinks
   create_symlinks

   # Show completion message
   show_completion_message

   echo ""
   print_success "Installation complete! 🎉"
   print_info "Run 'erssi' to start the client"
}

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
   main "$@"
fi
