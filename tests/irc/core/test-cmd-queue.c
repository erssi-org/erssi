/*
 test-cmd-queue.c : irssi

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

/* The flood queue keeps the order of the commands: irc_send_cmd() does not
   send a command at once while an earlier one still waits in the queue. */

#include <glib.h>

#include <irssi/src/common.h>
#include <irssi/src/core/core.h>
#include <irssi/src/core/modules.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>

#define MODULE_NAME "test-cmd-queue"

static IRC_SERVER_REC *server;
static GPtrArray *sent;

/* what would reach the socket; emptied, so nothing is written */
static void sig_outgoing_modify(SERVER_REC *srv, GString *str, int crlf)
{
	g_ptr_array_add(sent, g_strdup(str->str));
	g_string_truncate(str, 0);
}

static void setup(void)
{
	server = g_new0(IRC_SERVER_REC, 1);
	MODULE_DATA_INIT(server);
	server->type = module_get_uniq_id("SERVER", 0);
	server->max_message_len = MAX_IRC_MESSAGE_LEN;
	server->max_cmds_at_once = 5;
	server->cmd_queue_speed = 2200;
	sent = g_ptr_array_new_with_free_func(g_free);
	signal_add("server outgoing modify", (SIGNAL_FUNC) sig_outgoing_modify);
}

static void teardown(void)
{
	signal_remove("server outgoing modify", (SIGNAL_FUNC) sig_outgoing_modify);
	g_slist_free_full(server->cmdqueue, g_free);
	MODULE_DATA_DEINIT(server);
	g_free(server);
	g_ptr_array_unref(sent);
}

/* the queued commands, without their redirect entries */
static char *queued(void)
{
	GString *str = g_string_new(NULL);
	GSList *tmp;

	for (tmp = server->cmdqueue; tmp != NULL; tmp = tmp->next->next)
		g_string_append_printf(str, "%s|", (char *) tmp->data);
	return g_string_free(str, FALSE);
}

static void test_send_at_once(void)
{
	setup();
	irc_send_cmd(server, "PRIVMSG bob :hello");
	g_assert_cmpuint(sent->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(sent, 0), ==, "PRIVMSG bob :hello");
	g_assert_null(server->cmdqueue);
	teardown();
}

/* a long line sent earlier holds the queue (wait_cmd); the penalty is over
   before the queue timer runs again: the next command must not overtake
   the one that waits */
static void test_no_overtaking(void)
{
	char *q;

	setup();
	server->wait_cmd = g_get_real_time() + 60 * G_USEC_PER_SEC;
	irc_send_cmd(server, "PRIVMSG bob :first");
	g_assert_cmpuint(sent->len, ==, 0);

	server->wait_cmd = 0;
	irc_send_cmd(server, "PRIVMSG bob :second");
	g_assert_cmpuint(sent->len, ==, 0);
	q = queued();
	g_assert_cmpstr(q, ==, "PRIVMSG bob :first\r\n|PRIVMSG bob :second\r\n|");
	g_free(q);
	teardown();
}

/* commands queued for later (irc_send_cmd_later) do not hold back a
   normal one: it is sent at once, as before */
static void test_later_does_not_hold_back(void)
{
	char *q;

	setup();
	server->wait_cmd = g_get_real_time() + 60 * G_USEC_PER_SEC;
	irc_send_cmd_later(server, "WHO #test");
	g_assert_cmpuint(sent->len, ==, 0);

	server->wait_cmd = 0;
	irc_send_cmd(server, "PRIVMSG bob :now");
	g_assert_cmpuint(sent->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(sent, 0), ==, "PRIVMSG bob :now");
	q = queued();
	g_assert_cmpstr(q, ==, "WHO #test\r\n|");
	g_free(q);
	teardown();
}

int main(int argc, char **argv)
{
	int res;

	g_test_init(&argc, &argv, NULL);

	core_preinit(*argv);
	irssi_gui = IRSSI_GUI_NONE;

	modules_init();
	signals_init();

	g_test_add_func("/test/cmd_queue/send_at_once", test_send_at_once);
	g_test_add_func("/test/cmd_queue/no_overtaking", test_no_overtaking);
	g_test_add_func("/test/cmd_queue/later_does_not_hold_back", test_later_does_not_hold_back);

#if GLIB_CHECK_VERSION(2,38,0)
	g_test_set_nonfatal_assertions();
#endif
	res = g_test_run();

	signals_deinit();
	modules_deinit();

	return res;
}
