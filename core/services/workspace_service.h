#ifndef BIT_SHELL_CORE_SERVICES_WORKSPACE_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_WORKSPACE_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "state/state_store.h"

typedef struct _BsWorkspaceService BsWorkspaceService;

BsWorkspaceService *bs_workspace_service_new(BsStateStore *store);
void bs_workspace_service_free(BsWorkspaceService *service);

bool bs_workspace_service_rebuild(BsWorkspaceService *service, GError **error);

#endif
