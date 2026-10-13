#ifndef IRSSI_E2E_OUTQ_H
#define IRSSI_E2E_OUTQ_H

/*
 The module's send queue: lines waiting for the server's flood limit.

 A line is a raw IRC line without CR LF. CIPHER lines are ciphertext,
 ready to go out; PLAIN lines have not been through the gate yet (they
 are decided when they leave the queue). push_front puts a batch ahead of
 everything, in its own order; push_back appends one line.

 The queue owns copies of its lines and wipes each one when it frees it:
 e2e_outq_free(), e2e_outq_drop() and e2e_out_line_free() on a popped
 line. Pure: no irssi.

 A NULL queue, a NULL line, or a batch holding a NULL line is a
 programming error (g_return_if_fail): nothing is queued. Only
 e2e_outq_free() and e2e_out_line_free() take NULL as a no-op.
*/

#include <glib.h>

typedef enum {
	E2E_OUT_CIPHER,
	E2E_OUT_PLAIN		/* not gated yet */
} E2E_OUT_KIND;

typedef struct {
	E2E_OUT_KIND kind;
	char *line;		/* without CR LF */
} E2E_OUT_LINE;

typedef struct _E2E_OUTQ E2E_OUTQ;

E2E_OUTQ *e2e_outq_new(void);
/* wipes every line; NULL is a no-op */
void e2e_outq_free(E2E_OUTQ *q);
gboolean e2e_outq_empty(const E2E_OUTQ *q);
guint e2e_outq_length(const E2E_OUTQ *q);
/* copies of lines (char *), in order, ahead of all queued lines */
void e2e_outq_push_front(E2E_OUTQ *q, E2E_OUT_KIND kind, const GPtrArray *lines);
/* a copy of line, behind all queued lines */
void e2e_outq_push_back(E2E_OUTQ *q, E2E_OUT_KIND kind, const char *line);
/* the first line, which the caller frees with e2e_out_line_free(); NULL
   when empty */
E2E_OUT_LINE *e2e_outq_pop(E2E_OUTQ *q);
/* wipes the text; NULL is a no-op */
void e2e_out_line_free(E2E_OUT_LINE *line);
/* empties the queue, wiping every line; returns the lines dropped and,
   when plain is not NULL, sets *plain to the PLAIN ones among them */
guint e2e_outq_drop(E2E_OUTQ *q, guint *plain);

#endif
