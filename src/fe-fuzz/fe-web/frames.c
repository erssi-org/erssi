/*
 frames.c : fuzz the WebSocket frames of a logged-in web client

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The input is the byte stream after the handshake, cut into reads at
 "@@READ@@": frame headers (7/16/64-bit lengths, masks), text frames with
 JSON, binary frames with AES-GCM data, ping, pong, close, frames split
 over reads and several frames in one read.
*/

#include <irssi/src/fe-fuzz/fe-web/fe-web-fuzz.h>

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	fe_web_fuzz_init(FALSE);
	return 0;
}

/* what parse_frame reports must stay inside the data it was given */
static void check_parse_frame(const guchar *data, gsize size)
{
	int fin, opcode, masked, ret;
	guint64 payload_len;
	guchar mask_key[4];
	const guchar *payload = NULL;

	ret = fe_web_websocket_parse_frame(data, size, &fin, &opcode, &masked, &payload_len,
	                                   mask_key, &payload);
	if (ret < -1 || ret > 1)
		abort();
	if (ret == 1) {
		if (payload < data + 2 || payload > data + size ||
		    payload_len > (guint64) (data + size - payload) ||
		    payload_len > FE_WEB_MAX_FRAME)
			abort();
		if (opcode < 0 || opcode > 0x0F)
			abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	WEB_CLIENT_REC *client;
	guchar *copy;

	/* the header parser on its own, on an exact-size heap copy */
	copy = g_malloc(size > 0 ? size : 1);
	memcpy(copy, data, size);
	check_parse_frame(copy, size);
	g_free(copy);

	client = fe_web_fuzz_client_new(TRUE);
	fe_web_fuzz_client_send_split(client, data, size);
	fe_web_fuzz_client_free(client);

	fe_web_fuzz_reset_setup();
	return 0;
}
