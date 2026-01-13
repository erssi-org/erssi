# Erssi Architecture Documentation

This directory contains comprehensive documentation about the erssi IRC client codebase architecture, designed to help developers understand and extend the system, particularly for creating new modules like anti-flood protection.

## Documentation Files

### 1. ARCHITECTURE.md (35KB, 1,027 lines)

**Complete architectural guide covering:**

1. **Overall Directory Structure** - Full project layout with 3-layer architecture
2. **Message Flow** - Step-by-step IRC message handling from socket to display (6 stages)
3. **Signal/Hook System** - Gtk-like signal system (backbone of erssi)
4. **Existing Flood Module** - Reference 350-line implementation analyzed in detail
5. **Configuration System** - How /set commands and settings work
6. **Per-Server Module Data** - MODULE_DATA pattern for independent per-server state
7. **Memory Management** - GLib allocation patterns
8. **Fe-Web Advanced Pattern** - 55KB, 50+ signal handler example for complex modules
9. **Building and Testing** - meson/ninja commands and test patterns
10. **Complete Architecture Diagram** - Full application flow with event loop
11. **Creating Anti-Flood Module** - 5-step template with full code examples
12. **Key Takeaways & Debugging** - Best practices and common issues

**Best for:** In-depth understanding, implementation reference, debugging

### 2. QUICK_REFERENCE.md (11KB, 445 lines)

**Cheat sheet and quick lookup guide covering:**

1. **IRC Message Flow** - Visual diagram of 6-stage processing
2. **Module Registration Pattern** - Complete template with all 5 steps
3. **Essential Functions** - Parsing, server checks, signals, settings, memory
4. **Important Structures** - IRC_SERVER_REC, WI_ITEM_REC fields
5. **Configuration Example** - Real config file format
6. **Reference Modules** - Existing patterns to study
7. **Testing Pattern** - How to write and run tests
8. **File Locations by Task** - Where to look for specific features
9. **Signal Names Reference** - All 20+ signal names grouped by type
10. **Common Patterns** - Copy-paste snippets for frequent tasks
11. **Module Directory Layout** - Directory structure template
12. **Debugging Tips** - GDB, logging, verification commands
13. **Critical Gotchas** - 5 most common mistakes and fixes

**Best for:** Quick lookup, copy-paste code, verification, debugging

## How to Use This Documentation

### If you want to...

**Understand the overall architecture:**
→ Read ARCHITECTURE.md sections 1-3 (directory structure, message flow, signals)

**Create a new IRC module:**
→ Read QUICK_REFERENCE.md section 2 (module pattern)
→ Then ARCHITECTURE.md section 4 (existing flood module reference)
→ Then ARCHITECTURE.md section 11 (anti-flood template with full code)

**Debug signal issues:**
→ Check QUICK_REFERENCE.md section 9 (signal names)
→ Consult ARCHITECTURE.md section 3 (signal system details)
→ See QUICK_REFERENCE.md section 13 (gotchas)

**Understand configuration:**
→ Read ARCHITECTURE.md section 5 (configuration system)
→ See QUICK_REFERENCE.md section 5 (config file format)

**Study a complex module:**
→ Read ARCHITECTURE.md section 8 (fe-web advanced pattern)
→ Reference: `/Users/k/dev/erssi/src/fe-web/fe-web-signals.c` (55KB)

**Find where something happens:**
→ Check QUICK_REFERENCE.md section 8 (file locations by task)

## Key Insights Summary

### The 6-Stage IRC Message Processing Pipeline

```
Raw Socket → Parse → Classify → [MODULE INTERCEPTION] → Process → Display
  (1)         (2)      (3)            (4)              (5)       (6)
```

### Critical Concepts

1. **Signals** - GTK-like publish/subscribe system. ALL modules communicate via signals.
2. **Per-Server Data** - Use MODULE_DATA() macro, not static variables
3. **Settings** - Define in init, listen to "setup changed" signal for updates
4. **Signal Priority** - Use signal_add_first() to run early, signal_add_last() to run late
5. **Memory** - All GLib (g_malloc, g_strdup, g_free), never mix allocators

### The Flood Module Pattern (Reference)

Defines the standard IRC module pattern:
1. Per-server state struct
2. Track data per nick/target
3. Detect condition (flood)
4. Emit custom signal (flood)
5. Listen for cleanup signals

### Anti-Flood Module Creation (5 Steps)

1. Create `src/irc/anti-flood/` directory
2. Write header file (4 lines)
3. Implement main file (200-300 lines)
4. Register in build system
5. User configures with `/set anti_flood_enabled on`

## File References

### Core IRC Processing
- **Message parsing**: `/Users/k/dev/erssi/src/irc/core/irc.c:543-573`
- **Event classification**: `/Users/k/dev/erssi/src/irc/core/irc.c:365-393`

### Reference Implementations
- **Flood module**: `/Users/k/dev/erssi/src/irc/flood/flood.c` (350 lines)
- **Fe-web signals**: `/Users/k/dev/erssi/src/fe-web/fe-web-signals.c` (55KB, 50+ handlers)
- **Module init order**: `/Users/k/dev/erssi/src/fe-text/irssi.c` (initialization sequence)

### Supporting Systems
- **Settings**: `/Users/k/dev/erssi/src/core/settings.c:52-150`
- **Signal system**: `/Users/k/dev/erssi/src/core/signals.c`
- **Build config**: `/Users/k/dev/erssi/src/irc/flood/meson.build`

## Building & Testing

### Build
```bash
meson setup Build --prefix=/opt/erssi -Dwith-perl=yes -Dwith-otr=yes
ninja -C Build
```

### Test
```bash
ninja -C Build test
./Build/tests/irc/core/test-irc
```

## Next Steps

1. **Quick Overview** (5 min)
   - Read QUICK_REFERENCE.md section 1 (message flow)
   - Read QUICK_REFERENCE.md section 2 (module pattern)

2. **Deep Dive** (30 min)
   - Read ARCHITECTURE.md sections 1-4 (structure + signals + flood module)
   - Study `/Users/k/dev/erssi/src/irc/flood/flood.c`

3. **Implementation** (2 hours)
   - Follow ARCHITECTURE.md section 11 (anti-flood template)
   - Reference QUICK_REFERENCE.md sections 3-4 (functions + structures)
   - Test with `ninja -C Build && /set anti_flood_enabled on`

## Document Statistics

- **ARCHITECTURE.md**: 1,027 lines, 35KB
  - 12 major sections
  - 5+ code examples per section
  - Complete working code templates
  - Debugging guide

- **QUICK_REFERENCE.md**: 445 lines, 11KB
  - 13 sections
  - 100+ code snippets
  - Copy-paste ready patterns
  - Signal and function references

- **Total**: 1,472 lines of documentation

## Version Information

- **Erssi Version**: 1.2.6 (based on irssi 1.5)
- **Build System**: Meson + Ninja
- **Documentation Date**: January 13, 2026

---

**Start with QUICK_REFERENCE.md for a fast overview, then consult ARCHITECTURE.md for detailed implementation guidance.**
