/*
 e2e-core.c : RPE2E end-to-end encryption in erssi - the signal handlers

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The native replacement of the rpe2e.pl script (0.2.0), wire-compatible
 with repartee, rpe2e.pl and the WeeChat rpe2e.py. The hooks are the
 script's:

  - "server outgoing modify": THE outbound gate. irssi emits it for every
    line it is about to write to the socket, so every PRIVMSG - typed
    text, /me, /msg, /say, /quote, scripts, other frontends - passes here.
    On an encrypted context the line is replaced by its ciphertext or
    dropped (the GString emptied: irc_server_send_and_redirect writes
    nothing then); it is never sent in clear text. A buffer holding
    several IRC lines (CR/LF from a script's raw line) is dropped whole
    when one of its PRIVMSGs would need the gate. Only the exact lines of
    ciphertext the gate itself made pass unexamined.
  - The send queue (e2e-outq.h, one per server): an encrypted message is
    several lines - a REKEY NOTICE per recipient when the key changed,
    then the chunks. The first takes the place of the typed line in the
    emission; the others wait in the queue, ahead of everything in it,
    and a pump (every 100 ms) sends them one at a time when irssi's own
    flood rule would send a new command at once (cmds_max_at_once,
    cmd_queue_speed, the delay after a long line; never past a command
    waiting in irssi's queue). A PRIVMSG that comes while lines wait is
    taken out of its emission and queued behind them, not decided yet:
    the pump sends it again, as a new emission, and the gate decides it
    then like any other line (its own chunks then go to the front). So
    the lines of one message leave in order and before anything typed
    after it, and only the gate's own ciphertext or a line on its way to
    the gate ever leaves the queue. The gate never touches irssi's queue
    (irc->cmdqueue). The queue is dropped and wiped, with the count told
    in the status window, on disconnect, /e2e reset, /unload, and when
    rpe2e.pl is loaded (the script encrypts then, the queue is not used).
  - "session save server" (first): /upgrade writes the flood queue to the
    socket directly, past the gate; the module first sends its own queue,
    then those lines through irc_server_send_and_redirect, so the gate
    sees each of them, and the chunks of each right after it.
  - "event privmsg" (first): decryption before irssi splits CTCP/ACTION,
    so an encrypted /me renders as an action; the decrypted text is
    passed on with signal_continue, everything after (fe-common, frontends,
    logs) sees plain text.
  - "ctcp reply" (first): the key exchange NOTICEs (RPEE2E KEYREQ ...).
  - 001 / 311 / JOIN / CHGHOST / 396: our own ident@host as peers see it,
    the context of the DMs we receive.

 If rpe2e.pl is loaded at the same time, both would encrypt and decrypt:
 the module notices the script's /e2e command (a Perl script named rpe2e*
 binding it; another script binding /e2e does not count) and stays
 inactive until the script is unloaded.
*/

#include "module.h"
#include "e2e-core.h"
#include "e2e-crypto.h"
#include "e2e-formats.h"
#include "e2e-keyring.h"
#include "e2e-outq.h"
#include "e2e-statusbar.h"
#include "e2e-web.h"
#include "e2e-wire.h"

#include <irssi/src/core/channels.h>
#include <irssi/src/core/chatnets.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/modules.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/core/queries.h>
#include <irssi/src/core/servers-reconnect.h>
#include <irssi/src/core/servers-setup.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/irc/core/servers-redirect.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-common/core/window-items.h>
#include <irssi/src/perl/perl-core.h>

#include <gmodule.h>
#include <sys/stat.h>

static char *keyring_dir;
static char *keyring_file;

static GHashTable *keyreq_out;		/* handle -> our last KEYREQ (30 s) */
static GHashTable *keyreq_in;		/* "kind|handle|ctx" -> their last handshake (10 s) */
static GHashTable *own_wait_notice;	/* "tag|nick" -> last "held" notice */
/* "tag|handle|ctx" -> last "Pending key exchange" notice (bounded) */
static GHashTable *pending_notice;
static guint state_idle_id;
static GHashTable *own_handles;		/* server tag -> own ident@host */
/* the exact lines of ciphertext the pump is sending -> how many: these
   pass the gate (any other line, a wire too, is gated) */
static GHashTable *own_lines;

static gint64 lost_notice_at;
/* the keyring file as the module last read or wrote it */
static gboolean seen_stat_valid;
static struct stat seen_stat;

static gboolean script_seen;
static guint script_check_id;
/* rpe2e.pl loaded on a keyring with rows of the network scope: told once
   while it is loaded; the file is read again only when it changed */
static gboolean scoped_warned;
static gboolean scoped_stat_valid;
static struct stat scoped_stat;

/* ---- printing ---- */

void e2e_print_item(void *item, const char *fmt, ...)
{
	WI_ITEM_REC *wi = item;
	va_list va;
	char *text;

	va_start(va, fmt);
	text = g_strdup_vprintf(fmt, va);
	va_end(va);
	if (wi != NULL)
		printformat(wi->server, wi->visible_name, MSGLEVEL_CLIENTCRAP, TXT_E2E_MESSAGE, text);
	else
		printformat(NULL, NULL, MSGLEVEL_CLIENTCRAP, TXT_E2E_MESSAGE, text);
	g_free(text);
}

/* ---- loaded for this session only ---- */

/* "gui exit" came (/quit, /upgrade): the module is unloaded with the
   rest of erssi, not on its own */
static gboolean exiting;

/* a warning (client error level) in a window item, NULL: the active one */
static void print_warning(void *item, const char *text)
{
	WI_ITEM_REC *wi = item;

	printformat(wi != NULL ? wi->server : NULL, wi != NULL ? wi->visible_name : NULL,
	            MSGLEVEL_CLIENTERROR, TXT_E2E_MESSAGE, text);
}

static const char *contexts_word(guint n)
{
	return n == 1 ? "channel or query" : "channels or queries";
}

/* the channels and queries with E2E on in the keyring (0 when there is
   none yet); *lost: the keyring is lost (a corrupt one was moved aside) -
   the module refuses every message then, and which targets have E2E on
   is not known */
static guint enabled_contexts(gboolean *lost)
{
	E2E_JSON *kr;
	guint n;

	*lost = FALSE;
	if (keyring_file == NULL)
		return 0;
	if (e2e_keyring_is_lost()) {
		*lost = TRUE;
		return 0;
	}
	if (!g_file_test(keyring_file, G_FILE_TEST_EXISTS))
		return 0;
	kr = e2e_load_full(lost);
	n = *lost ? 0 : e2e_enabled_count(kr);
	e2e_json_free(kr);
	return n;
}

void e2e_warn_session_only(void *item)
{
	const char *list = settings_get_str("autoload_modules");
	char *text;

	if (e2e_autoload_listed(list))
		return;
	text = g_strdup_printf("the e2e module is loaded for this session only: \"e2e\" is not in "
	                       "autoload_modules, so after a restart or /UPGRADE it is not loaded, "
	                       "and messages to channels and queries with E2E on are sent in clear "
	                       "text. To load it at every start: /SET autoload_modules %s%se2e",
	                       list != NULL ? list : "", list != NULL && *list != '\0' ? " " : "");
	print_warning(item, text);
	g_free(text);
}

/* /upgrade starts a process that loads autoload_modules only */
static void sig_command_upgrade(void)
{
	const char *list = settings_get_str("autoload_modules");
	const char *sep = list != NULL && *list != '\0' ? " " : "";
	gboolean lost;
	guint n;
	char *text;

	if (e2e_autoload_listed(list))
		return;
	n = enabled_contexts(&lost);
	if (lost)
		text = g_strdup_printf("/UPGRADE: the new process will not load the e2e module (\"e2e\" is "
		                       "not in autoload_modules), and the keyring is lost: the messages "
		                       "it refuses now will be sent in clear text until /load e2e. To keep "
		                       "it loaded, run /SET autoload_modules %s%se2e before /UPGRADE",
		                       list != NULL ? list : "", sep);
	else if (n > 0)
		text = g_strdup_printf("/UPGRADE: the new process will not load the e2e module (\"e2e\" is "
		                       "not in autoload_modules): messages to the %u %s with E2E on will be "
		                       "sent in clear text until /load e2e. To keep it loaded, run "
		                       "/SET autoload_modules %s%se2e before /UPGRADE",
		                       n, contexts_word(n), list != NULL ? list : "", sep);
	else
		return;
	print_warning(NULL, text);
	g_free(text);
}

static void sig_gui_exit(void)
{
	exiting = TRUE;
}

/* /unload e2e: the gate goes with the module */
static void warn_unload(void)
{
	gboolean lost;
	guint n;
	char *text;

	if (exiting)
		return;
	n = enabled_contexts(&lost);
	if (lost)
		text = g_strdup("the e2e module is unloaded while the keyring is lost (a corrupt one "
		                "was moved aside): it refused every message but bot commands, to keep "
		                "channels and queries with E2E on safe, and every message is now sent in clear text "
		                "(/load e2e, then /e2e reset or /e2e import)");
	else if (n == 0)
		return;
	else if (e2e_native_active())
		text = g_strdup_printf("the e2e module is unloaded: E2E is on for %u %s, and messages "
		                       "there are now sent in clear text (/load e2e encrypts them again)",
		                       n, contexts_word(n));
	else
		text = g_strdup_printf("the e2e module is unloaded: E2E is on for %u %s; rpe2e.pl "
		                       "encrypts only its own entries, messages to channels and queries "
		                       "the module set up per network are now sent in clear text",
		                       n, contexts_word(n));
	print_warning(NULL, text);
	g_free(text);
}

void *e2e_notice_item(SERVER_REC *server, const char *ctx, const char *nick)
{
	const char *wire = e2e_ctx_wire(ctx);
	WI_ITEM_REC *item;

	if (server == NULL)
		return NULL;
	if (wire != NULL && e2e_is_channel(wire)) {
		item = window_item_find(server, wire);
		if (item != NULL)
			return item;
	}
	if (nick != NULL && *nick != '\0')
		return window_item_find(server, nick);
	return NULL;
}

/* rpe2e.pl's RPE2E_DEBUG_BUFFER, as a setting */
void e2e_print_debug(SERVER_REC *server, const char *ctx, const char *nick,
                     const char *fmt, ...)
{
	WI_ITEM_REC *item;
	va_list va;
	char *text;

	if (!settings_get_bool("e2e_debug"))
		return;
	va_start(va, fmt);
	text = g_strdup_vprintf(fmt, va);
	va_end(va);
	item = e2e_notice_item(server, ctx, nick);
	if (item != NULL)
		printformat(item->server, item->visible_name, MSGLEVEL_CLIENTCRAP, TXT_E2E_DEBUG, text);
	else
		printformat(NULL, NULL, MSGLEVEL_CLIENTCRAP, TXT_E2E_DEBUG, text);
	g_free(text);
}

/* ---- rpe2e.pl loaded as well ---- */

/* a Perl script named rpe2e (rpe2e.pl, rpe2e-0.2.0.pl, ...) is loaded:
   perl_scripts of the perl module, looked up at run time - the module
   may not be loaded at all */
static gboolean rpe2e_script_running(void)
{
	MODULE_REC *perl = module_find("perl");
	MODULE_FILE_REC *file = perl != NULL ? module_file_find(perl, "core") : NULL;
	GModule *self = NULL, *gm;
	gpointer sym = NULL;
	gboolean found = FALSE;
	GSList *tmp;

	if (file == NULL)
		return FALSE;
	gm = file->gmodule;
	if (gm == NULL)
		gm = self = g_module_open(NULL, 0);	/* perl built in */
	if (gm != NULL && g_module_symbol(gm, "perl_scripts", &sym) && sym != NULL) {
		for (tmp = *(GSList **) sym; tmp != NULL && !found; tmp = tmp->next) {
			PERL_SCRIPT_REC *script = tmp->data;
			char *name = g_ascii_strdown(script->name != NULL ? script->name : "", -1);

			found = strstr(name, "rpe2e") != NULL;
			g_free(name);
		}
	}
	if (self != NULL)
		g_module_close(self);
	return found;
}

/* the script binds /e2e too. Only rpe2e.pl counts: another script or
   module binding the name must not switch the outbound gate off. */
static gboolean script_loaded(void)
{
	COMMAND_REC *rec = command_find("e2e");
	GSList *tmp;

	if (rec == NULL)
		return FALSE;
	for (tmp = rec->modules; tmp != NULL; tmp = tmp->next) {
		COMMAND_MODULE_REC *modrec = tmp->data;

		if (strcmp(modrec->name, "perl/core") == 0 && modrec->callbacks != NULL)
			return rpe2e_script_running();
	}
	return FALSE;
}

gboolean e2e_same_file(const struct stat *a, const struct stat *b)
{
	return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
	       a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
	       a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

/* rpe2e.pl cannot see rows of the network scope: where only those say E2E
   is on, the script sends in clear text. Said loudly, once while the
   script is loaded; the keyring is read (without side effects) only when
   the file changed since the last look. */
static void warn_scoped_for_script(void)
{
	E2E_LOAD_STATUS status;
	struct stat st;
	E2E_JSON *kr;
	char *error = NULL;

	if (scoped_warned || keyring_file == NULL || stat(keyring_file, &st) != 0)
		return;
	if (scoped_stat_valid && e2e_same_file(&st, &scoped_stat))
		return;
	scoped_stat = st;
	scoped_stat_valid = TRUE;
	kr = e2e_keyring_load_checked(keyring_file, &status, &error);
	if (status == E2E_LOAD_OK && e2e_keyring_has_scoped(kr)) {
		scoped_warned = TRUE;
		printformat(NULL, NULL, MSGLEVEL_CLIENTERROR | MSGLEVEL_HILIGHT, TXT_E2E_MESSAGE,
		            "WARNING: " E2E_SCRIPT_SCOPED_WARN);
	}
	g_free(error);
	e2e_json_free(kr);
}

gboolean e2e_script_loaded(void)
{
	return script_loaded();
}

gboolean e2e_native_active(void)
{
	gboolean script = script_loaded();

	/* the statusbar shows nothing while the script is loaded */
	if (script != script_seen)
		e2e_state_changed();
	if (script && !script_seen) {
		script_seen = TRUE;
		e2e_print_item(NULL, "the rpe2e.pl script is loaded: the native e2e module leaves encryption to it and stays inactive - it only stops messages to conversations that its own per-network entries encrypt, which the script does not know. Unload the script (/script unload rpe2e, and remove it from scripts/autorun) to use the native module - it uses the same keyring, no new keys are needed.");
	} else if (!script && script_seen) {
		script_seen = FALSE;
		e2e_print_item(NULL, "rpe2e.pl is no longer loaded: the native e2e module encrypts now (same keyring)");
	}
	if (script) {
		warn_scoped_for_script();
	} else {
		/* loaded again later: told again */
		scoped_warned = FALSE;
		scoped_stat_valid = FALSE;
	}
	return !script;
}

static void sig_commandlist_new(COMMAND_REC *rec)
{
	if (rec != NULL && g_strcmp0(rec->cmd, "e2e") == 0)
		e2e_native_active();
}

static gboolean script_check(gpointer data)
{
	script_check_id = 0;
	e2e_native_active();
	return FALSE;
}

/* the script's command is unbound while it is being destroyed: look
   once that is over */
static void sig_script_destroyed(void)
{
	if (script_check_id == 0)
		script_check_id = g_idle_add(script_check, NULL);
}

/* ---- networks ---- */

/* the chatnet as configured (its own spelling), else the name given */
static const char *chatnet_name(const char *chatnet)
{
	CHATNET_REC *rec;

	if (chatnet == NULL || *chatnet == '\0')
		return NULL;
	rec = chatnet_find(chatnet);
	return rec != NULL && rec->name != NULL ? rec->name : chatnet;
}

static void add_label(GHashTable *labels, const char *chatnet, const char *tag)
{
	E2E_NET net;

	e2e_net_init(&net, chatnet_name(chatnet), tag, NULL, FALSE);
	/* a server without a name: one of its own */
	g_hash_table_add(labels, net.label != NULL ? net.label : g_strdup("\001"));
	net.label = NULL;
	e2e_net_clear(&net);
}

static gboolean has_live_conn(const char *address, int port)
{
	GSList *lists[2] = { servers, lookup_servers }, *tmp;
	int i;

	for (i = 0; i < 2; i++)
		for (tmp = lists[i]; tmp != NULL; tmp = tmp->next) {
			SERVER_REC *rec = tmp->data;

			if (rec->connrec != NULL && rec->connrec->port == port &&
			    rec->connrec->address != NULL &&
			    g_ascii_strcasecmp(rec->connrec->address, address) == 0)
				return TRUE;
		}
	for (tmp = reconnects; tmp != NULL; tmp = tmp->next) {
		RECONNECT_REC *rec = tmp->data;

		if (rec->conn != NULL && rec->conn->port == port && rec->conn->address != NULL &&
		    g_ascii_strcasecmp(rec->conn->address, address) == 0)
			return TRUE;
	}
	return FALSE;
}

gboolean e2e_legacy_ok(void)
{
	GHashTable *labels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GSList *lists[2] = { servers, lookup_servers }, *tmp;
	gboolean ok;
	int i;

	for (i = 0; i < 2; i++)
		for (tmp = lists[i]; tmp != NULL; tmp = tmp->next) {
			SERVER_REC *rec = tmp->data;

			add_label(labels, rec->connrec != NULL ? rec->connrec->chatnet : NULL, rec->tag);
		}
	/* waiting to reconnect: still a network in use */
	for (tmp = reconnects; tmp != NULL; tmp = tmp->next) {
		RECONNECT_REC *rec = tmp->data;

		if (rec->conn != NULL)
			add_label(labels, rec->conn->chatnet, rec->conn->tag);
	}
	/* will connect: the chatnet, or - without one, the tag is not known
	   yet - the address, unless that server is up already */
	for (tmp = setupservers; tmp != NULL; tmp = tmp->next) {
		SERVER_SETUP_REC *rec = tmp->data;

		if (!rec->autoconnect)
			continue;
		if (rec->chatnet != NULL && *rec->chatnet != '\0')
			add_label(labels, rec->chatnet, NULL);
		else if (rec->address != NULL && !has_live_conn(rec->address, rec->port))
			g_hash_table_add(labels, g_strconcat("\002", rec->address, NULL));
	}
	ok = g_hash_table_size(labels) <= 1;
	g_hash_table_destroy(labels);
	return ok;
}

void e2e_server_net(SERVER_REC *server, E2E_NET *net)
{
	const char *casemapping = NULL;
	gboolean motd_done = FALSE;

	if (IS_IRC_SERVER(server) && IRC_SERVER(server)->isupport != NULL)
		casemapping = g_hash_table_lookup(IRC_SERVER(server)->isupport, "CASEMAPPING");
	/* until the server named its CASEMAPPING (in any 005 line) or ended
	   its MOTD without one, nothing is known: the widest.
	   Both are kept over /upgrade (isupport, motd_got) */
	if (IS_IRC_SERVER(server))
		motd_done = IRC_SERVER(server)->motd_got;
	e2e_net_init(net, server != NULL && server->connrec != NULL ? chatnet_name(server->connrec->chatnet) : NULL,
	             server != NULL ? server->tag : NULL, casemapping, e2e_legacy_ok());
	net->casemap = e2e_casemap_of_server(casemapping, motd_done);
}

/* ---- the keyring ---- */

const char *e2e_keyring_file(void)
{
	return keyring_file;
}

gboolean e2e_keyring_is_lost(void)
{
	return e2e_keyring_lost(keyring_file);
}

/* the file now; changed since the module last read or wrote it (by
   another process, rpe2e.pl): the state of windows may have changed */
static void note_keyring_stat(gboolean ours)
{
	struct stat st;
	gboolean have = keyring_file != NULL && stat(keyring_file, &st) == 0;

	if (!ours && have != seen_stat_valid)
		e2e_state_changed();
	else if (!ours && have && !e2e_same_file(&st, &seen_stat))
		e2e_state_changed();
	seen_stat_valid = have;
	if (have)
		seen_stat = st;
}

E2E_JSON *e2e_load_full(gboolean *lost)
{
	E2E_LOAD_STATUS status;
	char *notice = NULL;
	gboolean was_lost;
	E2E_JSON *kr;

	/* the directory 0700, as rpe2e.pl keeps it */
	chmod(keyring_dir, 0700);
	was_lost = e2e_keyring_lost(keyring_file);
	note_keyring_stat(FALSE);
	kr = e2e_keyring_load_status(keyring_file, &status, &notice);
	if (lost != NULL)
		*lost = status == E2E_LOAD_LOST;
	/* moved aside just now: every window's state is an error now */
	if (status == E2E_LOAD_LOST && !was_lost)
		e2e_state_changed();
	/* moved aside just now: always loud; still lost: once a minute */
	if (notice != NULL && (status != E2E_LOAD_LOST || !was_lost ||
	                       e2e_now() - lost_notice_at >= 60)) {
		if (status == E2E_LOAD_LOST)
			lost_notice_at = e2e_now();
		e2e_print_item(NULL, "%s", notice);
	}
	g_free(notice);
	return kr;
}

E2E_JSON *e2e_load(void)
{
	return e2e_load_full(NULL);
}

gboolean e2e_save(const E2E_JSON *kr)
{
	char *error = NULL;

	if (e2e_keyring_save(keyring_file, kr, &error)) {
		note_keyring_stat(TRUE);
		e2e_state_changed();
		return TRUE;
	}
	e2e_print_item(NULL, "%s", error);
	g_free(error);
	return FALSE;
}

gboolean e2e_ensure_identity(E2E_JSON *kr, E2E_IDENTITY *id)
{
	gboolean created;
	char *error = NULL;

	if (!e2e_identity_get(kr, id, TRUE, &created, &error)) {
		e2e_print_item(NULL, "no identity loaded: %s", error);
		g_free(error);
		return FALSE;
	}
	/* an identity that cannot be stored would be a throw-away key peers
	   pin and then see change */
	if (created && !e2e_save(kr)) {
		e2e_print_item(NULL, "cannot store a new E2E identity in %s", keyring_file);
		e2e_json_set(kr, "identity", e2e_json_new(E2E_JSON_NULL));
		e2e_identity_wipe(id);
		return FALSE;
	}
	return TRUE;
}

GHashTable *e2e_keyreq_out_stamps(void)
{
	return keyreq_out;
}

/* through the flood queue, like every other command: a burst of
   handshakes must not get us disconnected for Excess Flood */
void e2e_send_notice(SERVER_REC *server, const char *nick, const char *body)
{
	char *cmd;

	if (!IS_IRC_SERVER(server))
		return;
	cmd = g_strdup_printf("NOTICE %s :%s", nick, body);
	irc_send_cmd(IRC_SERVER(server), cmd);
	g_free(cmd);
}

/* ---- handles ---- */

char *e2e_resolve_dm_handle(SERVER_REC *server, const E2E_JSON *kr, const char *nick)
{
	const char *cached;
	GSList *tmp;

	/* LIVE first: an open query or a common channel has the peer's
	   current ident@host, which a nick change does not alter (the
	   keyring's last_nick can be stale) */
	if (server != NULL) {
		QUERY_REC *query = query_find(server, nick);

		if (query != NULL && query->address != NULL && *query->address != '\0')
			return g_strdup(query->address);
		for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
			NICK_REC *rec = nicklist_find(tmp->data, nick);

			if (rec != NULL && rec->host != NULL && *rec->host != '\0')
				return g_strdup(rec->host);
		}
	}
	/* NULL: the live handle only (E2E_RESOLVE_FUNC) */
	if (kr == NULL)
		return NULL;
	cached = e2e_find_handle_by_nick(kr, nick);
	return cached != NULL && *cached != '\0' ? g_strdup(cached) : NULL;
}

static char *resolve_cb(const char *nick, const E2E_JSON *kr, void *data)
{
	return e2e_resolve_dm_handle(data, kr, nick);
}

/* Only PREFIX-VISIBLE values are stored - what peers see in our message
   prefix: our JOIN, CHGHOST, RPL_HOSTHIDDEN (396), the self-WHOIS 311 and
   our nick in a nicklist. A self-USERHOST can answer with the real host
   and server->userhost is not updated on CHGHOST, so neither is used. */
static void set_own_handle(SERVER_REC *server, const char *handle)
{
	if (server == NULL || server->tag == NULL || handle == NULL || *handle == '\0')
		return;
	if (g_strcmp0(g_hash_table_lookup(own_handles, server->tag), handle) != 0) {
		g_hash_table_replace(own_handles, g_strdup(server->tag), g_strdup(handle));
		/* a query's sessions are counted under "@<own>" */
		e2e_state_changed();
	}
}

const char *e2e_own_handle(SERVER_REC *server)
{
	const char *handle;
	GSList *tmp;

	if (server == NULL || server->tag == NULL)
		return NULL;
	handle = g_hash_table_lookup(own_handles, server->tag);
	if (handle != NULL)
		return handle;
	if (server->nick != NULL && *server->nick != '\0') {
		for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
			NICK_REC *rec = nicklist_find(tmp->data, server->nick);

			if (rec != NULL && rec->host != NULL && *rec->host != '\0') {
				set_own_handle(server, rec->host);
				return g_hash_table_lookup(own_handles, server->tag);
			}
		}
	}
	return NULL;
}

char *e2e_incoming_ctx_for(SERVER_REC *server, const char *ctx)
{
	const char *own;
	char *wire, *out;

	if (ctx == NULL || *e2e_ctx_wire(ctx) != '@')
		return g_strdup(ctx);
	own = e2e_own_handle(server);
	if (own == NULL || *own == '\0')
		return NULL;
	/* on the network of ctx */
	wire = g_strconcat("@", own, NULL);
	out = e2e_ctx_rescope(ctx, wire);
	g_free(wire);
	return out;
}

/* ---- the state of a window ---- */

static gboolean state_changed_idle(gpointer data)
{
	state_idle_id = 0;
	signal_emit("e2e state changed", 0);
	return FALSE;
}

void e2e_state_changed(void)
{
	if (state_idle_id == 0)
		state_idle_id = g_idle_add(state_changed_idle, NULL);
}

/* e2e_own_handle() without remembering what it finds */
static const char *own_handle_peek(SERVER_REC *server)
{
	const char *handle;
	GSList *tmp;

	if (server == NULL || server->tag == NULL)
		return NULL;
	handle = g_hash_table_lookup(own_handles, server->tag);
	if (handle != NULL || server->nick == NULL || *server->nick == '\0')
		return handle;
	for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
		NICK_REC *rec = nicklist_find(tmp->data, server->nick);

		if (rec != NULL && rec->host != NULL && *rec->host != '\0')
			return rec->host;
	}
	return NULL;
}

/* an item e2e_window_info_with() describes: a channel or a query, with a
   server */
static gboolean info_item(WI_ITEM_REC *item)
{
	return item != NULL && item->server != NULL && item->name != NULL &&
	       (e2e_is_channel(item->name) || IS_QUERY(item));
}

gboolean e2e_window_info_for(SERVER_REC *server, WI_ITEM_REC *item, E2E_WIN_INFO *out)
{
	E2E_LOAD_STATUS status;
	E2E_JSON *kr;
	gboolean ret;
	char *error = NULL;

	memset(out, 0, sizeof(*out));
	/* the item's own network, never another server's (server unused) */
	if (!info_item(item) || keyring_file == NULL)
		return FALSE;
	/* rpe2e.pl's _load_keyring_checked: a damaged file is left alone */
	kr = e2e_keyring_load_checked(keyring_file, &status, &error);
	g_free(error);
	ret = e2e_window_info_with(kr, status == E2E_LOAD_OK, item, out);
	e2e_json_free(kr);
	return ret;
}

gboolean e2e_window_info_with(const E2E_JSON *kr, gboolean kr_ok, WI_ITEM_REC *item,
                              E2E_WIN_INFO *out)
{
	SERVER_REC *server;
	E2E_NET net;
	const char *name, *own, *nick = NULL;
	char *wire = NULL, *own_wire = NULL, *handle;

	memset(out, 0, sizeof(*out));
	if (!info_item(item))
		return FALSE;
	server = item->server;
	name = item->name;
	if (e2e_is_channel(name)) {
		wire = g_strdup(name);
	} else {
		nick = name;
		handle = e2e_resolve_dm_handle(server, kr_ok ? kr : NULL, nick);
		if (handle != NULL)
			wire = g_strconcat("@", handle, NULL);
		g_free(handle);
	}
	own = own_handle_peek(server);
	if (own != NULL && *own != '\0')
		own_wire = g_strconcat("@", own, NULL);
	e2e_server_net(server, &net);
	e2e_window_info(kr, kr_ok, &net, wire, own_wire, nick, out);
	e2e_net_clear(&net);
	g_free(own_wire);
	g_free(wire);
	return TRUE;
}

void e2e_print_pending(SERVER_REC *server, const char *ctx, const char *nick, const char *handle,
                       gboolean have_theirs)
{
	WI_ITEM_REC *item = e2e_notice_item(server, ctx, nick);
	char *dnick = e2e_text_display(nick != NULL ? nick : "");
	char *dhandle = e2e_text_display(handle != NULL ? handle : "");
	char *dctx, *label;
	const char *theirs = have_theirs ? " - you can read them already; they get your key only when you accept" : "";

	if (ctx != NULL && !e2e_is_channel(e2e_ctx_wire(ctx))) {
		/* a query: /e2e accept <ident@host> in any window of the
		   network (in a channel window: when that channel has no
		   request of the address) */
		label = e2e_ctx_network(ctx);
		dctx = e2e_text_display(label != NULL ? label : server != NULL && server->tag != NULL ? server->tag : "?");
		g_free(label);
		printformat(server, item != NULL ? item->visible_name : NULL,
		            MSGLEVEL_CLIENTNOTICE | MSGLEVEL_HILIGHT, TXT_E2E_PENDING_QUERY, dnick, dhandle,
		            dctx, theirs);
	} else {
		dctx = e2e_ctx_display(ctx != NULL ? ctx : "");
		printformat(server, item != NULL ? item->visible_name : NULL,
		            MSGLEVEL_CLIENTNOTICE | MSGLEVEL_HILIGHT, TXT_E2E_PENDING, dnick, dhandle, dctx,
		            theirs);
	}
	g_free(dnick);
	g_free(dhandle);
	g_free(dctx);
}

void e2e_tell_damaged(void *item, const GPtrArray *found)
{
	guint i;

	for (i = 0; found != NULL && i + 1 < found->len; i += 2) {
		char *who = e2e_text_display(g_ptr_array_index(found, i));
		char *where = e2e_ctx_display(g_ptr_array_index(found, i + 1));

		e2e_print_item(item, "a damaged entry: %s was accepted for the query %s of another address (a request rpe2e.pl kept) and may hold your key - it rotates with your next message there, the new one reaches only that query's peer; /e2e decline %s in that query removes the entry",
		               who, where, who);
		g_free(who);
		g_free(where);
	}
}

/* a one-shot WHOIS of ourselves: its 311 has the displayed host, exactly
   what peers see. Redirected, so its reply is not printed. */
static void send_self_whois(SERVER_REC *server)
{
	IRC_SERVER_REC *irc = IRC_SERVER(server);
	char *cmd;

	if (irc == NULL || !irc->connected || irc->nick == NULL || *irc->nick == '\0')
		return;
	server_redirect_event(irc, "whois", 1, irc->nick, -1, NULL,
	                      "event 311", "redir e2e whois user",
	                      "", "event empty", NULL);
	cmd = g_strdup_printf("WHOIS %s", irc->nick);
	irc_send_cmd(irc, cmd);
	g_free(cmd);
}

static gboolean is_own_nick(SERVER_REC *server, const char *nick)
{
	return server != NULL && nick != NULL && server->nick != NULL &&
	       g_ascii_strcasecmp(nick, server->nick) == 0;
}

/* registration complete: the server may give us another ident, host or
   cloak this time */
static void sig_event_001(SERVER_REC *server)
{
	if (!IS_IRC_SERVER(server))
		return;
	if (server->tag != NULL && g_hash_table_remove(own_handles, server->tag))
		e2e_state_changed();
	send_self_whois(server);
}

/* RPL_WHOISUSER: "me nick ident host * :realname" */
static void sig_event_311(SERVER_REC *server, const char *data)
{
	char **f;

	if (!IS_IRC_SERVER(server) || data == NULL)
		return;
	f = g_strsplit(data, " ", 5);
	if (g_strv_length(f) >= 4 && is_own_nick(server, f[1]) && *f[2] != '\0' && *f[3] != '\0') {
		char *handle = g_strconcat(f[2], "@", f[3], NULL);

		set_own_handle(server, handle);
		g_free(handle);
	}
	g_strfreev(f);
}

/* our own JOIN carries our prefix exactly as peers see it */
static void sig_event_join(SERVER_REC *server, const char *data, const char *nick,
                           const char *address)
{
	if (IS_IRC_SERVER(server) && is_own_nick(server, nick) && address != NULL)
		set_own_handle(server, address);
}

/* RPL_HOSTHIDDEN: our new displayed host; the checks of irssi core's
   event_hosthidden */
static void sig_event_396(SERVER_REC *server, const char *data)
{
	const char *p, *own, *at;
	char *host, *handle, *ident;

	if (!IS_IRC_SERVER(server) || data == NULL)
		return;
	p = strchr(data, ' ');
	if (p == NULL)
		return;
	while (*p == ' ')
		p++;
	if (*p == ':')
		p++;
	host = g_strndup(p, strcspn(p, " "));
	if (*host == '\0' || strpbrk(host, "*?!#& \t") != NULL || strchr("@:-", *host) != NULL ||
	    host[strlen(host) - 1] == '-') {
		g_free(host);
		return;
	}
	at = strchr(host, '@');
	if (at != NULL && at > host) {
		set_own_handle(server, host);
	} else {
		own = e2e_own_handle(server);
		at = own != NULL ? strchr(own, '@') : NULL;
		if (at != NULL && at > own) {
			ident = g_strndup(own, at - own);
			handle = g_strconcat(ident, "@", host, NULL);
			set_own_handle(server, handle);
			g_free(handle);
			g_free(ident);
		}
	}
	g_free(host);
}

/* CHGHOST "newuser newhost" of us */
static void sig_event_chghost(SERVER_REC *server, const char *data, const char *nick)
{
	char **f;

	if (!IS_IRC_SERVER(server) || data == NULL || !is_own_nick(server, nick))
		return;
	f = g_strsplit(data, " ", 3);
	if (g_strv_length(f) >= 2) {
		const char *host = *f[1] == ':' ? f[1] + 1 : f[1];

		if (*f[0] != '\0' && *host != '\0') {
			char *handle = g_strconcat(f[0], "@", host, NULL);

			set_own_handle(server, handle);
			g_free(handle);
		}
	}
	g_strfreev(f);
}

/* ---- the outbound gate ---- */

static void own_line_add(const char *line)
{
	int n = GPOINTER_TO_INT(g_hash_table_lookup(own_lines, line));

	g_hash_table_replace(own_lines, g_strdup(line), GINT_TO_POINTER(n + 1));
}

/* TRUE (and counted off) when line is one the pump is sending */
static gboolean own_line_take(const char *line)
{
	int n = GPOINTER_TO_INT(g_hash_table_lookup(own_lines, line));

	if (n <= 0)
		return FALSE;
	if (n == 1)
		g_hash_table_remove(own_lines, line);
	else
		g_hash_table_replace(own_lines, g_strdup(line), GINT_TO_POINTER(n - 1));
	return TRUE;
}

static void gate_refuse(SERVER_REC *server, GString *str, const char *item, const char *msg)
{
	/* irssi already showed the line: say that it was not delivered */
	e2e_print_item(e2e_notice_item(server, item, item),
	               "%s — the message shown above was NOT delivered", msg);
	e2e_wipe(str->str, str->len);
	g_string_truncate(str, 0);
	signal_stop();
}

/* several IRC lines in one buffer (a script's raw line with CR/LF): the
   gate looks at one line, the server splits them */
static gboolean gate_multiline(SERVER_REC *server, GString *str, gsize len)
{
	E2E_LOAD_STATUS status;
	E2E_JSON *kr;
	E2E_NET net;
	char *error = NULL, *msg;

	if (memchr(str->str, '\r', len) == NULL && memchr(str->str, '\n', len) == NULL &&
	    memchr(str->str, '\0', len) == NULL)
		return FALSE;
	kr = e2e_keyring_load_checked(keyring_file, &status, &error);
	g_free(error);
	e2e_server_net(server, &net);
	msg = e2e_gate_multiline(kr, status == E2E_LOAD_OK, &net, str->str, len, resolve_cb, server);
	e2e_net_clear(&net);
	if (msg != NULL) {
		e2e_print_item(NULL, "%s", msg);
		e2e_wipe(str->str, str->len);
		g_string_truncate(str, 0);
		signal_stop();
		g_free(msg);
	}
	e2e_json_free(kr);
	return TRUE;
}

/* how many "server outgoing modify" emissions are running: above 1, a
   handler is sending a line while another is being sent (rpe2e.pl
   sends its chunks with send_raw_now from its gate). Counted by a hook
   ahead of every other, which runs the rest of the emission itself
   (signal_continue), so the count also comes down when a handler stops
   the signal. */
static int outgoing_depth;

/* ---- the send queue (statics; the pump is below the gate) ---- */

static GHashTable *outqs;		/* SERVER_REC * -> E2E_OUTQ * */
static guint pump_tag;
/* The pump is sending a line that has not met the gate yet (PLAIN): the
   emission it causes is the gate's to decide now, not to queue again.
   That emission is the first one at depth pump_depth after the pump
   armed it; the depth hook keeps its depth in pump_emission while it
   runs. Another emission - nested in it (a handler sending), or one
   after it while irc_send_cmd_now() has not returned (a rawlog hook) -
   is not the pump's line and is queued like any other. */
static gboolean in_pump;
static gboolean pump_armed;
static int pump_depth;
static int pump_emission;
/* per running emission (index outgoing_depth): the line as the module's
   gate decided it - the gate ran while the module was active - or NULL.
   The rpe2e.pl guard leaves a line to that gate only when it is still
   that line: a script of rpe2e.pl's name that dies in its gate is
   unloaded at once, and the module, inactive when its gate ran, is
   active by the time the guard runs; a handler between the two may have
   rewritten what the gate let pass */
static GPtrArray *gated_emissions;

static void gated_line_free(gpointer data)
{
	GString *line = data;

	if (line != NULL) {
		e2e_wipe(line->str, line->len);
		g_string_free(line, TRUE);
	}
}

/* the record of the running emission (index outgoing_depth) */
static gpointer *gated_slot(void)
{
	if (gated_emissions == NULL || outgoing_depth <= 0 ||
	    (guint) outgoing_depth >= gated_emissions->len)
		return NULL;
	return &g_ptr_array_index(gated_emissions, outgoing_depth);
}

static void sig_server_outgoing_depth(SERVER_REC *server, GString *str, int crlf)
{
	gpointer *slot;

	outgoing_depth++;
	if (in_pump && pump_armed && outgoing_depth == pump_depth) {
		pump_armed = FALSE;
		pump_emission = outgoing_depth;
	}
	if (gated_emissions != NULL && gated_emissions->len <= (guint) outgoing_depth)
		g_ptr_array_set_size(gated_emissions, outgoing_depth + 1);
	if ((slot = gated_slot()) != NULL) {
		gated_line_free(*slot);
		*slot = NULL;
	}
	signal_continue(3, server, str, GINT_TO_POINTER(crlf));
	/* the copy of a line is plain text: not kept after its emission */
	if ((slot = gated_slot()) != NULL) {
		gated_line_free(*slot);
		*slot = NULL;
	}
	if (pump_emission == outgoing_depth)
		pump_emission = 0;
	outgoing_depth--;
}

/* the module's gate decides str in the running emission */
static void mark_gated(const GString *str)
{
	gpointer *slot = gated_slot();

	if (slot != NULL) {
		gated_line_free(*slot);
		*slot = g_string_new_len(str->str, str->len);
	}
}

/* the module's gate decided str, as it is now, in the running emission;
   FALSE when that is not known (no count: decided again, fail closed) */
static gboolean was_gated(const GString *str)
{
	gpointer *slot = gated_slot();
	const GString *line = slot != NULL ? *slot : NULL;

	return line != NULL && line->len == str->len && memcmp(line->str, str->str, str->len) == 0;
}

/* ---- the send queue ---- */

/* the running emission is the one of the line the pump sends */
static gboolean from_pump(void)
{
	return in_pump && pump_emission > 0 && pump_emission == outgoing_depth;
}

static E2E_OUTQ *queue_of(SERVER_REC *server, gboolean create)
{
	E2E_OUTQ *q = g_hash_table_lookup(outqs, server);

	if (q == NULL && create) {
		q = e2e_outq_new();
		g_hash_table_insert(outqs, server, q);
	}
	return q;
}

static gboolean queue_waiting(SERVER_REC *server)
{
	E2E_OUTQ *q = queue_of(server, FALSE);

	return q != NULL && !e2e_outq_empty(q);
}

/* empties server's queue, wiping its lines; told in its status window,
   with more PRIVMSGs not sent (dropped from irssi's queue) */
static guint drop_queue(SERVER_REC *server, const char *reason, guint more)
{
	E2E_OUTQ *q = queue_of(server, FALSE);
	guint n = 0, plain = 0;
	GString *text;

	if (q != NULL)
		n = e2e_outq_drop(q, &plain);
	if (n + more == 0)
		return 0;
	text = g_string_new(NULL);
	if (n > plain) {
		g_string_printf(text, "%u encrypted line(s) to %s not sent: %s", n - plain,
		                server->tag != NULL ? server->tag : "?", reason);
		if (plain + more > 0)
			g_string_append_printf(text, " (and %u message(s) waiting behind them, not encrypted yet)",
			                       plain + more);
	} else {
		g_string_printf(text, "%u message(s) to %s not sent: %s (not encrypted yet)",
		                plain + more, server->tag != NULL ? server->tag : "?", reason);
	}
	printformat(server, NULL, MSGLEVEL_CLIENTERROR, TXT_E2E_MESSAGE, text->str);
	g_string_free(text, TRUE);
	return n;
}

/* irssi's own rule for sending a command at once (irc_send_cmd(): never
   past a normal command waiting in irssi's queue - so a line of ours
   never overtakes one irssi queued before it). At least one command at
   a time: with cmds_max_at_once 0 irssi still sends its queue one by
   one, the pump must not wait for ever. */
static gboolean flood_allows(IRC_SERVER_REC *s)
{
	return s->connected && !s->connection_lost &&
	       g_slist_length(s->cmdqueue) / 2 <= (guint) s->cmdlater &&
	       g_get_real_time() >= s->wait_cmd &&
	       (s->cmdcount < MAX(s->max_cmds_at_once, 1) || s->cmd_queue_speed <= 0);
}

/* sends the first lines of the queue of s while the flood rule allows
   (ignore_flood: all of them) */
static void pump_server(IRC_SERVER_REC *s, gboolean ignore_flood)
{
	SERVER_REC *server = SERVER(s);
	E2E_OUT_LINE *out;
	E2E_OUTQ *q;

	/* never inside an emission (a line sent there would leave ahead of
	   the one being sent), and one line at a time */
	if (s == NULL || in_pump || outgoing_depth > 0)
		return;
	server_ref(server);
	/* looked up again for every line: a handler may drop or free it */
	while ((q = queue_of(server, FALSE)) != NULL && !e2e_outq_empty(q) &&
	       !server->disconnected && !s->connection_lost) {
		/* rpe2e.pl encrypts now: nothing of the module's queue goes to
		   the script, or out as it is */
		if (!e2e_native_active()) {
			drop_queue(server, "rpe2e.pl is loaded", 0);
			break;
		}
		if (!ignore_flood && !flood_allows(s))
			break;
		out = e2e_outq_pop(q);
		if (out->kind == E2E_OUT_CIPHER) {
			/* the gate's own ciphertext: passes it as it is */
			own_line_add(out->line);
			irc_send_cmd_now(s, out->line);
			/* not taken when it never reached the gate (connection
			   lost, or a handler before it stopped the signal) */
			own_line_take(out->line);
		} else {
			/* not gated yet: the gate decides it now, as any line */
			in_pump = TRUE;
			pump_armed = TRUE;
			pump_depth = outgoing_depth + 1;
			irc_send_cmd_now(s, out->line);
			in_pump = FALSE;
			pump_armed = FALSE;
			pump_emission = 0;
		}
		e2e_out_line_free(out);
	}
	server_unref(server);
}

static gboolean pump_all(gpointer data)
{
	GHashTableIter iter;
	gpointer value;
	GList *keys, *tmp;

	keys = g_hash_table_get_keys(outqs);
	for (tmp = keys; tmp != NULL; tmp = tmp->next)
		if (g_hash_table_contains(outqs, tmp->data))
			pump_server(IRC_SERVER(tmp->data), FALSE);
	g_list_free(keys);
	/* empty queues are not kept */
	g_hash_table_iter_init(&iter, outqs);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		if (e2e_outq_empty(value))
			g_hash_table_iter_remove(&iter);
	if (g_hash_table_size(outqs) > 0)
		return TRUE;
	pump_tag = 0;
	return FALSE;
}

static void start_pump(void)
{
	if (pump_tag == 0)
		pump_tag = g_timeout_add(100, pump_all, NULL);
}

/* the module's gate on the line in str */
static void native_gate(SERVER_REC *server, GString *str)
{
	E2E_GATE_RESULT res;
	E2E_IDENTITY id;
	E2E_LOAD_STATUS status;
	E2E_JSON *kr;
	E2E_NET net;
	GPtrArray *lines;
	char *line, *tags, *target, *body, *error = NULL;
	gboolean have_id = FALSE, created;
	gsize len;
	guint i;

	len = str->len;
	while (len > 0 && (str->str[len - 1] == '\n' || str->str[len - 1] == '\r'))
		len--;
	if (gate_multiline(server, str, len))
		return;
	line = g_strndup(str->str, len);
	/* the ciphertext the pump sends right now */
	if (own_line_take(line)) {
		g_free(line);
		return;
	}
	if (!e2e_parse_privmsg_line(line, &tags, &target, &body)) {
		e2e_wipe(line, strlen(line));
		g_free(line);
		return;
	}
	/* lines wait in the queue: this one goes behind them, and is
	   decided when the pump sends it */
	if (!from_pump() && queue_waiting(server)) {
		e2e_outq_push_back(queue_of(server, FALSE), E2E_OUT_PLAIN, line);
		e2e_wipe(str->str, str->len);
		g_string_truncate(str, 0);
		signal_stop();
		goto out;
	}

	/* rpe2e.pl reads with _load_keyring_checked here: a damaged file is
	   left alone, and the gate refuses. Changed by another process since
	   the module's last look: the statusbar looks again. */
	note_keyring_stat(FALSE);
	kr = e2e_keyring_load_checked(keyring_file, &status, &error);
	g_free(error);
	error = NULL;
	/* only for a REKEY: never created here */
	if (status == E2E_LOAD_OK)
		have_id = e2e_identity_get(kr, &id, FALSE, &created, &error);
	g_free(error);
	/* this network's keys and settings */
	e2e_server_net(server, &net);
	e2e_gate_decide(kr, status == E2E_LOAD_OK, have_id ? &id : NULL, &net, target, body,
	                resolve_cb, server, &res);
	e2e_net_clear(&net);
	/* the corrupt keyring was moved aside: say what to do */
	if (status == E2E_LOAD_LOST && res.action == E2E_GATE_REFUSE) {
		g_free(res.message);
		res.message = g_strdup(E2E_REFUSE_LOST);
	}
	/* a line cut at the server's limit would be garbage: not sent at all
	   (nothing saved either - a rotation stays due) */
	if (res.action == E2E_GATE_CIPHER &&
	    !e2e_gate_lines_fit(&res, tags, target, IRC_SERVER(server)->max_message_len)) {
		e2e_gate_result_clear(&res);
		res.action = E2E_GATE_REFUSE;
		res.message = g_strdup(E2E_REFUSE_TOO_LONG);
	}

	switch (res.action) {
	case E2E_GATE_PASS:
		break;
	case E2E_GATE_BYPASS:
		/* the clear-text bot command goes out unchanged */
		if (res.message != NULL)
			e2e_print_item(e2e_notice_item(server, target, target), "%s", res.message);
		break;
	case E2E_GATE_REFUSE:
		gate_refuse(server, str, target, res.message);
		break;
	case E2E_GATE_CIPHER:
		if (res.save_needed)
			e2e_save(kr);
		for (i = 0; res.warnings != NULL && i < res.warnings->len; i++)
			e2e_print_item(e2e_notice_item(server, res.ctx, target), "%s",
			               (char *) g_ptr_array_index(res.warnings, i));
		lines = g_ptr_array_new_with_free_func(g_free);
		/* a REKEY reaches each recipient before the first chunk under
		   the new key */
		for (i = 0; res.notices != NULL && i + 1 < res.notices->len; i += 2)
			g_ptr_array_add(lines, g_strdup_printf("NOTICE %s :%s",
			                                       (char *) g_ptr_array_index(res.notices, i),
			                                       (char *) g_ptr_array_index(res.notices, i + 1)));
		/* IRCv3 tags of the original line stay on the chunks */
		for (i = 0; i < res.wires->len; i++)
			g_ptr_array_add(lines, g_strdup_printf("%sPRIVMSG %s :%s", tags, target,
			                                       (char *) g_ptr_array_index(res.wires, i)));
		/* the first goes out now, in the place of the plain text; the
		   others wait for the flood limit, ahead of all that waits */
		e2e_wipe(str->str, str->len);
		g_string_truncate(str, 0);
		if (lines->len > 0)
			g_string_assign(str, g_ptr_array_index(lines, 0));
		if (lines->len > 1) {
			g_ptr_array_remove_index(lines, 0);
			e2e_outq_push_front(queue_of(server, TRUE), E2E_OUT_CIPHER, lines);
			start_pump();
		}
		g_ptr_array_unref(lines);
		signal_stop();
		break;
	}
	e2e_gate_result_clear(&res);
	if (have_id)
		e2e_identity_wipe(&id);
	e2e_json_free(kr);
out:
	e2e_wipe(line, strlen(line));
	g_free(line);
	g_free(tags);
	g_free(target);
	if (body != NULL)
		e2e_wipe(body, strlen(body));
	g_free(body);
}

static void sig_server_outgoing(SERVER_REC *server, GString *str, int crlf)
{
	if (!IS_IRC_SERVER(server) || str == NULL || !e2e_native_active())
		return;
	mark_gated(str);
	native_gate(server, str);
}

/* while rpe2e.pl is loaded, after the script's gate (last, so
   it sees only what the script let pass unchanged): a PRIVMSG that the
   module's per-network entries - which the script cannot see - would
   encrypt or refuse is not sent in clear text either. Decided on those
   entries only, dry: the keyring is not changed */
static void sig_server_outgoing_script(SERVER_REC *server, GString *str, int crlf)
{
	E2E_LOAD_STATUS status;
	E2E_GATE_ACTION action;
	E2E_JSON *kr;
	E2E_NET net;
	char *error = NULL, *msg, *target;

	if (!IS_IRC_SERVER(server) || str == NULL ||
	    !e2e_buffer_has_privmsg(str->str, str->len))
		return;
	if (e2e_native_active()) {
		/* the module's gate let this line pass - unless it ran while the
		   module was inactive (rpe2e.pl, loaded, died in its gate since),
		   or a handler rewrote the line after it: then no gate has seen
		   it as it is, the module's decides it now */
		if (!was_gated(str)) {
			mark_gated(str);
			native_gate(server, str);
		}
		return;
	}
	/* an unreadable keyring may hold such an entry: fail closed */
	kr = e2e_keyring_load_checked(keyring_file, &status, &error);
	g_free(error);
	e2e_server_net(server, &net);
	action = e2e_script_guard(kr, status == E2E_LOAD_OK, status == E2E_LOAD_LOST,
	                          outgoing_depth > 1, &net, str->str, str->len, resolve_cb, server,
	                          &msg, &target);
	e2e_net_clear(&net);
	if (action == E2E_GATE_REFUSE) {
		e2e_print_item(e2e_notice_item(server, target, target), "%s", msg);
		g_string_truncate(str, 0);
		signal_stop();
	} else if (msg != NULL) {
		e2e_print_item(e2e_notice_item(server, target, target), "%s", msg);
	}
	g_free(msg);
	g_free(target);
	e2e_json_free(kr);
}

/* /upgrade: irc-session.c writes the flood queue straight to the socket,
   past "server outgoing modify". The module's own queue first (its lines
   are older than what irssi still holds), then irssi's the normal way
   (in order), so every queued PRIVMSG meets the gate, each followed by
   its chunks; redirected commands stay for irssi, which does not send
   those either. */
static void sig_session_save_server(SERVER_REC *server)
{
	IRC_SERVER_REC *irc = IRC_SERVER(server);
	GSList *tmp, *next;

	if (irc == NULL)
		return;
	pump_server(irc, TRUE);
	for (tmp = irc->cmdqueue; tmp != NULL; tmp = next) {
		GSList *link = tmp->next;
		char *cmd = tmp->data;
		GString *str;

		next = link->next;
		if (link->data != NULL)
			continue;
		irc->cmdqueue = g_slist_delete_link(irc->cmdqueue, link);
		irc->cmdqueue = g_slist_delete_link(irc->cmdqueue, tmp);
		irc->cmdcount--;
		str = g_string_new(cmd);
		irc_server_send_and_redirect(irc, str, NULL);
		g_string_free(str, TRUE);
		g_free(cmd);
		pump_server(irc, TRUE);
	}
	/* not sent (the connection is lost): not carried over */
	drop_queue(server, "/upgrade", 0);
	g_hash_table_remove(outqs, server);
}

/* would the gate send cmd (a queued buffer, CR LF at its end) as it
   is - clear text the user may send (PASS, or a bot command)? Decided
   on the keyring as it is now, dry: nothing saved, no REKEY; a keyring
   that cannot be read, or is lost, says no */
static gboolean gate_lets_pass(SERVER_REC *server, const char *cmd)
{
	E2E_LOAD_STATUS status;
	E2E_GATE_RESULT res;
	E2E_JSON *kr;
	E2E_NET net;
	char *error = NULL, *line, *tags = NULL, *target = NULL, *body = NULL, *msg;
	gboolean pass;
	gsize len = strlen(cmd);

	while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r'))
		len--;
	kr = e2e_keyring_load_checked(keyring_file, &status, &error);
	g_free(error);
	e2e_server_net(server, &net);
	if (memchr(cmd, '\r', len) != NULL || memchr(cmd, '\n', len) != NULL) {
		msg = e2e_gate_multiline(kr, status == E2E_LOAD_OK, &net, cmd, len, resolve_cb, server);
		pass = msg == NULL;
		g_free(msg);
	} else {
		line = g_strndup(cmd, len);
		if (!e2e_parse_privmsg_line(line, &tags, &target, &body)) {
			pass = FALSE;
		} else {
			e2e_gate_decide_dry(kr, status == E2E_LOAD_OK, &net, target, body,
			                    resolve_cb, server, &res);
			pass = res.action == E2E_GATE_PASS || res.action == E2E_GATE_BYPASS;
			e2e_gate_result_clear(&res);
		}
		e2e_wipe(line, strlen(line));
		g_free(line);
		if (body != NULL)
			e2e_wipe(body, strlen(body));
		g_free(body);
		g_free(tags);
		g_free(target);
	}
	e2e_net_clear(&net);
	e2e_json_free(kr);
	return pass;
}

/* the PRIVMSGs waiting in irssi's flood queue of irc, dropped and
   wiped - with only_gated, those the gate would not let out as they are
   (they would leave without it once the module is gone); how many */
static guint drop_irssi_privmsgs(IRC_SERVER_REC *irc, gboolean only_gated)
{
	GSList *tmp, *next;
	guint n = 0;

	for (tmp = irc->cmdqueue; tmp != NULL; tmp = next) {
		GSList *link = tmp->next;
		char *cmd = tmp->data;

		next = link->next;
		/* every line of it: "NICK x\r\nPRIVMSG #secret ..." too */
		if (!e2e_buffer_has_privmsg(cmd, strlen(cmd)))
			continue;
		if (only_gated && gate_lets_pass(SERVER(irc), cmd))
			continue;
		if (link->data != NULL)
			server_redirect_destroy(link->data);
		irc->cmdqueue = g_slist_delete_link(irc->cmdqueue, link);
		irc->cmdqueue = g_slist_delete_link(irc->cmdqueue, tmp);
		irc->cmdcount--;
		e2e_wipe(cmd, strlen(cmd));
		g_free(cmd);
		n++;
	}
	return n;
}

/* /e2e reset: PRIVMSGs typed while the keyring was lost may still wait
   in the flood queue or in the module's queue - the gate runs when they
   leave, and after the reset it would let them out in clear text. They
   are dropped instead (and the chunks waiting with them). */
int e2e_drop_queued_privmsgs(void)
{
	GHashTableIter iter;
	gpointer value;
	GSList *tmp;
	int n = 0;

	/* the module's queues: lines typed then, and chunks */
	g_hash_table_iter_init(&iter, outqs);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		n += e2e_outq_drop(value, NULL);
	for (tmp = servers; tmp != NULL; tmp = tmp->next)
		if (IS_IRC_SERVER(tmp->data))
			n += drop_irssi_privmsgs(IRC_SERVER(tmp->data), FALSE);
	return n;
}

/* the server is gone (a reconnect is a new record): its queue with it */
static void sig_server_gone(SERVER_REC *server)
{
	if (server == NULL)
		return;
	drop_queue(server, "disconnected", 0);
	g_hash_table_remove(outqs, server);
}

/* ---- incoming messages ---- */

static void sig_event_privmsg(SERVER_REC *server, const char *data, const char *nick,
                              const char *address)
{
	E2E_IN_PARAMS p;
	E2E_IN_RESULT res;
	E2E_IDENTITY id;
	E2E_JSON *kr;
	E2E_NET net;
	const char *sp, *text, *orig_nick = nick, *orig_address = address;
	char *target, **readings, *wait_key, *newdata;
	gboolean have_id, is_channel, lost;

	if (!IS_IRC_SERVER(server) || data == NULL || !e2e_native_active())
		return;
	/* "target :text" */
	sp = strchr(data, ' ');
	if (sp == NULL)
		return;
	text = sp;
	while (*text == ' ')
		text++;
	if (*text == ':')
		text++;
	/* the keyring is read only for well-formed RPE2E messages: anyone can
	   send "+RPE2E01 x", and it must not make us create or move files */
	if (strncmp(text, E2E_WIRE_PREFIX, strlen(E2E_WIRE_PREFIX)) != 0)
		return;
	{
		E2E_WIRE *w = e2e_wire_parse(text);

		if (w == NULL)
			return;
		e2e_wire_free(w);
	}
	if (nick == NULL)
		nick = "";
	if (address == NULL)
		address = "";
	target = g_strndup(data, sp - data);
	readings = e2e_channel_readings(target);
	is_channel = readings[0] != NULL;

	memset(&p, 0, sizeof(p));
	p.nick = nick;
	p.handle = address;
	p.is_own_line = is_own_nick(server, nick);
	/* the context: the channel; a DM is named by the sender, or by the
	   recipient for our own line relayed back */
	p.target = is_channel ? readings[0] : (p.is_own_line ? target : nick);
	p.alt_ctxs = is_channel && readings[1] != NULL ? readings + 1 : NULL;
	p.own_handle = e2e_own_handle(server);
	p.resolve = resolve_cb;
	p.resolve_data = server;
	p.keyreq_stamps = keyreq_out;
	e2e_server_net(server, &net);
	p.net = &net;

	/* the keyring is lost (moved aside): nothing to decrypt with, and no
	   new identity or KEYREQ until /e2e reset */
	kr = e2e_keyring_is_lost() ? NULL : e2e_load_full(&lost);
	if (kr == NULL || lost) {
		e2e_print_debug(server, target, nick, "wire from %s hidden: the keyring is lost (/e2e reset)",
		                nick);
		signal_stop();
		e2e_json_free(kr);
		e2e_net_clear(&net);
		g_strfreev(readings);
		g_free(target);
		return;
	}
	have_id = e2e_ensure_identity(kr, &id);
	e2e_decrypt_incoming(kr, have_id ? &id : NULL, &p, text, &res);

	switch (res.action) {
	case E2E_IN_NOT_WIRE:
		break;
	case E2E_IN_WAIT_OWN:
		/* decrypting or asking under the sender's handle would set up the
		   wrong direction: hold it, the next message works */
		wait_key = g_strconcat(server->tag, "|", nick, NULL);
		if (e2e_stamp_allow(own_wait_notice, wait_key, E2E_KEYREQ_MIN_INTERVAL))
			e2e_print_item(e2e_notice_item(server, nick, nick),
			               "encrypted DM from %s held — own identity not learned yet (waiting for the WHOIS reply)",
			               nick);
		g_free(wait_key);
		signal_stop();
		break;
	case E2E_IN_DROP:
		if (res.debug != NULL)
			e2e_print_debug(server, res.ctx, nick, "%s", res.debug);
		if (res.keyreq != NULL) {
			char *shown = e2e_ctx_display(res.ctx);

			e2e_send_notice(server, nick, res.keyreq);
			e2e_print_item(e2e_notice_item(server, res.ctx, nick), "KEYREQ sent to %s for %s",
			               nick, shown);
			e2e_print_debug(server, res.ctx, nick, "TX auto-KEYREQ to %s for %s", nick, shown);
			g_free(shown);
		}
		/* a KEYREQ of ours waits for its answer; old rows taken over */
		if (res.save_needed)
			e2e_save(kr);
		signal_stop();
		break;
	case E2E_IN_PLAIN:
		if (res.save_needed)
			e2e_save(kr);
		/* a decrypted non-ACTION CTCP would be answered by irssi with a
		   PLAIN TEXT reply; our side never encrypts those, so drop it */
		if (res.plain[0] == '\001' && strncmp(res.plain, "\001ACTION ", 8) != 0) {
			e2e_print_item(e2e_notice_item(server, p.target, nick),
			               "dropped an encrypted non-ACTION CTCP from %s", nick);
			signal_stop();
			break;
		}
		/* opened with the key of a channel the server may keep apart
		   from this one: shown with that channel's name */
		if (res.via != NULL) {
			char *marked = e2e_plain_with_via(res.plain, res.via);

			e2e_wipe(res.plain, strlen(res.plain));
			g_free(res.plain);
			res.plain = marked;
		}
		/* the wire's own target, so the routing (channel / query) stays */
		newdata = g_strconcat(target, " :", res.plain, NULL);
		signal_continue(4, server, newdata, orig_nick, orig_address);
		e2e_wipe(newdata, strlen(newdata));
		g_free(newdata);
		break;
	}
	e2e_in_result_clear(&res);
	if (have_id)
		e2e_identity_wipe(&id);
	e2e_json_free(kr);
	e2e_net_clear(&net);
	g_strfreev(readings);
	g_free(target);
}

/* ---- the key exchange ---- */

static void sig_ctcp_reply(SERVER_REC *server, const char *args, const char *nick,
                           const char *address, const char *target)
{
	E2E_HANDSHAKE *parsed;
	E2E_HS_TYPE type;
	E2E_IDENTITY id;
	E2E_JSON *kr;
	E2E_NET net;
	const char *sender, *ctx;
	char *before, *after, *key, *rsp = NULL, *recip = NULL, *limit, *sctx = NULL;
	gboolean have_id, lost;
	size_t taglen = strlen(E2E_CTCP_TAG);
	static const char *const names[] = { "", "KEYREQ", "KEYRSP", "REKEY" };

	if (!IS_IRC_SERVER(server) || args == NULL || strncmp(args, E2E_CTCP_TAG, taglen) != 0 ||
	    (args[taglen] != '\0' && !g_ascii_isspace(args[taglen])) || !e2e_native_active())
		return;
	/* anyone can send these: rate-limited, and the keyring is written only
	   when a handler changed it */
	signal_stop();
	if (nick == NULL)
		nick = "";
	sender = address != NULL ? address : "";
	type = e2e_handshake_type(args);
	if (type == E2E_HS_NONE)
		return;
	parsed = e2e_handshake_parse(args, type);
	ctx = parsed != NULL ? parsed->channel : "";

	/* a flood must not become a KEYRSP flood (Excess Flood), a stream of
	   signature checks or of keyring writes: each kind once per 10 s per
	   sender and context - of this server */
	limit = g_strdup_printf("%s%c%s", server->tag, E2E_SCOPE_SEP, ctx);
	if (!e2e_handshake_allow(keyreq_in, type, sender, limit)) {
		e2e_print_debug(server, ctx, nick, "%s from %s (%s) for %s dropped: rate limit",
		                names[type], nick, sender, ctx);
		g_free(limit);
		e2e_handshake_free(parsed);
		return;
	}
	g_free(limit);
	kr = e2e_keyring_is_lost() ? NULL : e2e_load_full(&lost);
	if (kr == NULL || lost) {
		e2e_print_debug(server, ctx, nick, "%s from %s ignored: the keyring is lost (/e2e reset)",
		                names[type], nick);
		e2e_json_free(kr);
		e2e_handshake_free(parsed);
		return;
	}
	e2e_server_net(server, &net);
	/* E2E set up with no network (rpe2e.pl), erssi on several: the
	   handshake is ignored (E2E is off there) until /e2e adopt */
	if (parsed != NULL && e2e_ctx_lookup(kr, &net, ctx, &sctx) == E2E_CTX_BLOCKED)
		e2e_print_debug(server, ctx, nick, "%s from %s (%s) for %s ignored: E2E there was set up with no network (by rpe2e.pl) and erssi uses several - /e2e adopt in this window binds it to this network",
		                names[type], nick, sender, ctx);
	g_free(sctx);
	sctx = NULL;
	before = e2e_keyring_snapshot(kr);
	have_id = e2e_ensure_identity(kr, &id);
	switch (type) {
	case E2E_HS_KEYREQ:
		if (parsed != NULL)
			e2e_print_debug(server, ctx, nick, "RX KEYREQ from %s (%s) for %s", nick, sender, ctx);
		if (have_id)
			e2e_handle_keyreq(kr, &id, keyreq_out, &net, sender, nick, args,
			                  e2e_own_handle(server), &rsp, &recip);
		break;
	case E2E_HS_KEYRSP:
		if (parsed != NULL)
			e2e_print_debug(server, ctx, nick, "RX KEYRSP from %s (%s) for %s", nick, sender, ctx);
		if (e2e_handle_keyrsp(kr, &net, sender, nick, args))
			e2e_print_debug(server, ctx, nick, "KEYRSP from %s (%s) installed session on %s",
			                nick, sender, ctx);
		break;
	case E2E_HS_REKEY:
		if (have_id && e2e_handle_rekey(kr, &id, &net, sender, nick, args))
			e2e_print_debug(server, ctx, nick, "REKEY from %s (%s) installed on %s",
			                nick, sender, ctx);
		break;
	default:
		break;
	}
	after = e2e_keyring_snapshot(kr);
	if (strcmp(before, after) != 0)
		e2e_save(kr);

	/* the request waits where e2e_handle_keyreq put it, "<sender>|<ctx>" */
	if (type == E2E_HS_KEYREQ && parsed != NULL && rsp == NULL &&
	    (key = e2e_pending_inbound_key(kr, &net, sender, ctx)) != NULL) {
		if (e2e_json_get(e2e_json_get(kr, "pending_inbound"), key) != NULL) {
			/* their key may be here already (they answered our request):
			   ours still goes to them only after /e2e accept */
			gboolean have_theirs = e2e_session_has_key(e2e_json_get(e2e_json_get(kr, "incoming"), key));
			const char *stored = key + strlen(sender) + 1;
			char *stamp = g_strdup_printf("%s|%s|%s", server->tag, sender, stored);

			/* asked again and again (their client asks with each
			   message): told once in E2E_PENDING_NOTICE_INTERVAL,
			   /e2e pending lists it */
			if (e2e_pending_notice_allow(pending_notice, stamp))
				e2e_print_pending(server, stored, nick, sender, have_theirs);
			e2e_print_debug(server, ctx, nick, "KEYREQ from %s (%s) is pending on %s",
			                nick, sender, ctx);
			g_free(stamp);
		}
		g_free(key);
	}
	if (rsp != NULL) {
		e2e_send_notice(server, nick, rsp);
		e2e_print_debug(server, ctx, nick, "TX KEYRSP to %s for %s", nick, ctx);
	}
	if (recip != NULL) {
		e2e_send_notice(server, nick, recip);
		e2e_print_debug(server, ctx, nick, "TX reciprocal KEYREQ to %s for %s", nick, ctx);
	}
	e2e_wipe(before, strlen(before));
	e2e_wipe(after, strlen(after));
	g_free(before);
	g_free(after);
	g_free(rsp);
	g_free(recip);
	g_free(sctx);
	e2e_net_clear(&net);
	if (have_id)
		e2e_identity_wipe(&id);
	e2e_json_free(kr);
	e2e_handshake_free(parsed);
}

/* ---- the module ---- */

void e2e_core_init(void)
{
	GSList *tmp;

	module_register("e2e", "core");
	theme_register(e2e_formats);
	settings_add_bool("e2e", "e2e_debug", FALSE);

	keyring_dir = g_build_filename(get_irssi_dir(), "rpe2e", NULL);
	keyring_file = g_build_filename(keyring_dir, "keyring.json", NULL);
	if (!g_file_test(keyring_dir, G_FILE_TEST_IS_DIR))
		g_mkdir_with_parents(keyring_dir, 0700);
	chmod(keyring_dir, 0700);

	keyreq_out = e2e_stamps_new();
	keyreq_in = e2e_stamps_new();
	own_wait_notice = e2e_stamps_new();
	pending_notice = e2e_stamps_new();
	own_handles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	gated_emissions = g_ptr_array_new_with_free_func(gated_line_free);
	own_lines = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	outqs = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
	                              (GDestroyNotify) e2e_outq_free);

	signal_add_full(MODULE_NAME, SIGNAL_PRIORITY_HIGH - 1, "server outgoing modify",
	                (SIGNAL_FUNC) sig_server_outgoing_depth, NULL);
	signal_add_first("server outgoing modify", (SIGNAL_FUNC) sig_server_outgoing);
	signal_add_last("server outgoing modify", (SIGNAL_FUNC) sig_server_outgoing_script);
	signal_add_first("session save server", (SIGNAL_FUNC) sig_session_save_server);
	signal_add("server disconnected", (SIGNAL_FUNC) sig_server_gone);
	signal_add("server connect failed", (SIGNAL_FUNC) sig_server_gone);
	signal_add("server destroyed", (SIGNAL_FUNC) sig_server_gone);
	signal_add_first("event privmsg", (SIGNAL_FUNC) sig_event_privmsg);
	signal_add_first("ctcp reply", (SIGNAL_FUNC) sig_ctcp_reply);
	signal_add("event 001", (SIGNAL_FUNC) sig_event_001);
	signal_add("event 311", (SIGNAL_FUNC) sig_event_311);
	signal_add("redir e2e whois user", (SIGNAL_FUNC) sig_event_311);
	signal_add("event join", (SIGNAL_FUNC) sig_event_join);
	signal_add("event chghost", (SIGNAL_FUNC) sig_event_chghost);
	signal_add("event 396", (SIGNAL_FUNC) sig_event_396);
	signal_add("commandlist new", (SIGNAL_FUNC) sig_commandlist_new);
	signal_add("script destroyed", (SIGNAL_FUNC) sig_script_destroyed);
	signal_add_first("command upgrade", (SIGNAL_FUNC) sig_command_upgrade);
	signal_add("gui exit", (SIGNAL_FUNC) sig_gui_exit);

	e2e_commands_init();
	e2e_statusbar_init();
	e2e_web_init();

	if (e2e_native_active()) {
		E2E_IDENTITY id;
		gboolean lost;
		E2E_JSON *kr = e2e_load_full(&lost);

		/* an unreadable keyring does not stop the module: the gate
		   refuses to send instead; a lost one gets no new identity
		   before /e2e reset */
		if (!lost && e2e_ensure_identity(kr, &id))
			e2e_identity_wipe(&id);
		/* entries a bug of before wrote may hold a key: rotated */
		if (!lost) {
			GPtrArray *found = e2e_rotate_for_damaged(kr, NULL);

			if (found->len > 0 && e2e_save(kr))
				e2e_tell_damaged(NULL, found);
			g_ptr_array_unref(found);
		}
		e2e_json_free(kr);
	}
	/* not in autoload_modules: gone after a restart or /upgrade */
	e2e_warn_session_only(NULL);
	/* loaded mid-session: 001 will not come for servers already up */
	for (tmp = servers; tmp != NULL; tmp = tmp->next)
		if (IS_IRC_SERVER(tmp->data))
			send_self_whois(tmp->data);
}

void e2e_core_deinit(void)
{
	GSList *list, *tmp;

	warn_unload();
	/* what still waits is not sent: told, and wiped - and the PRIVMSGs
	   in irssi's flood queue that the gate would encrypt or refuse (the
	   rest of a long line irssi split, typed while E2E was on): they
	   would leave in clear text without the module. Clear text the gate
	   lets pass stays queued. */
	list = g_slist_copy(servers);
	for (tmp = list; tmp != NULL; tmp = tmp->next)
		if (g_slist_find(servers, tmp->data) != NULL && IS_IRC_SERVER(tmp->data))
			drop_queue(tmp->data, "the e2e module is unloaded",
			           drop_irssi_privmsgs(IRC_SERVER(tmp->data), TRUE));
	g_slist_free(list);

	signal_remove("server outgoing modify", (SIGNAL_FUNC) sig_server_outgoing_depth);
	signal_remove("server outgoing modify", (SIGNAL_FUNC) sig_server_outgoing);
	signal_remove("server outgoing modify", (SIGNAL_FUNC) sig_server_outgoing_script);
	signal_remove("session save server", (SIGNAL_FUNC) sig_session_save_server);
	signal_remove("server disconnected", (SIGNAL_FUNC) sig_server_gone);
	signal_remove("server connect failed", (SIGNAL_FUNC) sig_server_gone);
	signal_remove("server destroyed", (SIGNAL_FUNC) sig_server_gone);
	signal_remove("event privmsg", (SIGNAL_FUNC) sig_event_privmsg);
	signal_remove("ctcp reply", (SIGNAL_FUNC) sig_ctcp_reply);
	signal_remove("event 001", (SIGNAL_FUNC) sig_event_001);
	signal_remove("event 311", (SIGNAL_FUNC) sig_event_311);
	signal_remove("redir e2e whois user", (SIGNAL_FUNC) sig_event_311);
	signal_remove("event join", (SIGNAL_FUNC) sig_event_join);
	signal_remove("event chghost", (SIGNAL_FUNC) sig_event_chghost);
	signal_remove("event 396", (SIGNAL_FUNC) sig_event_396);
	signal_remove("commandlist new", (SIGNAL_FUNC) sig_commandlist_new);
	signal_remove("script destroyed", (SIGNAL_FUNC) sig_script_destroyed);
	signal_remove("command upgrade", (SIGNAL_FUNC) sig_command_upgrade);
	signal_remove("gui exit", (SIGNAL_FUNC) sig_gui_exit);
	if (script_check_id != 0)
		g_source_remove(script_check_id);
	script_check_id = 0;
	if (pump_tag != 0)
		g_source_remove(pump_tag);
	pump_tag = 0;
	g_hash_table_destroy(outqs);
	outqs = NULL;
	in_pump = pump_armed = FALSE;
	pump_emission = 0;

	e2e_web_deinit();
	e2e_statusbar_deinit();
	e2e_commands_deinit();
	theme_unregister();

	g_hash_table_destroy(keyreq_out);
	g_hash_table_destroy(keyreq_in);
	g_hash_table_destroy(own_wait_notice);
	g_hash_table_destroy(pending_notice);
	if (state_idle_id != 0)
		g_source_remove(state_idle_id);
	state_idle_id = 0;
	g_hash_table_destroy(own_handles);
	g_ptr_array_unref(gated_emissions);
	gated_emissions = NULL;
	g_hash_table_destroy(own_lines);
	g_free(keyring_file);
	g_free(keyring_dir);
	keyring_file = keyring_dir = NULL;
	script_seen = FALSE;
	scoped_warned = FALSE;
	scoped_stat_valid = FALSE;
	seen_stat_valid = FALSE;
	exiting = FALSE;
}

void e2e_core_abicheck(int *version)
{
	*version = IRSSI_ABI_VERSION;
}
