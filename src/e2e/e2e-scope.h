#ifndef IRSSI_E2E_SCOPE_H
#define IRSSI_E2E_SCOPE_H

/*
 The network scope of a context, and CASEMAPPING folding.

 A context ("#chan", "@ident@host") on the wire is the bare name. In the
 keyring it is stored scoped to the network it belongs to:

   "<label>\x1f<wire ctx>"

 The label is the server's chatnet name if it has one, otherwise the
 server tag. C0 controls, DEL, C1 controls (U+0080-U+009F), '|' and every
 byte that is not part of valid UTF-8 become '_', so a label is valid
 UTF-8 and never holds the separator. A key without the separator is a
 legacy (unscoped) row, from rpe2e.pl or from before the scope: it matches
 any network. The wire (c= field, AAD, HKDF info, signatures) always uses
 the bare context, e2e_ctx_wire().

 A wire context that itself holds 0x1F cannot be told apart from a scoped
 one, so it is refused: e2e_ctx_scoped() and e2e_ctx_rescope() return NULL
 for it, and the callers refuse such a context (fail closed). A legacy
 row whose name holds 0x1F reads as scoped; callers flag such rows rather
 than reinterpret them.

 CASEMAPPING: ascii, rfc1459 and strict-rfc1459 are folded exactly, and
 Latin-1 bytes as the ircu family folds them; no value means rfc1459
 (before the server named it or ended its MOTD: E2E_CASEMAP_UNICODE,
 nothing is known);
 rfc7613, rfc8265 and utf8 get the superset fold E2E_CASEMAP_UNICODE,
 which can only equate more names than the server does, never fewer;
 every other value (unknown, empty) gets the same fold as
 E2E_CASEMAP_UNKNOWN, where only ASCII names are decided - it may be a
 byte code page such as cp1251.

 Old (unscoped) rows: a keyring of rpe2e.pl, or of the module before the
 scope, holds bare contexts. e2e_ctx_lookup() decides what a wire context
 is on a network. While erssi uses one network (E2E_NET.legacy_ok) old
 rows are LEGACY: e2e_ctx_find() uses them where they are (incoming
 traffic, commands), and only the user's own encrypted line takes them
 over, e2e_ctx_resolve() / e2e_adopt_legacy(), which marks the name as
 that network's (a stranger's KEYREQ or wire must not bind it).
 On several networks an old context is BLOCKED - the gate refuses it,
 nothing is decrypted or answered with it - until /e2e adopt binds it to
 one network, or /e2e off writes a scoped config for this one; a name
 another network took over is BLOCKED the same way.

 Everything here is pure: no irssi; the lookups read and change an
 in-memory keyring only. Strings returned as char * are newly allocated
 (g_free).
*/

#include <glib.h>

#include "e2e-json.h"

#define E2E_SCOPE_SEP '\x1f'

typedef enum {
	E2E_CASEMAP_RFC1459,
	E2E_CASEMAP_STRICT_RFC1459,
	E2E_CASEMAP_ASCII,
	E2E_CASEMAP_UNICODE,
	E2E_CASEMAP_UNKNOWN	/* a value erssi does not know: UNICODE's fold,
				   non-ASCII names undecided */
} E2E_CASEMAP;

typedef struct {
	char *label;		/* NULL when the server has no chatnet and no tag */
	E2E_CASEMAP casemap;
	gboolean legacy_ok;	/* whether unscoped rows may be used here */
} E2E_NET;

/* label from chatnet (if non-empty) or tag (if non-empty), else NULL;
   casemap from the CASEMAPPING value (NULL when the server sent none) */
void e2e_net_init(E2E_NET *net, const char *chatnet, const char *tag, const char *casemapping,
                  gboolean legacy_ok);
/* frees the label; safe to call twice */
void e2e_net_clear(E2E_NET *net);

/* ASCII case-insensitive; NULL -> RFC1459; utf8, rfc7613, rfc8265 ->
   UNICODE; anything else -> UNKNOWN */
E2E_CASEMAP e2e_casemap_parse(const char *value);
/* the map of a server: e2e_casemap_parse(value) once it named its
   CASEMAPPING (value) or ended its MOTD without one (motd_done: it sends
   none, rfc1459); UNICODE before */
E2E_CASEMAP e2e_casemap_of_server(const char *value, gboolean motd_done);
/*
 The folded form of name under map (NULL for NULL):
  - ASCII: A-Z -> a-z;
  - STRICT_RFC1459: ASCII and [ ] \ -> { } |;
  - RFC1459: STRICT_RFC1459 and ~ -> ^;
  - the three exact maps also fold the Latin-1 bytes C0-DE to E0-FE (but
    not D7), as servers of the ircu family do; continuation bytes
    (80-BF) stay, so two names in valid UTF-8 never meet that way;
  - UNKNOWN: as UNICODE;
  - UNICODE, pure ASCII: the RFC1459 fold (what the steps below give).
  - UNICODE, valid UTF-8, in this order:
      1. drop the invisible characters (general category Cf, variation
         selectors U+FE00-U+FE0F and U+E0100-U+E01EF, the Mongolian free
         variation selectors U+180B-U+180D and U+180F, the Khmer inherent
         vowels U+17B4 and U+17B5, U+034F, the Hangul fillers U+115F,
         U+1160, U+3164, U+FFA0), as NFKC_Casefold does;
      2. NFD, full case folding, NFKD, full case folding, NFKD: Unicode's
         compatibility caseless match (D146);
      3. drop the run of combining marks (g_unichar_ismark) directly
         before and directly after every U+03B9: U+0345 folds to that iota,
         and servers that case fold before normalising leave the marks on
         the iota where D146 leaves them on the letter;
      4. the Turkish i: U+0131 becomes i, and a U+0307 in the run of
         combining marks directly after an i is dropped;
      5. NFKC, for one representative;
      6. the RFC1459 specials.
    Full case folding is g_utf8_casefold() followed by a fix for GLib's
    Cherokee: a code point whose fold is a single other code point folding
    back to it becomes the smaller of the two (CaseFolding.txt's target).
    The result is valid UTF-8 and holds none of the characters of step 1.
  - UNICODE, invalid UTF-8: the byte-wise RFC1459 fold, Latin-1 bytes
    too.
 fold(fold(x)) == fold(x) for the exact maps and for valid UTF-8; not
 always for invalid UTF-8 under UNICODE, where the Latin-1 byte fold can
 make it valid UTF-8, which the second fold then takes the Unicode way:
 "#\xcf\xbc\xa1" folds to "#\xef\xbc\xa1" (U+FF21), and that folds to
 "#a". Compare fold(a) with fold(b), never a fold with a name folded
 twice.
 Step 4 covers servers that map case per code point (towlower, Go's
 unicode.ToLower, an upper-case irccmp), which take U+0130 to plain i and
 U+0131 to I: dotted, dotless and plain i fold to one letter (U+1D6A4
 too, through NFKD).

 Never persist the output of e2e_casefold() (it may change with the
 Unicode tables or this fold): persist the original name and fold both
 sides at comparison time.
*/
char *e2e_casefold(const char *name, E2E_CASEMAP map);
/* e2e_casefold(), and *exact (if not NULL) says whether the fold
   decides the name: FALSE when UNICODE fell back to the byte-wise RFC1459
   fold (invalid UTF-8, or GLib refused to normalise), for a name with an
   unassigned code point (a server with newer tables may fold it any
   way), for any non-ASCII name under UNKNOWN, and for a NULL name; TRUE
   otherwise, the exact maps always */
char *e2e_casefold_ex(const char *name, E2E_CASEMAP map, gboolean *exact);
/* what every server of map takes for the same name, at least: the exact
   maps' fold without the Latin-1 bytes; for UNKNOWN the ASCII letters
   only (a byte code page may fold any other byte its own way, or not at
   all); for UNICODE what every one of these takes for one name: a per
   code point server (simple lower case, g_unichar_tolower: towlower,
   Go), whole-string lower case with GLib's Final_Sigma and with
   Unicode's (its Cased and Case_Ignorable approximated with GLib),
   whole-string upper case, and a PRECIS / utf8 server (NFKC, then full
   case folding) - no rfc1459 specials, nothing dropped, no
   normalisation but in the last - and NULL for a name that is not valid
   UTF-8. Two names that differ here may be two channels to the server
   although e2e_casefold() joins them.
   Never persisted, as e2e_casefold(). */
char *e2e_casefold_narrow(const char *name, E2E_CASEMAP map);


/* "<label>\x1f<wire>"; a bare copy when net is NULL; NULL when net has no
   label (never a bare key in its place), and for a NULL wire or one that
   holds 0x1F */
char *e2e_ctx_scoped(const E2E_NET *net, const char *wire);
/* wire scoped to the same network as like (a bare copy if like is bare or
   NULL); NULL for a NULL wire or one that holds 0x1F */
char *e2e_ctx_rescope(const char *like, const char *wire);
/* the part after the first 0x1F, or ctx itself; points into ctx */
const char *e2e_ctx_wire(const char *ctx);
/* the label of a scoped ctx; NULL when unscoped */
char *e2e_ctx_network(const char *ctx);
/* TRUE when ctx is scoped to net's label, or unscoped (a legacy row); a
   scoped ctx is on no net without a label, and a NULL ctx on none */
gboolean e2e_ctx_on_net(const char *ctx, const E2E_NET *net);
/* "IRCnet:#chan" for a scoped ctx, a bare one as it is; the rest as
   e2e_text_display() shows it (a further 0x1F as '?'); NULL for NULL */
char *e2e_ctx_display(const char *ctx);
/* text with each control character as one '?': C0 (formatting codes),
   DEL, C1 (U+0080-U+009F, and a byte 0x80-0x9F that is not part of
   UTF-8), the bidi and format controls U+061C, U+200E, U+200F,
   U+202A-U+202E, U+2066-U+2069; anything else as it is. NULL for NULL */
char *e2e_text_display(const char *text);

/* ---- the keyring: legacy (unscoped) rows ---- */

typedef enum {
	E2E_CTX_NEW,		/* nothing stored for it on this network yet */
	E2E_CTX_SCOPED,		/* rows stored for this network */
	E2E_CTX_LEGACY,		/* only old unscoped rows, which may be used here */
	E2E_CTX_BLOCKED		/* old rows of unknown network, or no scoped name */
} E2E_CTX_KIND;

/*
 What wire (a bare "#chan" or "@ident@host") is on net. A context EXISTS
 when channels[ctx] or outgoing[ctx] is there, an incoming key is
 "<handle>|<ctx>" or a pending key (a request of ours) is "<ctx>|<handle>"
 or "<ctx>"; the handle of a key is its row's "handle" member, else
 everything up to the first '|' (incoming) or after the last '|'
 (pending), so "#a" is never a row of "#a|b".

   the bare wire exists, net->legacy_ok     LEGACY,  *ctx = wire
   else scoped "<label>\x1f<wire>" exists   SCOPED,  *ctx = scoped
   else the bare wire exists                BLOCKED, *ctx = wire
   else adopted by another network          BLOCKED, *ctx = wire
   else                                     NEW,     *ctx = scoped

 "Adopted by another network": a channels or outgoing row
 "<other label>\x1f<wire>" with the E2E_ADOPTED_MEMBER marker, and no row
 of this network for wire. It also keeps LEGACY from taking newer old
 rows of that name over here. A pending row without a "handle" member
 belongs to the context of its "channel" member.
   wire holds 0x1F, or net has no label     BLOCKED, *ctx = wire
   net NULL (unscoped, as before)           SCOPED if wire exists, else
                                            NEW;     *ctx = wire
   wire NULL                                BLOCKED, *ctx = NULL

 On the only network (legacy_ok) old rows are taken over even next to
 scoped ones, which win row by row (e2e_adopt_legacy): one scoped row of
 a context (a request of ours) must not hide the old sessions of others.
 On several networks a scoped row hides the old ones; they stay.

 A wire holding 0x1F cannot be scoped, and an old row under that name
 reads as another network's (or as one of a network named like its first
 part): BLOCKED whether a row exists or not, so callers never encrypt
 under it. *ctx is g_free'd by the caller.
*/
E2E_CTX_KIND e2e_ctx_lookup(const E2E_JSON *kr, const E2E_NET *net, const char *wire, char **ctx);
/* the storage context of wire on net: e2e_ctx_lookup(), and for LEGACY the
   old rows are taken over (e2e_adopt_legacy(), *changed set: the caller
   saves). NULL for BLOCKED (nothing changed). changed may be NULL. */
char *e2e_ctx_resolve(E2E_JSON *kr, const E2E_NET *net, const char *wire, gboolean *changed);
/* the storage context of wire on net WITHOUT taking anything over: for
   LEGACY the bare name (the old rows used where they are) - the scoped
   name when this network has rows of its own as well -, NULL for
   BLOCKED. For everything that is not the user's own outgoing line:
   incoming messages and handshakes, commands. */
char *e2e_ctx_find(const E2E_JSON *kr, const E2E_NET *net, const char *wire);
/*
 Moves every bare row of wire to "<label>\x1f<wire>": channels, outgoing,
 accepted "*|w", incoming "*|w", seen_rekeys "*|w", pending_inbound
 "*|w", pending "w|*" and "w", outgoing_recipients "w|*" (the rows'
 "channel" member too, when it names wire), pending_trust_change[] with
 channel w. A scoped row that already exists wins and the bare one is
 dropped (seen_rekeys: the used nonces of both are kept). The scoped
 channels and outgoing rows of wire get E2E_ADOPTED_MEMBER = label when
 any old row was found. Returns the rows moved; 0 for a label or wire
 that cannot make a scoped name.
*/
guint e2e_adopt_legacy(E2E_JSON *kr, const char *label, const char *wire);
/* With "@<peer>" (a DM config) taken over by the user's own DM line: the
   old rows of peer under any old "@<own>" - its sessions to us
   "<peer>|@x", their used REKEY nonces "<fp>|@x" and our requests to it
   "@x|<peer>" - become "<label>\x1f@x" too. The rows of other peers
   there stay old. An existing scoped row wins. Returns rows moved. */
guint e2e_adopt_legacy_peer_sessions(E2E_JSON *kr, const char *label, const char *peer);

/* the member of a scoped config or key row ("channels", "outgoing") whose
   old rows were taken over: the label. While one exists, the same wire
   name on any other network without rows of its own is BLOCKED, not NEW.
   Writers of those rows keep it. */
#define E2E_ADOPTED_MEMBER "adopted"

typedef enum {
	E2E_ADOPT_MOVED,	/* old rows of the name are this network's now */
	E2E_ADOPT_OWN,		/* no old rows: another network took the name over,
				   this one got a config of its own (E2E on) */
	E2E_ADOPT_ALREADY,	/* this network has a config of the name: nothing changed */
	E2E_ADOPT_NONE,		/* no old rows of the name: nothing changed */
	E2E_ADOPT_UNSCOPABLE	/* the name cannot be kept per network (0x1F, no
				   label): nothing changed */
} E2E_ADOPT_RESULT;

/*
 /e2e adopt: the user binds wire to net, on any number of networks.
  - net has a config of wire ("<label>\x1f<wire>" in channels): ALREADY,
    the old rows stay;
  - old rows of wire: e2e_adopt_legacy(); for a DM ("@<peer>") also the
    peer's sessions to us, its used nonces and our requests to it under
    every old "@<own>" (e2e_adopt_legacy_peer_sessions) - not those of
    other peers: MOVED, *moved the rows;
  - none, but another network took the name over (BLOCKED here): a
    config of this network, E2E on in normal mode ("nick" for a DM): OWN.
    The other network's rows are never touched or copied; networks
    other than net stay BLOCKED;
  - else NONE.
 nick: the query's nick for a DM (stored with an OWN config), or NULL.
*/
E2E_ADOPT_RESULT e2e_adopt_explicit(E2E_JSON *kr, const E2E_NET *net, const char *wire,
                                    const char *nick, guint *moved);

/* some channels, outgoing or incoming key holds 0x1F: the keyring has
   rows of the network scope, which rpe2e.pl cannot see */
gboolean e2e_keyring_has_scoped(const E2E_JSON *kr);

/* what the module says when rpe2e.pl is loaded on such a keyring */
#define E2E_SCRIPT_SCOPED_WARN "rpe2e.pl does not know the per-network entries of this keyring (written by the e2e module): the script does not encrypt the conversations set up there, so messages to them are NOT sent while it is loaded. Unload the script to use them (/e2e export keeps a copy)"

#endif
