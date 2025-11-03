/*
   Anti-floodnet module for erssi/irssi
   Detects and blocks floodnets using pattern recognition:
   - ~ident detection (5+ messages with ~user@host)
   - Duplicate message detection (3+ identical messages)
   - CTCP flood protection (5+ CTCP queries)
   - Nick change flood detection (10+ nick changes in 3s)

   Copyright (C) 2025 erssi project
*/

#include "module.h"
#include "anti-floodnet.h"
#include <irssi/src/core/modules.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/fe-common/core/printtext.h>

/* Function prototypes for component initialization */
void ctcp_flood_init(void);
void ctcp_flood_deinit(void);
void nick_flood_init(void);
void nick_flood_deinit(void);

ANTI_FLOODNET_REC *floodnet = NULL;

/* Read settings from irssi configuration */
static void read_settings(void)
{
    floodnet->tilde_threshold = settings_get_int("anti_floodnet_tilde_threshold");
    if (floodnet->tilde_threshold == 0)
        floodnet->tilde_threshold = DEFAULT_TILDE_THRESHOLD;

    floodnet->duplicate_threshold = settings_get_int("anti_floodnet_duplicate_threshold");
    if (floodnet->duplicate_threshold == 0)
        floodnet->duplicate_threshold = DEFAULT_DUPLICATE_THRESHOLD;

    floodnet->ctcp_threshold = settings_get_int("anti_floodnet_ctcp_threshold");
    if (floodnet->ctcp_threshold == 0)
        floodnet->ctcp_threshold = DEFAULT_CTCP_THRESHOLD;

    floodnet->nickchange_threshold = settings_get_int("anti_floodnet_nickchange_threshold");
    if (floodnet->nickchange_threshold == 0)
        floodnet->nickchange_threshold = DEFAULT_NICKCHANGE_THRESHOLD;

    floodnet->block_duration = settings_get_int("anti_floodnet_block_duration");
    if (floodnet->block_duration == 0)
        floodnet->block_duration = DEFAULT_BLOCK_DURATION;

    floodnet->time_window = settings_get_int("anti_floodnet_time_window");
    if (floodnet->time_window == 0)
        floodnet->time_window = DEFAULT_TIME_WINDOW;

    floodnet->nickchange_window = settings_get_int("anti_floodnet_nickchange_window");
    if (floodnet->nickchange_window == 0)
        floodnet->nickchange_window = DEFAULT_NICKCHANGE_WINDOW;

    floodnet->protection_notice_interval = settings_get_int("anti_floodnet_notice_interval");
    if (floodnet->protection_notice_interval == 0)
        floodnet->protection_notice_interval = 60;  /* Default: 60s */
}

/* Settings changed signal */
static void sig_settings_changed(void)
{
    read_settings();
}

/* Check if userhost contains ~ident */
gboolean check_tilde_ident(const char *userhost)
{
    const char *excl_mark, *tilde;

    if (!userhost)
        return FALSE;

    /* Find the ! in nick!user@host format */
    excl_mark = strchr(userhost, '!');
    if (!excl_mark)
        return FALSE;

    /* Check for ~ after the ! (in user part) */
    tilde = strchr(excl_mark + 1, '~');
    return (tilde != NULL && tilde < strchr(excl_mark, '@'));
}

/* Add message to tracking window */
static void add_message_to_window(const char *nick, const char *userhost,
                                  const char *text, time_t timestamp)
{
    FLOODMSG_REC *rec = g_new0(FLOODMSG_REC, 1);

    rec->timestamp = timestamp;
    rec->nick = g_strdup(nick);
    rec->userhost = g_strdup(userhost);
    rec->text = g_strdup(text);
    rec->has_tilde = check_tilde_ident(userhost);

    floodnet->message_window = g_slist_prepend(floodnet->message_window, rec);
    floodnet->message_count++;
}

/* Free message record */
static void free_floodmsg_rec(FLOODMSG_REC *rec)
{
    if (!rec)
        return;

    g_free(rec->nick);
    g_free(rec->userhost);
    g_free(rec->text);
    g_free(rec);
}

/* Clean up old messages outside the time window */
void cleanup_old_messages(time_t now)
{
    GSList *tmp, *next;
    time_t cutoff = now - floodnet->time_window;

    for (tmp = floodnet->message_window; tmp != NULL; tmp = next) {
        FLOODMSG_REC *rec = tmp->data;
        next = tmp->next;

        if (rec->timestamp < cutoff) {
            floodnet->message_window = g_slist_remove(floodnet->message_window, rec);
            floodnet->message_count--;
            free_floodmsg_rec(rec);
        }
    }
}

/* Count tilde ident users in current window */
static int count_tilde_users(void)
{
    GSList *tmp;
    int count = 0;

    for (tmp = floodnet->message_window; tmp != NULL; tmp = tmp->next) {
        FLOODMSG_REC *rec = tmp->data;
        if (rec->has_tilde)
            count++;
    }

    return count;
}

/* Find most common message and its count */
static char *find_most_common_message(int *count)
{
    GHashTable *freqs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GSList *tmp;
    char *most_common = NULL;
    int max_count = 0;
    FLOODMSG_REC *rec;
    int current_count;
    GHashTableIter iter;
    gpointer key, value;
    int cnt;

    /* Count message frequencies */
    for (tmp = floodnet->message_window; tmp != NULL; tmp = tmp->next) {
        rec = tmp->data;
        current_count = GPOINTER_TO_INT(g_hash_table_lookup(freqs, rec->text));

        g_hash_table_insert(freqs, g_strdup(rec->text),
                           GINT_TO_POINTER(current_count + 1));
    }

    /* Find most frequent message */
    g_hash_table_iter_init(&iter, freqs);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        cnt = GPOINTER_TO_INT(value);
        if (cnt > max_count) {
            max_count = cnt;
            most_common = g_strdup(key);
        }
    }

    g_hash_table_destroy(freqs);
    *count = max_count;
    return most_common;
}

/* Check if message is currently blocked */
gboolean is_message_blocked(const char *text)
{
    time_t now = time(NULL);
    time_t *blocked_until = g_hash_table_lookup(floodnet->blocked_patterns, text);

    if (blocked_until && now < *blocked_until) {
        return TRUE;
    }

    /* Expired entry, remove it */
    if (blocked_until) {
        g_hash_table_remove(floodnet->blocked_patterns, text);
    }

    return FALSE;
}

/* Block a specific message text for given duration */
void block_duplicate_message(const char *text, int duration)
{
    time_t *blocked_until = g_new(time_t, 1);
    *blocked_until = time(NULL) + duration;

    g_hash_table_insert(floodnet->blocked_patterns,
                        g_strdup(text), blocked_until);
}

/* Enter flood protection mode */
void enter_protection_mode(void)
{
    time_t now = time(NULL);

    if (!floodnet->in_protection_mode) {
        /* First time entering protection mode */
        floodnet->in_protection_mode = TRUE;
        floodnet->protection_started = now;
        floodnet->last_protection_notice = now;
        floodnet->blocked_since_notice = 0;

        printtext(NULL, NULL, MSGLEVEL_CRAP | MSGLEVEL_NOHILIGHT,
                 "*** Anti-Floodnet: PROTECTION MODE ACTIVATED - blocking flood");
    }
}

/* Exit flood protection mode */
void exit_protection_mode(void)
{
    int duration;

    if (floodnet->in_protection_mode) {
        duration = (int)(time(NULL) - floodnet->protection_started);
        
        printtext(NULL, NULL, MSGLEVEL_CRAP | MSGLEVEL_NOHILIGHT,
                 "*** Anti-Floodnet: Protection ended (duration: %ds, blocked: %d messages)",
                 duration, floodnet->total_messages_blocked);

        floodnet->in_protection_mode = FALSE;
        floodnet->blocked_since_notice = 0;
    }
}

/* Check protection status and show periodic updates */
void check_protection_status(void)
{
    time_t now = time(NULL);
    int elapsed;

    if (!floodnet->in_protection_mode)
        return;

    /* Check if we should show periodic update */
    elapsed = (int)(now - floodnet->last_protection_notice);
    
    if (elapsed >= floodnet->protection_notice_interval) {
        int total_duration = (int)(now - floodnet->protection_started);
        
        printtext(NULL, NULL, MSGLEVEL_CRAP | MSGLEVEL_NOHILIGHT,
                 "*** Anti-Floodnet: Still active (%ds elapsed, %d blocked since last notice)",
                 total_duration, floodnet->blocked_since_notice);
        
        floodnet->last_protection_notice = now;
        floodnet->blocked_since_notice = 0;
    }

    /* Auto-exit if no flood activity in last time_window */
    if (floodnet->message_count == 0 && 
        (now - floodnet->protection_started) > (floodnet->block_duration + floodnet->time_window)) {
        exit_protection_mode();
    }
}

/* Main message flood detection */
void check_message_flood(IRC_SERVER_REC *server, const char *nick,
                         const char *address, const char *text)
{
    time_t now = time(NULL);
    char *userhost;
    GSList *tmp;
    FLOODMSG_REC *rec;

    if (!settings_get_bool("anti_floodnet_enabled"))
        return;

    /* Check protection status first */
    check_protection_status();

    /* Build full userhost if only address is provided */
    if (strchr(address, '!') != NULL) {
        userhost = g_strdup(address);
    } else {
        userhost = g_strdup_printf("%s!%s", nick, address);
    }

    /* Clean up old messages first */
    cleanup_old_messages(now);

    /* If in protection mode, check if message is blocked */
    if (floodnet->in_protection_mode) {
        if (is_message_blocked(text) || check_tilde_ident(userhost)) {
            floodnet->total_messages_blocked++;
            floodnet->blocked_since_notice++;
            g_free(userhost);
            signal_stop();
            return;
        }
    }

    /* Add current message */
    add_message_to_window(nick, userhost, text, now);

    /* Check if we have enough messages to analyze */
    if (floodnet->message_count >= floodnet->tilde_threshold) {
        int tilde_count = count_tilde_users();

        /* Pattern A: ~ident flood detection */
        if (tilde_count >= floodnet->tilde_threshold) {
            enter_protection_mode();
            
            floodnet->flood_attempts_today++;
            floodnet->total_messages_blocked++;
            floodnet->blocked_since_notice++;

            /* Block all messages in current window */
            for (tmp = floodnet->message_window; tmp != NULL; tmp = tmp->next) {
                rec = tmp->data;
                if (!is_message_blocked(rec->text)) {
                    block_duplicate_message(rec->text, floodnet->block_duration);
                }
            }

            g_free(userhost);
            signal_stop();
            return;
        }
    }

    /* Pattern B: Duplicate message detection */
    if (floodnet->message_count >= 5) {
        int duplicate_count;
        char *most_common = find_most_common_message(&duplicate_count);

        if (duplicate_count >= floodnet->duplicate_threshold) {
            if (!is_message_blocked(most_common)) {
                enter_protection_mode();
                block_duplicate_message(most_common, floodnet->block_duration);
                floodnet->flood_attempts_today++;
            }

            if (strcmp(text, most_common) == 0) {
                floodnet->total_messages_blocked++;
                floodnet->blocked_since_notice++;
                g_free(most_common);
                g_free(userhost);
                signal_stop();
                return;
            }
        }
        g_free(most_common);
    }

    g_free(userhost);
}

/* Signal handlers for PRIVMSG */
static void sig_event_privmsg(IRC_SERVER_REC *server, const char *data,
                              const char *nick, const char *address)
{
    char *params, *target, *text;

    if (!IS_IRC_SERVER(server))
        return;

    params = event_get_params(data, 2, &target, &text);

    /* Check if this is a private message to us */
    if (strcmp(target, server->nick) == 0) {
        check_message_flood(server, nick, address, text);
    }

    g_free(params);
}

/* Initialize anti-floodnet module */
void irc_anti_floodnet_init(void)
{
    floodnet = g_new0(ANTI_FLOODNET_REC, 1);

    /* Initialize hash tables */
    floodnet->channel_nick_changes = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                          g_free,
                                                          (GDestroyNotify)g_slist_free);
    floodnet->blocked_patterns = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                      g_free, g_free);
    floodnet->ctcp_blocked_until = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                        NULL, g_free);
    floodnet->nick_blocked_channels = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                           g_free, g_free);

    /* Initialize statistics */
    floodnet->last_reset_date = time(NULL);
    floodnet->flood_attempts_today = 0;
    floodnet->total_messages_blocked = 0;
    
    /* Initialize protection mode state */
    floodnet->in_protection_mode = FALSE;
    floodnet->protection_started = 0;
    floodnet->last_protection_notice = 0;
    floodnet->blocked_since_notice = 0;

    /* Read initial settings */
    read_settings();

    /* Add settings */
    settings_add_bool("anti_floodnet", "anti_floodnet_enabled", TRUE);
    settings_add_int("anti_floodnet", "anti_floodnet_tilde_threshold", DEFAULT_TILDE_THRESHOLD);
    settings_add_int("anti_floodnet", "anti_floodnet_duplicate_threshold", DEFAULT_DUPLICATE_THRESHOLD);
    settings_add_int("anti_floodnet", "anti_floodnet_ctcp_threshold", DEFAULT_CTCP_THRESHOLD);
    settings_add_int("anti_floodnet", "anti_floodnet_nickchange_threshold", DEFAULT_NICKCHANGE_THRESHOLD);
    settings_add_int("anti_floodnet", "anti_floodnet_block_duration", DEFAULT_BLOCK_DURATION);
    settings_add_int("anti_floodnet", "anti_floodnet_time_window", DEFAULT_TIME_WINDOW);
    settings_add_int("anti_floodnet", "anti_floodnet_nickchange_window", DEFAULT_NICKCHANGE_WINDOW);
    settings_add_int("anti_floodnet", "anti_floodnet_notice_interval", 60);

    /* Initialize component modules */
    ctcp_flood_init();
    nick_flood_init();

    /* Add signal handlers */
    signal_add("event privmsg", (SIGNAL_FUNC) sig_event_privmsg);
    signal_add("setup changed", (SIGNAL_FUNC) sig_settings_changed);

    /* Add command */
    command_bind("floodnet", NULL, (SIGNAL_FUNC) cmd_floodnet_status);

    module_register("anti_floodnet", "irc");
}

/* Cleanup anti-floodnet module */
void irc_anti_floodnet_deinit(void)
{
    if (!floodnet)
        return;

    /* Cleanup component modules */
    ctcp_flood_deinit();
    nick_flood_deinit();

    /* Remove signal handlers */
    signal_remove("event privmsg", (SIGNAL_FUNC) sig_event_privmsg);
    signal_remove("setup changed", (SIGNAL_FUNC) sig_settings_changed);

    /* Remove command */
    command_unbind("floodnet", (SIGNAL_FUNC) cmd_floodnet_status);

    /* Free message window */
    g_slist_free_full(floodnet->message_window, (GDestroyNotify) free_floodmsg_rec);

    /* Free hash tables */
    g_hash_table_destroy(floodnet->channel_nick_changes);
    g_hash_table_destroy(floodnet->blocked_patterns);
    g_hash_table_destroy(floodnet->ctcp_blocked_until);
    g_hash_table_destroy(floodnet->nick_blocked_channels);

    g_free(floodnet);
    floodnet = NULL;
}

MODULE_ABICHECK(irc_anti_floodnet)