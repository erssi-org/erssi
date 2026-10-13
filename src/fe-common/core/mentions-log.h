#ifndef IRSSI_FE_COMMON_CORE_MENTIONS_LOG_H
#define IRSSI_FE_COMMON_CORE_MENTIONS_LOG_H

#include <glib.h>
#include <time.h>

/* One log line: "%Y-%m-%d %H:%M:%S [tag] where <nick> text\n" in local time.
 * Every C0 control (CR, LF, TAB, ESC, ...), DEL, UTF-8 C1 control (U+0080-U+009F) and
 * byte 0x80-0x9F outside a valid UTF-8 character in any field becomes one space; text
 * is trimmed. */
char *mentions_log_line(time_t when, const char *tag, const char *where, const char *nick,
                        const char *text);

/* Append LINE to PATH. Missing parent directories are created with mode 0700,
 * the file with mode 0600; an existing regular file owned by us is tightened to 0600.
 * Anything that is not a regular file (a directory, a FIFO) fails without blocking. */
gboolean mentions_log_append(const char *path, const char *line, GError **error);

/* Empty or NULL SETTING gives <irssi_dir>/logs/mentions.log (NULL when IRSSI_DIR is
 * NULL), otherwise convert_home(); a relative result is made relative to IRSSI_DIR. */
char *mentions_log_path(const char *setting, const char *irssi_dir);

/* TRUE if CHATNET is in LIST (separated by spaces and/or commas, case-insensitive). */
gboolean mentions_network_excluded(const char *chatnet, const char *list);

/* TRUE for NULL, "" or a nick containing a dot (a server name). */
gboolean mentions_is_server_nick(const char *nick);

#endif
