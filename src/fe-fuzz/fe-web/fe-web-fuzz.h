#ifndef IRSSI_FE_FUZZ_FE_WEB_FE_WEB_FUZZ_H
#define IRSSI_FE_FUZZ_FE_WEB_FE_WEB_FUZZ_H

/* Shared setup for the fe-web fuzz targets.
 *
 * A web client is driven exactly as fe-web drives it: the bytes go into
 * one end of a socketpair and the real client_input_once() of
 * fe-web-server.c reads and handles them from the other end (plain socket
 * instead of TLS - the TLS layer is OpenSSL's, fe-web parses what comes out
 * of it). Everything fe-web sends back is read and dropped. */

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

#include <irssi/src/fe-web/fe-web.h>

/* password the fuzz targets configure as fe_web_password */
#define FE_WEB_FUZZ_PASSWORD "fuzz-password"

/* input is cut into separate reads at this marker */
#define FE_WEB_FUZZ_SPLIT "@@READ@@"

/* Initialize irssi core, irc, fe-common and the parts of fe-web the
 * targets use. Commands sent by web clients are swallowed, never run, and
 * irssi's home is a fresh temporary directory. */
void fe_web_fuzz_init(gboolean with_irc_signals);

/* New web client on a socketpair; logged in already if 'logged_in' */
WEB_CLIENT_REC *fe_web_fuzz_client_new(gboolean logged_in);

/* Send data from the browser side, as one read */
void fe_web_fuzz_client_send(WEB_CLIENT_REC *client, const guchar *data, gsize len);

/* Send data cut into reads at FE_WEB_FUZZ_SPLIT */
void fe_web_fuzz_client_send_split(WEB_CLIENT_REC *client, const guchar *data, gsize len);

/* Is the client still connected (fe-web closes clients on bad input) */
gboolean fe_web_fuzz_client_alive(WEB_CLIENT_REC *client);

/* Close the client if fe-web did not already */
void fe_web_fuzz_client_free(WEB_CLIENT_REC *client);

/* Forget login failures so every input gets its password checked */
void fe_web_fuzz_reset_login_state(void);

/* Remove networks and servers that web clients added */
void fe_web_fuzz_reset_setup(void);

/* fe_web_verify_password() of fe-web-server.c, on a NUL-terminated
 * request */
int fe_web_fuzz_verify_password(const char *request);

/* the libFuzzer entry points every target defines */
int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#endif
