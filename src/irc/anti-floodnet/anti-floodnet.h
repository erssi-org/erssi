#ifndef IRSSI_SRC_IRC_ANTI_FLOODNET_H
#define IRSSI_SRC_IRC_ANTI_FLOODNET_H

#include <irssi/src/common.h>
#include <irssi/src/core/modules.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/channels.h>
#include <irssi/src/core/nicklist.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/fe-common/core/printtext.h>

/* Default settings */
#define DEFAULT_TILDE_THRESHOLD 5
#define DEFAULT_DUPLICATE_THRESHOLD 3
#define DEFAULT_CTCP_THRESHOLD 5
#define DEFAULT_NICKCHANGE_THRESHOLD 5
#define DEFAULT_BLOCK_DURATION 60
#define DEFAULT_TIME_WINDOW 5
#define DEFAULT_NICKCHANGE_WINDOW 3

/* Message record for flood detection */
typedef struct {
    time_t timestamp;
    char *nick;          /* Nick without !user@host */
    char *userhost;      /* Full nick!user@host */
    char *text;          /* Message text */
    gboolean has_tilde;  /* Contains ~ident */
} FLOODMSG_REC;

/* Channel nick change tracking */
typedef struct {
    time_t timestamp;
    char *old_nick;
    char *new_nick;
} NICKCHANGE_REC;

/* Per-channel nick flood tracking */
typedef struct {
    GSList *nick_changes;      /* List of NICKCHANGE_REC */
    int change_count;
} CHANNEL_NICKFLOOD_REC;

/* Main anti-floodnet structure */
typedef struct {
    /* Message flood detection */
    GSList *message_window;        /* List of FLOODMSG_REC */
    int message_count;

    /* Nick change flood detection */
    GHashTable *channel_nick_changes; /* channel -> CHANNEL_NICKFLOOD_REC */

    /* Blocking states */
    GHashTable *blocked_patterns;      /* blocked_text -> expiry_time */
    GHashTable *ctcp_blocked_until;    /* server -> expiry_time */
    GHashTable *nick_blocked_channels; /* channel -> expiry_time */

    /* Statistics */
    int flood_attempts_today;          /* Reset at midnight */
    time_t last_reset_date;
    int total_messages_blocked;

    /* Flood protection mode state */
    gboolean in_protection_mode;       /* Currently in flood protection */
    time_t protection_started;         /* When protection started */
    time_t last_protection_notice;     /* Last time we showed notice */
    int protection_notice_interval;    /* Show notice every N seconds (60) */
    int blocked_since_notice;          /* Messages blocked since last notice */

    /* Settings cache */
    int tilde_threshold;
    int duplicate_threshold;
    int ctcp_threshold;
    int nickchange_threshold;
    int block_duration;
    int time_window;
    int nickchange_window;

} ANTI_FLOODNET_REC;

/* Function prototypes */
void irc_anti_floodnet_init(void);
void irc_anti_floodnet_deinit(void);

/* Cleanup functions */
void free_channel_nickflood_rec(gpointer data);

/* Flood detection functions */
gboolean check_tilde_ident(const char *userhost);
void check_message_flood(IRC_SERVER_REC *server, const char *nick,
                         const char *address, const char *text);
void check_ctcp_flood(IRC_SERVER_REC *server, const char *nick,
                      const char *address, const char *cmd, const char *data);
void check_nick_flood(IRC_SERVER_REC *server, const char *channel,
                      const char *old_nick, const char *new_nick);

/* Blocking functions */
void block_duplicate_message(const char *text, int duration);
gboolean is_message_blocked(const char *text);
gboolean is_ctcp_blocked(IRC_SERVER_REC *server);
gboolean is_nick_channel_blocked(const char *channel);

/* Cleanup functions */
void cleanup_old_messages(time_t now);
void reset_daily_stats(void);

/* Protection mode management */
void enter_protection_mode(void);
void exit_protection_mode(void);
void check_protection_status(void);

/* Statistics */
void cmd_floodnet_status(const char *data);

#endif