/*
 keyring.c : fuzz keyring.json and /e2e import files

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The input is a JSON file. A keyring may come from rpe2e.pl, repartee's
 export or the migration tool, an import file from anywhere. What parses
 must write back to the same document (compact and pretty), and importing
 or reading an identity out of it must not crash. Every context of its
 "channels" is looked up on a network (e2e_ctx_lookup) and gives a window
 state whose "e2e" object (e2e_window_info_json) is valid UTF-8 and
 parses as JSON with "v":1.
*/

#include <irssi/src/fe-fuzz/e2e/e2e-fuzz.h>

#include <irssi/src/e2e/e2e-json.h>
#include <irssi/src/e2e/e2e-keyring.h>
#include <irssi/src/e2e/e2e-proto.h>
#include <irssi/src/e2e/e2e-scope.h>
#include <irssi/src/e2e/e2e-state.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char rpe2e_name[] = "{\"#\xc3\x85\xc2\xbc" "aba\":1}";

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	const char *error = NULL;
	E2E_JSON *j;

	(void) argc;
	(void) argv;
	/* rpe2e.pl's byte strings: "#Å¼aba" is the UTF-8 "#żaba" */
	j = e2e_json_parse(rpe2e_name, strlen(rpe2e_name), &error);
	if (j == NULL || e2e_json_get(j, "#\xc5\xbc" "aba") == NULL) {
		fprintf(stderr, "the JSON byte strings are not rpe2e.pl's\n");
		abort();
	}
	e2e_json_free(j);
	return 0;
}

static char *reencode(const char *text, gboolean pretty)
{
	const char *error = NULL;
	E2E_JSON *j = e2e_json_parse(text, strlen(text), &error);
	char *out;

	if (j == NULL) {
		fprintf(stderr, "own output does not parse (%s): %s\n", error, text);
		abort();
	}
	out = e2e_json_encode(j, pretty);
	e2e_json_free(j);
	return out;
}

/* the "channels" rows looked at per document: every lookup reads the
   whole keyring */
#define MAX_CONTEXTS 64

/* the "e2e" object of a window: valid UTF-8, a JSON object, version 1 */
static void check_window(const E2E_JSON *kr, const E2E_NET *net, const char *wire,
                         const char *nick)
{
	E2E_WIN_INFO info;
	E2E_JSON *parsed;
	const char *error = NULL;
	char *json;

	e2e_window_info(kr, TRUE, net, wire, "@me@127.0.0.1", nick, &info);
	if (info.pending == NULL)
		abort();
	json = e2e_window_info_json(&info, net->label);
	if (json == NULL || !g_utf8_validate(json, -1, NULL)) {
		fprintf(stderr, "e2e_window_info_json is not valid UTF-8: %s\n", json);
		abort();
	}
	parsed = e2e_json_parse(json, strlen(json), &error);
	if (parsed == NULL || g_strcmp0(e2e_json_get_string(parsed, "v"), "1") != 0) {
		fprintf(stderr, "e2e_window_info_json does not parse (%s): %s\n",
		        error != NULL ? error : "no v:1", json);
		abort();
	}
	e2e_json_free(parsed);
	g_free(json);
	e2e_window_info_clear(&info);
}

/* what a network makes of each context of the keyring */
static void check_contexts(const E2E_JSON *kr, gboolean legacy_ok)
{
	E2E_NET net;
	GPtrArray *keys;
	guint i;

	e2e_net_init(&net, "IRCnet", "IRCnet", "rfc8265", legacy_ok);
	if (net.casemap != E2E_CASEMAP_UNICODE)
		abort();
	keys = e2e_json_keys(e2e_json_get(kr, "channels"));
	for (i = 0; i < keys->len && i < MAX_CONTEXTS; i++) {
		const char *wire = e2e_ctx_wire(g_ptr_array_index(keys, i));
		gboolean dm = *wire == '@';
		E2E_CTX_KIND kind;
		char *ctx = NULL, *found, *network;

		kind = e2e_ctx_lookup(kr, &net, wire, &ctx);
		if (ctx == NULL)
			abort();
		/* NEW and SCOPED name this network's row of wire */
		if (kind == E2E_CTX_NEW || kind == E2E_CTX_SCOPED) {
			network = e2e_ctx_network(ctx);
			if (g_strcmp0(network, "IRCnet") != 0 || strcmp(e2e_ctx_wire(ctx), wire) != 0)
				abort();
			g_free(network);
		} else if (kind == E2E_CTX_LEGACY && (!legacy_ok || strcmp(ctx, wire) != 0)) {
			abort();
		}
		found = e2e_ctx_find(kr, &net, wire);
		if (kind == E2E_CTX_BLOCKED && found != NULL)
			abort();
		g_free(found);
		g_free(ctx);
		check_window(kr, &net, wire, dm ? "bob" : NULL);
	}
	g_ptr_array_unref(keys);
	/* a query whose address is not known yet */
	check_window(kr, &net, NULL, "bob");
	e2e_net_clear(&net);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const char *error = NULL;
	E2E_JSON *doc, *kr, *exported;
	E2E_IDENTITY id;
	gboolean created;
	char *compact, *pretty, *again, *err = NULL;

	doc = e2e_json_parse((const char *) data, size, &error);
	if (doc == NULL)
		return 0;

	compact = e2e_json_encode(doc, FALSE);
	again = reencode(compact, FALSE);
	if (strcmp(again, compact) != 0)
		abort();
	g_free(again);
	pretty = e2e_json_encode(doc, TRUE);
	again = reencode(pretty, FALSE);
	if (strcmp(again, compact) != 0)
		abort();
	g_free(again);
	g_free(pretty);
	g_free(compact);

	/* as a keyring: its identity, its contexts on a network */
	if (e2e_identity_get(doc, &id, FALSE, &created, &err))
		e2e_identity_wipe(&id);
	g_free(err);
	err = NULL;
	check_contexts(doc, FALSE);
	check_contexts(doc, TRUE);

	/* as an /e2e import file, and exported again */
	kr = e2e_keyring_import(doc, &err);
	g_free(err);
	if (kr != NULL) {
		check_contexts(kr, FALSE);
		exported = e2e_keyring_export(kr);
		e2e_json_free(exported);
		e2e_json_free(kr);
	}
	e2e_json_free(doc);
	return 0;
}
