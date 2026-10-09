/*
 fe-web-fuzz.c : shared setup for the fe-web fuzz targets

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/* The input path of a web client (client_input_once(), the handshake, the
 * password check, the frame loop) is static in fe-web-server.c. It is
 * compiled into the fuzz targets from here, unchanged, so they run the
 * real code without fe-web having to export anything for them. This file
 * must stay the only one that includes it. */
#include <irssi/src/fe-web/fe-web-server.c>

#include <irssi/src/fe-fuzz/fe-web/fe-web-fuzz.h>
#include <irssi/src/fe-fuzz/null-logger.h>

#include <irssi/src/core/core.h>
#include <irssi/src/core/args.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/chatnets.h>
#include <irssi/src/core/servers-setup.h>
#include <irssi/src/fe-common/core/fe-common-core.h>
#include <irssi/src/fe-ansi/sidepanels-activity.h>
#include <irssi/src/fe-ansi/sidepanels-render.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* irc-core.c */
void irc_core_init(void);
/* fe-common-irc.c */
void fe_common_irc_init(void);

/* fe-web.c registers these in fe_web_init(), which also creates the TLS
 * key and certificate in irssi's home - not wanted here */
static void fe_web_fuzz_settings(void)
{
	settings_add_bool("lookandfeel", "fe_web_enabled", FALSE);
	settings_add_int("lookandfeel", "fe_web_port", 9001);
	settings_add_str("lookandfeel", "fe_web_bind", "127.0.0.1");
	settings_add_str("lookandfeel", "fe_web_password", "");
	settings_set_str("fe_web_password", FE_WEB_FUZZ_PASSWORD);
}

/* The sidebar of the terminal frontend, which fe-web clears on mark_read */
void reset_window_priority(WINDOW_REC *win)
{
	(void) win;
}

void redraw_left_panels_only(const char *event_name)
{
	(void) event_name;
}

/* Web clients may run any command: never let a fuzzer do that */
static void sig_swallow(void)
{
	signal_stop();
}

static char *fuzz_home = NULL;
static gboolean verbose = FALSE;

static void remove_tree(const char *path)
{
	GDir *dir;
	const char *name;

	dir = g_dir_open(path, 0, NULL);
	if (dir != NULL) {
		while ((name = g_dir_read_name(dir)) != NULL) {
			char *child = g_build_filename(path, name, NULL);
			if (g_file_test(child, G_FILE_TEST_IS_DIR) &&
			    !g_file_test(child, G_FILE_TEST_IS_SYMLINK))
				remove_tree(child);
			else
				unlink(child);
			g_free(child);
		}
		g_dir_close(dir);
	}
	rmdir(path);
}

static void remove_fuzz_home(void)
{
	if (fuzz_home != NULL)
		remove_tree(fuzz_home);
}

void fe_web_fuzz_init(gboolean with_irc_signals)
{
	static char *home_arg; /* args_execute() takes it out of argv */
	char *argv[3];

	verbose = g_getenv("FE_WEB_FUZZ_VERBOSE") != NULL;
	if (!verbose)
		g_log_set_null_logger();

	fuzz_home = g_dir_make_tmp("erssi-fe-web-fuzz-XXXXXX", NULL);
	if (fuzz_home == NULL)
		abort();
	atexit(remove_fuzz_home);
	/* also HOME: settings such as autolog_path ("~/.erssi/logs/...") are
	 * relative to the home directory, not to --home */
	g_setenv("HOME", fuzz_home, TRUE);

	argv[0] = "fe-web-fuzz";
	argv[1] = home_arg = g_strconcat("--home=", fuzz_home, NULL);
	argv[2] = NULL;

	core_register_options();
	fe_common_core_register_options();
	args_execute(2, argv);
	core_preinit(argv[0]);
	core_init();
	irc_core_init();
	fe_common_core_init();
	fe_common_irc_init();
	module_register("web", "fe");

	signal_add_first("send command", (SIGNAL_FUNC) sig_swallow);
	signal_add_first("save config", (SIGNAL_FUNC) sig_swallow);

	fe_web_fuzz_settings();
	fe_web_crypto_init();
	if (fe_web_crypto_get_key() == NULL)
		abort();
	if (with_irc_signals)
		fe_web_signals_init();

	/* start from no networks and servers, so each input sees the same */
	fe_web_fuzz_reset_setup();
}

/* browser end of the socketpair of the current client */
static int peer_fd = -1;
/* what it got and did not show yet, and whether the HTTP part is over */
static GByteArray *sent = NULL;
static gboolean sent_http_done;

WEB_CLIENT_REC *fe_web_fuzz_client_new(gboolean logged_in)
{
	WEB_CLIENT_REC *client;
	int sv[2];

	if (peer_fd != -1 || socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		abort();
	fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL) | O_NONBLOCK);
	fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL) | O_NONBLOCK);
	peer_fd = sv[1];
	if (sent == NULL)
		sent = g_byte_array_new();
	g_byte_array_set_size(sent, 0);
	sent_http_done = logged_in;

	client = fe_web_client_create(sv[0], "fuzz:0");
	client->handle = net_sendbuffer_create(i_io_channel_new(sv[0]), 0);
	client->use_ssl = FALSE;
	client->ssl_channel = NULL;
	client->encryption_enabled = TRUE;
	if (logged_in) {
		client->handshake_done = TRUE;
		client->authenticated = TRUE;
	}
	return client;
}

gboolean fe_web_fuzz_client_alive(WEB_CLIENT_REC *client)
{
	return g_slist_find(web_clients, client) != NULL;
}

/* FE_WEB_FUZZ_VERBOSE: what the browser got, the HTTP response and then
 * each frame, encrypted ones decrypted */
static void show_sent(void)
{
	for (;;) {
		int fin, opcode, masked, ret, len;
		guint64 payload_len;
		guchar mask_key[4];
		const guchar *payload;
		char *text;

		if (!sent_http_done) {
			const guchar *end = memmem(sent->data, sent->len, "\r\n\r\n", 4);

			if (end == NULL)
				return;
			len = end + 4 - sent->data;
			fprintf(stderr, "fe-web sent: %.*s", len, (const char *) sent->data);
			g_byte_array_remove_range(sent, 0, len);
			sent_http_done = TRUE;
			continue;
		}
		ret = fe_web_websocket_parse_frame(sent->data, sent->len, &fin, &opcode, &masked,
		                                   &payload_len, mask_key, &payload);
		if (ret <= 0)
			return;
		len = payload - sent->data + payload_len;
		if (opcode == WS_OPCODE_BINARY) {
			guchar *plain = g_malloc(payload_len + 1);
			int plain_len = 0;

			if (fe_web_crypto_decrypt(payload, payload_len, fe_web_crypto_get_key(),
			                          plain, &plain_len))
				fprintf(stderr, "fe-web sent: %.*s\n", plain_len, (char *) plain);
			else
				fprintf(stderr, "fe-web sent: undecryptable frame\n");
			g_free(plain);
		} else {
			text = g_strndup((const char *) payload, payload_len);
			fprintf(stderr, "fe-web sent opcode %d: %s\n", opcode, text);
			g_free(text);
		}
		g_byte_array_remove_range(sent, 0, len);
	}
}

/* drop what fe-web sent to the browser */
static void drain_peer(void)
{
	char buf[16384];
	ssize_t n;

	while ((n = read(peer_fd, buf, sizeof(buf))) > 0) {
		if (verbose) {
			g_byte_array_append(sent, (const guchar *) buf, n);
			show_sent();
		}
	}
}

void fe_web_fuzz_client_send(WEB_CLIENT_REC *client, const guchar *data, gsize len)
{
	gsize off = 0;
	int rounds = 0;

	while (fe_web_fuzz_client_alive(client) && rounds++ < 100000) {
		int avail = 0;

		drain_peer();
		/* there is no main loop here: what the writable watch of fe-web
		 * does once the browser has read */
		if (client->send_tag != -1) {
			output_ready(client);
			continue;
		}
		if (off < len) {
			ssize_t n = write(peer_fd, data + off, len - off);
			if (n > 0)
				off += n;
		}
		if (ioctl(client->fd, FIONREAD, &avail) < 0)
			abort();
		if (avail == 0) {
			if (off >= len)
				break;
			continue;
		}
		/* readable: what the input watch of fe-web reacts to */
		client_input_once(client);
	}
	drain_peer();
}

void fe_web_fuzz_client_send_split(WEB_CLIENT_REC *client, const guchar *data, gsize len)
{
	const gsize mlen = sizeof(FE_WEB_FUZZ_SPLIT) - 1;
	const guchar *p = data, *end = data + len;

	while (p <= end && fe_web_fuzz_client_alive(client)) {
		const guchar *m = memmem(p, end - p, FE_WEB_FUZZ_SPLIT, mlen);
		const guchar *chunk_end = m != NULL ? m : end;

		if (chunk_end > p)
			fe_web_fuzz_client_send(client, p, chunk_end - p);
		if (m == NULL)
			break;
		p = m + mlen;
	}
}

void fe_web_fuzz_client_free(WEB_CLIENT_REC *client)
{
	drain_peer();
	if (fe_web_fuzz_client_alive(client))
		fe_web_close_client(client);
	if (peer_fd != -1) {
		close(peer_fd);
		peer_fd = -1;
	}
}

void fe_web_fuzz_reset_login_state(void)
{
	login_fails = 0;
	login_last_fail = 0;
	login_last_check = 0;
	refused_logged = 0;
	refused_unlogged = 0;
}

void fe_web_fuzz_reset_setup(void)
{
	while (setupservers != NULL)
		server_setup_remove(setupservers->data);
	while (chatnets != NULL)
		chatnet_remove(chatnets->data);
}

int fe_web_fuzz_verify_password(const char *request)
{
	return fe_web_verify_password(request);
}
