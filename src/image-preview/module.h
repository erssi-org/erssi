#ifndef IRSSI_IMAGE_PREVIEW_MODULE_H
#define IRSSI_IMAGE_PREVIEW_MODULE_H

#include <irssi/src/common.h>
#include <irssi/src/fe-text/term.h>

#define MODULE_NAME "image-preview"

extern int quitting;
void irssi_redraw(void);
void irssi_set_dirty(void);

/* Get irssi config directory */
const char *get_irssi_dir(void);

#endif
