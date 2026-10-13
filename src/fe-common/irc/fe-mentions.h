#ifndef IRSSI_FE_COMMON_IRC_FE_MENTIONS_H
#define IRSSI_FE_COMMON_IRC_FE_MENTIONS_H

#include <irssi/src/common.h>

/* Records one mention: prints it to the Mentions window (if mentions_window
   is not empty), appends it to the log (if mentions_log) and emits
   "mention" (SERVER_REC, kind, where, nick, text) with the text stripped of
   formatting codes and trimmed. KIND is one of "public", "action",
   "private", "notice" or "dcc"; SERVER may be NULL (DCC). */
void mentions_record(SERVER_REC *server, const char *kind, const char *tag, const char *where,
                     const char *nick, const char *text);

/* The Mentions window, created (when CREATE) if it does not exist yet.
   NULL when mentions_window is empty, or when it does not exist and
   CREATE is FALSE. */
WINDOW_REC *mentions_window(gboolean create);

void fe_mentions_init(void);
void fe_mentions_deinit(void);

#endif
