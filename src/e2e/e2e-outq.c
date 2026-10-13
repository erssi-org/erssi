/*
 e2e-outq.c : the module's send queue

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "e2e-outq.h"
#include "e2e-crypto.h"

#include <string.h>

struct _E2E_OUTQ {
	GQueue lines;		/* E2E_OUT_LINE * */
};

static E2E_OUT_LINE *out_line_new(E2E_OUT_KIND kind, const char *line)
{
	E2E_OUT_LINE *l = g_new0(E2E_OUT_LINE, 1);

	l->kind = kind;
	l->line = g_strdup(line);
	return l;
}

void e2e_out_line_free(E2E_OUT_LINE *line)
{
	if (line == NULL)
		return;
	if (line->line != NULL) {
		e2e_wipe(line->line, strlen(line->line));
		g_free(line->line);
	}
	g_free(line);
}

E2E_OUTQ *e2e_outq_new(void)
{
	E2E_OUTQ *q = g_new0(E2E_OUTQ, 1);

	g_queue_init(&q->lines);
	return q;
}

void e2e_outq_free(E2E_OUTQ *q)
{
	if (q == NULL)
		return;
	e2e_outq_drop(q, NULL);
	g_free(q);
}

gboolean e2e_outq_empty(const E2E_OUTQ *q)
{
	g_return_val_if_fail(q != NULL, TRUE);
	return q->lines.length == 0;
}

guint e2e_outq_length(const E2E_OUTQ *q)
{
	g_return_val_if_fail(q != NULL, 0);
	return q->lines.length;
}

void e2e_outq_push_front(E2E_OUTQ *q, E2E_OUT_KIND kind, const GPtrArray *lines)
{
	guint i;

	g_return_if_fail(q != NULL);
	g_return_if_fail(lines != NULL);
	/* all or nothing */
	for (i = 0; i < lines->len; i++)
		g_return_if_fail(g_ptr_array_index(lines, i) != NULL);
	/* last first, so the batch keeps its order ahead of everything */
	for (i = lines->len; i > 0; i--)
		g_queue_push_head(&q->lines,
		                  out_line_new(kind, g_ptr_array_index(lines, i - 1)));
}

void e2e_outq_push_back(E2E_OUTQ *q, E2E_OUT_KIND kind, const char *line)
{
	g_return_if_fail(q != NULL);
	g_return_if_fail(line != NULL);
	g_queue_push_tail(&q->lines, out_line_new(kind, line));
}

E2E_OUT_LINE *e2e_outq_pop(E2E_OUTQ *q)
{
	g_return_val_if_fail(q != NULL, NULL);
	return g_queue_pop_head(&q->lines);
}

guint e2e_outq_drop(E2E_OUTQ *q, guint *plain)
{
	E2E_OUT_LINE *l;
	guint n = 0, p = 0;

	if (plain != NULL)
		*plain = 0;
	g_return_val_if_fail(q != NULL, 0);
	while ((l = g_queue_pop_head(&q->lines)) != NULL) {
		n++;
		if (l->kind == E2E_OUT_PLAIN)
			p++;
		e2e_out_line_free(l);
	}
	if (plain != NULL)
		*plain = p;
	return n;
}
