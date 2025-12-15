#ifndef IRSSI_FE_NOTCURSES_MODULE_H
#define IRSSI_FE_NOTCURSES_MODULE_H

#include <irssi/src/common.h>
#include <irssi/src/fe-text/term.h>

#define MODULE_NAME "fe-notcurses"

extern int quitting;
void irssi_redraw(void);
void irssi_set_dirty(void);

#endif
