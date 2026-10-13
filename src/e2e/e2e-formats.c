/*
 e2e-formats.c : the messages of the e2e module

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 Every message of the module is "[E2E] <text>", as rpe2e.pl prints them;
 the text is an argument, so a "%" in a channel name, a nick or a path is
 shown as it is, not read as a theme code.
*/

#include "module.h"
#include "e2e-formats.h"

FORMAT_REC e2e_formats[] = {
	{ MODULE_NAME, "E2E", 0 },

	{ "e2e_message", "[E2E] $0", 1, { FORMAT_STRING } },
	{ "e2e_debug", "[E2E debug] $0", 1, { FORMAT_STRING } },
	/* nick, ident@host, <network>:<context>, " - you can read them
	   already ..." or "" - shown with control bytes as '?' */
	{ "e2e_pending", "[E2E] Pending key exchange from {hilight $0} ($1) for $2$3. Run /e2e accept $1 or /e2e decline $1 (/e2e pending lists all)", 4, { 0, 0, 0, 0 } },
	/* the E2E statusbar item of a window: its state, the requests waiting */
	{ "e2e_sb_off", "e2e off", 0 },
	{ "e2e_sb_on", "%gE2E%n", 0 },
	{ "e2e_sb_waiting", "%yE2E waiting for keys%n", 0 },
	{ "e2e_sb_blocked", "%RE2E blocked%n", 0 },
	{ "e2e_sb_error", "%RE2E keyring error%n", 0 },
	{ "e2e_sb_pending", " %R$0 to accept%n", 1, { 0 } },
	/* the same for a query: nick, ident@host, the network, the suffix -
	   answered from any window of that network */
	{ "e2e_pending_query", "[E2E] Pending key exchange from {hilight $0} ($1) for a query$3. Run /e2e accept $1 or /e2e decline $1 in any window of $2 (/e2e pending lists all)", 4, { 0, 0, 0, 0 } },

	{ NULL, NULL, 0 }
};
