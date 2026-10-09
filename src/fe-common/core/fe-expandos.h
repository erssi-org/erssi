#ifndef IRSSI_FE_COMMON_CORE_FE_EXPANDOS_H
#define IRSSI_FE_COMMON_CORE_FE_EXPANDOS_H

#include <glib.h>

/* Nick hash colour palette (setting nick_hash_colors). */
char *nick_palette_entry_normalize(const char *entry);
gchar **parse_color_palette(const char *colors_str, int *count);

/* The nick and mode of the message being printed, for $nickalign,
 * $nicktrunc and $nickcolored in the message formats. Set it right before
 * printing a message and clear it right after. */
void update_nick_context(const char *nick, const char *mode);
void clear_nick_context(void);

#endif
