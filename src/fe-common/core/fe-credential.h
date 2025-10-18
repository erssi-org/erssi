#ifndef IRSSI_FE_COMMON_CORE_FE_CREDENTIAL_H
#define IRSSI_FE_COMMON_CORE_FE_CREDENTIAL_H

#include <irssi/src/core/servers.h>
#include <irssi/src/fe-common/core/window-items.h>

/* Initialization and deinitialization functions */
void fe_credential_init(void);
void fe_credential_deinit(void);

/* User commands */
void cmd_credential(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_passwd(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_list(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_migrate(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_migrate_to_external(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_migrate_to_config(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_encrypt(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_decrypt(const char *data, SERVER_REC *server, WI_ITEM_REC *item);
void cmd_credential_reload(const char *data, SERVER_REC *server, WI_ITEM_REC *item);

/* Helper functions */
void credential_show_help(void);
void credential_show_status(const char *data, SERVER_REC *server, WI_ITEM_REC *item);

#endif