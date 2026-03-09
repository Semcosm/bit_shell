#include "niri/niri_backend.h"

#include <glib.h>

struct _BsNiriBackend {
  BsStateStore *store;
  char *socket_path;
  bool auto_reconnect;
  bool running;
};

BsNiriBackend *
bs_niri_backend_new(BsStateStore *store, const BsNiriBackendConfig *config) {
  BsNiriBackend *backend = g_new0(BsNiriBackend, 1);
  backend->store = store;
  if (config != NULL && config->socket_path != NULL) {
    backend->socket_path = g_strdup(config->socket_path);
  }
  backend->auto_reconnect = config != NULL ? config->auto_reconnect : true;
  return backend;
}

void
bs_niri_backend_free(BsNiriBackend *backend) {
  if (backend == NULL) {
    return;
  }

  g_free(backend->socket_path);
  g_free(backend);
}

bool
bs_niri_backend_start(BsNiriBackend *backend, GError **error) {
  g_return_val_if_fail(backend != NULL, false);
  backend->running = true;
  g_message("[bit_shelld] niri backend start%s%s",
            backend->socket_path != NULL ? " at " : "",
            backend->socket_path != NULL ? backend->socket_path : "");
  return true;
}

void
bs_niri_backend_stop(BsNiriBackend *backend) {
  if (backend == NULL) {
    return;
  }

  backend->running = false;
}

bool
bs_niri_backend_request_initial_snapshot(BsNiriBackend *backend, GError **error) {
  g_return_val_if_fail(backend != NULL, false);
  bs_state_store_mark_topic_changed(backend->store, BS_TOPIC_SHELL);
  bs_state_store_mark_topic_changed(backend->store, BS_TOPIC_WINDOWS);
  bs_state_store_mark_topic_changed(backend->store, BS_TOPIC_WORKSPACES);
  return true;
}

bool
bs_niri_backend_subscribe_event_stream(BsNiriBackend *backend, GError **error) {
  g_return_val_if_fail(backend != NULL, false);
  return true;
}
