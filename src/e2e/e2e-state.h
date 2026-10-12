#ifndef IRSSI_E2E_STATE_H
#define IRSSI_E2E_STATE_H

/*
 The E2E state of a window and the key requests that wait for
 /e2e accept - for the statusbar, /e2e pending, /e2e list, "e2e state".

 Everything here is pure (no irssi) and only READS the keyring: no
 legacy row is taken over, nothing is created or changed.

 A waiting request is a pending_inbound row "<handle>|<ctx>" with the
 members "handle" and "channel" (e2e_handle_keyreq and rpe2e.pl write
 both). A row counts only when its key is exactly "<handle>|<channel>":
 that is the row /e2e accept <handle> acts on in the window of
 <channel>; any other row is not shown and never picked. A request for
 a query counts only when its context is "@<handle>" - the sender's own
 query; one for another person's query is damaged (e2e_pending_damaged).
*/

#include <glib.h>

#include "e2e-json.h"
#include "e2e-scope.h"

/* the "Pending key exchange" notice for one request of one network and
   context comes at most once in this many seconds */
#define E2E_PENDING_NOTICE_INTERVAL 300
/* at most this many notice stamps are kept (the oldest go first) */
#define E2E_PENDING_NOTICE_MAX 256

typedef enum {
	E2E_WIN_OFF,
	E2E_WIN_ON,		/* E2E on, a key from someone to read */
	E2E_WIN_WAITING,	/* E2E on, no key from anyone yet */
	E2E_WIN_BLOCKED,	/* not sent at all: an old entry of unknown
				   network, or the query's nick has E2E on at
				   another (or an unknown) address */
	E2E_WIN_ERROR		/* the keyring cannot be read */
} E2E_WIN_STATE;

typedef struct {
	char *nick;		/* sender_nick, "" when the row has none */
	char *handle;
	char *ctx;		/* the storage context (the row's "channel") */
	char fp16[17];		/* start of the fingerprint of its key, "" if damaged */
	gint64 received_at;
} E2E_PENDING;

typedef struct {
	E2E_WIN_STATE state;
	char *ctx;		/* the storage context, NULL when none */
	char *mode;		/* the config's mode, else "normal" */
	guint sessions;		/* trusted sessions with a real key */
	GPtrArray *pending;	/* E2E_PENDING *, oldest first; never NULL */
} E2E_WIN_INFO;

/*
 The state of a window:
  - !kr_ok                                          ERROR
  - wire NULL (a query, address unknown): BLOCKED when E2E is on for nick
    at some address (e2e_dm_configs_of), else OFF
  - wire an old entry of unknown network (BLOCKED)  BLOCKED
  - E2E off there: BLOCKED in a query (nick) whose nick has E2E on at
    another address (the gate refuses), else OFF
  - E2E on: ON with sessions > 0, else WAITING. sessions: the trusted
    incoming rows with a real key - of a channel "*|<ctx>", of a query
    "<peer>|<own ctx>" (own_wire "@<own>", NULL while unknown: none).
 ctx as /e2e commands find it (e2e_ctx_find); pending: the waiting
 requests of ctx. wire: a channel name or "@<peer>"; nick: the query's
 nick (NULL in a channel).
*/
void e2e_window_info(const E2E_JSON *kr, gboolean kr_ok, const E2E_NET *net, const char *wire,
                     const char *own_wire, const char *nick, E2E_WIN_INFO *out);
void e2e_window_info_clear(E2E_WIN_INFO *info);

void e2e_pending_free(E2E_PENDING *p);
/* every waiting request (all networks), oldest first: E2E_PENDING * */
GPtrArray *e2e_pending_all(const E2E_JSON *kr);
/* the damaged waiting requests, oldest first: a query's request whose
   context is not "@" + its own address (rpe2e.pl 0.2.0 stores such
   rows). Never shown as waiting, never picked; listed to be told. */
GPtrArray *e2e_pending_damaged(const E2E_JSON *kr);
/* the waiting requests of the storage context ctx, oldest first */
GPtrArray *e2e_pending_of(const E2E_JSON *kr, const char *ctx);
/*
 The handle of the only waiting request of ctx that matches who: its
 handle is who, or - who holding no '@' - its sender_nick and who are the
 same under map (e2e_casefold); who NULL or "" matches every one.
 *count (if not NULL): how many match. NULL unless exactly one does - a
 nick that names requests of two addresses picks neither. A damaged
 request (e2e_pending_damaged) is never one.
 WARNING: the handle points into kr - into the row that e2e_accept()
 and /e2e decline remove: copy it (g_strdup) before changing kr.
*/
const char *e2e_pending_handle(const E2E_JSON *kr, const char *ctx, const char *who,
                               E2E_CASEMAP map, guint *count);

/* "off" "on" "waiting" "blocked" "error" */
const char *e2e_win_state_name(E2E_WIN_STATE state);
/*
 The "e2e" JSON object of a window ("e2e state"), version 1, UTF-8:
   {"v":1,"state":..,"network":..,"context":..,"mode":..,"sessions":..,
    "pending":[{"nick":..,"handle":..,"fingerprint":..,"received_at":..}]}
 context is the bare name (e2e_ctx_wire); network and context null when
 unknown. Strings: invalid UTF-8 as U+FFFD, '"' '\' and control
 characters escaped (\u00XX), everything else as it is.
*/
char *e2e_window_info_json(const E2E_WIN_INFO *info, const char *network);

/* "42s", "5m", "3h", "2d": an age for the user */
char *e2e_age_text(gint64 seconds);

/* the pending notice limiter: TRUE at most once per
   E2E_PENDING_NOTICE_INTERVAL for key; stamps (e2e_stamps_new()) holds
   at most E2E_PENDING_NOTICE_MAX keys - expired ones, then the oldest,
   are dropped */
gboolean e2e_pending_notice_allow(GHashTable *stamps, const char *key);

/* "e2e" is one of the modules of the autoload_modules setting (words
   split at spaces and commas, as irssi splits it; "e2e:<sub>" counts; the
   case counts, as in the module file name irssi builds from it):
   otherwise a restart or /upgrade does not load the module again */
gboolean e2e_autoload_listed(const char *autoload_modules);
/* the channels and queries with E2E on (every network, old entries too) */
guint e2e_enabled_count(const E2E_JSON *kr);

#endif
