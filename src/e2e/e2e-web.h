#ifndef IRSSI_E2E_WEB_H
#define IRSSI_E2E_WEB_H

/*
 The E2E state of channels and queries for other modules - a frontend
 such as fe-web can send it to its clients - and their accept/decline:
   "e2e state" SERVER_REC, char *target, char *json   (emitted)
   "e2e state request" SERVER_REC, char *target        (answered at once)
   "e2e action" SERVER_REC, char *target, char *action, char *handle,
                char *fingerprint (NULL: none)
   "e2e action refused" SERVER_REC, char *target, char *handle,
                char *reason                    (emitted)
*/

void e2e_web_init(void);
void e2e_web_deinit(void);
/* "e2e state" for every channel and query whose state differs from the
   one emitted last */
void e2e_web_refresh(void);

#endif
