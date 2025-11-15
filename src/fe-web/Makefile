# Makefile for irssi-fe-web module
# Standalone WebSocket frontend for irssi

# Configuration
MODULE_NAME = fe-web
MODULE_SO = lib$(MODULE_NAME).so

# Source files
SOURCES = fe-web.c \
          fe-web-client.c \
          fe-web-server.c \
          fe-web-signals.c \
          fe-web-websocket.c \
          fe-web-ssl.c \
          fe-web-crypto.c \
          fe-web-json.c \
          fe-web-netserver.c \
          fe-web-utils.c

OBJECTS = $(SOURCES:.c=.o)

# Installation paths
PREFIX ?= $(HOME)/.irssi
MODULE_DIR = $(PREFIX)/modules

# Compiler and tools
CC ?= gcc
PKG_CONFIG ?= pkg-config
INSTALL ?= install

# irssi include path detection
IRSSI_INCLUDE ?= $(shell $(PKG_CONFIG) --variable=includedir irssi 2>/dev/null || echo /usr/include/irssi)

# Check if IRSSI_INCLUDE exists, fallback to common locations
ifeq ($(wildcard $(IRSSI_INCLUDE)/src/common.h),)
    # Try standard locations
    IRSSI_INCLUDE := $(shell \
        for dir in /usr/include/irssi /usr/local/include/irssi \
                   /opt/homebrew/include/irssi /opt/local/include/irssi; do \
            test -f $$dir/src/common.h && echo $$dir && break; \
        done)
endif

# Verify IRSSI_INCLUDE
ifeq ($(wildcard $(IRSSI_INCLUDE)/src/common.h),)
    $(error Cannot find irssi headers. Please set IRSSI_INCLUDE=/path/to/irssi/include)
endif

# Compiler flags
CFLAGS += -fPIC -Wall -Wextra -std=gnu99
CFLAGS += -I$(IRSSI_INCLUDE)
CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 openssl)

# Linker flags
LDFLAGS += -shared
LIBS += $(shell $(PKG_CONFIG) --libs glib-2.0 openssl)

# Build info
$(info )
$(info ===== irssi-fe-web Build Configuration =====)
$(info Module: $(MODULE_SO))
$(info irssi headers: $(IRSSI_INCLUDE))
$(info Compiler: $(CC))
$(info ============================================)
$(info )

# Targets
.PHONY: all clean install uninstall help

all: $(MODULE_SO)

# Build shared library
$(MODULE_SO): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ Build successful: $@"

# Compile object files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Install module
install: $(MODULE_SO)
	@echo "Installing to $(MODULE_DIR)..."
	@mkdir -p $(MODULE_DIR)
	$(INSTALL) -m 644 $(MODULE_SO) $(MODULE_DIR)/
	@echo "✅ Installed: $(MODULE_DIR)/$(MODULE_SO)"
	@echo ""
	@echo "To load in irssi:"
	@echo "  /LOAD $(MODULE_NAME)"

# Uninstall module
uninstall:
	@echo "Uninstalling from $(MODULE_DIR)..."
	rm -f $(MODULE_DIR)/$(MODULE_SO)
	@echo "✅ Uninstalled"

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(MODULE_SO)
	@echo "✅ Cleaned"

# Help
help:
	@echo "irssi-fe-web Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make              - Build the module"
	@echo "  make install      - Install to ~/.irssi/modules (or $$PREFIX/modules)"
	@echo "  make uninstall    - Remove from ~/.irssi/modules"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Configuration:"
	@echo "  IRSSI_INCLUDE - Path to irssi headers (auto-detected)"
	@echo "  PREFIX        - Installation prefix (default: ~/.irssi)"
	@echo "  CC            - C compiler (default: gcc)"
	@echo ""
	@echo "Examples:"
	@echo "  make IRSSI_INCLUDE=/usr/local/include/irssi"
	@echo "  make PREFIX=/opt/irssi install"
	@echo "  make CFLAGS=\"-g -O0\" # Debug build"
