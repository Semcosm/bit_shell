#include "shelld/ipc_server.h"

#include <glib.h>

struct _BsIpcServer {
  BsShelldApp *app;
  char *socket_path;
  bool running;
};

BsIpcServer *
bs_ipc_server_new(BsShelldApp *app, const BsIpcServerConfig *config) {
  BsIpcServer *server = g_new0(BsIpcServer, 1);
  server->app = app;
  if (config != NULL && config->socket_path != NULL) {
    server->socket_path = g_strdup(config->socket_path);
  }
  return server;
}

void
bs_ipc_server_free(BsIpcServer *server) {
  if (server == NULL) {
    return;
  }

  g_free(server->socket_path);
  g_free(server);
}

bool
bs_ipc_server_start(BsIpcServer *server, GError **error) {
  g_return_val_if_fail(server != NULL, false);

  if (server->running) {
    return true;
  }

  g_message("[bit_shelld] ipc server stub start%s%s",
            server->socket_path != NULL ? " at " : "",
            server->socket_path != NULL ? server->socket_path : "");
  server->running = true;
  return true;
}

void
bs_ipc_server_stop(BsIpcServer *server) {
  if (server == NULL) {
    return;
  }

  server->running = false;
}
