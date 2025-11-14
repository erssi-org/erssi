# WeeChat Selective Redraw Architecture Analysis

## Executive Summary

WeeChat uses a **signal-driven dirty-tracking system** for selective sidebar redrawing. Rather than rebuilding entire nicklists or buffer lists on every change, WeeChat:

1. **Maintains sorted linked lists** (nicklist and buffer list) with proper pointers
2. **Uses dirty flags per bar item** (`items_refresh_needed[item][subitem]`)
3. **Marks items dirty on IRC events** via signal handlers
4. **Rebuilds only on access** (lazy evaluation)
5. **Renders on demand** via curses windows

This avoids full-screen redraws while maintaining correct sorting automatically.

---

## 1. Nicklist Data Structure (In-Memory)

### Sorted Linked List Design

**File**: `/Users/k/dev/weechat/src/gui/gui-nicklist.h` (lines 28-55)

```c
struct t_gui_nick_group {
    long long id;
    char *name;
    int visible;
    int level;
    
    struct t_gui_nick_group *parent;
    struct t_gui_nick_group *children;      // sorted children groups
    struct t_gui_nick_group *last_child;    // last child for fast insertion
    struct t_gui_nick_group *prev_group;    // doubly-linked for fast removal
    struct t_gui_nick_group *next_group;
    
    struct t_gui_nick *nicks;               // sorted nicks in group
    struct t_gui_nick *last_nick;           // last nick for fast insertion
};

struct t_gui_nick {
    long long id;
    struct t_gui_nick_group *group;
    char *name;
    char *prefix;
    char *color;
    
    struct t_gui_nick *prev_nick;           // doubly-linked for fast removal
    struct t_gui_nick *next_nick;
};
```

**Key insight**: The nicklist is **always sorted in memory** using doubly-linked lists. When "Adam" → "Zoltar", the nick isn't modified in-place; it's removed and reinserted at the correct position.

---

## 2. Nick Change Flow (The Critical Path)

### When a Nick Changes Name

**File**: `/Users/k/dev/weechat/src/gui/gui-nicklist.c` (lines 600-800)

WeeChat doesn't have a direct "nick rename" function. Instead:

1. **Server sends NICK update** → IRC protocol handler
2. **Creates new nick entry** with new name
3. **Inserts into sorted position** using `gui_nicklist_find_pos_nick()` + `gui_nicklist_insert_nick_sorted()`
4. **Removes old nick** from old position
5. **Emits `nicklist_nick_changed` signal**

### Finding Correct Position (Sorting Logic)

**File**: `/Users/k/dev/weechat/src/gui/gui-nicklist.c` (lines 417-434)

```c
struct t_gui_nick *
gui_nicklist_find_pos_nick(struct t_gui_nick_group *group,
                          struct t_gui_nick *nick)
{
    struct t_gui_nick *ptr_nick;
    
    // LINEAR SCAN through sorted list
    for (ptr_nick = group->nicks; ptr_nick; ptr_nick = ptr_nick->next_nick) {
        // Case-insensitive comparison
        if (string_strcasecmp(nick->name, ptr_nick->name) < 0)
            return ptr_nick;  // Insert BEFORE this nick
    }
    
    // Insert at end if no smaller nick found
    return NULL;
}
```

**Sorting is case-insensitive** (`string_strcasecmp`). The function does a **linear scan** to find the insertion point. This is O(n) per nick change.

### Inserting at Correct Position

**File**: `/Users/k/dev/weechat/src/gui/gui-nicklist.c` (lines 440-477)

```c
void
gui_nicklist_insert_nick_sorted(struct t_gui_nick_group *group,
                               struct t_gui_nick *nick)
{
    struct t_gui_nick *pos_nick;
    
    if (group->nicks) {
        pos_nick = gui_nicklist_find_pos_nick(group, nick);
        
        if (pos_nick) {
            // Insert BEFORE pos_nick
            nick->prev_nick = pos_nick->prev_nick;
            nick->next_nick = pos_nick;
            if (pos_nick->prev_nick)
                (pos_nick->prev_nick)->next_nick = nick;
            else
                group->nicks = nick;  // New head
            pos_nick->prev_nick = nick;
        } else {
            // Add to end
            nick->prev_nick = group->last_nick;
            nick->next_nick = NULL;
            group->last_nick->next_nick = nick;
            group->last_nick = nick;
        }
    } else {
        // First nick in group
        nick->prev_nick = NULL;
        nick->next_nick = NULL;
        group->nicks = nick;
        group->last_nick = nick;
    }
}
```

**No special "move" operation**: The nick is simply relinked. This is O(1) pointer manipulation once position is found.

---

## 3. Dirty Flag System (Selective Redraw)

### Per-Item Refresh Tracking

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-window.h` (lines 40-66)

```c
struct t_gui_bar_window {
    // ... other fields ...
    
    int **items_refresh_needed;    // 2D array: [item][subitem]
                                   // 1 = needs rebuild
                                   // 0 = cached/clean
    char ***items_content;         // cached rendered content
    int **items_num_lines;         // cached line count
};
```

**Structure**: A 2D array tracking **each bar item's dirty status**.

### Initializing Dirty Flags

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-window.c` (lines 495-506)

```c
for (i = 0; i < bar_window->items_count; i++) {
    bar_window->items_refresh_needed[i] = malloc(
        bar_window->items_subcount[i] * 
        sizeof(**bar_window->items_refresh_needed)
    );
    
    for (j = 0; j < bar_window->items_subcount[i]; j++) {
        bar_window->items_refresh_needed[i][j] = 1;  // Start dirty
    }
}
```

All items start **dirty** (need rebuild).

### Marking Item as Dirty

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-item.c` (lines 622-670)

```c
void
gui_bar_item_update(const char *item_name)
{
    struct t_gui_bar *ptr_bar;
    struct t_gui_window *ptr_window;
    struct t_gui_bar_window *ptr_bar_window;
    
    for (ptr_bar = gui_bars; ptr_bar; ptr_bar = ptr_bar->next_bar) {
        for (i = 0; i < ptr_bar->items_count; i++) {
            for (j = 0; j < ptr_bar->items_subcount[i]; j++) {
                if (ptr_bar->items_name[i][j] &&
                    strcmp(ptr_bar->items_name[i][j], item_name) == 0) {
                    
                    if (GUI_BAR_TYPE_ROOT) {
                        ptr_bar->bar_window->items_refresh_needed[i][j] = 1;
                    } else {
                        // Mark for all window bar instances
                        for (ptr_window = gui_windows; ptr_window; ...) {
                            ptr_bar_window->items_refresh_needed[i][j] = 1;
                        }
                    }
                    gui_bar_ask_refresh(ptr_bar);
                }
            }
        }
    }
}
```

**Process**:
1. Find all bars containing the item
2. Set `items_refresh_needed[item][subitem] = 1`
3. Request a bar refresh

### Lazy Content Rebuild

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-window.c` (lines 651-667)

```c
const char *
gui_bar_window_content_get(struct t_gui_bar_window *bar_window,
                          struct t_gui_window *window,
                          int index_item, int index_subitem)
{
    if (!bar_window)
        return NULL;
    
    // REBUILD ONLY IF DIRTY
    if (bar_window->items_refresh_needed[index_item][index_subitem]) {
        gui_bar_window_content_build_item(bar_window, window,
                                         index_item, index_subitem);
    }
    
    // Return cached content
    return bar_window->items_content[index_item][index_subitem];
}
```

**Key optimization**: Content is **only rebuilt when accessed and marked dirty**. This is the critical lazy-evaluation pattern.

---

## 4. Signal-Driven Dirty Marking

### Signal Subscriptions for Nicklist

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-item.c` (lines 2515-2516)

```c
gui_bar_item_new(
    NULL,
    "buffer_nicklist",
    &gui_bar_item_buffer_nicklist_cb,
    NULL, NULL
);

// Subscribe to all nicklist-related signals
gui_bar_item_hook_signal("nicklist_*;window_switch;buffer_switch",
                        "buffer_nicklist");
```

**Signals that trigger nicklist redraw**:
- `nicklist_nick_added` - new nick joined
- `nicklist_nick_removed` - nick quit/kicked
- `nicklist_nick_changed` - nick changed properties (name, prefix, etc.)
- `nicklist_group_added` - new group
- `nicklist_group_changed` - group property change
- `window_switch` - user switched windows
- `buffer_switch` - user switched channels

### Signal Handler

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-item.c` (lines 2291-2322)

```c
int
gui_bar_item_signal_cb(const void *pointer, void *data,
                      const char *signal,
                      const char *type_data, void *signal_data)
{
    const char *item = (const char *)pointer;
    
    if (item) {
        // Special handling for hotlist (batches updates)
        if (strcmp(item, "hotlist") == 0 &&
            strcmp(signal, "hotlist_changed") != 0) {
            if (!gui_bar_item_timer_hotlist_resort) {
                gui_bar_item_timer_hotlist_resort = hook_timer(
                    NULL, 1, 0, 1,
                    &gui_bar_item_timer_hotlist_resort_cb,
                    NULL, NULL
                );
            }
        }
        
        // MARK ITEM DIRTY
        gui_bar_item_update(item);
    }
    
    return WEECHAT_RC_OK;
}
```

**Flow**:
1. Signal fired (e.g., `nicklist_nick_changed`)
2. Handler calls `gui_bar_item_update("buffer_nicklist")`
3. All bars with this item marked dirty
4. Next render rebuilds only this item

---

## 5. Rendering the Nicklist Bar Item

### Bar Item Callback (Full Rebuild When Dirty)

**File**: `/Users/k/dev/weechat/src/gui/gui-bar-item.c` (lines 1832-1950+)

```c
char *
gui_bar_item_buffer_nicklist_cb(const void *pointer, void *data,
                               struct t_gui_bar_item *item,
                               struct t_gui_window *window,
                               struct t_gui_buffer *buffer,
                               struct t_hashtable *extra_info)
{
    struct t_gui_nick_group *ptr_group;
    struct t_gui_nick *ptr_nick;
    char **nicklist;
    
    nicklist = string_dyn_alloc(256);
    
    // ITERATE in sorted order through nicklist
    ptr_group = NULL;
    ptr_nick = NULL;
    gui_nicklist_get_next_item(buffer, &ptr_group, &ptr_nick);
    
    while (ptr_group || ptr_nick) {
        if ((ptr_nick && ptr_nick->visible) ||
            (ptr_group && !ptr_nick &&
             buffer->nicklist_display_groups &&
             ptr_group->visible)) {
            
            if ((*nicklist)[0])
                string_dyn_concat(nicklist, "\n", -1);
            
            if (ptr_nick) {
                // Render nick with indentation, prefix, color
                if (ptr_nick->prefix)
                    string_dyn_concat(nicklist, ptr_nick->prefix, -1);
                string_dyn_concat(nicklist, ptr_nick->name, -1);
            } else {
                // Render group header
                string_dyn_concat(nicklist, ptr_group->name, -1);
            }
        }
        
        gui_nicklist_get_next_item(buffer, &ptr_group, &ptr_nick);
    }
    
    return nicklist;  // Return rendered string
}
```

**Key points**:
- Iterates through **sorted linked list** (maintains order automatically)
- Builds a single multi-line string
- Returns rendered output
- **Not called until marked dirty**

---

## 6. Architecture Comparison

### WeeChat's Approach

```
Data Structure:   Sorted Linked Lists (always sorted)
                           ↓
Signal Emitted:   nicklist_nick_changed
                           ↓
Dirty Flag:       items_refresh_needed[item][subitem] = 1
                           ↓
Lazy Rebuild:     Only when content accessed + dirty
                           ↓
Render:           Iterate linked list → build string
                           ↓
Curses Draw:      wrefresh() / wnoutrefresh()
```

### Key Design Decisions

| Aspect | WeeChat | Implication |
|--------|---------|------------|
| **Sorting** | In-memory linked lists | O(n) to find position, O(1) to insert |
| **Nick rename** | Remove + reinsert | Handled automatically by sorted insertion |
| **Dirty tracking** | Per-item flags | Only rebuild items that changed |
| **Rebuild trigger** | On access when dirty | Lazy evaluation |
| **Iteration order** | Fixed linked list order | Renders in sorted order automatically |
| **Full vs partial** | Item-level granularity | Whole item rebuilt, not individual lines |

---

## 7. Critical Implementation Details

### No Line-Level Caching

WeeChat does **NOT** cache individual nicklist lines. When dirty, the **entire nicklist item is rebuilt** as a single multi-line string.

```c
// This is the level of granularity:
items_content[item_index][subitem_index] = "line1\nline2\nline3\n..."
items_num_lines[item_index][subitem_index] = 3
```

There's no mechanism to update just the "Adam" line when it becomes "Zoltar".

### Why This Works

1. **Nicklist is usually small** (50-500 nicks typical)
2. **Sorted order is maintained automatically** in memory
3. **Most events require full redraw anyway** (when nicks sort differently)
4. **Signal batching** prevents excessive redraws for mass events

### When Efficiency Matters

For **large channels (1000+ nicks)** with frequent sorting changes (e.g., voice status toggles):

- WeeChat rebuilds entire nicklist string each time
- This is **acceptable** because signal handlers batch updates
- The `gui_bar_item_timer_hotlist_resort` pattern shows batching approach

---

## 8. Buffer List Implementation

The buffer list (from `buflist` plugin) uses the **same pattern**:

**File**: `/Users/k/dev/weechat/src/plugins/buflist/buflist-bar-item.c`

```c
void
buflist_bar_item_update(int index, int force)
{
    // Mark bar item dirty
    weechat_bar_item_update(buflist_bar_item_get_name(index));
}

char *
buflist_bar_item_buflist_cb(const void *pointer, void *data, ...) {
    // Called only when dirty
    // Rebuilds entire buffer list as multi-line string
    // Iterates through global buffer list in order
}
```

Buffer list items are **also at item-level granularity**, not line-level.

---

## 9. Performance Optimizations

### Batching for Mass Events

**Pattern**: Instead of marking dirty immediately, batch updates:

```c
// For hotlist changes (which can be many):
if (strcmp(item, "hotlist") == 0 &&
    strcmp(signal, "hotlist_changed") != 0) {
    if (!gui_bar_item_timer_hotlist_resort) {
        gui_bar_item_timer_hotlist_resort = hook_timer(
            NULL, 1, 0, 1,
            &gui_bar_item_timer_hotlist_resort_cb,
            NULL, NULL
        );
    }
}
```

This creates a **1ms timer** that fires once, then processes all pending hotlist changes together. This prevents 100 immediate redraws for 100 hotlist changes.

### Root vs Window Bars

```c
if (GUI_BAR_TYPE_ROOT) {
    ptr_bar->bar_window->items_refresh_needed[i][j] = 1;
} else {
    // Mark each window's instance
    for (ptr_window = gui_windows; ptr_window; ...) {
        for (ptr_bar_window = ptr_window->bar_windows; ...) {
            if (ptr_bar_window->bar == ptr_bar) {
                ptr_bar_window->items_refresh_needed[i][j] = 1;
            }
        }
    }
}
```

Each **window-local bar** has its own dirty flags. This avoids unnecessary redraws of sidebars in inactive windows.

---

## 10. Key Findings for erssi Implementation

### What WeeChat Does Well

1. **Sorted linked lists** = automatic correct ordering
2. **Dirty flag per item** = selective redraw at item level
3. **Lazy evaluation** = rebuild only when accessed
4. **Signal-driven** = decoupled from rendering loop

### What WeeChat Doesn't Do

1. **No line-level dirty tracking** - entire item rebuilt
2. **No position caching** - scans from start each render
3. **No delta updates** - no "move line X to position Y"
4. **No special handling for rename** - remove + reinsert

### Implications for erssi SIDEPANEL-SELECTIVE-REDRAW

For erssi's selective redraw system (if implementing line-level tracking):

**Consider WeeChat's simplicity first**:
- If nicklist is usually <200 lines → full rebuild is fine
- If window list is usually <50 lines → full rebuild is fine

**Only optimize if profiling shows**:
- Excessive CPU during mass nick join/part events
- Visible rendering delays during large list updates

**WeeChat's batching pattern** (1ms timer) might be sufficient:
```c
// Instead of immediate redraw
redraw_nicklist_on_nick_change();

// Use batching
schedule_batched_sidepanel_redraw(5);  // 5ms timer
```

This defers updates and catches multiple changes in one redraw.

---

## File Reference

| File | Lines | Purpose |
|------|-------|---------|
| `gui-nicklist.h` | 28-55 | Nicklist structures |
| `gui-nicklist.c` | 417-434 | Find position (sorting) |
| `gui-nicklist.c` | 440-477 | Insert sorted |
| `gui-nicklist.c` | 1155-1214 | Nick property setter |
| `gui-bar-window.h` | 40-66 | Bar window with dirty flags |
| `gui-bar-window.c` | 495-506 | Init dirty flags |
| `gui-bar-window.c` | 651-667 | Lazy rebuild on access |
| `gui-bar-item.c` | 622-670 | Mark items dirty |
| `gui-bar-item.c` | 1832-1950 | Rebuild nicklist item |
| `gui-bar-item.c` | 2291-2322 | Signal handler |
| `gui-bar-item.c` | 2515-2516 | Subscribe to signals |
| `buflist-bar-item.c` | 122-143 | Buffer list update |

