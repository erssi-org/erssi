# Erssi Codebase - Quick Reference Guide

## 1. IRC Message Flow (Lowest to Highest Level)

```
Raw Socket Data                    src/irc/core/irc.c:543
         ↓
    irc_parse_incoming()
         ↓
signal_server_incoming            Line 560 (2 args: server, raw_line)
         ↓
    irc_parse_incoming_line()     Line 528 (parse prefix)
         ↓
signal_server_event_tags          Line 537 (5 args: server, cmd, nick, addr, tags)
         ↓
    irc_server_event()            Line 365 (classify event)
         ↓
signal_emit("event PRIVMSG")      Line 389 (4 args: server, args, nick, addr)
         ↓
    [Module Interception Point]
    ├─→ irc/flood/flood.c (flood detection)
    ├─→ irc/dcc/dcc.c (file transfer)
    └─→ fe-common/irc/* (processing)
         ↓
signal_emit("message public")     (rendered output)
         ↓
    fe-text/gui-printtext.c       (display)
    or
    fe-web/fe-web-signals.c       (web broadcast)
```

**Key Signals by Event**:
- `PRIVMSG` → `signal_emit("event privmsg", ...)`
- `NOTICE` → `signal_emit("event notice", ...)`
- `JOIN` → `signal_emit("message join", ...)`
- `PART` → `signal_emit("message part", ...)`
- `KICK` → `signal_emit("message kick", ...)`

---

## 2. Module Registration Pattern

```c
// 1. Define per-server state (attached to each IRC_SERVER_REC)
typedef struct {
    GHashTable *tracking;           // Your data structure
} MODULE_SERVER_REC;

// 2. Initialize on server connect
static void mod_init_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *rec = g_new0(MODULE_SERVER_REC, 1);
    rec->tracking = g_hash_table_new(...);
    MODULE_DATA_SET(server, rec);   // Attach
}

// 3. Cleanup on server destroy
static void mod_deinit_server(IRC_SERVER_REC *server) {
    MODULE_SERVER_REC *rec = MODULE_DATA(server);
    if (rec) {
        g_hash_table_destroy(rec->tracking);
        g_free(rec);
    }
    MODULE_DATA_UNSET(server);
}

// 4. Module init (called once at startup)
void module_init(void) {
    settings_add_bool("mymod", "enabled", FALSE);
    signal_add_first("server connected", (SIGNAL_FUNC) mod_init_server);
    signal_add("server destroyed", (SIGNAL_FUNC) mod_deinit_server);
    signal_add("setup changed", (SIGNAL_FUNC) read_settings);
    module_register("mymod", "irc");
}

// 5. Module deinit (cleanup)
void module_deinit(void) {
    signal_remove("server connected", (SIGNAL_FUNC) mod_init_server);
    signal_remove("server destroyed", (SIGNAL_FUNC) mod_deinit_server);
    signal_remove("setup changed", (SIGNAL_FUNC) read_settings);
}
```

---

## 3. Essential Functions

### Parsing IRC Events
```c
char *params = event_get_params(data, 2, &target, &text);
// Parses IRC protocol format
// Returns allocated string (must g_free!)

char *next_param = event_get_param(&data);
// Get one parameter, advances pointer
```

### Server Checks
```c
IS_IRC_SERVER(server)                       // Type check
server_ischannel(SERVER(server), target)    // Is target a channel?
ignore_check(SERVER(server), nick, addr, target, text, level)  // Ignored?
```

### Signal System
```c
signal_add("event privmsg", (SIGNAL_FUNC) handler);
signal_add_first("server connected", ...)       // Run first
signal_add_last("message public", ...)          // Run last
signal_remove("event privmsg", (SIGNAL_FUNC) handler);
signal_emit("custom", 3, arg1, arg2, arg3);    // Emit custom signal
```

### Settings
```c
settings_add_int("module", "name", default);
settings_add_bool("module", "enabled", FALSE);
settings_add_str("module", "string", "default");

int val = settings_get_int("name");
gboolean b = settings_get_bool("enabled");
const char *s = settings_get_str("string");
```

### Memory Management
```c
g_new0(TYPE, count)                 // Allocate zeroed
g_strdup(str)                       // String copy
g_strdup_printf(fmt, ...)           // Printf-style copy
g_free(ptr)                         // Free any allocation
g_slist_append(list, item)
g_slist_free_full(list, g_free)     // Free list and items
g_hash_table_new(hash_fn, cmp_fn)
g_hash_table_lookup(table, key)
g_hash_table_insert(table, key, val)
g_hash_table_destroy(table)         // Calls destroy callbacks
```

---

## 4. Important Structures

### IRC_SERVER_REC
```c
IRC_SERVER_REC {
    char *tag;                      // Server name/tag
    char *nick;                     // Current nick
    char *address;                  // Server address
    int port;
    gboolean connected;
    GHashTable *module_data;        // Where MODULE_DATA is stored
    // ... many more fields
}
```

### WI_ITEM_REC (Window Item - Channel/Query)
```c
WI_ITEM_REC {
    int type;                       // WI_CHANNEL, WI_QUERY, etc.
    WINDOW_REC *window;
    void *parent;                   // IRC_SERVER_REC
    // Actual type in parent pointer is (IRC_CHANNEL_REC) or (QUERY_REC)
}
```

---

## 5. Configuration Example

### User Config (~/.erssi/config)
```
settings = {
  flood = {
    flood_timecheck = "8";          # seconds window
    flood_max_msgs = "4";           # messages in window
  };
  "anti-flood" = {
    anti_flood_enabled = "on";
    anti_flood_rate = "5";          # msg/sec
  };
};
```

### Module Changes
User can update with:
```
/set flood_timecheck 10
/set anti_flood_enabled on
```

Signal "setup changed" fires automatically.

---

## 6. Existing Reference Modules

### Flood Module (src/irc/flood/)
- 350 lines of production code
- Detects rapid messages from single user
- Emits "flood" signal (caught by autoignore.c)
- Reference: `/Users/k/dev/erssi/src/irc/flood/flood.c`

### Fe-Web Signals (src/fe-web/fe-web-signals.c)
- 55KB file
- 50+ signal handlers
- Multi-line event assembly (WHOIS example)
- Real-world pattern for complex modules

### Sidepanels (src/fe-text/sidepanels-*.c)
- 5 separate files
- Modular architecture example
- Optimization patterns (batched updates)

---

## 7. Testing Pattern

### Minimal Test
```bash
# Build
ninja -C Build

# Run test
./Build/tests/irc/core/test-irc

# Or run all tests
ninja -C Build test
```

### Add Test
```c
// tests/irc/mymod/test-mymod.c
#include <glib.h>
#include "tests.h"

static void test_basic(void) {
    IRC_SERVER_REC *server = create_test_server();
    // Test code
    g_assert(condition);
    server_destroy(server);
}

int main(void) {
    g_test_add_func("/mymod/basic", test_basic);
    return g_test_run();
}
```

---

## 8. File Locations by Task

| Task | File(s) |
|------|---------|
| Add setting | `src/core/settings.c` + your module |
| Register signal | `src/core/signals.c` (system), your handler |
| Handle PRIVMSG | `src/irc/core/irc.c:365` (classification), your handler |
| Handle JOIN | `src/irc/core/channel-events.c` + your handler |
| Parse IRC | `src/irc/core/irc.c:286-336` (event_get_params) |
| Window management | `src/fe-common/core/fe-windows.c` |
| Terminal display | `src/fe-text/gui-printtext.c` |
| WebSocket | `src/fe-web/fe-web-server.c` |

---

## 9. Signal Names Reference

### Server Events
- `server connected` - Server connection established
- `server disconnected` - Server disconnected
- `server destroyed` - Server object destroyed
- `server event` - Classified IRC event (internal)
- `server incoming` - Raw IRC line received

### Message Events
- `message public` - Public message (to channel)
- `message private` - Private message (to nick)
- `message join` - User joined channel
- `message part` - User left channel
- `message quit` - User quit
- `message kick` - User was kicked
- `message topic` - Topic changed
- `message nick` - Nick changed
- `message irc mode` - Mode changed

### IRC Protocol Events
- `event privmsg` - PRIVMSG command received
- `event notice` - NOTICE command received
- `event join` - JOIN command received
- `event part` - PART command received
- `ctcp msg` - CTCP message received
- `event 311` through `event 999` - Numeric replies

### Configuration Events
- `setup changed` - Any setting changed
- `setting_<name> changed` - Specific setting changed

### Module Events
- `module loaded` - Module loaded
- `module unloaded` - Module unloaded

---

## 10. Common Patterns

### Ignore Existing Ignores
```c
if (ignore_check(SERVER(server), nick, addr, target, text, level)) {
    return;  // User is ignored, skip processing
}
```

### Get All Servers
```c
for (tmp = servers; tmp != NULL; tmp = tmp->next) {
    IRC_SERVER_REC *server = tmp->data;
    if (!IS_IRC_SERVER(server)) continue;
    // Process server
}
```

### Get Channel Members
```c
IRC_CHANNEL_REC *channel = irc_channel_find(server, "#channel");
GSList *nicks = nicklist_getnicks(CHANNEL(channel));
for (nick_tmp = nicks; nick_tmp != NULL; nick_tmp = nick_tmp->next) {
    NICK_REC *nick = nick_tmp->data;
    // Process nick
}
g_slist_free(nicks);
```

### Timer (Cleanup)
```c
static int timeout_tag;

// Start timer
timeout_tag = g_timeout_add(5000, (GSourceFunc) timeout_func, NULL);

// In timeout_func, return 1 to continue, 0 to stop

// Stop timer
if (timeout_tag != -1) {
    g_source_remove(timeout_tag);
    timeout_tag = -1;
}
```

---

## 11. Module Directory Layout

```
src/irc/yourmodule/
├── yourmodule.c         # Main implementation
├── yourmodule.h         # Public header (minimal)
├── helper.c             # Optional helper
├── helper.h
├── meson.build          # Build config
└── module.h             # Auto-generated module metadata
```

### meson.build template
```python
sources = files(
  'yourmodule.c',
  'helper.c',
)

shared_module(
  'irc_yourmodule',
  sources,
  dependencies: irc_deps,
  name_prefix: '',
  install: true,
  install_dir: irc_module_dir,
)
```

---

## 12. Debugging Tips

### See Raw IRC
```
/raw :test!test@test.com PRIVMSG #test :hello
```

### Check Module Loaded
```
/module list | grep yourmodule
```

### Verify Settings
```
/set yourmod_setting
```

### Enable Debug Output
```c
// In your code
g_debug("Debug message: %s", value);
```

Run with:
```bash
G_MESSAGES_DEBUG=all erssi
```

### Use GDB
```bash
gdb ./Build/src/fe-text/irssi
(gdb) break yourmodule.c:123
(gdb) run
```

---

## 13. Critical Gotchas

1. **Signal Handler Signature**: Must match exactly
   - Wrong: `void handler(IRC_SERVER_REC *s)`
   - Right: `void handler(IRC_SERVER_REC *server, const char *data, const char *nick, const char *addr)`

2. **Memory Leaks**: Always match allocator
   - Wrong: `malloc()` + `g_free()`
   - Right: `g_malloc()` + `g_free()`

3. **Module Data Lost**: Attach in "server connected", retrieve with MODULE_DATA()
   - Wrong: `static` global per-server data
   - Right: `MODULE_DATA_SET()` and `MODULE_DATA()`

4. **Settings Not Changing**: Must listen to "setup changed"
   - Wrong: Read setting once in init
   - Right: Read in init + add "setup changed" handler

5. **Handlers Not Firing**: Check signal name spelling exactly
   - Wrong: `signal_add("event_privmsg", ...)`
   - Right: `signal_add("event privmsg", ...)`

---

**Last Updated**: January 13, 2026
**Version**: Erssi 1.2.6 (irssi 1.5 base)

See `/Users/k/dev/erssi/docs/ARCHITECTURE.md` for complete details.
