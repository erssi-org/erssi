/* Enable GNU extensions (strcasestr) */
#define _GNU_SOURCE

/*
 fe-web-server.c : TCP / Unix socket WebSocket server for fe-web

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "fe-web.h"
#include "fe-web-ssl.h"
#include "fe-web-crypto.h"

#include <irssi/src/core/network.h>
#include <irssi/src/core/net-sendbuffer.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/misc.h>
#include <irssi/src/fe-common/core/printtext.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

static GIOChannel *listen_channel = NULL;
static int listen_port = -1;
static int listen_tag = -1;

/* Unix socket mode (fe_web_socket): the path listened on, and the device
 * and inode of the socket file made there - only that file is removed when
 * the server stops, never one something else has put in its place. */
static char *listen_path = NULL;
static dev_t listen_dev;
static ino_t listen_ino;

/* Limits for clients that have not logged in yet: one password guess per
 * connection, a few KB of request and a few seconds to send it. */
#define FE_WEB_MAX_CLIENTS 16
#define FE_WEB_MAX_REQUEST 16384
#define FE_WEB_HANDSHAKE_TIMEOUT 10
static guint relisten_tag = 0;
static time_t refused_logged = 0;
/* After FE_WEB_MAX_FAILS wrong passwords within a minute, logins are paced:
 * one password is checked every FE_WEB_CHECK_INTERVAL seconds and requests
 * in between are refused unchecked (429), until a minute passes without a
 * wrong password. Not a full lockout: fe-web usually listens on a loopback
 * port every user of a shared box can reach, and a lockout would let any of
 * them keep the owner's web client out for good. */
#define FE_WEB_MAX_FAILS 5
#define FE_WEB_CHECK_INTERVAL 2
static int login_fails = 0;
static time_t login_last_fail = 0;
static time_t login_last_check = 0;

/* Connections still logging in have their own limit, so they can never
 * take the places of logged-in clients; when it is reached the oldest of
 * them makes room (idle connections cannot keep new logins out). */
#define FE_WEB_MAX_PENDING 8
static int refused_unlogged = 0;

/* Output a client does not take yet is queued up to this much; a client
 * that stops reading for longer is closed instead of eating memory. */
#ifndef FE_WEB_MAX_OUTPUT
#define FE_WEB_MAX_OUTPUT (32 * 1024 * 1024)
#endif
/* How long a client being closed gets to take its last frame (a close
 * frame, a 401 or 429) */
#define FE_WEB_LINGER_TIMEOUT 5

/* Forward declarations */
static void sig_listen(void);
static void client_input(WEB_CLIENT_REC *client);
static void log_refused(WEB_CLIENT_REC *client, const char *reason);
static void log_refused_addr(const char *addr, const char *reason);

/* Close client connection */
static void fe_web_close_client(WEB_CLIENT_REC *client)
{
	if (client == NULL) {
		return;
	}

	if (client->handshake_tag != 0) {
		g_source_remove(client->handshake_tag);
		client->handshake_tag = 0;
	}

	if (client->close_tag != 0) {
		g_source_remove(client->close_tag);
		client->close_tag = 0;
	}

	/* Remove input and output handlers */
	if (client->recv_tag != -1) {
		g_source_remove(client->recv_tag);
		client->recv_tag = -1;
	}
	if (client->send_tag != -1) {
		g_source_remove(client->send_tag);
		client->send_tag = -1;
	}

	/* Free SSL channel if exists. With output still queued a TLS write
	 * may be half done: no close_notify then. */
	if (client->ssl_channel != NULL) {
		if (client->output_buffer->len > client->output_pos)
			client->ssl_channel->failed = 1;
		fe_web_ssl_channel_free(client->ssl_channel);
		client->ssl_channel = NULL;
	}

	/* Close socket */
	if (client->handle != NULL) {
		net_sendbuffer_destroy(client->handle, TRUE);
		client->handle = NULL;
	}

	/* Destroy client record */
	fe_web_client_destroy(client);
}

/* Output: every byte for a client goes through fe_web_client_send_raw().
 * The socket is non-blocking and a client may read slowly (a phone on
 * mobile data, a large state dump), so what the socket does not take is
 * queued in order and written when the socket is writable again. */

static gsize output_pending(WEB_CLIENT_REC *client)
{
	return client->output_buffer->len - client->output_pos;
}

/* One write to the socket: bytes written, 0 when the socket is full,
 * FE_WEB_SSL_WANT_READ when TLS has to read first, -1 on error. */
static int output_write(WEB_CLIENT_REC *client, const guchar *data, gsize len)
{
	int n = len > G_MAXINT ? G_MAXINT : (int) len;
	int ret;

	if (client->use_ssl) {
		if (client->ssl_channel == NULL)
			return -1;
		ret = fe_web_ssl_write(client->ssl_channel, (const char *) data, n);
		return ret == FE_WEB_SSL_WANT_WRITE ? 0 : ret;
	}
	if (client->handle == NULL)
		return -1;
	return net_transmit(net_sendbuffer_handle(client->handle), (const char *) data, n);
}

static void output_ready(WEB_CLIENT_REC *client);

/* A writable watch exactly while there is output the socket can take -
 * not while TLS waits for a read: the socket is writable then and the
 * watch would spin; client_input() retries after reading. */
static void output_watch_update(WEB_CLIENT_REC *client)
{
	gboolean want = output_pending(client) > 0 && !client->output_want_read &&
	                !client->output_failed && client->handle != NULL;

	if (want && client->send_tag == -1) {
		client->send_tag = i_input_add(net_sendbuffer_handle(client->handle),
		                               I_INPUT_WRITE, (GInputFunction) output_ready,
		                               client);
	} else if (!want && client->send_tag != -1) {
		g_source_remove(client->send_tag);
		client->send_tag = -1;
	}
}

/* Write what is queued, as far as the socket takes it. FALSE on a write
 * error. */
static gboolean output_flush(WEB_CLIENT_REC *client)
{
	GByteArray *buf = client->output_buffer;
	int ret;

	client->output_want_read = FALSE;
	while (output_pending(client) > 0) {
		ret = output_write(client, buf->data + client->output_pos, output_pending(client));
		if (ret > 0) {
			client->output_pos += ret;
		} else if (ret == 0) {
			break;
		} else if (ret == FE_WEB_SSL_WANT_READ) {
			client->output_want_read = TRUE;
			break;
		} else {
			return FALSE;
		}
	}

	if (output_pending(client) == 0) {
		g_byte_array_set_size(buf, 0);
		client->output_pos = 0;
	} else if (client->output_pos >= 65536 && client->output_pos >= buf->len / 2) {
		/* the written part is dropped once it is most of the buffer, not
		 * after every write. The unwritten bytes keep their contents,
		 * which is all a TLS retry needs. */
		g_byte_array_remove_range(buf, 0, client->output_pos);
		client->output_pos = 0;
	}
	output_watch_update(client);
	return TRUE;
}

static gboolean close_later(gpointer data)
{
	WEB_CLIENT_REC *client = data;

	client->close_tag = 0;
	fe_web_close_client(client);
	return FALSE;
}

/* Output to this client failed or overflowed: nothing more is sent to it
 * and it is closed from the main loop - not here, where the caller may be
 * walking web_clients or still using the client. */
static void output_fail(WEB_CLIENT_REC *client)
{
	if (client->ssl_channel != NULL && output_pending(client) > 0)
		client->ssl_channel->failed = 1; /* no close_notify after a cut record */
	client->output_failed = TRUE;
	g_byte_array_set_size(client->output_buffer, 0);
	client->output_pos = 0;
	output_watch_update(client);
	if (client->close_tag != 0)
		g_source_remove(client->close_tag);
	/* a timeout, not an idle: it has the priority of the input watches,
	 * so a busy socket cannot keep it from running */
	client->close_tag = g_timeout_add(0, close_later, client);
}

/* Writable watch */
static void output_ready(WEB_CLIENT_REC *client)
{
	if (!output_flush(client)) {
		fe_web_close_client(client);
		return;
	}
	if (client->closing && output_pending(client) == 0)
		fe_web_close_client(client);
}

/* Send bytes to a client, in order after everything sent before. What the
 * socket does not take now is queued. Never closes the client itself:
 * FALSE when it can no longer be sent to (it is closed shortly). */
gboolean fe_web_client_send_raw(WEB_CLIENT_REC *client, const void *data, gsize len)
{
	const guchar *p = data;
	gboolean queued_before;
	int ret;

	if (client == NULL || client->output_failed || client->closing)
		return FALSE;
	if (len == 0)
		return TRUE;

	queued_before = output_pending(client) > 0;
	if (!queued_before && !client->output_want_read) {
		/* nothing queued: straight to the socket */
		while (len > 0) {
			ret = output_write(client, p, len);
			if (ret > 0) {
				p += ret;
				len -= ret;
			} else if (ret == 0) {
				break;
			} else if (ret == FE_WEB_SSL_WANT_READ) {
				client->output_want_read = TRUE;
				break;
			} else {
				output_fail(client);
				return FALSE;
			}
		}
		if (len == 0)
			return TRUE;
	}

	if (output_pending(client) + len > FE_WEB_MAX_OUTPUT) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: [%s] %s does not read what is sent to it "
		          "(over %d MB queued) - disconnected",
		          client->id, client->addr, FE_WEB_MAX_OUTPUT >> 20);
		output_fail(client);
		return FALSE;
	}
	g_byte_array_append(client->output_buffer, p, len);

	/* with output queued before, the socket may have room again by now */
	if (queued_before && !client->output_want_read && !output_flush(client)) {
		output_fail(client);
		return FALSE;
	}
	output_watch_update(client);
	return TRUE;
}

/* Close the client once its queued output (a last frame, an HTTP error)
 * is written, or after FE_WEB_LINGER_TIMEOUT. Nothing is read from it or
 * sent to it any more. */
static void fe_web_close_client_flushed(WEB_CLIENT_REC *client)
{
	if (output_pending(client) == 0 || client->output_failed || client->handle == NULL) {
		fe_web_close_client(client);
		return;
	}

	client->closing = TRUE;
	if (client->recv_tag != -1) {
		g_source_remove(client->recv_tag);
		client->recv_tag = -1;
	}
	if (client->handshake_tag != 0) {
		g_source_remove(client->handshake_tag);
		client->handshake_tag = 0;
	}
	if (client->close_tag != 0)
		g_source_remove(client->close_tag);
	client->close_tag = g_timeout_add_seconds(FE_WEB_LINGER_TIMEOUT, close_later, client);
	output_watch_update(client);
}

/* Verify password from handshake request
 * Password MUST be provided in query parameter: GET /?password=secret HTTP/1.1
 */
static int fe_web_verify_password(const char *data)
{
	const char *configured_password;
	char *password_param;
	char *password_start;
	char *password_end;
	char *password = NULL;
	int result = 0;

	configured_password = settings_get_str("fe_web_password");

	/* Password is REQUIRED - reject if not configured */
	if (configured_password == NULL || *configured_password == '\0') {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: REJECTED - No password configured! Use /SET fe_web_password <password>");
		return 0;
	}

	/* "Authorization: Bearer <password>" header - the password stays out of
	 * the request line (and of anything that logs URLs) */
	{
		const char *headers = strstr(data, "\r\n");
		const char *h = headers;

		while (h != NULL && password == NULL) {
			h += 2;
			if (strncmp(h, "\r\n", 2) == 0)
				break;  /* end of headers */
			if (g_ascii_strncasecmp(h, "Authorization:", 14) == 0) {
				const char *v = h + 14;
				const char *end = strstr(v, "\r\n");

				while (*v == ' ' || *v == '\t')
					v++;
				if (g_ascii_strncasecmp(v, "Bearer ", 7) == 0 && end != NULL && end > v + 7)
					password = g_strndup(v + 7, end - (v + 7));
			}
			h = strstr(h, "\r\n");
		}
	}

	/* Otherwise the query parameter (?password=...), as older web clients send */
	password_param = password != NULL ? NULL : strstr(data, "?password=");
	if (password_param != NULL) {
		password_start = password_param + strlen("?password=");
		password_end = strpbrk(password_start, " &\r\n");
		if (password_end != NULL) {
			password = g_strndup(password_start, password_end - password_start);
		} else {
			password = g_strdup(password_start);
		}
	}

	/* Verify password: compare digests, in time independent of where
	 * the first difference is */
	if (password != NULL) {
		char *want, *got;
		unsigned char diff = 0;
		int i;

		want = g_compute_checksum_for_string(G_CHECKSUM_SHA256, configured_password, -1);
		got = g_compute_checksum_for_string(G_CHECKSUM_SHA256, password, -1);
		for (i = 0; want[i] != '\0'; i++)
			diff |= want[i] ^ got[i];
		result = diff == 0;
		memset(password, 0, strlen(password));
		g_free(password);
		g_free(want);
		g_free(got);
	}

	return result;
}

/* Handle WebSocket handshake (RFC 6455) */
static int fe_web_handle_handshake(WEB_CLIENT_REC *client, const char *data)
{
	char *key_line;
	char *key_start;
	char *key_end;
	char *accept_key;
	GString *response;
	time_t now;

	/* Look for Sec-WebSocket-Key header */
	key_line = strstr(data, "Sec-WebSocket-Key:");
	if (key_line == NULL) {
		/* Try case-insensitive search */
		key_line = strcasestr(data, "Sec-WebSocket-Key:");
		if (key_line == NULL) {
			return 0; /* Not complete handshake yet */
		}
	}

	/* Check for end of headers */
	if (strstr(data, "\r\n\r\n") == NULL) {
		return 0; /* Headers not complete */
	}

	/* Paced after too many wrong passwords: one check per interval */
	now = time(NULL);
	if (now - login_last_fail > 60)
		login_fails = 0;
	if (login_fails >= FE_WEB_MAX_FAILS && now - login_last_check < FE_WEB_CHECK_INTERVAL) {
		static const char busy[] = "HTTP/1.1 429 Too Many Requests\r\n"
		                           "Retry-After: " G_STRINGIFY(FE_WEB_CHECK_INTERVAL) "\r\n"
		                           "Content-Length: 0\r\n\r\n";
		fe_web_client_send_raw(client, busy, sizeof(busy) - 1);
		log_refused(client, "too many wrong passwords, logins paced");
		return -1;
	}
	login_last_check = now;

	/* Verify password */
	if (!fe_web_verify_password(data)) {
		login_last_fail = now;
		if (++login_fails == FE_WEB_MAX_FAILS) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: %d wrong passwords within a minute - "
			          "checking one login every %d s",
			          FE_WEB_MAX_FAILS, FE_WEB_CHECK_INTERVAL);
		}
		log_refused(client, "wrong or missing password");

		/* 401 Unauthorized; the caller closes once it is written */
		response = g_string_new("");
		g_string_append(response, "HTTP/1.1 401 Unauthorized\r\n");
		g_string_append(response, "Content-Type: text/plain\r\n");
		g_string_append(response, "Content-Length: 13\r\n");
		g_string_append(response, "\r\n");
		g_string_append(response, "Unauthorized\n");
		fe_web_client_send_raw(client, response->str, response->len);
		g_string_free(response, TRUE);
		return -1; /* Authentication failed */
	}

	/* Extract key value */
	key_start = key_line + strlen("Sec-WebSocket-Key:");
	while (*key_start == ' ' || *key_start == '\t') {
		key_start++;
	}

	key_end = strchr(key_start, '\r');
	if (key_end == NULL) {
		key_end = strchr(key_start, '\n');
	}

	if (key_end == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: [%s] Invalid WebSocket key format",
		          client->id);
		return 0;
	}

	/* Store key */
	if (client->websocket_key != NULL) {
		g_free(client->websocket_key);
	}
	client->websocket_key = g_strndup(key_start, key_end - key_start);

	/* Compute accept key */
	accept_key = fe_web_websocket_compute_accept(client->websocket_key);

	/* Build handshake response */
	response = g_string_new("");
	g_string_append(response, "HTTP/1.1 101 Switching Protocols\r\n");
	g_string_append(response, "Upgrade: websocket\r\n");
	g_string_append(response, "Connection: Upgrade\r\n");
	g_string_append_printf(response, "Sec-WebSocket-Accept: %s\r\n", accept_key);
	g_string_append(response, "\r\n");

	/* Send response (queued if the socket is full) */
	if (!fe_web_client_send_raw(client, response->str, response->len)) {
		g_free(accept_key);
		g_string_free(response, TRUE);
		return -1;
	}

	g_free(accept_key);
	g_string_free(response, TRUE);

	client->handshake_done = TRUE;
	return 1;
}

/* Handle WebSocket data */
static void fe_web_handle_websocket_data(WEB_CLIENT_REC *client)
{
	int fin;
	int opcode;
	int masked;
	guint64 payload_len;
	guchar mask_key[4];
	const guchar *payload;
	int ret;
	guchar *unmasked_payload;
	gsize frame_total_len;

	/* output_failed: the client is closed shortly, nothing it asks for
	 * can be answered */
	while (client->input_buffer->len > 0 && !client->output_failed) {
		/* Try to parse frame */
		ret = fe_web_websocket_parse_frame(client->input_buffer->data,
		                                    client->input_buffer->len,
		                                    &fin, &opcode, &masked,
		                                    &payload_len, mask_key, &payload);

		if (ret == 0) {
			/* Incomplete frame - wait for more data */
			break;
		}

		if (ret < 0) {
			/* Invalid frame - close connection */
			fe_web_close_client(client);
			return;
		}

		/* Calculate total frame length */
		frame_total_len = (payload - client->input_buffer->data) + payload_len;

		/* Handle different opcodes */
		if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) { /* binary = encrypted */
			/* Unmask payload if needed */
			if (masked) {
				unmasked_payload = g_malloc(payload_len + 1);
				memcpy(unmasked_payload, payload, payload_len);
				fe_web_websocket_unmask(unmasked_payload, payload_len, mask_key);
				unmasked_payload[payload_len] = '\0';

				/* Binary frame = encrypted data */
				if (opcode == WS_OPCODE_BINARY) {
					unsigned char *decrypted;
					int decrypted_len;
					const unsigned char *key;

					if (!client->encryption_enabled) {
						printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
						          "fe-web: [%s] Received encrypted data but encryption not enabled", client->id);
						g_free(unmasked_payload);
						fe_web_close_client(client);
						return;
					}

					key = fe_web_crypto_get_key();
					if (key == NULL) {
						printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
						          "fe-web: [%s] Encryption key not available", client->id);
						g_free(unmasked_payload);
						fe_web_close_client(client);
						return;
					}

					/* Allocate buffer for decrypted data */
					decrypted = g_malloc(payload_len);

					/* Decrypt */
					if (!fe_web_crypto_decrypt(unmasked_payload, payload_len, key, decrypted, &decrypted_len)) {
						printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
						          "fe-web: [%s] Decryption failed - wrong password or tampered data", client->id);
						g_free(unmasked_payload);
						g_free(decrypted);
						fe_web_close_client(client);
						return;
					}

					/* Null-terminate decrypted JSON */
					g_free(unmasked_payload);
					unmasked_payload = g_malloc(decrypted_len + 1);
					memcpy(unmasked_payload, decrypted, decrypted_len);
					unmasked_payload[decrypted_len] = '\0';
					g_free(decrypted);
				}

				/* Handle JSON message */
				fe_web_client_handle_message(client, (const char *)unmasked_payload);
				g_free(unmasked_payload);
			}
		} else if (opcode == WS_OPCODE_CLOSE) {
			/* RFC 6455 5.5.1: answer with a Close frame (echoing the
			 * status code), then close - the client then sees an orderly
			 * close instead of waiting for its timeout. A client's close
			 * payload is masked; only the 2-byte status code is echoed. */
			guchar status[2] = { 0x03, 0xE8 }; /* 1000 normal closure */
			guchar *close_frame;
			gsize close_len;

			if (payload_len >= 2) {
				status[0] = payload[0] ^ (masked ? mask_key[0] : 0);
				status[1] = payload[1] ^ (masked ? mask_key[1] : 0);
			}
			close_frame = fe_web_websocket_create_frame(WS_OPCODE_CLOSE, status, 2, &close_len);
			fe_web_client_send_raw(client, close_frame, close_len);
			g_free(close_frame);

			/* after what is queued before it, the close frame too */
			fe_web_close_client_flushed(client);
			return;
		} else if (opcode == WS_OPCODE_PING) {
			/* Send pong, in order behind what is queued */
			guchar *pong_frame;
			guchar *pong_data;
			gsize pong_len;

			/* the pong carries the ping's data, unmasked */
			pong_data = g_malloc(payload_len + 1);
			memcpy(pong_data, payload, payload_len);
			if (masked)
				fe_web_websocket_unmask(pong_data, payload_len, mask_key);
			pong_frame = fe_web_websocket_create_frame(WS_OPCODE_PONG, pong_data, payload_len, &pong_len);
			g_free(pong_data);

			fe_web_client_send_raw(client, pong_frame, pong_len);
			g_free(pong_frame);
		}
		/* WS_OPCODE_PONG - nothing to do */

		/* Remove processed frame from buffer */
		g_byte_array_remove_range(client->input_buffer, 0, frame_total_len);
	}
}

/* Read data from client */
static void client_input_once(WEB_CLIENT_REC *client);

/* Read until nothing is left: OpenSSL may hold already-decrypted data (a
 * record larger than one read, several frames in one record) that never
 * makes the socket readable again, so stopping after one read would leave
 * those frames waiting for the next packet */
static void client_input(WEB_CLIENT_REC *client)
{
	int rounds = 0;

	do {
		client_input_once(client);
	} while (g_slist_find(web_clients, client) != NULL && !client->closing &&
	         !client->output_failed && client->use_ssl &&
	         client->ssl_channel != NULL && client->ssl_channel->ssl != NULL &&
	         client->ssl_channel->handshake_done &&
	         SSL_pending(client->ssl_channel->ssl) > 0 && ++rounds < 1024);

	if (g_slist_find(web_clients, client) == NULL)
		return;
	if (client->output_failed) {
		/* top level here: no need to wait for close_later() */
		fe_web_close_client(client);
	} else if (client->output_want_read && !client->closing) {
		/* TLS has read what it waited for (renegotiation, key update):
		 * the queued output can go on */
		if (!output_flush(client))
			fe_web_close_client(client);
	}
}

static void client_input_once(WEB_CLIENT_REC *client)
{
	guchar buffer[8192];
	int ret;
	GIOChannel *channel;

	if (client == NULL || client->handle == NULL || client->closing) {
		return;
	}
	if (client->output_failed) {
		fe_web_close_client(client);
		return;
	}

	/* SSL handshake if needed */
	if (client->use_ssl && client->ssl_channel != NULL && !client->ssl_channel->handshake_done) {
		ret = fe_web_ssl_accept(client->ssl_channel);

		if (ret == 0) {
			/* Need more data */
			return;
		} else if (ret < 0) {
			/* Handshake failed: not TLS, a port scan, an old client */
			log_refused(client, "TLS handshake failed");
			fe_web_close_client(client);
			return;
		}

		/* Handshake complete */
	}

	/* Read from socket (SSL or plain) */
	if (client->use_ssl && client->ssl_channel != NULL) {
		ret = fe_web_ssl_read(client->ssl_channel, (char *)buffer, sizeof(buffer));

		if (ret == -2) {
			/* SSL wants read - wait for more data */
			return;
		}
	} else {
		channel = net_sendbuffer_handle(client->handle);
		if (channel == NULL) {
			return;
		}
		ret = net_receive(channel, (char *)buffer, sizeof(buffer));
	}

	if (ret <= 0) {
		/* Connection closed or error */
		if (ret < 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] Connection error (ret=%d, errno=%d: %s)",
			          client->id, ret, errno, strerror(errno));
		}
		fe_web_close_client(client);
		return;
	}

	/* Append to input buffer */
	g_byte_array_append(client->input_buffer, buffer, ret);

	/* Handle handshake first */
	if (!client->handshake_done) {
		/* Null-terminate for string operations */
		g_byte_array_append(client->input_buffer, (guchar *)"\0", 1);

		ret = fe_web_handle_handshake(client, (const char *)client->input_buffer->data);
		if (ret < 0 || (ret == 0 && client->input_buffer->len > FE_WEB_MAX_REQUEST)) {
			/* wrong password (after the 401 or 429 is written), failed
			 * write or an endless request */
			fe_web_close_client_flushed(client);
			return;
		}
		if (ret > 0) {
			/* Handshake complete - send auth_ok */
			WEB_MESSAGE_REC *msg;

			if (client->handshake_tag != 0) {
				g_source_remove(client->handshake_tag);
				client->handshake_tag = 0;
			}

			msg = fe_web_message_new(WEB_MSG_AUTH_OK);
			msg->id = fe_web_generate_message_id();
			fe_web_send_message(client, msg);
			fe_web_message_free(msg);

			client->authenticated = TRUE;
			login_fails = 0;

			/* Clear input buffer */
			g_byte_array_set_size(client->input_buffer, 0);
		} else {
			/* Remove null terminator */
			g_byte_array_set_size(client->input_buffer, client->input_buffer->len - 1);
		}
		return;
	}

	/* Handle WebSocket frames */
	fe_web_handle_websocket_data(client);
}

/* Refused connections are reported once a minute at most, so a client
 * guessing passwords or a port scan cannot flood the windows. */
static void log_refused(WEB_CLIENT_REC *client, const char *reason)
{
	log_refused_addr(client->addr, reason);
}

static void log_refused_addr(const char *addr, const char *reason)
{
	if (time(NULL) - refused_logged < 60) {
		refused_unlogged++;
		return;
	}
	if (refused_unlogged > 0)
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: %d more connections refused", refused_unlogged);
	printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
	          "fe-web: connection from %s refused: %s", addr, reason);
	refused_logged = time(NULL);
	refused_unlogged = 0;
}

static gboolean handshake_timeout(gpointer data)
{
	WEB_CLIENT_REC *client = data;

	client->handshake_tag = 0;
	if (!client->handshake_done)
		fe_web_close_client(client);
	return FALSE;
}

static gboolean relisten(gpointer data)
{
	relisten_tag = 0;
	if (listen_channel != NULL && listen_tag == -1)
		listen_tag = i_input_add(listen_channel, I_INPUT_READ,
		                         (GInputFunction) sig_listen, NULL);
	return FALSE;
}

/* The uid of the process at the other end of a Unix socket connection.
 * Returns 1 when known, 0 when it could not be read and -1 when this system
 * has no way to tell (then the socket file's 0600 mode is the only guard). */
static int socket_peer_uid(int fd, uid_t *uid)
{
#if defined(__linux__) && defined(SO_PEERCRED)
	struct ucred cred;
	socklen_t len = sizeof(cred);

	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0)
		return 0;
	if (len != sizeof(cred)) {
		errno = EPROTO;
		return 0;
	}
	*uid = cred.uid;
	return 1;
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__)
	gid_t gid;

	return getpeereid(fd, uid, &gid) == 0 ? 1 : 0;
#else
	(void) fd;
	(void) uid;
	return -1;
#endif
}

/* Accept a connection on the Unix socket */
static GIOChannel *socket_accept(GIOChannel *channel)
{
	struct sockaddr_un sa;
	socklen_t len = sizeof(sa);
	int fd;

	fd = accept(g_io_channel_unix_get_fd(channel), (struct sockaddr *) &sa, &len);
	if (fd < 0)
		return NULL;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	/* /exec children must not inherit web clients' connections */
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	return i_io_channel_new(fd);
}

/* Accept new connection */
static void sig_listen(void)
{
	IPADDR ip;
	int port;
	GIOChannel *handle;
	char host[MAX_IP_LEN];
	char *addr;
	WEB_CLIENT_REC *client;
	NET_SENDBUF_REC *sendbuf;

	/* Accept connection */
	if (listen_path != NULL)
		handle = socket_accept(listen_channel);
	else
		handle = net_accept(listen_channel, &ip, &port);
	if (handle != NULL) {
		/* not inherited by /exec children */
		fcntl(g_io_channel_unix_get_fd(handle), F_SETFD, FD_CLOEXEC);
	}
	if (handle == NULL) {
		/* Out of file descriptors: the connection stays queued and the
		 * socket stays readable - pause instead of spinning. */
		if ((errno == EMFILE || errno == ENFILE) && listen_tag != -1) {
			g_source_remove(listen_tag);
			listen_tag = -1;
			relisten_tag = g_timeout_add_seconds(1, relisten, NULL);
		}
		return;
	}

	if (listen_path != NULL) {
		uid_t uid = 0;
		int known;

		addr = g_strdup_printf("unix:%s", listen_path);
		/* only processes of the user running erssi */
		known = socket_peer_uid(g_io_channel_unix_get_fd(handle), &uid);
		if (known == 0 || (known > 0 && uid != getuid())) {
			char *reason = known == 0 ?
			    g_strdup_printf("cannot read peer credentials: %s", g_strerror(errno)) :
			    g_strdup_printf("process of uid %lu, not ours", (unsigned long) uid);

			log_refused_addr(addr, reason);
			g_free(reason);
			g_free(addr);
			net_disconnect(handle);
			return;
		}
	} else {
		net_ip2host(&ip, host);
		addr = g_strdup_printf("%s:%d", host, port);
	}

	{
		WEB_CLIENT_REC *oldest = NULL;
		int logged_in = 0, pending = 0;
		GSList *tmp;

		for (tmp = web_clients; tmp != NULL; tmp = tmp->next) {
			WEB_CLIENT_REC *c = tmp->data;
			if (c->authenticated) {
				logged_in++;
			} else {
				pending++;
				if (oldest == NULL || c->connected_at < oldest->connected_at)
					oldest = c;
			}
		}
		if (logged_in >= FE_WEB_MAX_CLIENTS) {
			log_refused_addr(addr, "too many web clients logged in");
			g_free(addr);
			net_disconnect(handle);
			return;
		}
		if (pending >= FE_WEB_MAX_PENDING && oldest != NULL) {
			log_refused(oldest, "too many connections logging in, oldest closed");
			fe_web_close_client(oldest);
		}
	}

	/* Increase the send buffer to 2MB for large state dumps */
	{
		int fd = g_io_channel_unix_get_fd(handle);
		int bufsize = 2 * 1024 * 1024; /* 2MB */
		if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) < 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP,
			          "fe-web: Warning: Failed to set SO_SNDBUF to %d bytes: %s",
			          bufsize, strerror(errno));
		}
	}

	/* Create client record */
	client = fe_web_client_create(g_io_channel_unix_get_fd(handle), addr);

	/* Create send buffer */
	sendbuf = net_sendbuffer_create(handle, 0);
	client->handle = sendbuf;

	/* SSL is ALWAYS enabled - no option to disable */
	client->ssl_channel = fe_web_ssl_channel_create(handle);
	client->use_ssl = TRUE;
	if (client->ssl_channel == NULL) {
		/* never a plain text connection instead */
		fe_web_close_client(client);
		g_free(addr);
		return;
	}

	/* Encryption is ALWAYS enabled - no option to disable */
	client->encryption_enabled = TRUE;

	/* Add input handler */
	client->recv_tag = i_input_add(handle, I_INPUT_READ,
	                               (GInputFunction) client_input, client);
	client->handshake_tag = g_timeout_add_seconds(FE_WEB_HANDSHAKE_TIMEOUT,
	                                              handshake_timeout, client);

	g_free(addr);
}

/* fe_web_socket: "~/" is the home directory, a relative path is relative
 * to the irssi directory */
static char *socket_path_expand(const char *value)
{
	char *path, *full;

	path = convert_home(value);
	if (!g_path_is_absolute(path)) {
		full = g_build_filename(get_irssi_dir(), path, NULL);
		g_free(path);
		path = full;
	}
	return path;
}

/* No one else may be able to swap any part of the socket path: every
 * component is owned by the user or root, a directory writable by others
 * only if sticky (like /tmp), and a symbolic link only one owned by the
 * user or root (e.g. /home -> /usr/home on FreeBSD; another user's link in
 * /tmp could later be pointed elsewhere). Returns NULL when trusted, else
 * the offending component (to be freed). */
static char *socket_path_untrusted(const char *dir)
{
	char **parts, *prefix, *existing = NULL;
	gboolean link_seen = FALSE;
	int i;
	uid_t me = getuid();

	if (!g_path_is_absolute(dir))
		return g_strdup(dir);
	parts = g_strsplit(dir, "/", -1);
	prefix = g_strdup("/");
	for (i = 0; parts[i] != NULL; i++) {
		struct stat st;
		char *next;

		if (*parts[i] == '\0')
			continue;
		next = g_build_filename(prefix, parts[i], NULL);
		g_free(prefix);
		prefix = next;
		if (lstat(prefix, &st) < 0) {
			if (errno == ENOENT)
				break;	/* the rest is created 0700 by us */
			g_strfreev(parts);
			return prefix;
		}
		g_free(existing);
		existing = g_strdup(prefix);
		if (st.st_uid != me && st.st_uid != 0)
			goto bad;
		if (S_ISLNK(st.st_mode)) {
			link_seen = TRUE;
			continue;
		}
		if (!S_ISDIR(st.st_mode))
			goto bad;
		if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0 && (st.st_mode & S_ISVTX) == 0)
			goto bad;
		continue;
bad:
		g_strfreev(parts);
		g_free(existing);
		return prefix;
	}
	g_strfreev(parts);
	g_free(prefix);
	if (link_seen) {
		/* a link of ours may still lead through a directory someone else
		 * can change - check where the existing part really is */
		char *real = realpath(existing, NULL), *bad;

		if (real == NULL)
			return existing;
		bad = strcmp(real, existing) == 0 ? NULL : socket_path_untrusted(real);
		free(real);
		g_free(existing);
		return bad;
	}
	g_free(existing);
	return NULL;
}

/* The socket's directory must be the user's own and writable only by the
 * user, and no one else may be able to swap any directory above it -
 * otherwise someone else could replace the socket with their own and
 * receive the web client's password. The directory itself must be a real
 * directory, not a symbolic link (lstat). A missing directory is made 0700. */
static gboolean socket_dir_check(const char *path)
{
	struct stat st;
	char *dir, *parent, *bad;
	gboolean ok = FALSE;

	dir = g_path_get_dirname(path);
	parent = g_path_get_dirname(dir);
	/* the directory itself is checked below, with a message for each case */
	bad = socket_path_untrusted(parent);
	g_free(parent);
	if (bad != NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: %s in the fe_web_socket path can be changed by "
		          "another user (owner, mode or a link that is not yours) - put the socket "
		          "in a directory of your own, e.g. ~/.erssi/fe-web.sock",
		          bad);
		g_free(bad);
		g_free(dir);
		return FALSE;
	}
	if (lstat(dir, &st) < 0) {
		if (errno != ENOENT || (errno = 0, g_mkdir_with_parents(dir, 0700) < 0) ||
		    lstat(dir, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
		    chmod(dir, 0700) < 0 || lstat(dir, &st) < 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: Not starting: directory %s of fe_web_socket: %s", dir,
			          errno ? g_strerror(errno) : "not created as a directory of yours");
			g_free(dir);
			return FALSE;
		}
		/* someone else may have made a missing directory above it
		 * between the check and mkdir - check the path again */
		parent = g_path_get_dirname(dir);
		bad = socket_path_untrusted(parent);
		g_free(parent);
		if (bad != NULL) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: Not starting: %s in the fe_web_socket path can be changed by "
			          "another user (it appeared while erssi created the directory)",
			          bad);
			g_free(bad);
			g_free(dir);
			return FALSE;
		}
	}

	if (S_ISLNK(st.st_mode)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: directory %s of fe_web_socket is a symbolic link - "
		          "use the real directory",
		          dir);
	} else if (!S_ISDIR(st.st_mode)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: %s is not a directory", dir);
	} else if (st.st_uid != getuid()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: directory %s of fe_web_socket is not owned by you",
		          dir);
	} else if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
		/* printtext() knows no %o */
		char *mode = g_strdup_printf("%04o", (unsigned int) (st.st_mode & 07777));

		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: directory %s of fe_web_socket is writable by "
		          "others (mode %s) - use a directory only you can write to, "
		          "e.g. chmod 700 %s",
		          dir, mode, dir);
		g_free(mode);
	} else {
		ok = TRUE;
	}
	g_free(dir);
	return ok;
}

static void socket_addr_set(struct sockaddr_un *sa, const char *path)
{
	memset(sa, 0, sizeof(*sa));
	sa->sun_family = AF_UNIX;
	strncpy(sa->sun_path, path, sizeof(sa->sun_path) - 1);
}

/* A socket file left by an erssi that did not stop cleanly is removed - but
 * only a socket of the user's own that nothing listens on any more. */
static gboolean socket_stale_remove(const char *path)
{
	struct sockaddr_un sa;
	struct stat st;
	int fd, ret, err;

	if (lstat(path, &st) < 0) {
		if (errno == ENOENT)
			return TRUE;
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Cannot use %s: %s", path, g_strerror(errno));
		return FALSE;
	}
	if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: %s exists and is not a socket of yours - "
		          "remove it or choose another fe_web_socket",
		          path);
		return FALSE;
	}

	/* non-blocking: a listener with a full queue must not hang erssi */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Cannot create a socket: %s", g_strerror(errno));
		return FALSE;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);
	socket_addr_set(&sa, path);
	ret = connect(fd, (struct sockaddr *) &sa, sizeof(sa));
	err = errno;
	close(fd);

	if (ret == 0 || err == EAGAIN || err == EINPROGRESS) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: another program is listening on unix:%s", path);
		return FALSE;
	}
	if (err != ECONNREFUSED) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Not starting: cannot check the old socket %s: %s", path,
		          g_strerror(err));
		return FALSE;
	}
	if (unlink(path) < 0 && errno != ENOENT) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Cannot remove the old socket %s: %s", path, g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

/* Bind and listen. The socket file is made 0600 from the start (umask), so
 * there is no moment in which another user could connect. */
static GIOChannel *socket_listen(const char *path)
{
	struct sockaddr_un sa;
	struct stat st;
	mode_t old_umask;
	int fd, ret, err;

	if (strlen(path) >= sizeof(sa.sun_path)) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return NULL;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	socket_addr_set(&sa, path);
	old_umask = umask(077);
	ret = bind(fd, (struct sockaddr *) &sa, sizeof(sa));
	err = errno;
	umask(old_umask);
	if (ret < 0) {
		close(fd);
		errno = err;
		return NULL;
	}

	if (chmod(path, 0600) < 0 || lstat(path, &st) < 0 ||
	    listen(fd, FE_WEB_MAX_CLIENTS) < 0) {
		err = errno;
		unlink(path);
		close(fd);
		errno = err;
		return NULL;
	}
	listen_dev = st.st_dev;
	listen_ino = st.st_ino;
	return i_io_channel_new(fd);
}

/* Remove the socket file - if it still is the one this server made */
static void socket_remove(void)
{
	struct stat st;

	if (listen_path == NULL)
		return;
	if (lstat(listen_path, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_dev == listen_dev &&
	    st.st_ino == listen_ino)
		unlink(listen_path);
	g_free(listen_path);
	listen_path = NULL;
}

static void server_listen_unix(const char *value)
{
	char *path;

	path = socket_path_expand(value);
	{
		struct sockaddr_un sa;

		if (strlen(path) >= sizeof(sa.sun_path)) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: Not starting: fe_web_socket path %s is too long "
			          "(at most %d bytes)",
			          path, (int) sizeof(sa.sun_path) - 1);
			g_free(path);
			return;
		}
	}
	if (!socket_dir_check(path) || !socket_stale_remove(path)) {
		g_free(path);
		return;
	}

	listen_channel = socket_listen(path);
	if (listen_channel == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Failed to listen on unix:%s: %s", path, g_strerror(errno));
		g_free(path);
		return;
	}
	listen_path = path;

	listen_tag = i_input_add(listen_channel, I_INPUT_READ,
	                         (GInputFunction) sig_listen, NULL);

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: WebSocket server listening on unix:%s (SSL + AES-256-GCM)", path);
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: Security: SSL/TLS enabled, Application-level encryption enabled, "
	          "socket 0600, only your own processes");
}

/* Initialize server */
void fe_web_server_init(void)
{
	IPADDR *bind_ip;
	const char *bind_addr;
	const char *password;
	const char *socket_path;
	int port;

	/* Check if already running */
	if (listen_channel != NULL) {
		return;
	}

	/* SECURITY: Verify password is set */
	password = settings_get_str("fe_web_password");
	if (password == NULL || *password == '\0') {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: FATAL: Cannot start server without password!");
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Please set password: /SET fe_web_password <strong-password>");
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Use a long random secret, e.g. the output of: openssl rand -base64 32");
		return;
	}

	/* SECURITY: TLS context (key and certificate made at the first start;
	 * the Unix socket uses TLS too) */
	if (!fe_web_ssl_prepare()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: FATAL: SSL/TLS not initialized!");
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: SSL certificate generation failed. Check OpenSSL installation.");
		return;
	}

	/* SECURITY: Verify encryption is initialized */
	if (!fe_web_crypto_is_enabled()) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: FATAL: Encryption not initialized!");
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Encryption key derivation failed. Check password setting.");
		return;
	}

	/* A Unix socket instead of TCP */
	socket_path = settings_get_str("fe_web_socket");
	if (socket_path != NULL && *socket_path != '\0') {
		server_listen_unix(socket_path);
		return;
	}

	/* Get settings */
	port = settings_get_int("fe_web_port");
	bind_addr = settings_get_str("fe_web_bind");

	/* Parse bind address */
	bind_ip = g_new0(IPADDR, 1);
	if (net_host2ip(bind_addr, bind_ip) != 0) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Invalid bind address: %s", bind_addr);
		g_free(bind_ip);
		return;
	}

	/* Create listening socket */
	listen_channel = net_listen(bind_ip, &port);
	if (listen_channel != NULL)
		fcntl(g_io_channel_unix_get_fd(listen_channel), F_SETFD, FD_CLOEXEC);
	g_free(bind_ip);

	if (listen_channel == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: Failed to bind to %s:%d: %s",
		          bind_addr, port, strerror(errno));
		return;
	}

	listen_port = port;

	/* Add input handler */
	listen_tag = i_input_add(listen_channel, I_INPUT_READ,
	                         (GInputFunction) sig_listen, NULL);

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: WebSocket server listening on wss://%s:%d (SSL + AES-256-GCM)",
	          bind_addr, port);
	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: Security: SSL/TLS enabled, Application-level encryption enabled");
}

/* Deinitialize server */
void fe_web_server_deinit(void)
{
	GSList *tmp;
	GSList *next;

	/* Close all clients */
	for (tmp = web_clients; tmp != NULL; tmp = next) {
		WEB_CLIENT_REC *client = tmp->data;
		next = tmp->next;
		fe_web_close_client(client);
	}

	if (relisten_tag != 0) {
		g_source_remove(relisten_tag);
		relisten_tag = 0;
	}

	/* Close listening socket */
	if (listen_tag != -1) {
		g_source_remove(listen_tag);
		listen_tag = -1;
	}

	if (listen_channel != NULL) {
		net_disconnect(listen_channel);
		listen_channel = NULL;
	}
	socket_remove();

	listen_port = -1;

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: WebSocket server stopped");
}
