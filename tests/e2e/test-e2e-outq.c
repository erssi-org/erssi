/*
 test-e2e-outq.c : the module's send queue

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <glib.h>

#include <irssi/src/e2e/e2e-outq.h>

static void assert_pop(E2E_OUTQ *q, E2E_OUT_KIND kind, const char *line)
{
	E2E_OUT_LINE *l = e2e_outq_pop(q);

	g_assert_nonnull(l);
	g_assert_cmpint(l->kind, ==, kind);
	g_assert_cmpstr(l->line, ==, line);
	e2e_out_line_free(l);
}

static GPtrArray *lines_of(const char *first, ...)
{
	GPtrArray *a = g_ptr_array_new();
	const char *s;
	va_list ap;

	va_start(ap, first);
	for (s = first; s != NULL; s = va_arg(ap, const char *))
		g_ptr_array_add(a, (gpointer) s);
	va_end(ap);
	return a;
}

static void test_order(void)
{
	E2E_OUTQ *q = e2e_outq_new();
	GPtrArray *a = lines_of("1", "2", NULL);

	g_assert_true(e2e_outq_empty(q));
	g_assert_null(e2e_outq_pop(q));
	e2e_outq_push_back(q, E2E_OUT_PLAIN, "A");
	e2e_outq_push_front(q, E2E_OUT_CIPHER, a);
	e2e_outq_push_back(q, E2E_OUT_PLAIN, "B");
	g_ptr_array_free(a, TRUE);
	g_assert_false(e2e_outq_empty(q));
	g_assert_cmpuint(e2e_outq_length(q), ==, 4);

	assert_pop(q, E2E_OUT_CIPHER, "1");
	assert_pop(q, E2E_OUT_CIPHER, "2");
	assert_pop(q, E2E_OUT_PLAIN, "A");
	assert_pop(q, E2E_OUT_PLAIN, "B");
	g_assert_null(e2e_outq_pop(q));
	g_assert_true(e2e_outq_empty(q));
	g_assert_cmpuint(e2e_outq_length(q), ==, 0);
	e2e_outq_free(q);
}

static void test_front_of_front(void)
{
	E2E_OUTQ *q = e2e_outq_new();
	GPtrArray *a = lines_of("1", "2", NULL), *b = lines_of("3", NULL);

	e2e_outq_push_front(q, E2E_OUT_CIPHER, a);
	e2e_outq_push_front(q, E2E_OUT_CIPHER, b);
	g_ptr_array_free(a, TRUE);
	g_ptr_array_free(b, TRUE);

	assert_pop(q, E2E_OUT_CIPHER, "3");
	assert_pop(q, E2E_OUT_CIPHER, "1");
	assert_pop(q, E2E_OUT_CIPHER, "2");
	g_assert_null(e2e_outq_pop(q));
	e2e_outq_free(q);
}

static void test_drop_counts_plain(void)
{
	E2E_OUTQ *q = e2e_outq_new();
	GPtrArray *a = lines_of("c1", "c2", NULL);
	guint plain = 99;

	e2e_outq_push_back(q, E2E_OUT_PLAIN, "p1");
	e2e_outq_push_front(q, E2E_OUT_CIPHER, a);
	e2e_outq_push_back(q, E2E_OUT_PLAIN, "p2");
	e2e_outq_push_back(q, E2E_OUT_PLAIN, "p3");
	g_ptr_array_free(a, TRUE);

	g_assert_cmpuint(e2e_outq_drop(q, &plain), ==, 5);
	g_assert_cmpuint(plain, ==, 3);
	g_assert_true(e2e_outq_empty(q));
	g_assert_null(e2e_outq_pop(q));

	/* an empty queue drops nothing; plain may be NULL */
	plain = 99;
	g_assert_cmpuint(e2e_outq_drop(q, &plain), ==, 0);
	g_assert_cmpuint(plain, ==, 0);
	e2e_outq_push_back(q, E2E_OUT_PLAIN, "p4");
	g_assert_cmpuint(e2e_outq_drop(q, NULL), ==, 1);
	g_assert_true(e2e_outq_empty(q));
	e2e_outq_free(q);
}

/* freeing a queue that still holds lines (and an empty push_front) */
static void test_free_full(void)
{
	E2E_OUTQ *q = e2e_outq_new();
	GPtrArray *none = g_ptr_array_new();

	e2e_outq_push_front(q, E2E_OUT_CIPHER, none);
	g_ptr_array_free(none, TRUE);
	g_assert_true(e2e_outq_empty(q));
	e2e_outq_push_back(q, E2E_OUT_CIPHER, "x");
	e2e_outq_push_back(q, E2E_OUT_PLAIN, "y");
	e2e_outq_free(q);
	e2e_outq_free(NULL);
	e2e_out_line_free(NULL);
}

/* misuse is a programming error: g_return_if_fail, and the queue stays
   as it was (a batch with a NULL line is not pushed at all) */
static void expect_critical(void)
{
	g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL, "*assertion*failed*");
}

static void test_null_args(void)
{
	E2E_OUTQ *q = e2e_outq_new();
	GPtrArray *a = lines_of("1", NULL);
	guint plain = 99;

	g_ptr_array_add(a, NULL);
	g_ptr_array_add(a, (gpointer) "2");
	expect_critical();
	e2e_outq_push_front(q, E2E_OUT_CIPHER, a);
	g_test_assert_expected_messages();
	g_assert_true(e2e_outq_empty(q));
	g_ptr_array_free(a, TRUE);

	expect_critical();
	e2e_outq_push_front(q, E2E_OUT_CIPHER, NULL);
	g_test_assert_expected_messages();
	expect_critical();
	e2e_outq_push_back(q, E2E_OUT_PLAIN, NULL);
	g_test_assert_expected_messages();
	g_assert_true(e2e_outq_empty(q));

	a = lines_of("x", NULL);
	expect_critical();
	e2e_outq_push_front(NULL, E2E_OUT_CIPHER, a);
	g_test_assert_expected_messages();
	g_ptr_array_free(a, TRUE);
	expect_critical();
	e2e_outq_push_back(NULL, E2E_OUT_PLAIN, "x");
	g_test_assert_expected_messages();
	expect_critical();
	g_assert_null(e2e_outq_pop(NULL));
	g_test_assert_expected_messages();
	expect_critical();
	g_assert_true(e2e_outq_empty(NULL));
	g_test_assert_expected_messages();
	expect_critical();
	g_assert_cmpuint(e2e_outq_length(NULL), ==, 0);
	g_test_assert_expected_messages();
	expect_critical();
	g_assert_cmpuint(e2e_outq_drop(NULL, &plain), ==, 0);
	g_test_assert_expected_messages();
	g_assert_cmpuint(plain, ==, 0);
	e2e_outq_free(q);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/e2e/outq/order", test_order);
	g_test_add_func("/e2e/outq/front-of-front", test_front_of_front);
	g_test_add_func("/e2e/outq/drop-counts-plain", test_drop_counts_plain);
	g_test_add_func("/e2e/outq/free-full", test_free_full);
	g_test_add_func("/e2e/outq/null-args", test_null_args);

	return g_test_run();
}
