#ifndef IRSSI_E2E_KEYRING_H
#define IRSSI_E2E_KEYRING_H

/*
 keyring.json on disk - the file and layout of rpe2e.pl 0.2.0 (repartee):

   { "identity": { "pk", "sk", "fp", "created_at" },
     "peers": { <fp hex>: { "pk", "last_handle", "last_nick", "first_seen",
                            "last_seen", "status" } },
     "outgoing": { <ctx>: { "sk", "created_at", "pending_rotation" } },
     "incoming": { "<handle>|<ctx>": { "fp", "sk", "status", "created_at" } },
     "channels": { <ctx>: { "enabled", "mode" } },
     "pending": { "<ctx>|<handle>": { "eph_sk", "handle", "channel", "created_at" } },
     "autotrust": [ { "scope", "handle_pattern", "created_at" } ],
     "outgoing_recipients": { "<ctx>|<handle>": { "channel", "handle",
                                                  "fingerprint", "first_sent_at" } },
     "pending_inbound": { "<handle>|<ctx>": { ... the KEYREQ ... } },
     "pending_trust_change": [ { "handle", "channel", "change", ... } ] }

 Keys are base64; <ctx> is a channel name or "@ident@host" for a query.

 The same safety as rpe2e.pl: a file that exists but cannot be read is
 never replaced (the outbound gate refuses to send instead); a file that
 is not JSON is moved aside to keyring.json.corrupt-<time>; a write goes to
 a new 0600 file (O_EXCL), is fsync'ed and renamed over, then the
 directory is fsync'ed. Unlike rpe2e.pl, a keyring moved aside is not
 replaced by an empty one: while keyring.json is missing and a
 keyring.json.corrupt-* exists, the state is LOST (nothing is saved, the
 gate refuses) until e2e_keyring_start_fresh (/e2e reset).

 Members of this client only, kept by rpe2e.pl as unknown members:
     "accepted": { "<handle>|<ctx>": { "fp", "accepted_at", "by" } },
     "seen_rekeys": { "<fp>|<ctx>": { <REKEY nonce b64>: <seen at> } }
*/

#include "e2e-json.h"

#define E2E_LOST_HINT "E2E state is LOST: nothing is sent to any channel or query (except bot commands) and nothing is saved until you run /e2e reset (a new keyring and identity; E2E is then off everywhere until /e2e on)"

typedef enum {
	E2E_LOAD_OK,		/* read - or absent: an empty keyring */
	E2E_LOAD_READ_ERROR,	/* exists but cannot be read, or is no keyring */
	E2E_LOAD_PARSE_ERROR,	/* not valid JSON */
	E2E_LOAD_LOST		/* absent, but a corrupt one was moved aside and
				   no new one started (e2e_keyring_start_fresh) */
} E2E_LOAD_STATUS;

E2E_JSON *e2e_keyring_new(void);
/* always returns a keyring (empty unless E2E_LOAD_OK); *error on failure */
E2E_JSON *e2e_keyring_load_checked(const char *path, E2E_LOAD_STATUS *status, char **error);
/* load_keyring of rpe2e.pl: a corrupt file is moved aside first; *notice
   is the message for the user, NULL when there is none */
E2E_JSON *e2e_keyring_load(const char *path, char **notice);
/* the same with the status: E2E_LOAD_LOST once the file was moved aside */
E2E_JSON *e2e_keyring_load_status(const char *path, E2E_LOAD_STATUS *status, char **notice);
/* keyring.json is missing because a corrupt one was moved aside */
gboolean e2e_keyring_lost(const char *path);
gboolean e2e_keyring_save(const char *path, const E2E_JSON *kr, char **error);
/* /e2e reset: kr becomes the keyring while there is none (E2E_LOAD_LOST) */
gboolean e2e_keyring_start_fresh(const char *path, const E2E_JSON *kr, char **error);
/* canonical text, to compare before / after */
char *e2e_keyring_snapshot(const E2E_JSON *kr);
/* a 0600 copy keyring.json.bak-<time>: its path, "" without a keyring,
   NULL on error */
char *e2e_keyring_backup(const char *path, char **error);
/* a new file, 0600, never over an existing file or through a symlink */
gboolean e2e_write_new_file(const char *path, const char *data, size_t len, char **error);

#endif
