/*
   CTCP Flood Protection for anti-floodnet module
   Blocks all incoming CTCP when threshold is exceeded
*/

#include "module.h"
#include "anti-floodnet.h"
#include <irssi/src/core/modules.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/fe-common/core/printtext.h>

/* Global floodnet instance - declared in anti-floodnet.c */
extern ANTI_FLOODNET_REC *floodnet;

/* CTCP tracking per server */
typedef struct {
    GSList *ctcp_timestamps;  /* List of time_t* */
    int ctcp_count;
} SERVER_CTCP_REC;

/* Hash table: server -> SERVER_CTCP_REC */
static GHashTable *ctcp_tracking = NULL;

/* Get or create CTCP tracking record for server */
static SERVER_CTCP_REC *get_server_ctcp_rec(IRC_SERVER_REC *server)
{
    SERVER_CTCP_REC *rec = g_hash_table_lookup(ctcp_tracking, server);

    if (!rec) {
        rec = g_new0(SERVER_CTCP_REC, 1);
        g_hash_table_insert(ctcp_tracking, server, rec);
    }

    return rec;
}

/* Clean up old CTCP entries outside time window */
static void cleanup_old_ctcp(SERVER_CTCP_REC *rec, time_t now)
{
    GSList *tmp, *next;
    time_t cutoff = now - floodnet->time_window;

    for (tmp = rec->ctcp_timestamps; tmp != NULL; tmp = next) {
        time_t *timestamp = tmp->data;
        next = tmp->next;

        if (*timestamp < cutoff) {
            rec->ctcp_timestamps = g_slist_remove(rec->ctcp_timestamps, timestamp);
            g_free(timestamp);
            rec->ctcp_count--;
        }
    }
}

/* Check if CTCP is blocked for this server */
gboolean is_ctcp_blocked(IRC_SERVER_REC *server)
{
    time_t *blocked_until;
    time_t current_time;

    if (!server)
        return FALSE;

    blocked_until = g_hash_table_lookup(floodnet->ctcp_blocked_until, server);
    current_time = time(NULL);

    if (blocked_until && current_time < *blocked_until) {
        return TRUE;
    }

    /* Expired entry, remove it */
    if (blocked_until) {
        g_hash_table_remove(floodnet->ctcp_blocked_until, server);
    }

    return FALSE;
}

/* Handle incoming CTCP requests */
void check_ctcp_flood(IRC_SERVER_REC *server, const char *nick,
                      const char *address, const char *cmd, const char *data)
{
    SERVER_CTCP_REC *rec;
    time_t now;
    time_t *timestamp;

    if (!settings_get_bool("anti_floodnet_enabled"))
        return;

    if (!IS_IRC_SERVER(server))
        return;

    /* Check if already blocked */
    if (is_ctcp_blocked(server)) {
        signal_stop();
        floodnet->total_messages_blocked++;
        floodnet->blocked_since_notice++;
        return;
    }

    rec = get_server_ctcp_rec(server);
    now = time(NULL);

    /* Clean up old entries */
    cleanup_old_ctcp(rec, now);

    /* Add current CTCP */
    timestamp = g_new(time_t, 1);
    *timestamp = now;
    rec->ctcp_timestamps = g_slist_prepend(rec->ctcp_timestamps, timestamp);
    rec->ctcp_count++;

    /* Check if threshold exceeded */
    if (rec->ctcp_count >= floodnet->ctcp_threshold) {
        /* Block all CTCP for this server */
        time_t *blocked_until;

        enter_protection_mode();

        blocked_until = g_new(time_t, 1);
        *blocked_until = now + floodnet->block_duration;

        g_hash_table_insert(floodnet->ctcp_blocked_until, server, blocked_until);

        floodnet->flood_attempts_today++;
        floodnet->total_messages_blocked++;
        floodnet->blocked_since_notice++;

        /* Block this current CTCP */
        signal_stop();
        return;
    }
}

/* Signal handlers for various CTCP events */
static void sig_ctcp_version(IRC_SERVER_REC *server, const char *data,
                             const char *nick, const char *address)
{
    check_ctcp_flood(server, nick, address, "VERSION", data);
}

static void sig_ctcp_ping(IRC_SERVER_REC *server, const char *data,
                         const char *nick, const char *address)
{
    check_ctcp_flood(server, nick, address, "PING", data);
}

static void sig_ctcp_time(IRC_SERVER_REC *server, const char *data,
                         const char *nick, const char *address)
{
    check_ctcp_flood(server, nick, address, "TIME", data);
}

static void sig_ctcp_clientinfo(IRC_SERVER_REC *server, const char *data,
                               const char *nick, const char *address)
{
    check_ctcp_flood(server, nick, address, "CLIENTINFO", data);
}

static void sig_ctcp_userinfo(IRC_SERVER_REC *server, const char *data,
                             const char *nick, const char *address)
{
    check_ctcp_flood(server, nick, address, "USERINFO", data);
}

static void sig_ctcp_finger(IRC_SERVER_REC *server, const char *data,
                           const char *nick, const char *address)
{
    check_ctcp_flood(server, nick, address, "FINGER", data);
}

/* Generic CTCP handler for unknown CTCP commands */
static void sig_ctcp_default(IRC_SERVER_REC *server, const char *data,
                            const char *nick, const char *address, const char *cmd)
{
    check_ctcp_flood(server, nick, address, cmd, data);
}

/* Cleanup function for CTCP tracking */
static void cleanup_server_ctcp(gpointer key, gpointer value, gpointer user_data)
{
    SERVER_CTCP_REC *rec = value;

    g_slist_free_full(rec->ctcp_timestamps, g_free);
    g_free(rec);
}

/* Initialize CTCP flood protection */
void ctcp_flood_init(void)
{
    if (ctcp_tracking)
        return;

    ctcp_tracking = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, NULL);

    /* Register CTCP signal handlers */
    signal_add_first("ctcp msg version", (SIGNAL_FUNC) sig_ctcp_version);
    signal_add_first("ctcp msg ping", (SIGNAL_FUNC) sig_ctcp_ping);
    signal_add_first("ctcp msg time", (SIGNAL_FUNC) sig_ctcp_time);
    signal_add_first("ctcp msg clientinfo", (SIGNAL_FUNC) sig_ctcp_clientinfo);
    signal_add_first("ctcp msg userinfo", (SIGNAL_FUNC) sig_ctcp_userinfo);
    signal_add_first("ctcp msg finger", (SIGNAL_FUNC) sig_ctcp_finger);
    signal_add_first("default ctcp msg", (SIGNAL_FUNC) sig_ctcp_default);
}

/* Deinitialize CTCP flood protection */
void ctcp_flood_deinit(void)
{
    if (!ctcp_tracking)
        return;

    /* Remove signal handlers */
    signal_remove("ctcp msg version", (SIGNAL_FUNC) sig_ctcp_version);
    signal_remove("ctcp msg ping", (SIGNAL_FUNC) sig_ctcp_ping);
    signal_remove("ctcp msg time", (SIGNAL_FUNC) sig_ctcp_time);
    signal_remove("ctcp msg clientinfo", (SIGNAL_FUNC) sig_ctcp_clientinfo);
    signal_remove("ctcp msg userinfo", (SIGNAL_FUNC) sig_ctcp_userinfo);
    signal_remove("ctcp msg finger", (SIGNAL_FUNC) sig_ctcp_finger);
    signal_remove("default ctcp msg", (SIGNAL_FUNC) sig_ctcp_default);

    /* Cleanup tracking data */
    g_hash_table_foreach(ctcp_tracking, cleanup_server_ctcp, NULL);
    g_hash_table_destroy(ctcp_tracking);
    ctcp_tracking = NULL;
}