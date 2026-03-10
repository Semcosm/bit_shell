#include "services/dock_service.h"

#include <glib.h>

struct _BsDockService {
  BsStateStore *store;
};

BsDockService *
bs_dock_service_new(BsStateStore *store) {
  BsDockService *service = g_new0(BsDockService, 1);
  service->store = store;
  return service;
}

void
bs_dock_service_free(BsDockService *service) {
  g_free(service);
}

bool
bs_dock_service_rebuild(BsDockService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  (void) error;
  return true;
}
