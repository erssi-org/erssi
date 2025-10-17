#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Global variables
app_name=""
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

check_existing_irssi() {
   if command -v irssi >/dev/null 2>&1; then
       local irssi_path=$(which irssi)
       print_warning "Existing irssi installation found at: $irssi_path"
       return 0
   else
       print_info "No existing irssi installation found"
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
               print_info "See INSTALL-SCRIPT.md for complete dependency lists for your system."
               exit 0 
               ;;
           *) print_error "Please enter 1, 2, or 3" ;;
       esac
   done
}

# Funkcja yes/no wzorowana na tahioN
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

ask_installation_type() {
   echo ""
   print_info "Choose installation type:"
   echo "1) Install as 'irssi' (replaces system irssi if exists)"
   echo "2) Install as 'erssi' (independent installation - Evolved irssi)"
   echo ""

   if check_existing_irssi; then
       print_warning "WARNING: Choosing option 1 will REPLACE your existing irssi installation!"
       print_warning "Your existing irssi configuration will be preserved, but the binary will be replaced."
   fi

   while true; do
       echo -e "${CYAN}Enter your choice (1 or 2): \c"
       read -n 1 choice
       echo -e "\n"
       case "$choice" in
           1) app_name="irssi"; break ;;
           2) app_name="erssi"; break ;;
           *) print_error "Please enter 1 or 2" ;;
       esac
   done
}

ask_installation_location() {
   echo ""
   print_info "Choose installation location:"
   echo "1) Global installation to /opt/$app_name (requires sudo)"
   echo "2) Local installation to ~/.local (user only)"
   echo ""

   while true; do
       echo -e "${CYAN}Enter your choice (1 or 2): \c"
       read -n 1 choice
       echo -e "\n"
       case "$choice" in
           1) install_path="/opt/$app_name"; break ;;
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
   
   # Sprawdź czy potrzebne pliki istnieją
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
       # Na macOS ustawienie LC_ALL=C rozwiązuje problemy z kodowaniem
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
       # Na macOS ustawienie LC_ALL=C rozwiązuje problemy z kodowaniem
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

   print_success "Successfully converted to erssi - exactly 5 changes made"
}

build_and_install() {
   print_info "Building $app_name..."

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

   print_info "Installing $app_name to $install_path..."

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

   print_success "$app_name built and installed successfully"
}

create_symlinks() {
   local bin_path="$install_path/bin/$app_name"

   if [[ ! -f "$bin_path" ]]; then
       print_error "Binary not found at $bin_path"
       return 1
   fi

   # Create symlink in user's local bin if it's a global install
   if [[ "$install_path" == "/opt/"* ]]; then
       mkdir -p "$HOME/.local/bin" || {
           print_error "Failed to create $HOME/.local/bin"
           return 1
       }
       
       ln -sf "$bin_path" "$HOME/.local/bin/$app_name" || {
           print_error "Failed to create symlink"
           return 1
       }
       
       print_success "Created symlink: $HOME/.local/bin/$app_name -> $bin_path"

       # Add to PATH if not already there
       if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
           print_info "Add to your shell profile: export PATH=\"\$HOME/.local/bin:\$PATH\""
       fi
   fi
}

show_completion_message() {
   echo ""
   print_success "🎉 $app_name installation completed!"
   echo ""
   print_info "Installation details:"
   echo "  • Application: $app_name"
   echo "  • Location: $install_path"
   echo "  • Binary: $install_path/bin/$app_name"
   echo ""

   if [[ "$app_name" == "erssi" ]]; then
       print_info "Erssi-specific features:"
       echo "  • Configuration directory: ~/.erssi/"
       echo "  • All irssi functionality with evolved features"
   fi

   print_info "To run: $app_name"

   if [[ "$install_path" == "/opt/"* ]] && [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
       echo ""
       print_warning "Note: Add ~/.local/bin to your PATH to run $app_name from anywhere:"
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
   echo "🚀 Advanced Irssi/Erssi Installation Script"
   echo "=========================================="

   # Detect system and package manager
   detect_system
   detect_package_manager

   print_info "Detected system: $system"
   print_info "Package manager: $pkg_mgr"

   # Check if we're in the right directory
   if [[ ! -f "meson.build" ]]; then
       print_error "meson.build not found. Please run this script from the irssi source directory."
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

   # Ask user preferences
   ask_installation_type
   ask_installation_location

   # Convert to erssi if needed
   if [[ "$app_name" == "erssi" ]]; then
       convert_to_erssi
   fi

   # Build and install
   build_and_install

   # Create symlinks if needed
   create_symlinks

   # Show completion message
   show_completion_message

   echo ""
   print_success "Installation complete! 🎉"
}

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
   main "$@"
fi