# Erssi IRC Client - Architecture Overview & Anti-Flood Module Guide

## Executive Summary

Erssi is a modernized IRC client built on irssi's foundation with a clean three-layer architecture:
- **Core Layer**: IRC protocol handling (`src/core/`, `src/irc/`)
- **Frontend-Agnostic Layer**: Common UI logic (`src/fe-common/`)
- **Frontend Layers**: TUI (`src/fe-text/`), Web (`src/fe-web/`), Headless (`src/fe-none/`)

This document provides everything needed to understand and create an anti-flood module by examining existing patterns, particularly the `src/irc/flood/` module as a reference.

---

## 1. Overall Directory Structure

```
erssi/
├── src/
│   ├── core/                 # Core IRC client logic
│   │   ├── servers.c, nicklist.c, channels.c, queries.c
│   │   ├── settings.c        # Settings/configuration system
│   │   ├── signals.c         # Signal/hook system
│   │   ├── commands.c        # Command processing
│   │   ├── rawlog.c          # Raw IRC logging
│   │   ├── credential.c      # Credential encryption system
│   │   └── ...
│   ├── irc/                  # IRC-specific modules
│   │   ├── core/             # IRC protocol handling
│   │   │   ├── irc.c         # Lowest-level IRC parsing (signal emission point)
│   │   │   ├── irc-servers.c # Server management
│   │   │   ├── channel-events.c
│   │   │   ├── modes.c, ctcp.c
│   │   │   └── ...
│   │   ├── flood/            # Flood protection module (REFERENCE)
│   │   │   ├── flood.c
│   │   │   ├── autoignore.c
│   │   │   └── flood.h
│   │   ├── dcc/              # File transfer
│   │   ├── proxy/            # IRC proxy
│   │   └── notifylist/       # Notify list
│   ├── fe-common/            # Frontend-agnostic UI logic
│   │   ├── core/
│   │   │   ├── printtext.c   # Message display
│   │   │   ├── fe-windows.c  # Window management
│   │   │   ├── themes.c      # Theme system
│   │   │   ├── hilight-text.c # Highlighting
│   │   │   └── ...
│   │   └── irc/              # IRC-specific UI helpers
│   ├── fe-text/              # Terminal UI (TUI)
│   │   ├── irssi.c           # Main entry point & init order
│   │   ├── mainwindows.c     # Window management
│   │   ├── gui-printtext.c   # Text rendering
│   │   ├── gui-readline.c    # Input handling
│   │   ├── sidepanels-*.c    # Advanced sidepanel system
│   │   ├── gui-gestures.c    # Mouse gesture navigation
│   │   ├── gui-mouse.c       # Mouse protocol handling
│   │   ├── term.c            # Terminal control
│   │   └── ...
│   ├── fe-web/               # WebSocket web interface
│   │   ├── fe-web.c          # Coordinator
│   │   ├── fe-web-server.c   # TCP/WebSocket server
│   │   ├── fe-web-client.c   # Per-client state
│   │   ├── fe-web-signals.c  # IRC event forwarding (55KB)
│   │   ├── fe-web-crypto.c   # Message encryption
│   │   ├── fe-web-json.c     # JSON serialization
│   │   ├── fe-web-websocket.c # RFC 6455 protocol
│   │   ├── fe-web-ssl.c      # TLS/SSL support
│   │   └── fe-web.h          # Public API
│   ├── lib-config/           # Config file parsing
│   ├── perl/                 # Perl scripting
│   ├── otr/                  # Off-The-Record encryption
│   └── fe-none/              # Headless mode
├── tests/                    # Test suite
│   ├── irc/core/
│   ├── fe-text/
│   └── fe-common/core/
├── themes/                   # Color themes
├── docs/                     # Documentation
└── README.md, meson.build, CLAUDE.md

```

---

## 2. Message Flow: From Server to Module

Understanding this flow is **critical** for implementing anti-flood:

```
┌─────────────────────────────────────────────────────────────────┐
│                        IRC SERVER                                │
│                    (sends raw IRC line)                          │
└─────────────────────────────────────────────────────────────────┘
                              ↓
                 /Users/k/dev/erssi/src/irc/core/irc.c
                    Line 543: irc_parse_incoming()
                    (reads socket via net_sendbuffer)
                              ↓
                   signal_emit_id(signal_server_incoming, ...)
                   (signals that raw IRC line received)
                              ↓
                    Line 528: irc_parse_incoming_line()
                 (parses IRC prefix: @tags :nick!user@host)
                              ↓
              signal_emit_id(signal_server_event_tags, ...)
                  (signals with parsed components)
                              ↓
           Line 365: irc_server_event()
         (determines event type: "event privmsg", "event notice", etc.)
                              ↓
          Line 389: signal_emit(signal, 4, server, args, nick, address)
     (emits event-specific signal like "event privmsg", "event notice")
                              ↓
      ┌──────────────────────┬───────────────────────┬──────────────────┐
      ↓                      ↓                       ↓                  ↓
 flood module         CTCP handlers          fe-common/irc        fe-web signals
 (monitors)           (processes)          (processes display)   (web clients)
```

**Key Points**:
1. **Raw IRC parsing**: `src/irc/core/irc.c::irc_parse_incoming()` at line 543
2. **Signal emission point**: `signal_emit_id(signal_server_incoming, ...)` at line 560
3. **Event classification**: `irc_server_event()` at line 365 converts raw IRC to `event PRIVMSG`, etc.
4. **Module interception**: Modules register signal handlers to intercept at appropriate levels

---

## 3. Signal/Hook System - The Backbone

### How Signals Work

The signal system is **gtk-like** and central to all communication:

```c
// 1. Register a handler (usually in module init)
signal_add("event privmsg", (SIGNAL_FUNC) my_privmsg_handler);

// 2. Handler receives signal with parameters
static void my_privmsg_handler(IRC_SERVER_REC *server, const char *data,
                               const char *nick, const char *addr) {
    // Process message
}

// 3. Signal is emitted from event source
signal_emit("event privmsg", 4, server, args, nick, address);

// 4. Remove handler when done
signal_remove("event privmsg", (SIGNAL_FUNC) my_privmsg_handler);
```

### Key Signals for Anti-Flood Modules

**Lowest-Level IRC Signals** (closest to raw):
```c
// Fired for every line from server (most raw)
signal_emit_id(signal_server_incoming, 2, server, raw_line);

// Fired after basic parsing (prefix extracted)
signal_emit_id(signal_server_event_tags, 5, server, command, nick, addr, tags);

// Fired for event-specific handlers
signal_emit("event privmsg", 4, server, args, nick, address);
signal_emit("event notice", 4, server, args, nick, address);
signal_emit("ctcp msg", 5, server, data, nick, addr, target);
```

**Message-Level Signals** (higher abstraction):
```c
signal_emit("message public", 6, server, msg, nick, addr, target, ...);
signal_emit("message private", 6, server, msg, nick, addr, ...);
signal_emit("message join", ...);
signal_emit("message part", ...);
signal_emit("message quit", ...);
signal_emit("message kick", ...);
signal_emit("message topic", ...);
```

**Server Lifecycle Signals**:
```c
signal_emit("server connected", 1, server);
signal_emit("server disconnected", 1, server);
signal_emit("server destroyed", 1, server);
```

**Configuration Signals**:
```c
signal_emit("setup changed", 0);  // Any setting changed
```

**Module Loading Signals**:
```c
signal_emit("module loaded", 2, module_name, module_rec);
signal_emit("module unloaded", 2, module_name, module_rec);
```

### Signal Registration Order (Critical!)

From `src/fe-text/irssi.c::textui_finish_init()`:

```c
gui_printtext_init();
gui_readline_init();
gui_entry_init();
mainwindows_init();
mainwindow_activity_init();
gui_windows_init();
gui_mouse_init();
gui_gestures_init();       // BEFORE sidepanels!
sidepanels_init();         // Depends on mouse system
// ... later, in module loading
irc_flood_init();          // Flood module registers signals
```

**Important**: Use `signal_add_first()` for handlers that must run BEFORE default handlers, `signal_add_last()` for those that run AFTER.

---

## 4. Existing Flood Module - Reference Implementation

### File Structure

```
src/irc/flood/
├── flood.c          # Main flood detection logic (350 lines)
├── flood.h          # Public interface (7 lines)
├── autoignore.c     # Auto-ignore integration
├── autoignore.h
├── module.h         # Generated module metadata
└── meson.build      # Build configuration
```

### Core Data Structures (flood.c)

```c
typedef struct {
    char *nick;
    GSList *items;              // List of FLOOD_ITEM_REC
} FLOOD_REC;

typedef struct {
    char *target;               // Channel or query target
    int level;                  // MSGLEVEL_PUBLIC, MSGLEVEL_MSGS, etc.
    GSList *msgtimes;           // List of time_t pointers (timestamps)
} FLOOD_ITEM_REC;

// Per-server tracking (in MODULE_DATA(server)->floodlist)
// GHashTable: nick -> FLOOD_REC*
```

### Module Initialization Pattern (Critical)

```c
void irc_flood_init(void) {
    // 1. Register settings with defaults
    settings_add_int("flood", "flood_timecheck", 8);      // seconds
    settings_add_int("flood", "flood_max_msgs", 4);       // max messages

    // 2. Initialize module state
    flood_tag = -1;             // Timer tag
    read_settings();            // Load current settings

    // 3. Register for configuration changes
    signal_add("setup changed", (SIGNAL_FUNC) read_settings);

    // 4. Register for server lifecycle (with signal_add_first!)
    signal_add_first("server connected", (SIGNAL_FUNC) flood_init_server);
    signal_add("server destroyed", (SIGNAL_FUNC) flood_deinit_server);

    // 5. Register for message events (add AFTER setup)
    // These are added dynamically in read_settings() when enabled

    // 6. Call submodule initialization
    autoignore_init();

    // 7. Validate settings exist
    settings_check();

    // 8. Register module itself
    module_register("flood", "irc");
}

void irc_flood_deinit(void) {
    autoignore_deinit();

    if (flood_tag != -1) {
        g_source_remove(flood_tag);
        // Remove signal handlers
    }

    // Unregister all signals in reverse of registration
    signal_remove("setup changed", (SIGNAL_FUNC) read_settings);
    signal_remove("server connected", (SIGNAL_FUNC) flood_init_server);
    signal_remove("server destroyed", (SIGNAL_FUNC) flood_deinit_server);
}
```

### Event Handlers

```c
// Per-server initialization (called on "server connected" signal)
static void flood_init_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *rec;
    g_return_if_fail(server != NULL);
    if (!IS_IRC_SERVER(server)) return;

    rec = g_new0(MODULE_SERVER_REC, 1);
    rec->floodlist = g_hash_table_new(
        (GHashFunc) i_istr_hash,
        (GCompareFunc) i_istr_equal
    );
    MODULE_DATA_SET(server, rec);  // Attach to server
}

// Called on every PRIVMSG (via signal_add in read_settings)
static void flood_privmsg(IRC_SERVER_REC *server, const char *data,
                          const char *nick, const char *addr) {
    char *params, *target, *text;

    // Parse IRC parameters
    params = event_get_params(data, 2, &target, &text);

    // Check ignore list first
    level = server_ischannel(SERVER(server), target) ? 
        MSGLEVEL_PUBLIC : MSGLEVEL_MSGS;
    if (addr != NULL && !ignore_check(SERVER(server), nick, addr, target, text, level)) {
        flood_newmsg(server, level, nick, addr, target);  // Track message
    }

    g_free(params);
}

// Core flood detection logic
static void flood_newmsg(IRC_SERVER_REC *server, int level, const char *nick,
                         const char *host, const char *target) {
    MODULE_SERVER_REC *mserver = MODULE_DATA(server);
    FLOOD_REC *flood = g_hash_table_lookup(mserver->floodlist, nick);

    // Find or create tracking record for (nick, level, target)
    FLOOD_ITEM_REC *rec = flood == NULL ? NULL : 
        flood_find(flood, level, target);

    if (rec != NULL) {
        // Remove timestamps older than flood_timecheck
        for (times = rec->msgtimes; times != NULL; times = tnext) {
            if (*now - *((time_t *) times->data) >= flood_timecheck) {
                rec->msgtimes = g_slist_remove(rec->msgtimes, data);
                g_free(data);
            }
        }

        // Add current timestamp
        ttime = g_new(time_t, 1);
        *ttime = now;
        rec->msgtimes = g_slist_append(rec->msgtimes, ttime);

        // Check if exceeded limit
        if (g_slist_length(rec->msgtimes) > flood_max_msgs) {
            // FLOOD DETECTED! Emit signal
            signal_emit("flood", 5, server, nick, host,
                        GINT_TO_POINTER(rec->level), target);
        }
        return;
    }

    // First message from this nick/level/target - create tracking
    // (initialization code...)
}

// Dynamic handler registration
static void read_settings(void) {
    flood_timecheck = settings_get_int("flood_timecheck");
    flood_max_msgs = settings_get_int("flood_max_msgs");

    if (flood_timecheck > 0 && flood_max_msgs > 0) {
        if (flood_tag == -1) {
            // Enable flood checking
            flood_tag = g_timeout_add(5000, (GSourceFunc) flood_timeout, NULL);

            signal_add("event privmsg", (SIGNAL_FUNC) flood_privmsg);
            signal_add("event notice", (SIGNAL_FUNC) flood_notice);
            signal_add("ctcp msg", (SIGNAL_FUNC) flood_ctcp);
        }
    } else if (flood_tag != -1) {
        // Disable flood checking
        g_source_remove(flood_tag);
        flood_tag = -1;

        signal_remove("event privmsg", (SIGNAL_FUNC) flood_privmsg);
        signal_remove("event notice", (SIGNAL_FUNC) flood_notice);
        signal_remove("ctcp msg", (SIGNAL_FUNC) flood_ctcp);
    }
}

// Periodic cleanup (removes old entries every 5 seconds)
static int flood_timeout(void) {
    time_t now = time(NULL);
    for (tmp = servers; tmp != NULL; tmp = tmp->next) {
        IRC_SERVER_REC *rec = tmp->data;
        if (!IS_IRC_SERVER(rec)) continue;

        mserver = MODULE_DATA(rec);
        g_hash_table_foreach_remove(mserver->floodlist,
                                    (GHRFunc) flood_hash_check_remove,
                                    &now);
    }
    return 1;  // Keep timer running
}
```

### Emit Signal on Flood Detection

When flood detected at line 215:
```c
signal_emit("flood", 5, server, nick, host,
            GINT_TO_POINTER(rec->level), target);
```

This signal is caught by `autoignore` module which automatically ignores the flooder.

---

## 5. Configuration System

### How Settings Work

**Definition** (usually in module init):
```c
settings_add_int("flood", "flood_timecheck", 8);
settings_add_int("flood", "flood_max_msgs", 4);
settings_add_bool("fe-text", "mouse_gestures", TRUE);
settings_add_str("fe-text", "gesture_left_short", "/window prev");
settings_add_time("flood", "autoignore_time", "5min");
settings_add_level("flood", "autoignore_level", "");
```

**Reading**:
```c
int timecheck = settings_get_int("flood_timecheck");
int max_msgs = settings_get_int("flood_max_msgs");
const char *str = settings_get_str("some_setting");
gboolean enabled = settings_get_bool("some_bool");
int time_ms = settings_get_time("some_time");
int level_bits = settings_get_level("some_level");
```

**User Configuration** (in `~/.erssi/config`):
```
settings = {
  flood = {
    flood_timecheck = "10";
    flood_max_msgs = "5";
  };
  fe-text = {
    mouse_gestures = "on";
  };
};
```

**Listen for Changes**:
```c
signal_add("setup changed", (SIGNAL_FUNC) on_settings_changed);
signal_add("setting_changed", (SIGNAL_FUNC) on_single_setting_changed);
```

**Key Insight**: Settings are **file-backed** (loaded from config), but modules can listen for changes via signals.

---

## 6. Per-Server Module Data

### Module Data Pattern

Most modules need to store per-server state:

```c
// Define per-server structure
typedef struct {
    GHashTable *floodlist;      // Nick -> FLOOD_REC*
    // ... other per-server state
} MODULE_SERVER_REC;

// Attach to server on "server connected"
static void flood_init_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *rec = g_new0(MODULE_SERVER_REC, 1);
    rec->floodlist = g_hash_table_new(...);
    MODULE_DATA_SET(server, rec);   // Macro that attaches to server->module_data
}

// Access anywhere
MODULE_SERVER_REC *mserver = MODULE_DATA(server);  // Macro that retrieves

// Clean up on "server destroyed"
static void flood_deinit_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *mserver = MODULE_DATA(server);
    if (mserver != NULL) {
        g_hash_table_destroy(mserver->floodlist);
        g_free(mserver);
    }
    MODULE_DATA_UNSET(server);
}
```

The `MODULE_DATA()` macro system allows each module to attach opaque data to server records without conflicting with other modules.

---

## 7. Memory Management Patterns

Erssi uses **GLib memory management**:

```c
// Allocation
char *str = g_strdup("hello");
char *new = g_strdup_printf("format: %s", str);
STRUCT *rec = g_new0(STRUCT, 1);
GSList *list = g_slist_append(list, item);
GHashTable *hash = g_hash_table_new(...);

// Freeing
g_free(str);
g_free(new);
g_free(rec);
g_slist_free(list);
g_slist_foreach(list, (GFunc) g_free, NULL);
g_slist_free_full(list, g_free);  // Free list and all items
g_hash_table_destroy(hash);        // Includes destroy callbacks
g_hash_table_remove(hash, key);    // Calls destroy callback
```

**Critical**: Always use `g_free()` for allocations made with `g_malloc()/g_strdup()/g_new()`. Mismatches cause crashes.

---

## 8. How Fe-Web Differs (Advanced Pattern)

Fe-web shows an alternative pattern for more complex modules with multiple frontends:

### Modular Signal Handling

```c
// fe-web-signals.c: 55KB file with 50+ signal handlers

void fe_web_signals_init(void) {
    // Message signals
    signal_add("message public", (SIGNAL_FUNC) sig_message_public);
    signal_add("message private", (SIGNAL_FUNC) sig_message_private);
    signal_add("message join", (SIGNAL_FUNC) sig_message_join);
    signal_add("message part", (SIGNAL_FUNC) sig_message_part);
    // ... many more

    // WHOIS events (multi-line)
    signal_add_last("event 311", (SIGNAL_FUNC) event_whois);
    signal_add_last("event 312", (SIGNAL_FUNC) event_whois_server);
    signal_add_last("event 318", (SIGNAL_FUNC) event_end_of_whois);

    // Server events
    signal_add("server connected", (SIGNAL_FUNC) sig_server_connected);
    signal_add("server disconnected", (SIGNAL_FUNC) sig_server_disconnected);

    // Activity tracking
    signal_add("window hilight", (SIGNAL_FUNC) sig_window_hilight);
    signal_add("window activity", (SIGNAL_FUNC) sig_window_activity);
}
```

### Multi-Line Event Assembly

Fe-web uses a hash table to assemble multi-line IRC responses (like WHOIS):

```c
// Hash table to track in-progress WHOIS requests
static GHashTable *active_whois = NULL;  // Key: "servername:nick"

// Called on "event 311" (start of WHOIS reply)
static void event_whois(IRC_SERVER_REC *server, const char *data) {
    char *nick = event_get_param(&data);
    WHOIS_REC *whois = whois_get_or_create(server, nick);
    // Extract fields from data
    whois->user = g_strdup(user);
    whois->host = g_strdup(host);
    // ... etc
}

// Called on "event 312" (WHOIS server info)
static void event_whois_server(IRC_SERVER_REC *server, const char *data) {
    char *nick = event_get_param(&data);
    WHOIS_REC *whois = whois_get_or_create(server, nick);  // Get existing
    whois->server = g_strdup(server_name);
}

// Called on "event 318" (end of WHOIS)
static void event_end_of_whois(IRC_SERVER_REC *server, const char *data) {
    char *nick = event_get_param(&data);
    WHOIS_REC *whois = g_hash_table_lookup(active_whois, key);
    
    // Now send complete WHOIS message to web clients
    WEB_MESSAGE_REC *msg = fe_web_message_new(WEB_MSG_WHOIS);
    // ... populate msg with complete WHOIS data ...
    fe_web_send_to_server_clients(server, msg);
    
    // Clean up
    g_hash_table_remove(active_whois, key);
}
```

---

## 9. Building and Testing

### Build Commands

```bash
# Initial configuration
meson setup Build --prefix=/opt/erssi -Dwith-perl=yes -Dwith-otr=yes

# Compile
ninja -C Build

# Run tests
ninja -C Build test

# Run specific test
./Build/tests/irc/core/test-irc
```

### Adding Tests

Tests go in `tests/irc/flood/` (create if needed):

```c
// test-flood.c
#include <glib.h>
#include "tests.h"
#include "src/irc/flood/flood.h"

static void test_flood_detection(void) {
    // Setup IRC server mock
    IRC_SERVER_REC *server = create_test_server();
    
    // Initialize flood module
    irc_flood_init();
    
    // Simulate flood
    for (int i = 0; i < 5; i++) {
        flood_privmsg(server, "test", "attacker", "attacker@host.com");
    }
    
    // Verify flood signal was emitted
    g_assert(flood_signal_emitted);
    
    // Cleanup
    irc_flood_deinit();
    server_destroy(server);
}
```

---

## 10. Architecture Diagram - Complete Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Application Startup                          │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
                      src/fe-text/irssi.c
                      irssi_main() → main_loop
                              ↓
         core_preinit() → modules loaded → core_init()
                              ↓
       ┌──────────────────────┬──────────────────────┐
       ↓                      ↓                      ↓
   IRC Core Module        fe-common Module      fe-web Module
   (irc_irc_init)         (fe_common_init)      (fe_web_init)
       ↓                      ↓                      ↓
   Flood Module          Sidepanels Module      Web Signals
   (irc_flood_init)      (sidepanels_init)      (fe_web_signals_init)
       ↓                      ↓                      ↓
   Register signals:     Register signals:      Register signals:
   - event privmsg       - window changed       - message public
   - event notice        - channel joined       - message private
   - ctcp msg            - activity changed     - server connected
   - setup changed       - hilight              - whois events
   - server connected    - window destroyed     - ... 50+ more
   - server destroyed
       ↓                      ↓                      ↓
       └──────────────────────┬──────────────────────┘
                              ↓
                    signal_emit("module loaded", ...)
                              ↓
                    Main Event Loop Starts
                    (GMainLoop with event-driven)
                              ↓
   ┌──────────────────────────┼──────────────────────────┐
   ↓                          ↓                         ↓
Server Input             User Input              Timers/Idle
(net_sendbuffer)         (stdin)                 (GSource)
   ↓                          ↓                         ↓
IRC Line from            /command or                 Timeout
Server                   text input                  events
   ↓                          ↓                         ↓
rawlog_input()           process_command()      flood_timeout()
   ↓                          ↓                         ↓
signal_server_incoming   signal_command()       Cleanup old
   ↓                          ↓                    entries
Parse & Extract          Execute handler
Prefix/Tags                    ↓
   ↓                      Display output
signal_server_event_tags
   ↓
Determine Event Type
(PRIVMSG, NOTICE, etc.)
   ↓
signal_emit("event PRIVMSG", ...)
   ↓
   ├→ FLOOD MODULE catches it
   │  ├→ flood_privmsg() called
   │  ├→ Track message timestamps
   │  ├→ Detect flooding
   │  └→ signal_emit("flood", ...)
   │
   ├→ FE-COMMON processes message
   │  ├→ Printtext module renders
   │  ├→ signal_emit("message public", ...)
   │  └→ Display in window
   │
   └→ FE-WEB signals module catches
      ├→ sig_message_public() called
      ├→ Create WEB_MESSAGE_REC
      ├→ Send to web clients via WebSocket
      └→ Update activity tracking
```

---

## 11. Creating a New Anti-Flood Module

### Step 1: Module Structure

```
src/irc/anti-flood/
├── anti-flood.c         # Main module (similar to flood.c)
├── anti-flood.h         # Public interface (minimal)
├── throttle.c           # Optional: message rate limiter
├── throttle.h
├── module.h             # Generated by meson
├── meson.build          # Build config
└── README               # Documentation
```

### Step 2: Header File (anti-flood.h)

```c
#ifndef IRSSI_IRC_ANTI_FLOOD_ANTI_FLOOD_H
#define IRSSI_IRC_ANTI_FLOOD_ANTI_FLOOD_H

void irc_anti_flood_init(void);
void irc_anti_flood_deinit(void);

#endif
```

### Step 3: Module Implementation (anti-flood.c)

```c
#include "module.h"
#include <erssi/src/core/modules.h>
#include <erssi/src/core/signals.h>
#include <erssi/src/core/levels.h>
#include <erssi/src/core/settings.h>
#include <erssi/src/core/misc.h>
#include <erssi/src/irc/core/irc.h>
#include <erssi/src/irc/core/irc-servers.h>
#include <erssi/src/core/ignore.h>

// Per-server state
typedef struct {
    GHashTable *users;          // nick -> USER_STATS_REC*
    // Additional per-server data
} MODULE_SERVER_REC;

// Per-user statistics
typedef struct {
    char *nick;
    time_t first_message;       // First message timestamp
    int message_count;          // Messages in time window
    int kick_count;             // Times kicked
    int channels;               // Number of channels joined
    // Additional stats
} USER_STATS_REC;

static int anti_flood_enabled;
static int rate_limit;          // messages per second
static int burst_limit;         // max messages in burst

// CRITICAL: Use signal_add_first to run BEFORE default handlers
static void anti_flood_privmsg(IRC_SERVER_REC *server, const char *data,
                                const char *nick, const char *addr) {
    MODULE_SERVER_REC *mserver = MODULE_DATA(server);
    USER_STATS_REC *stats;
    char *params, *target, *text;
    time_t now = time(NULL);

    g_return_if_fail(server != NULL);
    g_return_if_fail(nick != NULL);

    if (!anti_flood_enabled || mserver == NULL)
        return;

    // Parse message
    params = event_get_params(data, 2, &target, &text);

    // Skip if ignored
    if (addr != NULL && ignore_check(SERVER(server), nick, addr, target, text, MSGLEVEL_PUBLIC)) {
        g_free(params);
        return;
    }

    // Get or create user stats
    stats = g_hash_table_lookup(mserver->users, nick);
    if (stats == NULL) {
        stats = g_new0(USER_STATS_REC, 1);
        stats->nick = g_strdup(nick);
        stats->first_message = now;
        g_hash_table_insert(mserver->users, stats->nick, stats);
    }

    // Update statistics
    if (now - stats->first_message > RATE_WINDOW) {
        // Reset window
        stats->first_message = now;
        stats->message_count = 1;
    } else {
        stats->message_count++;
        
        // Check rate limit
        if (stats->message_count > rate_limit) {
            // ACTION: Log, block, or ignore
            signal_emit("anti-flood", 5, server, nick, addr, 
                        GINT_TO_POINTER(MSGLEVEL_PUBLIC), target);
        }
    }

    g_free(params);
}

static void anti_flood_init_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *rec;

    g_return_if_fail(server != NULL);
    if (!IS_IRC_SERVER(server)) return;

    rec = g_new0(MODULE_SERVER_REC, 1);
    rec->users = g_hash_table_new((GHashFunc) i_istr_hash,
                                   (GCompareFunc) i_istr_equal);
    MODULE_DATA_SET(server, rec);
}

static void anti_flood_deinit_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *mserver;

    g_return_if_fail(server != NULL);
    if (!IS_IRC_SERVER(server)) return;

    mserver = MODULE_DATA(server);
    if (mserver != NULL && mserver->users != NULL) {
        // Clean up user stats
        g_hash_table_destroy(mserver->users);
        g_free(mserver);
    }
    MODULE_DATA_UNSET(server);
}

static void read_settings(void) {
    anti_flood_enabled = settings_get_bool("anti_flood_enabled");
    rate_limit = settings_get_int("anti_flood_rate");
    burst_limit = settings_get_int("anti_flood_burst");

    if (anti_flood_enabled) {
        // Enable monitoring
    } else {
        // Disable monitoring
    }
}

void irc_anti_flood_init(void) {
    settings_add_bool("anti-flood", "anti_flood_enabled", FALSE);
    settings_add_int("anti-flood", "anti_flood_rate", 5);        // msg/sec
    settings_add_int("anti-flood", "anti_flood_burst", 10);      // burst msgs

    anti_flood_enabled = FALSE;
    read_settings();

    signal_add("setup changed", (SIGNAL_FUNC) read_settings);
    signal_add_first("server connected", (SIGNAL_FUNC) anti_flood_init_server);
    signal_add("server destroyed", (SIGNAL_FUNC) anti_flood_deinit_server);
    
    // Add message handlers dynamically when enabled
    if (anti_flood_enabled) {
        signal_add_first("event privmsg", (SIGNAL_FUNC) anti_flood_privmsg);
    }

    settings_check();
    module_register("anti-flood", "irc");
}

void irc_anti_flood_deinit(void) {
    signal_remove("event privmsg", (SIGNAL_FUNC) anti_flood_privmsg);
    signal_remove("setup changed", (SIGNAL_FUNC) read_settings);
    signal_remove("server connected", (SIGNAL_FUNC) anti_flood_init_server);
    signal_remove("server destroyed", (SIGNAL_FUNC) anti_flood_deinit_server);
}

MODULE_ABICHECK(irc_anti_flood)
```

### Step 4: Register in Build System

Add to `src/irc/meson.build`:
```python
subdir('anti-flood')
```

Create `src/irc/anti-flood/meson.build`:
```python
sources = files(
  'anti-flood.c',
)

fuzz_sources = files()

shared_module(
  'irc_anti_flood',
  sources,
  dependencies: irc_deps,
  name_prefix: '',
  install: true,
  install_dir: irc_module_dir,
)
```

### Step 5: Configuration

Users enable in `~/.erssi/config`:
```
settings = {
  "anti-flood" = {
    anti_flood_enabled = "on";
    anti_flood_rate = "5";
    anti_flood_burst = "10";
  };
};
```

Or via command:
```
/set anti_flood_enabled on
/set anti_flood_rate 5
```

---

## 12. Key Takeaways for Anti-Flood Development

1. **Early Interception**: Use `signal_add_first()` for anti-flood to catch events BEFORE normal processing
2. **Per-Server State**: Attach MODULE_DATA to each server for independent tracking
3. **Settings Integration**: Define all parameters as settings for user control
4. **Signal Patterns**: Register for setup changes to enable/disable dynamically
5. **Cleanup**: Ensure all server data freed in "server destroyed" handler
6. **Timers**: Use `g_timeout_add()` for periodic cleanup/timeout operations
7. **Memory**: Always use GLib allocation functions and match with g_free
8. **Testing**: Write tests in `tests/irc/anti-flood/` for validation
9. **Documentation**: Update CLAUDE.md with new module patterns

---

## Useful Files to Reference

| Task | File |
|------|------|
| Understand signal flow | `/Users/k/dev/erssi/src/irc/core/irc.c` (lines 365-393) |
| Learn flood module | `/Users/k/dev/erssi/src/irc/flood/flood.c` |
| See settings system | `/Users/k/dev/erssi/src/core/settings.c` (lines 52-150) |
| Check fe-web pattern | `/Users/k/dev/erssi/src/fe-web/fe-web.h` (types) |
| Web signal handlers | `/Users/k/dev/erssi/src/fe-web/fe-web-signals.c` (1519+) |
| Module init order | `/Users/k/dev/erssi/src/fe-text/irssi.c` (lines 240+) |
| IRC event names | `/Users/k/dev/erssi/src/core/module.h` (event list) |

---

## Common Issues & Debugging

### Issue: Signals not firing
**Cause**: Wrong signal name, handler not registered, or registration timing
**Solution**: Check signal names in `src/core/module.h`, ensure `signal_add()` called during init

### Issue: Module data lost on server switch
**Cause**: Not using MODULE_DATA() macro pattern
**Solution**: Always attach per-server state via MODULE_DATA_SET in "server connected" handler

### Issue: Memory leaks
**Cause**: Using wrong allocator (malloc vs g_malloc) or forgetting g_free
**Solution**: Always match g_malloc/g_strdup/g_new with g_free, use g_hash_table_destroy

### Issue: Settings changes not reflected
**Cause**: Not listening to "setup changed" signal
**Solution**: Register `signal_add("setup changed", callback)` to handle dynamic settings

### Issue: Handlers run in wrong order
**Cause**: Using `signal_add()` instead of `signal_add_first()` or `signal_add_last()`
**Solution**: Use appropriate registration order for your module's position in chain

---

## Performance Considerations

1. **Avoid full screen redraws**: Use granular redraws (sidepanels example)
2. **Batch updates**: For multi-line events, collect then emit once
3. **Hash tables for lookups**: O(1) average vs O(n) with lists
4. **Timeout cleanup**: Don't let data structures grow unbounded
5. **Early exit**: Check conditions early to avoid unnecessary processing

---

**Document Last Updated**: November 3, 2025
**Erssi Version**: 1.0.0 (based on irssi 1.5)
**Build System**: Meson + Ninja
