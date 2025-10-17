#!/bin/bash

set -e

detect_system() {
    case "$(uname -s)" in
        Darwin*) echo "macos" ;;
        Linux*) echo "linux" ;;
        *) echo "Unsupported system: $(uname -s)" >&2; exit 1 ;;
    esac
}

SYSTEM=$(detect_system)

echo "🚀 Convert Irssi to Erssi - exact changes from commit 7bb2ee9 + executable name"

# 1. w meson.build: incdir = 'irssi' -> incdir = 'erssi'
if [[ "$SYSTEM" == "macos" ]]; then
    sed -i '' "s/incdir              = 'irssi'/incdir              = 'erssi'/" meson.build
else
    sed -i "s/incdir              = 'irssi'/incdir              = 'erssi'/" meson.build
fi

# 2. w src/common.h: .irssi -> .erssi
if [[ "$SYSTEM" == "macos" ]]; then
    sed -i '' 's/\.irssi/\.erssi/g' src/common.h
else
    sed -i 's/\.irssi/\.erssi/g' src/common.h
fi

# 3. GLOBALNA zmiana: #include <irssi/ -> #include <erssi/
if [[ "$SYSTEM" == "macos" ]]; then
    find . -type f | xargs sed -i '' 's|<irssi/|<erssi/|g'
else
    find . -type f | xargs sed -i 's|<irssi/|<erssi/|g'
fi

# 4. GLOBALNA zmiana: ".irssi/" -> ".erssi/" (tylko z slashem)
if [[ "$SYSTEM" == "macos" ]]; then
    find . -type f | xargs sed -i '' 's|\.irssi/|\.erssi/|g'
else
    find . -type f | xargs sed -i 's|\.irssi/|\.erssi/|g'
fi

# 5. w src/fe-text/meson.build: executable('irssi', -> executable('erssi',
if [[ "$SYSTEM" == "macos" ]]; then
    sed -i '' "s/executable('irssi',/executable('erssi',/" src/fe-text/meson.build
else
    sed -i "s/executable('irssi',/executable('erssi',/" src/fe-text/meson.build
fi

echo "✅ Gotowe - dokładnie 5 zmian wykonanych"