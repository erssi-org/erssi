#ifndef IRSSI_FE_TEXT_SIDEPANELS_TEXT_H
#define IRSSI_FE_TEXT_SIDEPANELS_TEXT_H

#include <glib.h>
#include <irssi/src/core/utf8.h>

/* Called for every printable character of a formatted line with the
 * colour to draw it in: ATTR_* bits and 4-bit colours as term_set_color2()
 * takes them, fg24/bg24 for 24-bit colours. Return FALSE to stop. */
typedef gboolean (*SIDEPANEL_TEXT_FUNC)(unichar chr, int width, int color, unsigned int fg24,
                                        unsigned int bg24, void *data);

/* Walk text returned by format_get_text_theme*(). Its colour codes are
 * already expanded into \004 sequences and its arguments (nicks, window
 * names) are copied verbatim, so it must not be expanded again with
 * format_string_expand(): a 24-bit colour stores a channel byte below 0x21
 * as byte + 0x20, so 0x05 becomes '%', and a '%' in a channel name or the
 * halfop prefix would be read as a format code too. */
void sidepanel_text_walk(const char *text, SIDEPANEL_TEXT_FUNC func, void *data);

#endif
