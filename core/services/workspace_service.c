#include "services/workspace_service.h"

#include <glib.h>

struct _BsWorkspaceService {
  BsStateStore *store;
};

BsWorkspaceService *
bs_workspace_service_new(BsStateStore *store) {
  BsWorkspaceService *service = g_new0(BsWorkspaceService, 1);
  service->store = store;
  return service;
}

void
bs_workspace_service_free(BsWorkspaceService *service) {
  g_free(service);
}

bool
bs_workspace_service_rebuild(BsWorkspaceService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  bs_state_store_mark_topic_changed(service->store, BS_TOPIC_WORKSPACES);
  return true;
}
