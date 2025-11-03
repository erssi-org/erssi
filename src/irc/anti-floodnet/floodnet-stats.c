/*
   Statistics and status commands for anti-floodnet module
*/

#include "module.h"
#include "anti-floodnet.h"
#include <irssi/src/core/modules.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/commands.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/fe-common/core/printtext.h>

/* Global floodnet instance - declared in anti-floodnet.c */
extern ANTI_FLOODNET_REC *floodnet;

/* Reset daily statistics if it's a new day */
static void check_daily_reset(void)
{
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    struct tm *tm_last = localtime(&floodnet->last_reset_date);

    /* Check if day changed (different date) */
    if (tm_now->tm_year != tm_last->tm_year ||
        tm_now->tm_mon != tm_last->tm_mon ||
        tm_now->tm_mday != tm_last->tm_mday) {

        floodnet->flood_attempts_today = 0;
        floodnet->last_reset_date = now;
    }
}

/* Cleanup expired blocks for statistics */
static void cleanup_expired_blocks(void)
{
    time_t now = time(NULL);
    GHashTableIter iter;
    gpointer key, value;
    GList *keys_to_remove = NULL;
    GList *tmp;

    /* Cleanup expired message pattern blocks */
    g_hash_table_iter_init(&iter, floodnet->blocked_patterns);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        time_t *blocked_until = value;
        if (now >= *blocked_until) {
            keys_to_remove = g_list_prepend(keys_to_remove, g_strdup(key));
        }
    }

    for (tmp = keys_to_remove; tmp != NULL; tmp = tmp->next) {
        char *pattern_key = tmp->data;
        g_hash_table_remove(floodnet->blocked_patterns, pattern_key);
        g_free(pattern_key);
    }
    g_list_free(keys_to_remove);

    /* Cleanup expired CTCP blocks */
    keys_to_remove = NULL;
    g_hash_table_iter_init(&iter, floodnet->ctcp_blocked_until);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        time_t *blocked_until = value;
        if (now >= *blocked_until) {
            keys_to_remove = g_list_prepend(keys_to_remove, key);
        }
    }

    for (tmp = keys_to_remove; tmp != NULL; tmp = tmp->next) {
        IRC_SERVER_REC *server = tmp->data;
        g_hash_table_remove(floodnet->ctcp_blocked_until, server);
    }
    g_list_free(keys_to_remove);

    /* Cleanup expired nick channel blocks */
    keys_to_remove = NULL;
    g_hash_table_iter_init(&iter, floodnet->nick_blocked_channels);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        time_t *blocked_until = value;
        if (now >= *blocked_until) {
            keys_to_remove = g_list_prepend(keys_to_remove, g_strdup(key));
        }
    }

    for (tmp = keys_to_remove; tmp != NULL; tmp = tmp->next) {
        char *channel = tmp->data;
        g_hash_table_remove(floodnet->nick_blocked_channels, channel);
        g_free(channel);
    }
    g_list_free(keys_to_remove);
}

/* Get formatted time until expiry */
static char *format_time_remaining(time_t expiry_time)
{
    time_t now = time(NULL);
    int remaining = expiry_time - now;

    if (remaining <= 0)
        return g_strdup("expired");

    if (remaining < 60)
        return g_strdup_printf("%ds", remaining);

    if (remaining < 3600)
        return g_strdup_printf("%dm %ds", remaining / 60, remaining % 60);

    return g_strdup_printf("%dh %dm %ds",
                          remaining / 3600,
                          (remaining % 3600) / 60,
                          remaining % 60);
}

/* Main /floodnet status command */
void cmd_floodnet_status(const char *data)
{
    const char *cmd;
    void *free_arg;
    int active_blocks;
    int ctcp_blocks;
    int nick_blocks;
    GHashTableIter iter;
    gpointer key, value;
    int count = 0;
    char *pattern;
    time_t *blocked_until;
    char *time_str;
    IRC_SERVER_REC *server;
    char *channel;

    if (!cmd_get_params(data, &free_arg, 1, &cmd))
        return;

    if (*cmd == '\0' || g_ascii_strcasecmp(cmd, "status") == 0) {
        /* Main status display */
        check_daily_reset();
        cleanup_expired_blocks();

        active_blocks = g_hash_table_size(floodnet->blocked_patterns);
        ctcp_blocks = g_hash_table_size(floodnet->ctcp_blocked_until);
        nick_blocks = g_hash_table_size(floodnet->nick_blocked_channels);

        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Anti-floodnet Status:");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  Enabled: %s",
                 settings_get_bool("anti_floodnet_enabled") ? "YES" : "NO");

        if (settings_get_bool("anti_floodnet_enabled")) {
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  Flood attempts today: %d", floodnet->flood_attempts_today);
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  Total messages blocked: %d", floodnet->total_messages_blocked);
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  Current message window: %d messages", floodnet->message_count);
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  Active blocks:");
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "    Message patterns: %d", active_blocks);
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "    CTCP servers: %d", ctcp_blocks);
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "    Nick change channels: %d", nick_blocks);
        }

        printtext(NULL, NULL, MSGLEVEL_CRAP, "");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Thresholds: ~ident=%d, duplicate=%d, CTCP=%d, nick=%d",
                 floodnet->tilde_threshold, floodnet->duplicate_threshold,
                 floodnet->ctcp_threshold, floodnet->nickchange_threshold);
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Time windows: messages=%ds, nick=%ds, block=%ds",
                 floodnet->time_window, floodnet->nickchange_window,
                 floodnet->block_duration);

    } else if (g_ascii_strcasecmp(cmd, "reset") == 0) {
        /* Reset statistics */
        floodnet->flood_attempts_today = 0;
        floodnet->total_messages_blocked = 0;
        floodnet->last_reset_date = time(NULL);

        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Anti-floodnet statistics reset.");

    } else if (g_ascii_strcasecmp(cmd, "details") == 0) {
        /* Show detailed information about active blocks */
        check_daily_reset();
        cleanup_expired_blocks();

        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Anti-floodnet Detailed Status:");

        /* Show blocked message patterns */
        g_hash_table_iter_init(&iter, floodnet->blocked_patterns);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (count == 0) {
                printtext(NULL, NULL, MSGLEVEL_CRAP,
                         "  Blocked message patterns:");
            }

            pattern = key;
            blocked_until = value;
            time_str = format_time_remaining(*blocked_until);

            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "    %s (expires: %s)", pattern, time_str);
            g_free(time_str);
            count++;
        }
        if (count == 0) {
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  No blocked message patterns.");
        }

        /* Show CTCP blocked servers */
        count = 0;
        g_hash_table_iter_init(&iter, floodnet->ctcp_blocked_until);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (count == 0) {
                printtext(NULL, NULL, MSGLEVEL_CRAP,
                         "  CTCP blocked servers:");
            }

            server = key;
            blocked_until = value;
            time_str = format_time_remaining(*blocked_until);

            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "    %s (expires: %s)",
                     server ? server->tag : "unknown", time_str);
            g_free(time_str);
            count++;
        }
        if (count == 0) {
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  No CTCP blocked servers.");
        }

        /* Show nick blocked channels */
        count = 0;
        g_hash_table_iter_init(&iter, floodnet->nick_blocked_channels);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (count == 0) {
                printtext(NULL, NULL, MSGLEVEL_CRAP,
                         "  Nick change blocked channels:");
            }

            channel = key;
            blocked_until = value;
            time_str = format_time_remaining(*blocked_until);

            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "    %s (expires: %s)", channel, time_str);
            g_free(time_str);
            count++;
        }
        if (count == 0) {
            printtext(NULL, NULL, MSGLEVEL_CRAP,
                     "  No nick change blocked channels.");
        }

    } else if (g_ascii_strcasecmp(cmd, "help") == 0) {
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Anti-floodnet commands:");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  /FLOODNET              - Show status");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  /FLOODNET STATUS       - Show status");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  /FLOODNET RESET        - Reset statistics");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  /FLOODNET DETAILS      - Show detailed block information");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  /FLOODNET HELP         - Show this help");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Settings (use /SET to change):");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_enabled");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_tilde_threshold");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_duplicate_threshold");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_ctcp_threshold");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_nickchange_threshold");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_block_duration");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_time_window");
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "  anti_floodnet_nickchange_window");

    } else {
        printtext(NULL, NULL, MSGLEVEL_CRAP,
                 "Unknown floodnet command. Use /FLOODNET HELP for help.");
    }

    cmd_params_free(free_arg);
}