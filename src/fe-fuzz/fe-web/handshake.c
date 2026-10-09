/*
 handshake.c : fuzz what a web client sends before it is logged in

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The input is what a browser sends on a fresh connection, cut into reads at
 "@@READ@@": the HTTP upgrade request (Sec-WebSocket-Key, Authorization:
 Bearer or ?password=), then - when it carries the right password - the
 WebSocket frames that follow. Anyone who can reach the port gets this far.
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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	WEB_CLIENT_REC *client;
	char *request;

	fe_web_fuzz_reset_login_state();

	/* the password parser on its own, whatever the rest of the request */
	request = g_strndup((const char *) data, size);
	fe_web_fuzz_verify_password(request);
	g_free(request);

	client = fe_web_fuzz_client_new(FALSE);
	fe_web_fuzz_client_send_split(client, data, size);
	fe_web_fuzz_client_free(client);

	fe_web_fuzz_reset_setup();
	return 0;
}
