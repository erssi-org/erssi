/*
 fe-web-websocket.c : WebSocket protocol implementation (RFC 6455)

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "fe-web.h"

#include <string.h>
#include <glib.h>

/* WebSocket magic GUID for handshake */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xA

/* Compute WebSocket accept key from client key */
char *fe_web_websocket_compute_accept(const char *client_key)
{
	GChecksum *checksum;
	guint8 digest[20];
	gsize digest_len;
	char *accept_key;
	char *concat;

	if (client_key == NULL) {
		return NULL;
	}

	/* Concatenate client key with magic GUID */
	concat = g_strconcat(client_key, WS_GUID, NULL);

	/* Compute SHA-1 */
	digest_len = 20;
	checksum = g_checksum_new(G_CHECKSUM_SHA1);
	g_checksum_update(checksum, (const guchar *)concat, strlen(concat));
	g_checksum_get_digest(checksum, digest, &digest_len);
	g_checksum_free(checksum);

	/* Base64 encode */
	accept_key = g_base64_encode(digest, digest_len);

	g_free(concat);
	return accept_key;
}

/* Parse WebSocket frame header */
int fe_web_websocket_parse_frame(const guchar *data, gsize data_len,
                                  int *fin, int *opcode, int *masked,
                                  guint64 *payload_len, guchar mask_key[4],
                                  const guchar **payload)
{
	gsize header_len;
	guint64 len;
	const guchar *p;

	if (data == NULL || data_len < 2) {
		return -1;
	}

	p = data;

	/* First byte: FIN, RSV, opcode */
	*fin = (*p & 0x80) ? 1 : 0;
	*opcode = *p & 0x0F;
	p++;

	/* Second byte: MASK, payload length */
	*masked = (*p & 0x80) ? 1 : 0;
	len = *p & 0x7F;
	p++;

	/* Extended payload length */
	if (len == 126) {
		if (data_len < 4) {
			return -1;
		}
		len = (p[0] << 8) | p[1];
		p += 2;
	} else if (len == 127) {
		if (data_len < 10) {
			return -1;
		}
		len = ((guint64)p[0] << 56) | ((guint64)p[1] << 48) |
		      ((guint64)p[2] << 40) | ((guint64)p[3] << 32) |
		      ((guint64)p[4] << 24) | ((guint64)p[5] << 16) |
		      ((guint64)p[6] << 8) | (guint64)p[7];
		p += 8;
	}

	*payload_len = len;

	/* Masking key */
	if (*masked) {
		if (p + 4 > data + data_len) {
			return -1;
		}
		memcpy(mask_key, p, 4);
		p += 4;
	}

	header_len = p - data;

	/* Check if we have complete frame */
	if (header_len + len > data_len) {
		return 0; /* Incomplete frame */
	}

	*payload = p;
	return 1; /* Complete frame */
}

/* Unmask WebSocket payload */
void fe_web_websocket_unmask(guchar *payload, guint64 payload_len,
                              const guchar mask_key[4])
{
	guint64 i;

	for (i = 0; i < payload_len; i++) {
		payload[i] ^= mask_key[i % 4];
	}
}

/* Create WebSocket frame (server->client, unmasked) */
guchar *fe_web_websocket_create_frame(int opcode, const guchar *payload,
                                       guint64 payload_len, gsize *frame_len)
{
	guchar *frame;
	gsize header_len;
	guchar *p;

	/* Calculate header length */
	header_len = 2;
	if (payload_len > 65535) {
		header_len += 8;
	} else if (payload_len > 125) {
		header_len += 2;
	}

	/* Allocate frame */
	*frame_len = header_len + payload_len;
	frame = g_malloc(*frame_len);
	p = frame;

	/* First byte: FIN=1, opcode */
	*p = 0x80 | (opcode & 0x0F);
	p++;

	/* Second byte: MASK=0, payload length */
	if (payload_len > 65535) {
		*p = 127;
		p++;
		*p++ = (payload_len >> 56) & 0xFF;
		*p++ = (payload_len >> 48) & 0xFF;
		*p++ = (payload_len >> 40) & 0xFF;
		*p++ = (payload_len >> 32) & 0xFF;
		*p++ = (payload_len >> 24) & 0xFF;
		*p++ = (payload_len >> 16) & 0xFF;
		*p++ = (payload_len >> 8) & 0xFF;
		*p++ = payload_len & 0xFF;
	} else if (payload_len > 125) {
		*p = 126;
		p++;
		*p++ = (payload_len >> 8) & 0xFF;
		*p++ = payload_len & 0xFF;
	} else {
		*p = payload_len & 0x7F;
		p++;
	}

	/* Copy payload */
	if (payload_len > 0 && payload != NULL) {
		memcpy(p, payload, payload_len);
	}

	return frame;
}
