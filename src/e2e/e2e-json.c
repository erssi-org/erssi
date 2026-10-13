/*
 e2e-json.c : the JSON of keyring.json, with rpe2e.pl's byte strings

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 RFC 8259 as JSON::PP's decode_json reads it: UTF-8 input (invalid UTF-8
 is an error), no lone surrogates, at most 512 levels, an object at the
 top. A string with a NUL is refused as well: erssi has none to store.
*/

#include "e2e-json.h"
#include "e2e-crypto.h"

#include <string.h>

#define MAX_DEPTH 512

typedef struct {
	const char *p, *end;
	const char *error;
	int depth;
} PARSER;

E2E_JSON *e2e_json_new(E2E_JSON_TYPE type)
{
	E2E_JSON *node = g_new0(E2E_JSON, 1);

	node->type = type;
	if (type == E2E_JSON_ARRAY)
		node->array = g_ptr_array_new_with_free_func((GDestroyNotify) e2e_json_free);
	else if (type == E2E_JSON_OBJECT)
		node->object = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
		                                      (GDestroyNotify) e2e_json_free);
	return node;
}

E2E_JSON *e2e_json_new_string(const char *value)
{
	E2E_JSON *node;

	if (value == NULL)
		return e2e_json_new(E2E_JSON_NULL);
	node = e2e_json_new(E2E_JSON_STRING);
	node->str = g_strdup(value);
	return node;
}

E2E_JSON *e2e_json_new_int(gint64 value)
{
	E2E_JSON *node = e2e_json_new(E2E_JSON_NUMBER);

	node->str = g_strdup_printf("%" G_GINT64_FORMAT, value);
	return node;
}

void e2e_json_free(E2E_JSON *node)
{
	if (node == NULL)
		return;
	if (node->array != NULL)
		g_ptr_array_unref(node->array);
	if (node->object != NULL)
		g_hash_table_destroy(node->object);
	/* the keyring holds secret keys (as base64 strings) */
	if (node->str != NULL)
		e2e_wipe(node->str, strlen(node->str));
	g_free(node->str);
	g_free(node);
}

E2E_JSON *e2e_json_copy(const E2E_JSON *node)
{
	E2E_JSON *copy;
	GHashTableIter iter;
	gpointer key, value;
	guint i;

	if (node == NULL)
		return NULL;
	copy = e2e_json_new(node->type);
	copy->str = g_strdup(node->str);
	if (node->type == E2E_JSON_ARRAY) {
		for (i = 0; i < node->array->len; i++)
			g_ptr_array_add(copy->array, e2e_json_copy(g_ptr_array_index(node->array, i)));
	} else if (node->type == E2E_JSON_OBJECT) {
		g_hash_table_iter_init(&iter, node->object);
		while (g_hash_table_iter_next(&iter, &key, &value))
			g_hash_table_insert(copy->object, g_strdup(key), e2e_json_copy(value));
	}
	return copy;
}

/* ---- parsing ---- */

static E2E_JSON *parse_value(PARSER *ps);

static void skip_ws(PARSER *ps)
{
	while (ps->p < ps->end &&
	       (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
		ps->p++;
}

static int hex4(const char *p)
{
	int i, v = 0, d;

	for (i = 0; i < 4; i++) {
		d = g_ascii_xdigit_value(p[i]);
		if (d < 0)
			return -1;
		v = (v << 4) | d;
	}
	return v;
}

static void append_utf8(GString *out, gunichar c)
{
	char buf[6];

	g_string_append_len(out, buf, g_unichar_to_utf8(c, buf));
}

/* the characters of one string, then rpe2e.pl's rule: all <= 0xFF ->
   those bytes, otherwise UTF-8 */
static char *parse_string(PARSER *ps)
{
	/* never regrown (a grown buffer leaves secrets in freed memory): a
	   string has at most as many characters as bytes are left */
	GArray *chars = g_array_sized_new(FALSE, FALSE, sizeof(gunichar), ps->end - ps->p);
	gunichar c, max = 0;
	GString *out;
	guint i;

	ps->p++;	/* the opening quote */
	for (;;) {
		if (ps->p >= ps->end) {
			ps->error = "unterminated string";
			goto fail;
		}
		if (*ps->p == '"') {
			ps->p++;
			break;
		}
		if ((unsigned char) *ps->p < 0x20) {
			ps->error = "control character in a string";
			goto fail;
		}
		if (*ps->p == '\\') {
			if (ps->end - ps->p < 2) {
				ps->error = "unterminated escape";
				goto fail;
			}
			ps->p++;
			switch (*ps->p++) {
			case '"': c = '"'; break;
			case '\\': c = '\\'; break;
			case '/': c = '/'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'u': {
				int hi, lo;

				if (ps->end - ps->p < 4 || (hi = hex4(ps->p)) < 0) {
					ps->error = "bad \\u escape";
					goto fail;
				}
				ps->p += 4;
				if (hi >= 0xdc00 && hi <= 0xdfff) {
					ps->error = "lone low surrogate";
					goto fail;
				}
				if (hi >= 0xd800 && hi <= 0xdbff) {
					if (ps->end - ps->p < 6 || ps->p[0] != '\\' || ps->p[1] != 'u' ||
					    (lo = hex4(ps->p + 2)) < 0xdc00 || lo > 0xdfff) {
						ps->error = "missing low surrogate";
						goto fail;
					}
					ps->p += 6;
					c = 0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00);
				} else {
					c = hi;
				}
				break;
			}
			default:
				ps->error = "bad escape";
				goto fail;
			}
		} else {
			c = g_utf8_get_char_validated(ps->p, ps->end - ps->p);
			if (c == (gunichar) -1 || c == (gunichar) -2) {
				ps->error = "malformed UTF-8";
				goto fail;
			}
			ps->p = g_utf8_next_char(ps->p);
		}
		if (c == 0) {
			ps->error = "NUL in a string";
			goto fail;
		}
		if (c > max)
			max = c;
		g_array_append_val(chars, c);
	}

	out = g_string_sized_new(chars->len * 4 + 1);
	for (i = 0; i < chars->len; i++) {
		c = g_array_index(chars, gunichar, i);
		if (max > 0xff)
			append_utf8(out, c);
		else
			g_string_append_c(out, (char) c);
	}
	e2e_wipe(chars->data, chars->len * sizeof(gunichar));
	g_array_free(chars, TRUE);
	return g_string_free(out, FALSE);
fail:
	e2e_wipe(chars->data, chars->len * sizeof(gunichar));
	g_array_free(chars, TRUE);
	return NULL;
}

static E2E_JSON *parse_number(PARSER *ps)
{
	const char *start = ps->p;
	E2E_JSON *node;

	if (ps->p < ps->end && *ps->p == '-')
		ps->p++;
	if (ps->p >= ps->end || !g_ascii_isdigit(*ps->p))
		goto fail;
	if (*ps->p == '0')
		ps->p++;
	else
		while (ps->p < ps->end && g_ascii_isdigit(*ps->p))
			ps->p++;
	if (ps->p < ps->end && *ps->p == '.') {
		ps->p++;
		if (ps->p >= ps->end || !g_ascii_isdigit(*ps->p))
			goto fail;
		while (ps->p < ps->end && g_ascii_isdigit(*ps->p))
			ps->p++;
	}
	if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
		ps->p++;
		if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-'))
			ps->p++;
		if (ps->p >= ps->end || !g_ascii_isdigit(*ps->p))
			goto fail;
		while (ps->p < ps->end && g_ascii_isdigit(*ps->p))
			ps->p++;
	}
	/* "01": a digit right after a leading zero */
	if (ps->p < ps->end && g_ascii_isdigit(*ps->p))
		goto fail;
	node = e2e_json_new(E2E_JSON_NUMBER);
	node->str = g_strndup(start, ps->p - start);
	return node;
fail:
	ps->error = "malformed number";
	return NULL;
}

static gboolean literal(PARSER *ps, const char *word)
{
	size_t len = strlen(word);

	if ((size_t) (ps->end - ps->p) < len || strncmp(ps->p, word, len) != 0)
		return FALSE;
	ps->p += len;
	return TRUE;
}

static E2E_JSON *parse_container(PARSER *ps, gboolean object)
{
	E2E_JSON *node = e2e_json_new(object ? E2E_JSON_OBJECT : E2E_JSON_ARRAY);
	char close = object ? '}' : ']';

	if (++ps->depth > MAX_DEPTH) {
		ps->error = "nested too deep";
		goto fail;
	}
	ps->p++;
	skip_ws(ps);
	if (ps->p < ps->end && *ps->p == close) {
		ps->p++;
		ps->depth--;
		return node;
	}
	for (;;) {
		char *key = NULL;
		E2E_JSON *value;

		skip_ws(ps);
		if (object) {
			if (ps->p >= ps->end || *ps->p != '"') {
				ps->error = "expected a key";
				goto fail;
			}
			key = parse_string(ps);
			if (key == NULL)
				goto fail;
			skip_ws(ps);
			if (ps->p >= ps->end || *ps->p != ':') {
				g_free(key);
				ps->error = "expected ':'";
				goto fail;
			}
			ps->p++;
		}
		value = parse_value(ps);
		if (value == NULL) {
			g_free(key);
			goto fail;
		}
		if (object)
			g_hash_table_replace(node->object, key, value);	/* last one wins */
		else
			g_ptr_array_add(node->array, value);
		skip_ws(ps);
		if (ps->p < ps->end && *ps->p == ',') {
			ps->p++;
			continue;
		}
		if (ps->p < ps->end && *ps->p == close) {
			ps->p++;
			break;
		}
		ps->error = object ? "expected ',' or '}'" : "expected ',' or ']'";
		goto fail;
	}
	ps->depth--;
	return node;
fail:
	e2e_json_free(node);
	return NULL;
}

static E2E_JSON *parse_value(PARSER *ps)
{
	E2E_JSON *node;
	char *s;

	skip_ws(ps);
	if (ps->p >= ps->end) {
		ps->error = "unexpected end";
		return NULL;
	}
	switch (*ps->p) {
	case '{':
		return parse_container(ps, TRUE);
	case '[':
		return parse_container(ps, FALSE);
	case '"':
		s = parse_string(ps);
		if (s == NULL)
			return NULL;
		node = e2e_json_new(E2E_JSON_STRING);
		node->str = s;
		return node;
	case 't':
		if (literal(ps, "true"))
			return e2e_json_new(E2E_JSON_TRUE);
		break;
	case 'f':
		if (literal(ps, "false"))
			return e2e_json_new(E2E_JSON_FALSE);
		break;
	case 'n':
		if (literal(ps, "null"))
			return e2e_json_new(E2E_JSON_NULL);
		break;
	default:
		if (*ps->p == '-' || g_ascii_isdigit(*ps->p))
			return parse_number(ps);
		break;
	}
	ps->error = "unexpected character";
	return NULL;
}

E2E_JSON *e2e_json_parse(const char *data, size_t len, const char **error)
{
	PARSER ps;
	E2E_JSON *node;

	ps.p = data;
	ps.end = data + len;
	ps.error = NULL;
	ps.depth = 0;
	skip_ws(&ps);
	if (ps.p >= ps.end || *ps.p != '{') {
		*error = "not a JSON object";
		return NULL;
	}
	node = parse_value(&ps);
	if (node != NULL) {
		skip_ws(&ps);
		if (ps.p != ps.end) {
			e2e_json_free(node);
			node = NULL;
			ps.error = "garbage after the JSON object";
		}
	}
	if (node == NULL)
		*error = ps.error != NULL ? ps.error : "not valid JSON";
	return node;
}

/* ---- writing ---- */

/* each byte as the character of that code point, escaped like JSON::PP */
static void encode_string(GString *out, const char *s)
{
	const unsigned char *p;

	g_string_append_c(out, '"');
	for (p = (const unsigned char *) s; *p != '\0'; p++) {
		switch (*p) {
		case '"': g_string_append(out, "\\\""); break;
		case '\\': g_string_append(out, "\\\\"); break;
		case '\n': g_string_append(out, "\\n"); break;
		case '\r': g_string_append(out, "\\r"); break;
		case '\t': g_string_append(out, "\\t"); break;
		case '\f': g_string_append(out, "\\f"); break;
		case '\b': g_string_append(out, "\\b"); break;
		default:
			if (*p < 0x20)
				g_string_append_printf(out, "\\u%04x", *p);
			else if (*p < 0x80)
				g_string_append_c(out, *p);
			else
				append_utf8(out, *p);
		}
	}
	g_string_append_c(out, '"');
}

static void indent(GString *out, int level)
{
	int i;

	g_string_append_c(out, '\n');
	for (i = 0; i < level * 3; i++)
		g_string_append_c(out, ' ');
}

static gint compare_keys(gconstpointer a, gconstpointer b)
{
	return strcmp(*(const char *const *) a, *(const char *const *) b);
}

static void encode_value(GString *out, const E2E_JSON *node, gboolean pretty, int level)
{
	GPtrArray *keys;
	guint i;

	switch (node->type) {
	case E2E_JSON_NULL:
		g_string_append(out, "null");
		break;
	case E2E_JSON_FALSE:
		g_string_append(out, "false");
		break;
	case E2E_JSON_TRUE:
		g_string_append(out, "true");
		break;
	case E2E_JSON_NUMBER:
		g_string_append(out, node->str);
		break;
	case E2E_JSON_STRING:
		encode_string(out, node->str);
		break;
	case E2E_JSON_ARRAY:
		g_string_append_c(out, '[');
		for (i = 0; i < node->array->len; i++) {
			if (i > 0)
				g_string_append_c(out, ',');
			if (pretty)
				indent(out, level + 1);
			encode_value(out, g_ptr_array_index(node->array, i), pretty, level + 1);
		}
		if (pretty && node->array->len > 0)
			indent(out, level);
		g_string_append_c(out, ']');
		break;
	case E2E_JSON_OBJECT:
		keys = e2e_json_keys(node);
		g_string_append_c(out, '{');
		for (i = 0; i < keys->len; i++) {
			const char *key = g_ptr_array_index(keys, i);

			if (i > 0)
				g_string_append_c(out, ',');
			if (pretty)
				indent(out, level + 1);
			encode_string(out, key);
			g_string_append(out, pretty ? " : " : ":");
			encode_value(out, g_hash_table_lookup(node->object, key), pretty, level + 1);
		}
		if (pretty && keys->len > 0)
			indent(out, level);
		g_string_append_c(out, '}');
		g_ptr_array_unref(keys);
		break;
	}
}

/* an upper bound of the encoded size: the buffer is never regrown, so no
   freed copy of the keys stays behind */
static gsize encoded_bound(const E2E_JSON *node, int level)
{
	GHashTableIter iter;
	gpointer key, value;
	gsize n = 0;
	guint i;

	switch (node->type) {
	case E2E_JSON_STRING:
		return strlen(node->str) * 6 + 2;
	case E2E_JSON_NUMBER:
		return strlen(node->str);
	case E2E_JSON_ARRAY:
		for (i = 0; i < node->array->len; i++)
			n += encoded_bound(g_ptr_array_index(node->array, i), level + 1) + 2 + (level + 2) * 3;
		return n + 2 + (level + 1) * 3 + 2;
	case E2E_JSON_OBJECT:
		g_hash_table_iter_init(&iter, node->object);
		while (g_hash_table_iter_next(&iter, &key, &value))
			n += strlen(key) * 6 + 2 + 3 + encoded_bound(value, level + 1) + 2 + (level + 2) * 3;
		return n + 2 + (level + 1) * 3 + 2;
	default:
		return 5;
	}
}

char *e2e_json_encode(const E2E_JSON *node, gboolean pretty)
{
	GString *out = g_string_sized_new(encoded_bound(node, 0) + 2);

	encode_value(out, node, pretty, 0);
	if (pretty)
		g_string_append_c(out, '\n');
	return g_string_free(out, FALSE);
}

/* ---- access ---- */

E2E_JSON *e2e_json_get(const E2E_JSON *obj, const char *key)
{
	if (obj == NULL || obj->type != E2E_JSON_OBJECT || key == NULL)
		return NULL;
	return g_hash_table_lookup(obj->object, key);
}

const char *e2e_json_get_string(const E2E_JSON *obj, const char *key)
{
	E2E_JSON *node = e2e_json_get(obj, key);

	if (node == NULL || (node->type != E2E_JSON_STRING && node->type != E2E_JSON_NUMBER))
		return NULL;
	return node->str;
}

gint64 e2e_json_get_int(const E2E_JSON *obj, const char *key, gint64 def)
{
	const char *s = e2e_json_get_string(obj, key);

	/* Perl numifies a string the same way: leading digits */
	return s == NULL ? def : g_ascii_strtoll(s, NULL, 10);
}

gboolean e2e_json_truthy(const E2E_JSON *node)
{
	if (node == NULL)
		return FALSE;
	switch (node->type) {
	case E2E_JSON_NULL:
	case E2E_JSON_FALSE:
		return FALSE;
	case E2E_JSON_NUMBER:
		return g_ascii_strtod(node->str, NULL) != 0.0;
	case E2E_JSON_STRING:
		return *node->str != '\0' && strcmp(node->str, "0") != 0;
	default:
		return TRUE;
	}
}

void e2e_json_set(E2E_JSON *obj, const char *key, E2E_JSON *value)
{
	g_return_if_fail(obj != NULL && obj->type == E2E_JSON_OBJECT);
	g_hash_table_replace(obj->object, g_strdup(key), value);
}

void e2e_json_set_string(E2E_JSON *obj, const char *key, const char *value)
{
	e2e_json_set(obj, key, e2e_json_new_string(value));
}

void e2e_json_set_int(E2E_JSON *obj, const char *key, gint64 value)
{
	e2e_json_set(obj, key, e2e_json_new_int(value));
}

gboolean e2e_json_remove(E2E_JSON *obj, const char *key)
{
	if (obj == NULL || obj->type != E2E_JSON_OBJECT)
		return FALSE;
	return g_hash_table_remove(obj->object, key);
}

E2E_JSON *e2e_json_object_member(E2E_JSON *obj, const char *key)
{
	E2E_JSON *node = e2e_json_get(obj, key);

	if (node == NULL || node->type != E2E_JSON_OBJECT) {
		node = e2e_json_new(E2E_JSON_OBJECT);
		e2e_json_set(obj, key, node);
	}
	return node;
}

GPtrArray *e2e_json_keys(const E2E_JSON *obj)
{
	GPtrArray *keys = g_ptr_array_new();
	GHashTableIter iter;
	gpointer key;

	if (obj != NULL && obj->type == E2E_JSON_OBJECT) {
		g_hash_table_iter_init(&iter, obj->object);
		while (g_hash_table_iter_next(&iter, &key, NULL))
			g_ptr_array_add(keys, key);
		g_ptr_array_sort(keys, compare_keys);
	}
	return keys;
}

guint e2e_json_size(const E2E_JSON *node)
{
	if (node == NULL)
		return 0;
	if (node->type == E2E_JSON_OBJECT)
		return g_hash_table_size(node->object);
	if (node->type == E2E_JSON_ARRAY)
		return node->array->len;
	return 0;
}

void e2e_json_array_add(E2E_JSON *array, E2E_JSON *value)
{
	g_return_if_fail(array != NULL && array->type == E2E_JSON_ARRAY);
	g_ptr_array_add(array->array, value);
}
