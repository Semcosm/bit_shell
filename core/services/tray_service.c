#include "services/tray_service.h"

#include <glib.h>

struct _BsTrayService {
  BsStateStore *store;
  char *watcher_name;
  bool running;
};

BsTrayService *
bs_tray_service_new(BsStateStore *store, const BsTrayServiceConfig *config) {
  BsTrayService *service = g_new0(BsTrayService, 1);
  service->store = store;
  if (config != NULL && config->watcher_name != NULL) {
    service->watcher_name = g_strdup(config->watcher_name);
  }
  return service;
}

void
bs_tray_service_free(BsTrayService *service) {
  if (service == NULL) {
    return;
  }

  g_free(service->watcher_name);
  g_free(service);
}

bool
bs_tray_service_start(BsTrayService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  service->running = true;
  bs_state_store_mark_topic_changed(service->store, BS_TOPIC_TRAY);
  return true;
}

void
bs_tray_service_stop(BsTrayService *service) {
  if (service == NULL) {
    return;
  }

  service->running = false;
}
