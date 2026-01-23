/* Enable GNU extensions (strcasestr) */
#define _GNU_SOURCE

/*
 fe-web-server.c : TCP/WebSocket server for fe-web

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
#include <irssi/src/fe-common/core/printtext.h>

#include <string.h>
#include <errno.h>
#include <sys/socket.h>

static GIOChannel *listen_channel = NULL;
static int listen_port = -1;
static int listen_tag = -1;

/* Forward declarations */
static void sig_listen(void);
static void client_input(WEB_CLIENT_REC *client);

/* Close client connection */
static void fe_web_close_client(WEB_CLIENT_REC *client)
{
	if (client == NULL) {
		return;
	}

	/* Remove input handler */
	if (client->recv_tag != -1) {
		g_source_remove(client->recv_tag);
		client->recv_tag = -1;
	}

	/* Free SSL channel if exists */
	if (client->ssl_channel != NULL) {
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

	/* Check query parameter (?password=...) */
	password_param = strstr(data, "?password=");
	if (password_param != NULL) {
		password_start = password_param + strlen("?password=");
		password_end = strpbrk(password_start, " &\r\n");
		if (password_end != NULL) {
			password = g_strndup(password_start, password_end - password_start);
		} else {
			password = g_strdup(password_start);
		}
	}

	/* Verify password */
	if (password != NULL) {
		if (g_strcmp0(configured_password, password) == 0) {
			result = 1;
		} else {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: Invalid password!");
		}
		g_free(password);
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: No password provided in request");
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

	/* Verify password */
	if (!fe_web_verify_password(data)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: [%s] Authentication failed - closing connection",
		          client->id);

		/* Send 401 Unauthorized response - MUST use SSL if enabled! */
		response = g_string_new("");
		g_string_append(response, "HTTP/1.1 401 Unauthorized\r\n");
		g_string_append(response, "Content-Type: text/plain\r\n");
		g_string_append(response, "Content-Length: 13\r\n");
		g_string_append(response, "\r\n");
		g_string_append(response, "Unauthorized\n");

		if (client->use_ssl && client->ssl_channel != NULL) {
			/* Send through SSL */
			fe_web_ssl_write(client->ssl_channel, response->str, response->len);
		} else if (client->handle != NULL) {
			/* Plain connection (should never happen - SSL is mandatory) */
			net_sendbuffer_send(client->handle, response->str, response->len);
		}

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

	/* Send response - MUST use SSL if enabled! */
	if (client->use_ssl && client->ssl_channel != NULL) {
		int ssl_ret;
		ssl_ret = fe_web_ssl_write(client->ssl_channel, response->str, response->len);
		if (ssl_ret < 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] SSL write failed for handshake response",
			          client->id);
			g_free(accept_key);
			g_string_free(response, TRUE);
			return -1;
		}
	} else if (client->handle != NULL) {
		/* Plain connection (should never happen - SSL is mandatory) */
		net_sendbuffer_send(client->handle, response->str, response->len);
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web: [%s] ERROR: Cannot send handshake - no handle!",
		          client->id);
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

	while (client->input_buffer->len > 0) {
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
		if (opcode == 0x1 || opcode == 0x2) { /* Text frame or Binary frame (encrypted) */
			/* Unmask payload if needed */
			if (masked) {
				unmasked_payload = g_malloc(payload_len + 1);
				memcpy(unmasked_payload, payload, payload_len);
				fe_web_websocket_unmask(unmasked_payload, payload_len, mask_key);
				unmasked_payload[payload_len] = '\0';

				/* Binary frame = encrypted data */
				if (opcode == 0x2) {
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
		} else if (opcode == 0x8) { /* Close frame */
			fe_web_close_client(client);
			return;
		} else if (opcode == 0x9) { /* Ping frame */
			/* Send pong - MUST use SSL if enabled! */
			guchar *pong_frame;
			gsize pong_len;
			pong_frame = fe_web_websocket_create_frame(0xA, payload, payload_len, &pong_len);

			if (client->use_ssl && client->ssl_channel != NULL) {
				/* Send through SSL */
				fe_web_ssl_write(client->ssl_channel, (const char *)pong_frame, pong_len);
			} else {
				/* Plain connection (should never happen - SSL is mandatory) */
				net_sendbuffer_send(client->handle, (const char *)pong_frame, pong_len);
			}

			g_free(pong_frame);
		}
		/* Opcode 0xA (pong) - ignore */

		/* Remove processed frame from buffer */
		g_byte_array_remove_range(client->input_buffer, 0, frame_total_len);
	}
}

/* Read data from client */
static void client_input(WEB_CLIENT_REC *client)
{
	guchar buffer[8192];
	int ret;
	GIOChannel *channel;

	if (client == NULL || client->handle == NULL) {
		return;
	}

	/* SSL handshake if needed */
	if (client->use_ssl && client->ssl_channel != NULL && !client->ssl_channel->handshake_done) {
		ret = fe_web_ssl_accept(client->ssl_channel);

		if (ret == 0) {
			/* Need more data */
			return;
		} else if (ret < 0) {
			/* Handshake failed */
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "fe-web: [%s] SSL handshake failed", client->id);
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

		if (fe_web_handle_handshake(client, (const char *)client->input_buffer->data)) {
			/* Handshake complete - send auth_ok */
			WEB_MESSAGE_REC *msg;

			msg = fe_web_message_new(WEB_MSG_AUTH_OK);
			msg->id = fe_web_generate_message_id();
			fe_web_send_message(client, msg);
			fe_web_message_free(msg);

			client->authenticated = TRUE;

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
	handle = net_accept(listen_channel, &ip, &port);
	if (handle == NULL) {
		return;
	}

	/* Increase TCP send buffer to 2MB for large state dumps */
	{
		int fd = g_io_channel_unix_get_fd(handle);
		int bufsize = 2 * 1024 * 1024; /* 2MB */
		if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) < 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP,
			          "fe-web: Warning: Failed to set SO_SNDBUF to %d bytes: %s",
			          bufsize, strerror(errno));
		}
	}

	/* Get address string */
	net_ip2host(&ip, host);
	addr = g_strdup_printf("%s:%d", host, port);

	/* Create client record */
	client = fe_web_client_create(g_io_channel_unix_get_fd(handle), addr);

	/* Create send buffer */
	sendbuf = net_sendbuffer_create(handle, 0);
	client->handle = sendbuf;

	/* SSL is ALWAYS enabled - no option to disable */
	client->ssl_channel = fe_web_ssl_channel_create(handle);
	client->use_ssl = TRUE;

	/* Encryption is ALWAYS enabled - no option to disable */
	client->encryption_enabled = TRUE;

	/* Add input handler */
	client->recv_tag = i_input_add(handle, I_INPUT_READ,
	                               (GInputFunction) client_input, client);

	g_free(addr);
}

/* Initialize server */
void fe_web_server_init(void)
{
	IPADDR *bind_ip;
	const char *bind_addr;
	const char *password;
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
		          "fe-web: Example: /SET fe_web_password $(openssl rand -base64 32)");
		return;
	}

	/* SECURITY: Verify SSL is initialized */
	if (!fe_web_ssl_is_enabled()) {
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

	/* Close listening socket */
	if (listen_tag != -1) {
		g_source_remove(listen_tag);
		listen_tag = -1;
	}

	if (listen_channel != NULL) {
		net_disconnect(listen_channel);
		listen_channel = NULL;
	}

	listen_port = -1;

	printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
	          "fe-web: WebSocket server stopped");
}
