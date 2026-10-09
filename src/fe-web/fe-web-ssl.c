/*
 * fe-web-ssl.c : SSL/TLS support for fe-web WebSocket server
 *
 * Provides encryption for WebSocket connections (wss://) using
 * auto-generated self-signed certificates.
 */

#include "module.h"
#include "fe-web-ssl.h"

#include <irssi/src/core/settings.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/core/levels.h>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Global SSL context */
SSL_CTX *fe_web_ssl_ctx = NULL;

/* Global certificate and key (in memory) */
static X509 *server_cert = NULL;
static EVP_PKEY *server_key = NULL;

/* Generate RSA key pair (2048-bit) using modern OpenSSL 3.0 API */
static EVP_PKEY *generate_rsa_key(void)
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *ctx;

	/* Create context for RSA key generation */
	ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
	if (!ctx) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to create EVP_PKEY_CTX");
		return NULL;
	}

	/* Initialize key generation */
	if (EVP_PKEY_keygen_init(ctx) <= 0) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to initialize key generation");
		EVP_PKEY_CTX_free(ctx);
		return NULL;
	}

	/* Set RSA key size to 2048 bits */
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to set key size");
		EVP_PKEY_CTX_free(ctx);
		return NULL;
	}

	/* Generate the key */
	if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to generate RSA key");
		EVP_PKEY_CTX_free(ctx);
		return NULL;
	}

	EVP_PKEY_CTX_free(ctx);
	return pkey;
}

/* Generate self-signed X.509 certificate */
static X509 *generate_self_signed_cert(EVP_PKEY *pkey)
{
	X509 *x509;
	X509_NAME *name;
	ASN1_INTEGER *serial;

	x509 = X509_new();
	if (!x509) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to create X509 structure");
		return NULL;
	}

	/* Set version to X509v3 */
	X509_set_version(x509, 2);

	/* Set serial number */
	serial = ASN1_INTEGER_new();
	ASN1_INTEGER_set(serial, 1);
	X509_set_serialNumber(x509, serial);
	ASN1_INTEGER_free(serial);

	/* Valid for 10 years */
	X509_gmtime_adj(X509_get_notBefore(x509), 0);
	X509_gmtime_adj(X509_get_notAfter(x509), 315360000L); /* 10 years */

	/* Set public key */
	X509_set_pubkey(x509, pkey);

	/* Set subject name */
	name = X509_get_subject_name(x509);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
	                            (unsigned char *)"irssi-fe-web", -1, -1, 0);
	X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
	                            (unsigned char *)"irssi", -1, -1, 0);

	/* Self-signed: issuer = subject */
	X509_set_issuer_name(x509, name);

	/* Names a client checks when it pins this certificate (the web
	 * client connects to 127.0.0.1) */
	{
		X509V3_CTX v3ctx;
		X509_EXTENSION *ext;

		X509V3_set_ctx_nodb(&v3ctx);
		X509V3_set_ctx(&v3ctx, x509, x509, NULL, NULL, 0);
		ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_subject_alt_name,
		                          "IP:127.0.0.1,IP:::1,DNS:localhost");
		if (ext != NULL) {
			X509_add_ext(x509, ext, -1);
			X509_EXTENSION_free(ext);
		}
	}

	/* Sign certificate with our key */
	if (!X509_sign(x509, pkey, EVP_sha256())) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to sign certificate");
		X509_free(x509);
		return NULL;
	}

	return x509;
}

/* The key and certificate are kept in the irssi directory, so the
 * certificate stays the same across restarts and a web client can pin it
 * instead of accepting any certificate from whatever listens on the port. */
#define FE_WEB_KEY_FILE "fe-web-key.pem"
#define FE_WEB_CERT_FILE "fe-web-cert.pem"

static char *ssl_file_path(const char *name)
{
	return g_strdup_printf("%s/%s", get_irssi_dir(), name);
}

static gboolean load_key_and_cert(void)
{
	char *key_path, *cert_path;
	FILE *f;
	EVP_PKEY *pkey = NULL;
	X509 *x509 = NULL;
	time_t tomorrow;
	struct stat st;
	int fd;

	key_path = ssl_file_path(FE_WEB_KEY_FILE);
	cert_path = ssl_file_path(FE_WEB_CERT_FILE);

	fd = open(key_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd >= 0 && (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
	                (st.st_mode & 077) != 0)) {
		/* someone else may have read or replaced it: not used */
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "fe-web-ssl: %s can be read by other users or is not yours - "
		          "making a new key",
		          key_path);
		close(fd);
		fd = -1;
	}
	if (fd >= 0) {
		if ((f = fdopen(fd, "r")) != NULL) {
			pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
			fclose(f);
		} else {
			close(fd);
		}
	}
	if ((f = fopen(cert_path, "r")) != NULL) {
		x509 = PEM_read_X509(f, NULL, NULL, NULL);
		fclose(f);
	}
	g_free(key_path);
	g_free(cert_path);

	/* unusable (missing, damaged, other key, expiring within a day):
	 * a new pair is generated */
	tomorrow = time(NULL) + 86400;
	if (pkey == NULL || x509 == NULL || !X509_check_private_key(x509, pkey) ||
	    X509_cmp_time(X509_get0_notAfter(x509), &tomorrow) <= 0) {
		EVP_PKEY_free(pkey);
		X509_free(x509);
		ERR_clear_error();
		return FALSE;
	}

	server_key = pkey;
	server_cert = x509;
	return TRUE;
}

static gboolean write_pem_file(const char *name, EVP_PKEY *pkey, X509 *x509)
{
	char *path, *tmp_path;
	FILE *f;
	int fd, ok;

	path = ssl_file_path(name);
	tmp_path = g_strconcat(path, ".tmp", NULL);
	unlink(tmp_path);
	fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	f = fd < 0 ? NULL : fdopen(fd, "w");
	if (f == NULL) {
		if (fd >= 0)
			close(fd);
		g_free(path);
		g_free(tmp_path);
		return FALSE;
	}
	ok = pkey != NULL ? PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL) :
	                    PEM_write_X509(f, x509);
	ok = fclose(f) == 0 && ok;
	if (ok)
		ok = rename(tmp_path, path) == 0;
	else
		unlink(tmp_path);
	g_free(path);
	g_free(tmp_path);
	return ok;
}

static void save_key_and_cert(void)
{
	if (!write_pem_file(FE_WEB_KEY_FILE, server_key, NULL) ||
	    !write_pem_file(FE_WEB_CERT_FILE, NULL, server_cert)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Could not save the certificate in %s: %s - "
		          "a new one is made at every start", get_irssi_dir(),
		          g_strerror(errno));
	}
}

/* Initialize SSL subsystem: the library only - the key and certificate are
 * loaded or made when the server starts (fe_web_ssl_prepare), so an erssi
 * without fe-web never writes a key into ~/.erssi */
void fe_web_ssl_init(void)
{
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
}

/* The TLS context with the stored (or a new) key and certificate; made once,
 * at the first server start. Returns TRUE when TLS can be used. */
int fe_web_ssl_prepare(void)
{
	if (fe_web_ssl_ctx != NULL)
		return TRUE;

	if (load_key_and_cert())
		goto have_cert;

	/* Generate RSA key */
	server_key = generate_rsa_key();
	if (!server_key) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to generate RSA key - SSL disabled");
		return FALSE;
	}

	/* Generate self-signed certificate */
	server_cert = generate_self_signed_cert(server_key);
	if (!server_cert) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to generate certificate - SSL disabled");
		EVP_PKEY_free(server_key);
		server_key = NULL;
		return FALSE;
	}
	save_key_and_cert();

have_cert:
	/* Create SSL context */
	fe_web_ssl_ctx = SSL_CTX_new(TLS_server_method());
	if (!fe_web_ssl_ctx) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to create SSL context");
		X509_free(server_cert);
		EVP_PKEY_free(server_key);
		server_cert = NULL;
		server_key = NULL;
		return FALSE;
	}

	/* Enforce TLS 1.2 minimum (required by modern clients like cloudflared) */
	if (!SSL_CTX_set_min_proto_version(fe_web_ssl_ctx, TLS1_2_VERSION)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to set minimum TLS version to 1.2");
		SSL_CTX_free(fe_web_ssl_ctx);
		X509_free(server_cert);
		EVP_PKEY_free(server_key);
		fe_web_ssl_ctx = NULL;
		server_cert = NULL;
		server_key = NULL;
		return FALSE;
	}

	/* Writes to a slow client are queued and retried from the queue
	 * (fe_web_client_send_raw): a record may be written in parts, and the
	 * retry after WANT_WRITE comes from a buffer that may have moved and
	 * grown since */
	SSL_CTX_set_mode(fe_web_ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE |
	                                 SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	/* Use generated certificate and key */
	if (!SSL_CTX_use_certificate(fe_web_ssl_ctx, server_cert)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to use certificate");
		SSL_CTX_free(fe_web_ssl_ctx);
		X509_free(server_cert);
		EVP_PKEY_free(server_key);
		fe_web_ssl_ctx = NULL;
		server_cert = NULL;
		server_key = NULL;
		return FALSE;
	}

	if (!SSL_CTX_use_PrivateKey(fe_web_ssl_ctx, server_key)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to use private key");
		SSL_CTX_free(fe_web_ssl_ctx);
		X509_free(server_cert);
		EVP_PKEY_free(server_key);
		fe_web_ssl_ctx = NULL;
		server_cert = NULL;
		server_key = NULL;
		return FALSE;
	}

	/* Verify key matches certificate */
	if (!SSL_CTX_check_private_key(fe_web_ssl_ctx)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Private key does not match certificate");
		SSL_CTX_free(fe_web_ssl_ctx);
		X509_free(server_cert);
		EVP_PKEY_free(server_key);
		fe_web_ssl_ctx = NULL;
		server_cert = NULL;
		server_key = NULL;
		return FALSE;
	}
	return TRUE;
}

/* Cleanup SSL subsystem */
void fe_web_ssl_deinit(void)
{
	if (fe_web_ssl_ctx) {
		SSL_CTX_free(fe_web_ssl_ctx);
		fe_web_ssl_ctx = NULL;
	}

	if (server_cert) {
		X509_free(server_cert);
		server_cert = NULL;
	}

	if (server_key) {
		EVP_PKEY_free(server_key);
		server_key = NULL;
	}

	ERR_free_strings();
	EVP_cleanup();
}

/* Create SSL channel from plain GIOChannel */
FE_WEB_SSL_CHANNEL *fe_web_ssl_channel_create(GIOChannel *plain_channel)
{
	FE_WEB_SSL_CHANNEL *ssl_chan;
	int fd;

	if (!fe_web_ssl_ctx) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL context not initialized");
		return NULL;
	}

	ssl_chan = g_new0(FE_WEB_SSL_CHANNEL, 1);
	ssl_chan->plain_channel = plain_channel;
	ssl_chan->fd = g_io_channel_unix_get_fd(plain_channel);
	ssl_chan->ssl_enabled = FALSE;
	ssl_chan->handshake_done = FALSE;

	/* Create SSL object */
	ssl_chan->ssl = SSL_new(fe_web_ssl_ctx);
	if (!ssl_chan->ssl) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to create SSL object");
		g_free(ssl_chan);
		return NULL;
	}

	/* Set file descriptor */
	fd = ssl_chan->fd;
	if (!SSL_set_fd(ssl_chan->ssl, fd)) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: Failed to set SSL fd");
		SSL_free(ssl_chan->ssl);
		g_free(ssl_chan);
		return NULL;
	}

	/* Set to server mode */
	SSL_set_accept_state(ssl_chan->ssl);

	/* Set non-blocking */
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	ssl_chan->ssl_enabled = TRUE;

	return ssl_chan;
}

/* Free SSL channel */
void fe_web_ssl_channel_free(FE_WEB_SSL_CHANNEL *ssl_chan)
{
	if (!ssl_chan) {
		return;
	}

	if (ssl_chan->ssl) {
		/* SSL_shutdown after a fatal error or EOF is forbidden by OpenSSL
		 * and only queues more errors. */
		if (ssl_chan->handshake_done && !ssl_chan->failed)
			SSL_shutdown(ssl_chan->ssl);
		SSL_free(ssl_chan->ssl);
		/* the error queue is per thread and shared with irssi's own TLS */
		ERR_clear_error();
	}

	g_free(ssl_chan);
}

/* Perform SSL accept handshake (non-blocking) */
int fe_web_ssl_accept(FE_WEB_SSL_CHANNEL *ssl_chan)
{
	int ret;
	int ssl_err;

	if (!ssl_chan || !ssl_chan->ssl) {
		return -1;
	}

	if (ssl_chan->handshake_done) {
		return 1; /* Already done */
	}

	ret = SSL_accept(ssl_chan->ssl);
	if (ret == 1) {
		/* Handshake successful */
		ssl_chan->handshake_done = TRUE;
		return 1;
	}

	ssl_err = SSL_get_error(ssl_chan->ssl, ret);
	if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
		/* Need more data - not an error */
		return 0;
	}

	/* Real error - reported by the caller */
	ERR_clear_error();
	return -1;
}

/* Read data from SSL channel */
int fe_web_ssl_read(FE_WEB_SSL_CHANNEL *ssl_chan, char *buf, int len)
{
	int ret;
	int ssl_err;
	unsigned long err_code;
	char err_buf[256];

	if (!ssl_chan || !ssl_chan->ssl || !ssl_chan->handshake_done) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL_read called with invalid state (ssl_chan=%p, ssl=%p, handshake_done=%d)",
		          ssl_chan, ssl_chan ? ssl_chan->ssl : NULL, ssl_chan ? ssl_chan->handshake_done : 0);
		return -1;
	}

	/* errno and the OpenSSL error queue must describe THIS call only */
	errno = 0;
	ERR_clear_error();
	ret = SSL_read(ssl_chan->ssl, buf, len);

	if (ret > 0) {
		return ret; /* Success */
	}

	ssl_err = SSL_get_error(ssl_chan->ssl, ret);

	if (ssl_err == SSL_ERROR_WANT_READ) {
		return -2; /* Need more data */
	}

	if (ssl_err == SSL_ERROR_WANT_WRITE) {
		return -2; /* Need to write */
	}

	if (ssl_err == SSL_ERROR_ZERO_RETURN) {
		return 0; /* Connection closed */
	}

	/* The client went away without a TLS close_notify (killed process,
	 * dropped network, WebSocket terminate()). That is an ordinary
	 * disconnect, not an error worth a line in the Notices window:
	 * OpenSSL 3 reports it as SSL_R_UNEXPECTED_EOF_WHILE_READING,
	 * OpenSSL 1.1 as SSL_ERROR_SYSCALL with ret 0 (errno untouched), and
	 * a reset connection gives ECONNRESET. */
	if (ssl_err == SSL_ERROR_SYSCALL &&
	    (ret == 0 || errno == 0 || errno == ECONNRESET)) {
		ssl_chan->failed = 1;
		ERR_clear_error();
		return 0;
	}

#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
	if (ssl_err == SSL_ERROR_SSL &&
	    ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
		ssl_chan->failed = 1;
		ERR_clear_error();
		return 0;
	}
#endif

	if (ssl_err == SSL_ERROR_SYSCALL) {
		ssl_chan->failed = 1;
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL_ERROR_SYSCALL - system call error (errno=%d: %s)",
		          errno, strerror(errno));
		ERR_clear_error();
		return -1;
	}

	if (ssl_err == SSL_ERROR_SSL) {
		ssl_chan->failed = 1;
		err_code = ERR_peek_last_error();
		ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL_ERROR_SSL - protocol error: %s", err_buf);
		ERR_clear_error();
		return -1;
	}

	/* Real error */
	printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
	          "fe-web-ssl: Unknown SSL error %d", ssl_err);
	return -1;
}

/* Write data to SSL channel */
int fe_web_ssl_write(FE_WEB_SSL_CHANNEL *ssl_chan, const char *data, int len)
{
	int ret;
	int ssl_err;
	unsigned long err_code;
	char err_buf[256];

	if (!ssl_chan || !ssl_chan->ssl || !ssl_chan->handshake_done) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL_write called with invalid state");
		return -1;
	}

	errno = 0;
	ERR_clear_error();
	ret = SSL_write(ssl_chan->ssl, data, len);

	if (ret > 0) {
		return ret; /* Success */
	}

	ssl_err = SSL_get_error(ssl_chan->ssl, ret);

	if (ssl_err == SSL_ERROR_WANT_WRITE) {
		return FE_WEB_SSL_WANT_WRITE; /* socket full: retry when writable */
	}

	if (ssl_err == SSL_ERROR_WANT_READ) {
		return FE_WEB_SSL_WANT_READ; /* retry after the next read */
	}

	/* the client went away: an ordinary disconnect, as on the read side */
	if (ssl_err == SSL_ERROR_ZERO_RETURN ||
	    (ssl_err == SSL_ERROR_SYSCALL &&
	     (errno == 0 || errno == EPIPE || errno == ECONNRESET))) {
		ssl_chan->failed = 1;
		ERR_clear_error();
		return -1;
	}

	if (ssl_err == SSL_ERROR_SYSCALL) {
		ssl_chan->failed = 1;
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL_ERROR_SYSCALL - system call error (errno=%d: %s)",
		          errno, strerror(errno));
		ERR_clear_error();
		return -1;
	}

	if (ssl_err == SSL_ERROR_SSL) {
		ssl_chan->failed = 1;
		err_code = ERR_peek_last_error();
		ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
		printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
		          "fe-web-ssl: SSL_ERROR_SSL - protocol error: %s", err_buf);
		ERR_clear_error();
		return -1;
	}

	/* Real error */
	ssl_chan->failed = 1;
	printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
	          "fe-web-ssl: Unknown SSL write error %d", ssl_err);
	ERR_clear_error();
	return -1;
}

/* Check if SSL is enabled globally */
int fe_web_ssl_is_enabled(void)
{
	/* SSL is always enabled - just check if context is initialized */
	return fe_web_ssl_ctx != NULL;
}

