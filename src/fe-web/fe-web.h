#ifndef IRSSI_FE_WEB_FE_WEB_H
#define IRSSI_FE_WEB_FE_WEB_H

#include <irssi/src/common.h>
#include <irssi/src/core/network.h>
#include <irssi/src/irc/core/irc.h>
#include <irssi/src/irc/core/irc-servers.h>
#include <irssi/src/irc/core/irc-chatnets.h>
#include <irssi/src/irc/core/irc-servers-setup.h>
#include <irssi/src/core/servers-setup.h>
#include <irssi/src/core/chatnets.h>

/* Forward declaration for SSL channel */
typedef struct _FE_WEB_SSL_CHANNEL FE_WEB_SSL_CHANNEL;

/* Message types for WebSocket protocol (from PROTOCOL.md) */
typedef enum {
	WEB_MSG_AUTH_OK = 1,
	WEB_MSG_MESSAGE,
	WEB_MSG_SERVER_STATUS,
	WEB_MSG_CHANNEL_JOIN,
	WEB_MSG_CHANNEL_PART,
	WEB_MSG_CHANNEL_KICK,
	WEB_MSG_USER_QUIT,
	WEB_MSG_TOPIC,
	WEB_MSG_CHANNEL_MODE,
	WEB_MSG_NICKLIST,
	WEB_MSG_NICKLIST_UPDATE,
	WEB_MSG_NICK_CHANGE,
	WEB_MSG_USER_MODE,
	WEB_MSG_AWAY,
	WEB_MSG_WHOIS,
	WEB_MSG_CHANNEL_LIST,
	WEB_MSG_STATE_DUMP,
	WEB_MSG_ERROR,
	WEB_MSG_PONG,
	WEB_MSG_QUERY_OPENED,
	WEB_MSG_QUERY_CLOSED,
	WEB_MSG_ACTIVITY_UPDATE,     /* Activity level changed (unread markers) */
	WEB_MSG_MARK_READ,           /* Mark channel as read (from client) */
	WEB_MSG_NETWORK_LIST,        /* Request: list all networks */
	WEB_MSG_NETWORK_LIST_RESPONSE, /* Response: network list */
	WEB_MSG_SERVER_LIST,         /* Request: list all servers */
	WEB_MSG_SERVER_LIST_RESPONSE, /* Response: server list */
	WEB_MSG_NETWORK_ADD,         /* Request: add/modify network */
	WEB_MSG_NETWORK_REMOVE,      /* Request: remove network */
	WEB_MSG_SERVER_ADD,          /* Request: add/modify server */
	WEB_MSG_SERVER_REMOVE,       /* Request: remove server */
	WEB_MSG_COMMAND_RESULT       /* Response: operation result */
} WEB_MESSAGE_TYPE;

/* WebSocket client connection record */
typedef struct {
	int fd;
	char *id;                    /* UUID or timestamp-counter */
	char *addr;                  /* Client IP address (for logging) */
	time_t connected_at;

	/* WebSocket state */
	unsigned int authenticated:1;
	unsigned int handshake_done:1;
	char *websocket_key;

	/* irssi context - per-client server assignment */
	IRC_SERVER_REC *server;      /* Assigned server (or NULL) */
	GSList *synced_channels;     /* List of channel names (char *) */
	unsigned int wants_all_servers:1;

	/* Network */
	NET_SENDBUF_REC *handle;
	GString *output_buffer;
	GByteArray *input_buffer;  /* For incomplete WebSocket frames */
	int recv_tag;

	/* SSL/TLS */
	FE_WEB_SSL_CHANNEL *ssl_channel; /* SSL wrapper (if SSL enabled) */
	unsigned int use_ssl:1;           /* Whether this connection uses SSL */

	/* Encryption */
	unsigned int encryption_enabled:1; /* Whether this connection uses encryption */

	/* Statistics */
	unsigned long messages_sent;
	unsigned long messages_received;

	/* Request tracking (for WHOIS, ban list, etc.) */
	GHashTable *pending_requests; /* request_id -> response_type mapping */
} WEB_CLIENT_REC;

/* WHOIS data collection structure */
typedef struct {
	char *nick;
	char *user;
	char *host;
	char *realname;
	char *server;
	char *server_info;
	char *idle;
	char *signon;
	char *channels;
	char *account;
	unsigned int secure:1;
	unsigned int oper:1;
	time_t timestamp;
	GSList *special;  /* List of special/non-standard WHOIS lines */
} WHOIS_REC;

/* Message structure for internal use */
typedef struct {
	char *id;                    /* Message ID (UUID or timestamp) */
	WEB_MESSAGE_TYPE type;
	char *server_tag;
	char *target;                /* Channel or nick */
	char *nick;
	char *text;
	int level;                   /* MSGLEVEL_* */
	time_t timestamp;
	unsigned int is_own:1;

	/* Additional data (for complex messages like WHOIS, channel_list) */
	GHashTable *extra_data;      /* key -> value string pairs */

	/* Response tracking */
	char *response_to;           /* Request ID this responds to */
} WEB_MESSAGE_REC;

/* Global clients list */
extern GSList *web_clients;

/* Module initialization */
void fe_web_init(void);
void fe_web_deinit(void);

/* Server functions */
void fe_web_server_init(void);
void fe_web_server_deinit(void);

/* Client functions */
WEB_CLIENT_REC *fe_web_client_create(int fd, const char *addr);
void fe_web_client_destroy(WEB_CLIENT_REC *client);
void fe_web_client_handle_message(WEB_CLIENT_REC *client, const char *json);
void fe_web_client_sync_server(WEB_CLIENT_REC *client, const char *server_tag);
void fe_web_client_execute_command(WEB_CLIENT_REC *client, const char *command);

/* Signal handlers */
void fe_web_signals_init(void);
void fe_web_signals_deinit(void);

/* Nicklist helpers */
void fe_web_send_nicklist_for_channel(IRC_SERVER_REC *server, IRC_CHANNEL_REC *channel);

/* Message creation/destruction */
WEB_MESSAGE_REC *fe_web_message_new(WEB_MESSAGE_TYPE type);
void fe_web_message_free(WEB_MESSAGE_REC *msg);

/* Message sending */
void fe_web_send_message(WEB_CLIENT_REC *client, WEB_MESSAGE_REC *msg);
void fe_web_send_to_server_clients(IRC_SERVER_REC *server, WEB_MESSAGE_REC *msg);
void fe_web_send_to_all_clients(WEB_MESSAGE_REC *msg);

/* JSON utilities */
char *fe_web_message_to_json(WEB_MESSAGE_REC *msg);
char *fe_web_escape_json(const char *str);
char *fe_web_generate_message_id(void);

/* JSON parsing */
char *fe_web_json_get_string(const char *json, const char *key);
int fe_web_json_get_int(const char *json, const char *key, int default_value);
int fe_web_json_has_key(const char *json, const char *key);

/* JSON building for network/server management */
GString *fe_web_build_network_json(IRC_CHATNET_REC *rec);
GString *fe_web_build_server_json(IRC_SERVER_SETUP_REC *rec);
GString *fe_web_build_command_result_json(gboolean success, const char *message, 
                                          const char *error_code);

/* Network/Server management handlers */
void fe_web_handle_network_list(WEB_CLIENT_REC *client, const char *json);
void fe_web_handle_server_list(WEB_CLIENT_REC *client, const char *json);
void fe_web_handle_network_add(WEB_CLIENT_REC *client, const char *json);
void fe_web_handle_network_remove(WEB_CLIENT_REC *client, const char *json);
void fe_web_handle_server_add(WEB_CLIENT_REC *client, const char *json);
void fe_web_handle_server_remove(WEB_CLIENT_REC *client, const char *json);

/* State dump */
void fe_web_dump_state(WEB_CLIENT_REC *client);

/* WebSocket protocol (RFC 6455) */
char *fe_web_websocket_compute_accept(const char *client_key);
int fe_web_websocket_parse_frame(const guchar *data, gsize data_len,
                                  int *fin, int *opcode, int *masked,
                                  guint64 *payload_len, guchar mask_key[4],
                                  const guchar **payload);
void fe_web_websocket_unmask(guchar *payload, guint64 payload_len,
                              const guchar mask_key[4]);
guchar *fe_web_websocket_create_frame(int opcode, const guchar *payload,
                                       guint64 payload_len, gsize *frame_len);

#endif /* IRSSI_FE_WEB_FE_WEB_H */
