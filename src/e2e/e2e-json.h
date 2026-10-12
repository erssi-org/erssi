#ifndef IRSSI_E2E_JSON_H
#define IRSSI_E2E_JSON_H

/*
 A small JSON document model for keyring.json - erssi has no JSON library
 (its config parser reads no JSON) and a new dependency is not wanted.

 Strings are byte strings, the way rpe2e.pl (JSON::PP) keeps them:
  - reading: a string whose characters all fit a byte becomes those bytes,
    any other string becomes its UTF-8 (rpe2e.pl's _bytes_deep);
  - writing: every byte is written as the character of that code point.
 So "#żaba" (the UTF-8 bytes erssi has) is stored as "#Å¼aba", exactly as
 rpe2e.pl stores it, and a name written as real Unicode by another tool
 (repartee's export, the migration tool) reads back as UTF-8.

 Numbers keep their literal text. Objects are written with sorted keys
 (a canonical form, also used to tell whether something changed).
*/

#include <glib.h>
#include <stddef.h>

typedef enum {
	E2E_JSON_NULL,
	E2E_JSON_FALSE,
	E2E_JSON_TRUE,
	E2E_JSON_NUMBER,
	E2E_JSON_STRING,
	E2E_JSON_ARRAY,
	E2E_JSON_OBJECT
} E2E_JSON_TYPE;

typedef struct _E2E_JSON E2E_JSON;
struct _E2E_JSON {
	E2E_JSON_TYPE type;
	char *str;		/* STRING: the bytes; NUMBER: the literal */
	GPtrArray *array;	/* ARRAY: E2E_JSON * */
	GHashTable *object;	/* OBJECT: char * -> E2E_JSON * */
};

/* NULL and *error on anything but a valid JSON object */
E2E_JSON *e2e_json_parse(const char *data, size_t len, const char **error);
/* compact (rpe2e.pl's encode_json) or JSON::PP's pretty form */
char *e2e_json_encode(const E2E_JSON *node, gboolean pretty);
void e2e_json_free(E2E_JSON *node);
E2E_JSON *e2e_json_copy(const E2E_JSON *node);

E2E_JSON *e2e_json_new(E2E_JSON_TYPE type);
E2E_JSON *e2e_json_new_string(const char *value);
E2E_JSON *e2e_json_new_int(gint64 value);

/* object member, NULL when obj is no object or has no such key */
E2E_JSON *e2e_json_get(const E2E_JSON *obj, const char *key);
/* a string member (or the literal of a number), else NULL */
const char *e2e_json_get_string(const E2E_JSON *obj, const char *key);
gint64 e2e_json_get_int(const E2E_JSON *obj, const char *key, gint64 def);
/* Perl's truth: null, false, 0, "" and "0" are false; NULL too */
gboolean e2e_json_truthy(const E2E_JSON *node);

/* setters take the value; a NULL string sets null */
void e2e_json_set(E2E_JSON *obj, const char *key, E2E_JSON *value);
void e2e_json_set_string(E2E_JSON *obj, const char *key, const char *value);
void e2e_json_set_int(E2E_JSON *obj, const char *key, gint64 value);
gboolean e2e_json_remove(E2E_JSON *obj, const char *key);
/* the object member of obj named key, created (or replacing a value that
   is no object) when needed */
E2E_JSON *e2e_json_object_member(E2E_JSON *obj, const char *key);

/* the keys of an object, sorted; the strings belong to the object */
GPtrArray *e2e_json_keys(const E2E_JSON *obj);
guint e2e_json_size(const E2E_JSON *node);
void e2e_json_array_add(E2E_JSON *array, E2E_JSON *value);

#endif
