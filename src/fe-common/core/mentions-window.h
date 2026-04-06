#ifndef IRSSI_FE_COMMON_CORE_MENTIONS_WINDOW_H
#define IRSSI_FE_COMMON_CORE_MENTIONS_WINDOW_H

/* Create the (mentions) window if use_mentions_window is enabled.
 * Safe to call multiple times — no-op if window already exists.
 * Must be called after status window is created. */
void mentions_window_ensure(void);

void mentions_window_init(void);
void mentions_window_deinit(void);

#endif
