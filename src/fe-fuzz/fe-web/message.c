/*
 message.c : fuzz the JSON messages of a logged-in web client

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The input is one message as fe-web gets it from a text frame (or from a
 decrypted binary frame): fe_web_client_handle_message() with its JSON
 lookups, and the handlers of each message type (network_add,
 server_list, mark_read, ...). Commands are swallowed, not run.
*/

#include <irssi/src/fe-fuzz/fe-web/fe-web-fuzz.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* known answers of the JSON string decoding, checked once at start */
static const char *const decoded[][2] = {
	{ "{\"v\":\"a\\\"b\\\\c\\/\\n\"}", "a\"b\\c/\n" },
	{ "{\"v\" : \"\\u00e9\\u20AC\"}", "\xc3\xa9\xe2\x82\xac" },
	{ "{\"v\":\"\\ud83d\\ude00!\"}", "\xf0\x9f\x98\x80!" },
	{ "{\"v\":\"\\ud800x\\udc00\"}", "\xef\xbf\xbdx\xef\xbf\xbd" },
	{ "{\"v\":\"a\\u0000b\"}", "a\xef\xbf\xbd" "b" },
	{ "{\"v\":\"\\uzz12\\u12\"}", "uzz12u12" },
};

/* and of the number lookup (-1 = not a number) */
static const struct {
	const char *json;
	int value;
} numbers[] = {
	{ "{\"v\":6697}", 6697 },
	{ "{\"v\": -12,\"w\":1}", -12 },
	{ "{\"v\":true}", 1 },
	{ "{\"v\":false}", 0 },
	{ "{\"v\":99999999999}", -1 },
	{ "{\"v\":\"6697\"}", -1 },
	{ "{\"v\":}", -1 },
};

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	gsize i;

	(void) argc;
	(void) argv;
	fe_web_fuzz_init(FALSE);

	for (i = 0; i < G_N_ELEMENTS(decoded); i++) {
		char *value = fe_web_json_get_string(decoded[i][0], "v");

		if (g_strcmp0(value, decoded[i][1]) != 0) {
			fprintf(stderr, "fe_web_json_get_string(%s) = \"%s\", not \"%s\"\n",
			        decoded[i][0], value, decoded[i][1]);
			abort();
		}
		g_free(value);
	}
	for (i = 0; i < G_N_ELEMENTS(numbers); i++) {
		int value = fe_web_json_get_int(numbers[i].json, "v", -1);

		if (value != numbers[i].value) {
			fprintf(stderr, "fe_web_json_get_int(%s) = %d, not %d\n", numbers[i].json,
			        value, numbers[i].value);
			abort();
		}
	}
	return 0;
}

static const char *const keys[] = {
	"type", "id", "server", "command", "target", "nick", "channel", "name",
	"address", "port", "chatnet", "password", "network", "",
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	WEB_CLIENT_REC *client;
	char *json;
	gboolean utf8;
	gsize i;

	json = g_strndup((const char *) data, size);
	utf8 = g_utf8_validate(json, -1, NULL);

	/* the lookups on their own, and the escaping of what they return */
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		char *value = fe_web_json_get_string(json, keys[i]);
		char *escaped = fe_web_escape_json(value);

		if (value != NULL && strlen(escaped) < strlen(value))
			abort();
		/* \u escapes must not turn valid UTF-8 (what browsers send)
		 * into invalid, which irssi would send on to IRC */
		if (value != NULL && utf8 && !g_utf8_validate(value, -1, NULL))
			abort();
		fe_web_json_get_int(json, keys[i], -1);
		fe_web_json_has_key(json, keys[i]);
		g_free(escaped);
		g_free(value);
	}

	client = fe_web_fuzz_client_new(TRUE);
	fe_web_client_handle_message(client, json);
	/* a network_list/server_list now lists what the message added */
	fe_web_client_handle_message(client, "{\"type\":\"network_list\",\"id\":\"1\"}");
	fe_web_client_handle_message(client, "{\"type\":\"server_list\",\"id\":\"2\"}");
	fe_web_fuzz_client_free(client);

	fe_web_fuzz_reset_setup();
	g_free(json);
	return 0;
}
