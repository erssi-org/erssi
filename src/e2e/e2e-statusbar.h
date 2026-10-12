#ifndef IRSSI_E2E_STATUSBAR_H
#define IRSSI_E2E_STATUSBAR_H

/* the statusbar item "e2e": the E2E state of the active window */

void e2e_statusbar_init(void);
void e2e_statusbar_deinit(void);
/* refreshes what the item shows (the keyring is read only when its file
   changed or the state did) and redraws it */
void e2e_statusbar_redraw(void);

#endif
