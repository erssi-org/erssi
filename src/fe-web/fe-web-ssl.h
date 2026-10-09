#ifndef FE_WEB_SSL_H
#define FE_WEB_SSL_H

#include <glib.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/* SSL channel wrapper - wraps GIOChannel with SSL */
struct _FE_WEB_SSL_CHANNEL {
	GIOChannel *plain_channel;  /* Original GIOChannel from net_accept() */
	SSL *ssl;                   /* OpenSSL SSL connection object */
	int fd;                     /* File descriptor */
	unsigned int ssl_enabled:1; /* Whether SSL is active */
	unsigned int handshake_done:1; /* Whether SSL handshake completed */
	unsigned int failed:1;      /* Fatal error/EOF seen: no SSL_shutdown allowed */
};

typedef struct _FE_WEB_SSL_CHANNEL FE_WEB_SSL_CHANNEL;

/* Global SSL context (shared by all connections) */
extern SSL_CTX *fe_web_ssl_ctx;

/* Initialize the SSL library (no key or certificate yet) */
void fe_web_ssl_init(void);

/* Load or make the key and self-signed certificate and the TLS context
 * (once, when the server starts); TRUE when TLS can be used */
int fe_web_ssl_prepare(void);

/* Cleanup SSL subsystem */
void fe_web_ssl_deinit(void);

/* Create SSL channel from plain GIOChannel */
FE_WEB_SSL_CHANNEL *fe_web_ssl_channel_create(GIOChannel *plain_channel);

/* Free SSL channel */
void fe_web_ssl_channel_free(FE_WEB_SSL_CHANNEL *ssl_chan);

/* Perform SSL accept handshake (non-blocking) */
/* Returns: 1 = success, 0 = need more data, -1 = error */
int fe_web_ssl_accept(FE_WEB_SSL_CHANNEL *ssl_chan);

/* Read data from SSL channel */
/* Returns: bytes read, 0 = connection closed, -1 = error, -2 = want read */
int fe_web_ssl_read(FE_WEB_SSL_CHANNEL *ssl_chan, char *buf, int len);

/* Write data to SSL channel. The context allows partial writes and a
 * moved buffer: after a WANT result the same bytes must be offered again,
 * from wherever they are now, followed by any number of new ones. */
/* Returns: bytes written (may be fewer than len), -1 = error (or the peer
 * went away), FE_WEB_SSL_WANT_WRITE = socket full, FE_WEB_SSL_WANT_READ =
 * TLS has to read from the peer before it can write */
#define FE_WEB_SSL_WANT_WRITE (-2)
#define FE_WEB_SSL_WANT_READ (-3)
int fe_web_ssl_write(FE_WEB_SSL_CHANNEL *ssl_chan, const char *data, int len);

/* Check if SSL is enabled globally */
int fe_web_ssl_is_enabled(void);

#endif /* FE_WEB_SSL_H */

