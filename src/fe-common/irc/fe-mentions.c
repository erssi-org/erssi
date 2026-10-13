/*
 fe-mentions.c : the Mentions window - what was written to you, in one place

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "module.h"
#include <irssi/src/core/core.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/ignore.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/signals.h>

#include <irssi/src/irc/dcc/dcc-chat.h>

#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-common/core/mentions-log.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/irc/fe-mentions.h>
#include <irssi/src/fe-common/irc/module-formats.h>

#include <time.h>

/* entries that arrive before the window exists (at most this many) */
#define MENTIONS_PENDING_MAX 100

typedef struct {
	int format;
	char *where;
	char *nick;
	char *text;
} PENDING_REC;

static WINDOW_REC *mwin;   /* the Mentions window, NULL if none */
static char *mwin_name;    /* mentions_window as last seen */
static GQueue *pending;    /* PENDING_REC, oldest first */
static guint create_tag;   /* idle source that creates the window */
static char *log_err_path; /* the log path the last error was printed for */
static gboolean deiniting;

static int kind_format(const char *kind)
{
	if (g_strcmp0(kind, "action") == 0)
		return IRCTXT_MENTIONS_ACTION;
	if (g_strcmp0(kind, "private") == 0)
		return IRCTXT_MENTIONS_PRIVATE;
	if (g_strcmp0(kind, "notice") == 0)
		return IRCTXT_MENTIONS_NOTICE;
	if (g_strcmp0(kind, "dcc") == 0)
		return IRCTXT_MENTIONS_DCC;
	return IRCTXT_MENTIONS_PUBLIC;
}

static void pending_free(PENDING_REC *rec)
{
	g_free(rec->where);
	g_free(rec->nick);
	g_free(rec->text);
	g_free(rec);
}

/* The text is a format argument, as in fe-messages: it is inserted as it
   is, so '%' and '$' in a message stay plain characters. */
static void print_entry(WINDOW_REC *window, int format, const char *where, const char *nick,
                        const char *text)
{
	printformat_module_window(MODULE_NAME, window, MSGLEVEL_HILIGHT | MSGLEVEL_NOHILIGHT,
	                          format, where, nick, text);
}

static void pending_add(int format, const char *where, const char *nick, const char *text)
{
	PENDING_REC *rec;

	if (pending == NULL)
		pending = g_queue_new();
	while (g_queue_get_length(pending) >= MENTIONS_PENDING_MAX)
		pending_free(g_queue_pop_head(pending));

	rec = g_new0(PENDING_REC, 1);
	rec->format = format;
	rec->where = g_strdup(where);
	rec->nick = g_strdup(nick);
	rec->text = g_strdup(text);
	g_queue_push_tail(pending, rec);
}

static void pending_clear(void)
{
	if (pending == NULL)
		return;
	g_queue_free_full(pending, (GDestroyNotify) pending_free);
	pending = NULL;
}

static void pending_flush(WINDOW_REC *window)
{
	PENDING_REC *rec;

	if (pending == NULL)
		return;
	while ((rec = g_queue_pop_head(pending)) != NULL) {
		print_entry(window, rec->format, rec->where, rec->nick, rec->text);
		pending_free(rec);
	}
	pending_clear();
}

WINDOW_REC *mentions_window(gboolean create)
{
	WINDOW_REC *window, *active;

	if (mwin != NULL)
		return mwin;
	if (mwin_name == NULL || *mwin_name == '\0' || deiniting)
		return NULL;

	/* a window of that name may exist already (a saved layout) */
	window = window_find_name(mwin_name);
	if (window == NULL) {
		if (!create)
			return NULL;
		active = active_win;
		window = window_create(NULL, TRUE);
		window_set_name(window, mwin_name);
		if (active != NULL && active != window && g_slist_find(windows, active) != NULL)
			window_set_active(active);
	}
	window_set_immortal(window, TRUE);
	mwin = window;
	pending_flush(window);
	return window;
}

static gboolean create_idle(gpointer data)
{
	(void) data;
	create_tag = 0;
	mentions_window(TRUE);
	return G_SOURCE_REMOVE;
}

static void create_later(void)
{
	if (create_tag == 0)
		create_tag = g_idle_add(create_idle, NULL);
}

/* The reason in an error of mentions_log_append(), without the path that
   the message repeats ("cannot open PATH: Is a directory"). */
static const char *log_error_reason(const char *message, const char *path)
{
	const char *p;
	size_t len;

	len = strlen(path);
	p = strstr(message, path);
	if (p == NULL || len == 0)
		return message;
	p += len;
	if (p[0] == ':' && p[1] == ' ')
		return p + 2;
	if (p[0] == ' ' && p - len == message)
		return p + 1;
	return message;
}

static void log_entry(const char *tag, const char *where, const char *nick, const char *text)
{
	GError *error = NULL;
	char *path, *line;

	path = mentions_log_path(settings_get_str("mentions_log_file"), get_irssi_dir());
	if (path == NULL)
		return;

	line = mentions_log_line(time(NULL), tag, where, nick, text);
	if (mentions_log_append(path, line, &error)) {
		g_free(log_err_path);
		log_err_path = NULL;
	} else {
		/* one error line per path, not one per mention */
		if (g_strcmp0(log_err_path, path) != 0) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR | MSGLEVEL_NO_ACT,
			          "Mentions: cannot write %s: %s", path,
			          log_error_reason(error->message, path));
			g_free(log_err_path);
			log_err_path = g_strdup(path);
		}
		g_error_free(error);
	}
	g_free(line);
	g_free(path);
}

void mentions_record(SERVER_REC *server, const char *kind, const char *tag, const char *where,
                     const char *nick, const char *text)
{
	WINDOW_REC *window;
	char *stripped;
	int format;

	g_return_if_fail(kind != NULL);

	stripped = strip_codes(text != NULL ? text : "");
	g_strstrip(stripped);
	if (where == NULL)
		where = "";
	if (nick == NULL)
		nick = "?";
	format = kind_format(kind);

	if (mwin_name != NULL && *mwin_name != '\0') {
		window = mentions_window(irssi_init_finished);
		if (window != NULL)
			print_entry(window, format, where, nick, stripped);
		else
			pending_add(format, where, nick, stripped);
	}

	if (settings_get_bool("mentions_log"))
		log_entry(tag, where, nick, stripped);

	signal_emit("mention", 5, server, kind, where, nick, stripped);
	g_free(stripped);
}

static gboolean is_excluded(SERVER_REC *server)
{
	if (server == NULL || server->connrec == NULL)
		return FALSE;
	return mentions_network_excluded(server->connrec->chatnet,
	                                 settings_get_str("mentions_exclude_networks"));
}

static gboolean is_own_nick(SERVER_REC *server, const char *nick)
{
	return nick != NULL && server->nick != NULL && g_ascii_strcasecmp(nick, server->nick) == 0;
}

/* a message to a server or host mask ($*.example.net) is not written to you */
static gboolean is_mask_target(const char *target)
{
	return target != NULL && *target == '$';
}

/* The channel of a NOTICE that starts with "[#channel]" (a cnotice), as
   fe-irc-messages finds it when it prints the notice. */
static char *notice_context_channel(SERVER_REC *server, const char *msg)
{
	const char *end;
	char *channel;

	if (!settings_get_bool("notice_channel_context") || *msg != '[')
		return NULL;
	end = strpbrk(msg, " ,]");
	if (end == NULL || *end != ']')
		return NULL;
	channel = g_strndup(msg + 1, end - msg - 1);
	if (server_ischannel(server, channel))
		return channel;
	g_free(channel);
	return NULL;
}

/* A flag ignore (/IGNORE nick MSGS NOHILIGHT) does not hide the line, it
   only takes the hilight away. fe-messages and fe-irc-messages print such a
   line with MSGLEVEL_NOHILIGHT, and so a channel hilight is not recorded
   (sig_print_text); the same ignore keeps a PM, action or notice out. */
static gboolean nohilight_ignored(SERVER_REC *server, const char *nick, const char *address,
                                  const char *context, const char *msg, int level)
{
	return ignore_check_flags(server, nick, address, context, msg, level, MSGLEVEL_NOHILIGHT) !=
	       0;
}

static void sig_message_private(SERVER_REC *server, const char *msg, const char *nick,
                                const char *address, const char *target)
{
	if (server == NULL || is_own_nick(server, nick) || is_mask_target(target) ||
	    is_excluded(server))
		return;
	if (ignore_check(server, nick, address, NULL, msg, MSGLEVEL_MSGS) ||
	    nohilight_ignored(server, nick, address, NULL, msg, MSGLEVEL_MSGS))
		return;
	mentions_record(server, "private", server->tag, "PM", nick, msg);
}

/* a private /ME only - channel actions are hilights */
static void sig_message_irc_action(SERVER_REC *server, const char *msg, const char *nick,
                                   const char *address, const char *target)
{
	if (server == NULL || target == NULL || server->nick == NULL ||
	    g_ascii_strcasecmp(target, server->nick) != 0)
		return;
	if (is_own_nick(server, nick) || is_excluded(server))
		return;
	if (ignore_check(server, nick, address, NULL, msg, MSGLEVEL_ACTIONS | MSGLEVEL_MSGS) ||
	    nohilight_ignored(server, nick, address, NULL, msg, MSGLEVEL_ACTIONS | MSGLEVEL_MSGS))
		return;
	mentions_record(server, "action", server->tag, "PM", nick, msg);
}

/* a NOTICE from a person to your nick: not from a server, not to a channel
   or a mask */
static void sig_message_irc_notice(SERVER_REC *server, const char *msg, const char *nick,
                                   const char *address, const char *target)
{
	char *context;
	int ignored, level;

	if (server == NULL || msg == NULL || mentions_is_server_nick(nick) || address == NULL ||
	    *address == '\0')
		return;
	if (target == NULL || server->nick == NULL || g_ascii_strcasecmp(target, server->nick) != 0)
		return;
	if (is_own_nick(server, nick) || is_excluded(server))
		return;

	/* a cnotice is ignored by the ignores of its channel */
	context = notice_context_channel(server, msg);
	level = context != NULL ? MSGLEVEL_PUBNOTICES : MSGLEVEL_NOTICES;
	ignored = ignore_check(server, nick, address, context, msg, level) ||
	          nohilight_ignored(server, nick, address, context, msg, level);
	g_free(context);
	if (ignored)
		return;
	mentions_record(server, "notice", server->tag, "NOTICE", nick, msg);
}

/* CTCPs over DCC CHAT are handled (and stopped) earlier */
static void sig_dcc_chat_message(CHAT_DCC_REC *dcc, const char *msg)
{
	if (dcc == NULL || is_excluded(SERVER(dcc->server)))
		return;
	mentions_record(SERVER(dcc->server), "dcc", "DCC", "DCC",
	                dcc->nick != NULL ? dcc->nick : "DCC", msg);
}

static void sig_init_finished(void)
{
	mentions_window(TRUE);
}

static void sig_window_destroyed(WINDOW_REC *window)
{
	if (window != mwin)
		return;
	mwin = NULL;
	/* closed after /WINDOW IMMORTAL OFF: make it again */
	if (!deiniting && irssi_init_finished)
		create_later();
}

static void sig_setup_changed(void)
{
	const char *name;

	name = settings_get_str("mentions_window");
	if (g_strcmp0(name, mwin_name) == 0)
		return;

	/* two windows of one name: refused, as /WINDOW NAME does */
	if (*name != '\0' && mwin != NULL) {
		WINDOW_REC *other = window_find_name(name);

		if (other != NULL && other != mwin) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTERROR,
			          "Mentions: there is a window named %s already - "
			          "mentions_window stays %s",
			          name, mwin_name);
			settings_set_str("mentions_window", mwin_name);
			return;
		}
	}

	g_free(mwin_name);
	mwin_name = g_strdup(name);

	if (*name == '\0') {
		/* off: the window stays, but as an ordinary one */
		if (mwin != NULL)
			window_set_immortal(mwin, FALSE);
		mwin = NULL;
		pending_clear();
		return;
	}

	if (mwin != NULL)
		window_set_name(mwin, name);
	else if (irssi_init_finished)
		mentions_window(TRUE);
}

/* SYNTAX: MENTIONS [CLEAR] */
static void cmd_mentions(const char *data, SERVER_REC *server, void *item)
{
	WINDOW_REC *window;
	char *arg, *refnum;
	void *free_arg;

	(void) server;
	(void) item;

	window = mentions_window(TRUE);
	if (window == NULL) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTNOTICE,
		          "The Mentions window is off - "
		          "/SET mentions_window Mentions turns it on.");
		return;
	}

	if (!cmd_get_params(data, &free_arg, 1, &arg))
		return;
	if (g_ascii_strcasecmp(arg, "clear") == 0) {
		/* the entries are removed, not only scrolled away as by /CLEAR */
		refnum = g_strdup_printf("%d", window->refnum);
		signal_emit("command scrollback clear", 3, refnum, NULL, NULL);
		g_free(refnum);
	} else {
		window_set_active(window);
	}
	cmd_params_free(free_arg);
}

void fe_mentions_init(void)
{
	settings_add_str("mentions", "mentions_window", "Mentions");
	settings_add_bool("mentions", "mentions_log", FALSE);
	settings_add_str("mentions", "mentions_log_file", "");
	settings_add_str("mentions", "mentions_exclude_networks", "");

	/* Note on the order: the "add_last" handlers below are registered after
	   fe-messages and fe-irc-messages, and a new hook of the same priority
	   goes in front of the older ones, so for a PM, a private action and a
	   notice the "mention" signal fires BEFORE the original line is
	   printed. */

	deiniting = FALSE;
	mwin = NULL;
	mwin_name = g_strdup(settings_get_str("mentions_window"));

	signal_add_last("message private", (SIGNAL_FUNC) sig_message_private);
	signal_add_last("message irc action", (SIGNAL_FUNC) sig_message_irc_action);
	signal_add_last("message irc notice", (SIGNAL_FUNC) sig_message_irc_notice);
	signal_add_last("dcc chat message", (SIGNAL_FUNC) sig_dcc_chat_message);
	signal_add("window destroyed", (SIGNAL_FUNC) sig_window_destroyed);
	signal_add("setup changed", (SIGNAL_FUNC) sig_setup_changed);
	command_bind("mentions", NULL, (SIGNAL_FUNC) cmd_mentions);

	/* At "irssi init finished" the side panels name window 1, so the window
	   is made after that (or on idle after a /LOAD). */
	if (irssi_init_finished)
		create_later();
	else
		signal_add_last("irssi init finished", (SIGNAL_FUNC) sig_init_finished);
}

void fe_mentions_deinit(void)
{
	deiniting = TRUE;

	if (create_tag != 0) {
		g_source_remove(create_tag);
		create_tag = 0;
	}

	signal_remove("message private", (SIGNAL_FUNC) sig_message_private);
	signal_remove("message irc action", (SIGNAL_FUNC) sig_message_irc_action);
	signal_remove("message irc notice", (SIGNAL_FUNC) sig_message_irc_notice);
	signal_remove("dcc chat message", (SIGNAL_FUNC) sig_dcc_chat_message);
	signal_remove("window destroyed", (SIGNAL_FUNC) sig_window_destroyed);
	signal_remove("setup changed", (SIGNAL_FUNC) sig_setup_changed);
	signal_remove("irssi init finished", (SIGNAL_FUNC) sig_init_finished);
	command_unbind("mentions", (SIGNAL_FUNC) cmd_mentions);

	pending_clear();
	mwin = NULL;
	g_free(mwin_name);
	mwin_name = NULL;
	g_free(log_err_path);
	log_err_path = NULL;
}
