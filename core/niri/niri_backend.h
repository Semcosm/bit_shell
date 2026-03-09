#ifndef BIT_SHELL_CORE_NIRI_BACKEND_H
#define BIT_SHELL_CORE_NIRI_BACKEND_H

#include <gio/gio.h>
#include <stdbool.h>

#include "state/state_store.h"

typedef struct _BsNiriBackend BsNiriBackend;

typedef struct {
  const char *socket_path;
  bool auto_reconnect;
} BsNiriBackendConfig;

BsNiriBackend *bs_niri_backend_new(BsStateStore *store, const BsNiriBackendConfig *config);
void bs_niri_backend_free(BsNiriBackend *backend);

bool bs_niri_backend_start(BsNiriBackend *backend, GError **error);
void bs_niri_backend_stop(BsNiriBackend *backend);

bool bs_niri_backend_request_initial_snapshot(BsNiriBackend *backend, GError **error);
bool bs_niri_backend_subscribe_event_stream(BsNiriBackend *backend, GError **error);

#endif
