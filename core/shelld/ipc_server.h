#ifndef BIT_SHELL_CORE_SHELLD_IPC_SERVER_H
#define BIT_SHELL_CORE_SHELLD_IPC_SERVER_H

#include <gio/gio.h>
#include <stdbool.h>

struct _BsShelldApp;
typedef struct _BsShelldApp BsShelldApp;

typedef struct _BsIpcServer BsIpcServer;

typedef struct {
  const char *socket_path;
} BsIpcServerConfig;

BsIpcServer *bs_ipc_server_new(BsShelldApp *app, const BsIpcServerConfig *config);
void bs_ipc_server_free(BsIpcServer *server);

bool bs_ipc_server_start(BsIpcServer *server, GError **error);
void bs_ipc_server_stop(BsIpcServer *server);

#endif
