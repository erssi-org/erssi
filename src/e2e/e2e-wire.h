#ifndef IRSSI_E2E_WIRE_H
#define IRSSI_E2E_WIRE_H

/*
 RPE2E v1.0 messages, byte-compatible with repartee and rpe2e.pl:

   +RPE2E01 <msgid hex16> <ts> <part>/<total> <nonce b64>:<ciphertext b64>

 one encrypted PRIVMSG per chunk of at most 180 plaintext bytes, 16 chunks
 at most, each chunk decryptable on its own; and the key exchange as CTCP
 in a NOTICE:

   RPEE2E KEYREQ v=1 c=<ctx> p=<b64u32> e=<b64u32> n=<b64u16> s=<b64u64>
   RPEE2E KEYRSP v=1 c=<ctx> p=... e=... wn=<b64u24> w=<b64u> n=... s=...
   RPEE2E REKEY  v=1 (the same fields as KEYRSP)

 Everything here is pure: no irssi, no keyring.
*/

#include <glib.h>
#include <stddef.h>

#define E2E_PROTO             "RPE2E01"
#define E2E_WIRE_PREFIX       "+RPE2E01"
#define E2E_CTCP_TAG          "RPEE2E"
#define E2E_MAX_CHUNKS        16
#define E2E_MAX_PT_PER_CHUNK  180
#define E2E_TS_TOLERANCE      300

/* base64 (standard alphabet with padding / URL-safe without), strict */
char *e2e_b64_encode(const unsigned char *data, size_t len);
char *e2e_b64url_encode(const unsigned char *data, size_t len);
unsigned char *e2e_b64_decode(const char *text, size_t *len);
unsigned char *e2e_b64url_decode(const char *text, size_t *len);
char *e2e_hex_encode(const unsigned char *data, size_t len);
/* exactly len bytes from 2 * len hex digits */
gboolean e2e_hex_decode(const char *hex, unsigned char *out, size_t len);

typedef struct {
	unsigned char msgid[8];
	gint64 ts;
	int part, total;
	unsigned char nonce[24];
	unsigned char *ct;	/* ciphertext || tag */
	size_t ctlen;
} E2E_WIRE;

/* NULL when the line is not a well-formed wire message */
E2E_WIRE *e2e_wire_parse(const char *line);
void e2e_wire_free(E2E_WIRE *wire);
char *e2e_wire_encode(const E2E_WIRE *wire);
GByteArray *e2e_aad(const char *ctx, const unsigned char msgid[8], gint64 ts,
                    int part, int total);

/* Splits on UTF-8 boundaries into pieces of at most budget bytes. NULL
   (with *error set) for an empty text, more than E2E_MAX_CHUNKS pieces, or
   a character larger than the budget. */
GPtrArray *e2e_split_plaintext(const char *text, size_t budget, const char **error);
/* the wire lines of one message under key for context ctx */
GPtrArray *e2e_encrypt_plain(const unsigned char key[32], const char *ctx,
                             const char *plain, gint64 ts, const char **error);
/* the same for a CTCP frame: a long \001ACTION ...\001 becomes several
   ACTION frames, any other CTCP must fit one chunk */
GPtrArray *e2e_encrypt_ctcp(const unsigned char key[32], const char *ctx,
                            const char *frame, gint64 ts, const char **error);
/* the plaintext bytes (NUL-terminated, *len without it), or NULL */
char *e2e_wire_decrypt(const E2E_WIRE *wire, const unsigned char key[32],
                       const char *ctx, size_t *len);
/* valid UTF-8, invalid bytes replaced by U+FFFD */
char *e2e_utf8_clean(const char *bytes, size_t len);

typedef enum {
	E2E_HS_NONE,
	E2E_HS_KEYREQ,
	E2E_HS_KEYRSP,
	E2E_HS_REKEY
} E2E_HS_TYPE;

typedef struct {
	E2E_HS_TYPE type;
	char *channel;
	unsigned char pub[32];		/* Ed25519 identity of the sender */
	unsigned char eph[32];		/* X25519 ephemeral public key */
	unsigned char nonce[16];
	unsigned char sig[64];
	unsigned char wrap_nonce[24];	/* KEYRSP / REKEY */
	unsigned char *wrap_ct;
	size_t wrap_ctlen;
} E2E_HANDSHAKE;

/* "RPEE2E KEYREQ ..." etc. by its first two words */
E2E_HS_TYPE e2e_handshake_type(const char *body);
E2E_HANDSHAKE *e2e_handshake_parse(const char *body, E2E_HS_TYPE type);
/* without the \001 framing */
char *e2e_handshake_encode(const E2E_HANDSHAKE *hs);
gboolean e2e_handshake_sign(E2E_HANDSHAKE *hs, const unsigned char sk[64]);
gboolean e2e_handshake_verify(const E2E_HANDSHAKE *hs);
void e2e_handshake_free(E2E_HANDSHAKE *hs);

/* channel name: starts with # & ! or + */
gboolean e2e_is_channel(const char *name);
/* every channel reading of a possibly STATUSMSG-prefixed target, the most
   stripped first; empty when it is no channel */
char **e2e_channel_readings(const char *target);
/* ".cmd" / "!cmd": a letter right after the prefix, one line */
gboolean e2e_is_bot_command(const char *body);
gboolean e2e_is_ctcp(const char *body);
gboolean e2e_is_action(const char *body);
/* [@tags ]PRIVMSG <target> [:]<text>, the command in any case */
gboolean e2e_parse_privmsg_line(const char *line, char **tags, char **target, char **body);

#endif
