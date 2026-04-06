/*
 mentions-window.c : erssi

    Copyright (C) 2026 erssi contributors

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
#include <irssi/src/fe-common/core/module-formats.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/fe-common/core/formats.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/hilight-text.h>

#define MENTIONS_WINDOW_NAME "(mentions)"
#define MENTIONS_REFNUM 2

void mentions_window_ensure(void)
{
	WINDOW_REC *window, *status;

	if (!settings_get_bool("use_mentions_window"))
		return;

	window = window_find_name(MENTIONS_WINDOW_NAME);
	if (window != NULL) {
		if (window->refnum != MENTIONS_REFNUM)
			window_set_refnum(window, MENTIONS_REFNUM);
		return;
	}

	window = window_create(NULL, TRUE);
	window_set_refnum(window, MENTIONS_REFNUM);
	window_set_name(window, MENTIONS_WINDOW_NAME);
	window_set_immortal(window, TRUE);

	/* Restore focus to status window so startup messages
	 * (banner, firsttimer) go there, not mentions. */
	status = window_find_name("(status)");
	if (status == NULL)
		status = window_find_refnum(1);
	if (status != NULL)
		window_set_active(status);
}

static void remove_mentions_window(void)
{
	WINDOW_REC *window;

	window = window_find_name(MENTIONS_WINDOW_NAME);
	if (window == NULL)
		return;

	window_set_immortal(window, FALSE);
	window_destroy(window);
}

static void sig_setup_changed(void)
{
	if (settings_get_bool("use_mentions_window"))
		mentions_window_ensure();
	else
		remove_mentions_window();
}

/* Capture "message public" — raw IRC message before formatting.
 * Check if our nick is mentioned, then print to mentions window
 * using the themed mention_public format for proper colours. */
static void sig_message_public_mentions(SERVER_REC *server, const char *msg,
					const char *nick, const char *address,
					const char *target, NICK_REC *nickrec)
{
	WINDOW_REC *mentions_win;
	CHANNEL_REC *chanrec;
	char *nickmode, *color;
	int for_me;
	HILIGHT_REC *hilight;

	mentions_win = window_find_name(MENTIONS_WINDOW_NAME);
	if (mentions_win == NULL)
		return;

	chanrec = channel_find(server, target);
	if (nickrec == NULL && chanrec != NULL)
		nickrec = nicklist_find(chanrec, nick);

	/* Check if our nick is mentioned in the message */
	for_me = !settings_get_bool("hilight_nick_matches") ?
		     FALSE :
		 !settings_get_bool("hilight_nick_matches_everywhere") ?
		     nick_match_msg(chanrec, msg, server->nick) :
		     nick_match_msg_everywhere(chanrec, msg, server->nick);

	hilight = for_me ? NULL :
		  hilight_match_nick(server, target, nick, address,
				     MSGLEVEL_PUBLIC, msg);

	if (!for_me && hilight == NULL)
		return;

	color = hilight != NULL ? hilight_get_color(hilight) : NULL;

	/* Build nick prefix (@, +, etc.) */
	if (nickrec == NULL || nickrec->prefixes[0] == '\0')
		nickmode = g_strdup("");
	else {
		nickmode = g_malloc(2);
		nickmode[0] = nickrec->prefixes[0];
		nickmode[1] = '\0';
	}

	/* Use themed format: %c[channel]%n {pubmsghinick color nickmode nick}msg
	 * This gives identical colours to channel hilight messages.
	 * MSGLEVEL_HILIGHT triggers sidepanel activity (priority 4, magenta).
	 * MSGLEVEL_NOHILIGHT prevents hilight-text.c from re-processing. */
	printformat_window(mentions_win,
			   MSGLEVEL_HILIGHT | MSGLEVEL_NOHILIGHT,
			   TXT_MENTION_PUBLIC,
			   target,
			   color != NULL ? color : "",
			   nick,
			   nickmode,
			   msg);

	g_free(nickmode);
	g_free(color);
}

void mentions_window_init(void)
{
	settings_add_bool("lookandfeel", "use_mentions_window", TRUE);

	signal_add("setup changed", (SIGNAL_FUNC) sig_setup_changed);
	signal_add_last("message public", (SIGNAL_FUNC) sig_message_public_mentions);
}

void mentions_window_deinit(void)
{
	signal_remove("setup changed", (SIGNAL_FUNC) sig_setup_changed);
	signal_remove("message public", (SIGNAL_FUNC) sig_message_public_mentions);
}
