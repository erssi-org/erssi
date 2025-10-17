# Test Resize Fix - macOS Terminal Panel Flickering

## Problem Description
Na macOS (zarówno Ghostty jak i iTerm2) podczas powolnego resize'u okna terminala piksel po piksel:
- Co 2gi pixel znika całkowicie zawartość obu paneli (wraca dopiero gdy resize idzie na kolejny pixel)
- W iTerm2 dodatkowo występuje problem z formatowaniem:
  ```
  |@soczek
  |@soltys
  |@yooz
  ```
  zamiast poprawnego:
  ```
  |@soczek
  |@soltys
  |@yooz
  |[N]
  |adrael5
  ```

## Root Cause Analysis
Problem wynika z **race condition** podczas resize'u terminala:

1. **SIGWINCH** ustawia `resize_dirty = TRUE` i `irssi_set_dirty()`
2. **mainwindows_resize()** wywołuje `irssi_redraw()` (full redraw)
3. **Sidepanels** reagują na `"mainwindow resized"` i robią własny `term_refresh()`
4. **Podwójne wywołania `term_refresh(NULL)`**:
   - W `redraw_one()` (linia 637) - wywoływane przez `sig_mainwindow_resized`
   - W głównej pętli `dirty_check()` (linia 139) - wywoływane po `mainwindows_redraw_dirty()`

## Solution Implemented

### Debouncing Mechanism
Dodano mechanizm **debouncing** dla resize'u paneli w `sidepanels-signals.c`:

```c
/* Resize debouncing to prevent conflicts with main irssi redraw system */
static int resize_timer_tag = -1;
static GSList *pending_resize_windows = NULL;

static gboolean delayed_resize_redraw(gpointer data)
{
    /* Redraw all pending windows */
    for (tmp = pending_resize_windows; tmp; tmp = tmp->next) {
        MAIN_WINDOW_REC *mw = tmp->data;
        if (mw) {
            /* Use position_tw only, let main irssi handle the actual redraw */
            SP_MAINWIN_CTX *ctx = get_ctx(mw, FALSE);
            if (ctx) {
                position_tw(mw, ctx);
            }
        }
    }

    /* Clear pending list and let main irssi system handle refresh */
    g_slist_free(pending_resize_windows);
    pending_resize_windows = NULL;
    resize_timer_tag = -1;
    return FALSE;
}

void sig_mainwindow_resized(MAIN_WINDOW_REC *mw)
{
    /* Add to pending list if not already there */
    if (!g_slist_find(pending_resize_windows, mw)) {
        pending_resize_windows = g_slist_prepend(pending_resize_windows, mw);
    }

    /* Cancel existing timer and start new one */
    if (resize_timer_tag != -1) {
        g_source_remove(resize_timer_tag);
    }

    /* Delay redraw by 50ms to let main irssi system settle */
    resize_timer_tag = g_timeout_add(50, delayed_resize_redraw, NULL);
}
```

### Key Changes:
1. **Eliminuje podwójne `term_refresh()`** - sidepanels nie wywołują już własnego refresh'u
2. **50ms delay** - pozwala głównemu systemowi irssi zakończyć resize przed aktualizacją paneli
3. **Batching** - grupuje multiple resize events w jeden update
4. **Position-only update** - aktualizuje tylko pozycje paneli, pozostawiając renderowanie głównemu systemowi

### Cleanup
Dodano proper cleanup w `sidepanels_signals_deinit()`:
```c
/* Clean up resize timer */
if (resize_timer_tag != -1) {
    g_source_remove(resize_timer_tag);
    resize_timer_tag = -1;
}
if (pending_resize_windows) {
    g_slist_free(pending_resize_windows);
    pending_resize_windows = NULL;
}
```

## Expected Results
- **Eliminacja flickering'u** podczas resize'u na macOS
- **Stabilne renderowanie** paneli podczas powolnego resize'u
- **Zachowanie kompatybilności** z istniejącą funkcjonalnością
- **Brak wpływu na Linux** - problem występował tylko na macOS

## Testing
1. Kompilacja przeszła pomyślnie
2. Wymagane testy na macOS:
   - Powolny resize piksel po piksel w Ghostty
   - Powolny resize piksel po piksel w iTerm2
   - Sprawdzenie czy panele nie znikają
   - Sprawdzenie poprawności formatowania

## Files Modified
- `src/fe-text/sidepanels-signals.c` - główna implementacja fix'u
- Dodano external declarations dla `get_ctx` i `position_tw`
