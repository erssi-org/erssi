/*
   Nick Change Flood Detection for anti-floodnet module
   Detects mass nick changes and blocks nick events for affected channels
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
#include <irssi/src/core/channels.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/printtext.h>

/* Global floodnet instance - declared in anti-floodnet.c */
extern ANTI_FLOODNET_REC *floodnet;

/* Check if nick events are blocked for this channel */
gboolean is_nick_channel_blocked(const char *channel)
{
    time_t *blocked_until;
    time_t current_time;

    if (!channel)
        return FALSE;

    blocked_until = g_hash_table_lookup(floodnet->nick_blocked_channels, channel);
    current_time = time(NULL);

    if (blocked_until && current_time < *blocked_until) {
        return TRUE;
    }

    /* Expired entry, remove it */
    if (blocked_until) {
        g_hash_table_remove(floodnet->nick_blocked_channels, channel);
    }

    return FALSE;
}

/* Get or create channel nick flood record */
static CHANNEL_NICKFLOOD_REC *get_channel_nickflood_rec(const char *channel)
{
    CHANNEL_NICKFLOOD_REC *rec = g_hash_table_lookup(floodnet->channel_nick_changes, channel);

    if (!rec) {
        rec = g_new0(CHANNEL_NICKFLOOD_REC, 1);
        g_hash_table_insert(floodnet->channel_nick_changes,
                           g_strdup(channel), rec);
    }

    return rec;
}

/* Clean up old nick changes outside the time window */
static void cleanup_old_nick_changes(CHANNEL_NICKFLOOD_REC *rec, time_t now)
{
    GSList *tmp, *next;
    time_t cutoff = now - floodnet->nickchange_window;

    for (tmp = rec->nick_changes; tmp != NULL; tmp = next) {
        NICKCHANGE_REC *change = tmp->data;
        next = tmp->next;

        if (change->timestamp < cutoff) {
            rec->nick_changes = g_slist_remove(rec->nick_changes, change);
            rec->change_count--;
            g_free(change->old_nick);
            g_free(change->new_nick);
            g_free(change);
        }
    }
}

/* Check for nick flood and block if necessary */
void check_nick_flood(IRC_SERVER_REC *server, const char *channel,
                      const char *old_nick, const char *new_nick)
{
    CHANNEL_NICKFLOOD_REC *rec;
    time_t now;
    NICKCHANGE_REC *change;

    if (!settings_get_bool("anti_floodnet_enabled"))
        return;

    if (!channel || !old_nick || !new_nick)
        return;

    /* Check if already blocked for this channel */
    if (is_nick_channel_blocked(channel)) {
        signal_stop();
        floodnet->total_messages_blocked++;
        floodnet->blocked_since_notice++;
        return;
    }

    rec = get_channel_nickflood_rec(channel);
    now = time(NULL);

    /* Clean up old entries */
    cleanup_old_nick_changes(rec, now);

    /* Add current nick change */
    change = g_new0(NICKCHANGE_REC, 1);
    change->timestamp = now;
    change->old_nick = g_strdup(old_nick);
    change->new_nick = g_strdup(new_nick);

    rec->nick_changes = g_slist_prepend(rec->nick_changes, change);
    rec->change_count++;

    /* Check if threshold exceeded */
    if (rec->change_count >= floodnet->nickchange_threshold) {
        /* Block all nick events for this channel */
        time_t *blocked_until;

        enter_protection_mode();

        blocked_until = g_new(time_t, 1);
        *blocked_until = now + floodnet->block_duration;

        g_hash_table_insert(floodnet->nick_blocked_channels,
                           g_strdup(channel), blocked_until);

        floodnet->flood_attempts_today++;
        floodnet->total_messages_blocked++;
        floodnet->blocked_since_notice++;

        /* Block this current nick change */
        signal_stop();
        return;
    }
}

/* Signal handler for NICK events */
static void sig_event_nick(IRC_SERVER_REC *server, const char *data,
                          const char *nick, const char *address)
{
    GSList *tmp;
    char *new_nick;
    WI_ITEM_REC *item;
    CHANNEL_REC *channel;
    NICK_REC *usernick;

    if (!IS_IRC_SERVER(server))
        return;

    new_nick = g_strdup(data);

    /* Check all channels we share with this user */
    for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
        item = tmp->data;
        if (!IS_CHANNEL(item))
            continue;

        channel = (CHANNEL_REC *)item;

        /* Check if user is on this channel */
        usernick = nicklist_find(channel, new_nick);
        if (usernick && usernick->host) {
            /* User is changing nick while on this channel */
            check_nick_flood(server, channel->name, nick, new_nick);
        }
    }

    g_free(new_nick);
}

/* Pre-check for nick events - block if channel is in flood mode */
static void sig_nick_pre_check(IRC_SERVER_REC *server, const char *new_nick,
                              const char *old_nick, const char *address)
{
    GSList *tmp;

    if (!settings_get_bool("anti_floodnet_enabled"))
        return;

    if (!IS_IRC_SERVER(server))
        return;

    /* Check all channels we might be affected */
    for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
        WI_ITEM_REC *item;
        CHANNEL_REC *channel;

        item = tmp->data;
        if (!IS_CHANNEL(item))
            continue;

        channel = (CHANNEL_REC *)item;

        if (is_nick_channel_blocked(channel->name)) {
            /* This channel is in nick flood mode */
            signal_stop();
            return;
        }
    }
}

/* Initialize nick change flood protection */
void nick_flood_init(void)
{
    /* Register nick change signal handlers */
    signal_add_first("event nick", (SIGNAL_FUNC) sig_event_nick);
    signal_add_first("nicklist new", (SIGNAL_FUNC) sig_nick_pre_check);
    signal_add_first("nicklist changed", (SIGNAL_FUNC) sig_nick_pre_check);
    signal_add_first("nicklist host changed", (SIGNAL_FUNC) sig_nick_pre_check);
}

/* Deinitialize nick change flood protection */
void nick_flood_deinit(void)
{
    /* Remove signal handlers */
    signal_remove("event nick", (SIGNAL_FUNC) sig_event_nick);
    signal_remove("nicklist new", (SIGNAL_FUNC) sig_nick_pre_check);
    signal_remove("nicklist changed", (SIGNAL_FUNC) sig_nick_pre_check);
    signal_remove("nicklist host changed", (SIGNAL_FUNC) sig_nick_pre_check);
}