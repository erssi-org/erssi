/*
 test-session-motd.c : irssi

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

/* /UPGRADE keeps whether the server ended its MOTD (motd_got): the session
   file holds it, and the restored server has it again. A session file
   without it (an older version) restores the server as before. */

#include <glib.h>

#include <irssi/src/common.h>
#include <irssi/src/core/chat-protocols.h>
#include <irssi/src/core/core.h>
#include <irssi/src/core/modules.h>
#include <irssi/src/core/net-sendbuffer.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/lib-config/iconfig.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>

/* irc-session.c */
void irc_session_init(void);
void irc_session_deinit(void);

#define MODULE_NAME "test-session-motd"

static IRC_SERVER_REC *server_new(void)
{
	IRC_SERVER_REC *server = g_new0(IRC_SERVER_REC, 1);

	MODULE_DATA_INIT(server);
	server->type = module_get_uniq_id("SERVER", 0);
	server->chat_type = chat_protocol_lookup("IRC");
	server->connrec = g_new0(IRC_SERVER_CONNECT_REC, 1);
	server->connrec->chat_type = server->chat_type;
	/* nothing buffered: the flush before saving writes nothing */
	server->handle = g_new0(NET_SENDBUF_REC, 1);
	return server;
}

static void free_isupport(gpointer key, gpointer value, gpointer data)
{
	g_free(key);
	g_free(value);
}

static void server_free(IRC_SERVER_REC *server)
{
	if (server->isupport != NULL) {
		g_hash_table_foreach(server->isupport, free_isupport, NULL);
		g_hash_table_destroy(server->isupport);
	}
	g_free(server->real_address);
	g_free(server->userhost);
	g_free(server->usermode);
	g_free(server->away_reason);
	g_free(server->connrec->sasl_username);
	g_free(server->connrec->sasl_password);
	g_free(server->connrec->channels);
	g_free(server->connrec);
	g_free(server->handle);
	MODULE_DATA_DEINIT(server);
	g_free(server);
}

/* what "session save server" writes for a server with motd_got */
static gboolean saved_motd_got(gboolean motd_got)
{
	IRC_SERVER_REC *server = server_new();
	CONFIG_REC *config = config_open(NULL, -1);
	CONFIG_NODE *node;
	gboolean saved;

	server->isupport = g_hash_table_new(g_str_hash, g_str_equal);
	server->motd_got = motd_got;
	node = config_node_section(config, config->mainnode, "server", NODE_TYPE_BLOCK);
	signal_emit("session save server", 3, server, config, node);
	saved = config_node_get_bool(node, "motd_got", FALSE);

	config_close(config);
	server_free(server);
	return saved;
}

/* motd_got of a server restored from a session file block */
static gboolean restored_motd_got(const char *block)
{
	IRC_SERVER_REC *server = server_new();
	CONFIG_REC *config = config_open(NULL, -1);
	CONFIG_NODE *node;
	gboolean motd_got;

	g_assert_cmpint(config_parse_data(config, block, "session"), ==, 0);
	node = config_node_section(NULL, config->mainnode, "server", -1);
	g_assert_nonnull(node);
	signal_emit("session restore server", 2, server, node);
	motd_got = server->motd_got;

	config_close(config);
	server_free(server);
	return motd_got;
}

static void test_save(void)
{
	g_assert_true(saved_motd_got(TRUE));
	g_assert_false(saved_motd_got(FALSE));
}

static void test_restore(void)
{
	g_assert_true(restored_motd_got("server = { motd_got = \"yes\"; };"));
	g_assert_false(restored_motd_got("server = { motd_got = \"no\"; };"));
	/* a session file written before motd_got was kept */
	g_assert_false(restored_motd_got("server = { isupport_sent = \"yes\"; };"));
}

int main(int argc, char **argv)
{
	CHAT_PROTOCOL_REC *rec;
	int res;

	g_test_init(&argc, &argv, NULL);

	core_preinit(*argv);
	irssi_gui = IRSSI_GUI_NONE;

	modules_init();
	signals_init();
	chat_protocols_init();
	rec = g_new0(CHAT_PROTOCOL_REC, 1);
	rec->name = "IRC";
	chat_protocol_register(rec);
	g_free(rec);
	irc_session_init();

	g_test_add_func("/test/session_motd/save", test_save);
	g_test_add_func("/test/session_motd/restore", test_restore);

#if GLIB_CHECK_VERSION(2,38,0)
	g_test_set_nonfatal_assertions();
#endif
	res = g_test_run();

	irc_session_deinit();
	chat_protocols_deinit();
	signals_deinit();
	modules_deinit();

	return res;
}
