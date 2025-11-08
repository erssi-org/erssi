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

/* Cleanup function for CHANNEL_NICKFLOOD_REC */
void free_channel_nickflood_rec(gpointer data)
{
    CHANNEL_NICKFLOOD_REC *rec = (CHANNEL_NICKFLOOD_REC *)data;
    GSList *tmp;

    if (!rec)
        return;

    /* Free all NICKCHANGE_REC in the list */
    for (tmp = rec->nick_changes; tmp != NULL; tmp = tmp->next) {
        NICKCHANGE_REC *change = tmp->data;
        g_free(change->old_nick);
        g_free(change->new_nick);
        g_free(change);
    }

    /* Free the list itself */
    g_slist_free(rec->nick_changes);

    /* Free the record */
    g_free(rec);
}

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

/* Extend or create channel block */
static void extend_channel_block(const char *channel)
{
    time_t *blocked_until;
    time_t now = time(NULL);
    
    blocked_until = g_hash_table_lookup(floodnet->nick_blocked_channels, channel);
    
    if (blocked_until) {
        /* Extend existing block */
        *blocked_until = now + floodnet->block_duration;
    } else {
        /* Create new block */
        blocked_until = g_new(time_t, 1);
        *blocked_until = now + floodnet->block_duration;
        g_hash_table_insert(floodnet->nick_blocked_channels,
                           g_strdup(channel), blocked_until);
    }
}

/* Signal handler for "message nick" - block display if nick is on any blocked channel */
static void sig_message_nick(IRC_SERVER_REC *server, const char *newnick,
                             const char *oldnick, const char *address)
{
    GSList *tmp;

    if (!settings_get_bool("anti_floodnet_enabled"))
        return;

    if (!IS_IRC_SERVER(server))
        return;

    /* Check if nick is on any blocked channel */
    for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
        CHANNEL_REC *channel = tmp->data;
        
        if (!IS_CHANNEL(channel))
            continue;

        /* Check if this channel is blocked and user is on it */
        if (is_nick_channel_blocked(channel->name)) {
            /* Check if the nick that changed is actually on this channel */
            if (nicklist_find(channel, newnick) != NULL) {
                /* Block display and extend protection */
                extend_channel_block(channel->name);
                floodnet->total_messages_blocked++;
                floodnet->blocked_since_notice++;
                signal_stop();
                return;
            }
        }
    }
}

/* Signal handler for "nicklist changed" - fires once per channel where nick changed */
static void sig_nicklist_changed(CHANNEL_REC *channel, NICK_REC *nick, const char *oldnick)
{
    CHANNEL_NICKFLOOD_REC *rec;
    NICKCHANGE_REC *change;
    time_t now;

    if (!settings_get_bool("anti_floodnet_enabled"))
        return;

    if (!channel || !nick || !oldnick)
        return;

    /* Don't track if already blocked - just tracking, not blocking display here */
    if (is_nick_channel_blocked(channel->name)) {
        /* Extend block - flood is still happening */
        extend_channel_block(channel->name);
        return;
    }

    rec = get_channel_nickflood_rec(channel->name);
    now = time(NULL);

    /* Clean up old entries */
    cleanup_old_nick_changes(rec, now);

    /* Add current nick change */
    change = g_new0(NICKCHANGE_REC, 1);
    change->timestamp = now;
    change->old_nick = g_strdup(oldnick);
    change->new_nick = g_strdup(nick->nick);

    rec->nick_changes = g_slist_prepend(rec->nick_changes, change);
    rec->change_count++;

    /* Check if threshold exceeded */
    if (rec->change_count >= floodnet->nickchange_threshold) {
        enter_protection_mode();
        extend_channel_block(channel->name);

        floodnet->flood_attempts_today++;
        /* Note: We don't block here - "message nick" handler will block display */
    }
}

/* Initialize nick change flood protection */
void nick_flood_init(void)
{
    /* Register for "nicklist changed" - track flood per channel */
    signal_add_first("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
    
    /* Register for "message nick" - block display if on blocked channel */
    signal_add_first("message nick", (SIGNAL_FUNC) sig_message_nick);
}

/* Deinitialize nick change flood protection */
void nick_flood_deinit(void)
{
    signal_remove("nicklist changed", (SIGNAL_FUNC) sig_nicklist_changed);
    signal_remove("message nick", (SIGNAL_FUNC) sig_message_nick);
}
