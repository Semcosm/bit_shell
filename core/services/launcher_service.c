#include "services/launcher_service.h"

#include <glib.h>

struct _BsLauncherService {
  BsStateStore *store;
};

BsLauncherService *
bs_launcher_service_new(BsStateStore *store) {
  BsLauncherService *service = g_new0(BsLauncherService, 1);
  service->store = store;
  return service;
}

void
bs_launcher_service_free(BsLauncherService *service) {
  g_free(service);
}

bool
bs_launcher_service_refresh(BsLauncherService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  bs_state_store_mark_topic_changed(service->store, BS_TOPIC_SETTINGS);
  return true;
}
