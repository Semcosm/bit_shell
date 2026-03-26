#ifndef BIT_SHELL_CORE_FRONTENDS_COMMON_IPC_CLIENT_H
#define BIT_SHELL_CORE_FRONTENDS_COMMON_IPC_CLIENT_H

#include <gio/gio.h>
#include <glib.h>
#include <stdbool.h>

typedef struct _BsFrontendIpcClient BsFrontendIpcClient;

typedef void (*BsFrontendIpcLineFn)(BsFrontendIpcClient *client,
                                    const char *line,
                                    gpointer user_data);
typedef void (*BsFrontendIpcStateFn)(BsFrontendIpcClient *client,
                                     gpointer user_data);

typedef struct {
  const char *socket_path;
  guint reconnect_delay_ms;
  BsFrontendIpcStateFn on_connected;
  BsFrontendIpcStateFn on_disconnected;
  BsFrontendIpcLineFn on_line;
  gpointer user_data;
} BsFrontendIpcClientConfig;

BsFrontendIpcClient *bs_frontend_ipc_client_new(const BsFrontendIpcClientConfig *config);
void bs_frontend_ipc_client_free(BsFrontendIpcClient *client);

bool bs_frontend_ipc_client_start(BsFrontendIpcClient *client, GError **error);
void bs_frontend_ipc_client_stop(BsFrontendIpcClient *client);
void bs_frontend_ipc_client_disconnect(BsFrontendIpcClient *client);

bool bs_frontend_ipc_client_send_line(BsFrontendIpcClient *client,
                                      const char *json_line,
                                      GError **error);

const char *bs_frontend_ipc_client_socket_path(BsFrontendIpcClient *client);
const char *bs_frontend_ipc_client_last_error(BsFrontendIpcClient *client);
bool bs_frontend_ipc_client_ready(BsFrontendIpcClient *client);

#endif
