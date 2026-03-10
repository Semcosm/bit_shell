#ifndef BIT_SHELL_CORE_SERVICES_DOCK_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_DOCK_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "services/app_registry.h"
#include "state/state_store.h"

typedef struct _BsDockService BsDockService;

BsDockService *bs_dock_service_new(BsStateStore *store, BsAppRegistry *app_registry);
void bs_dock_service_free(BsDockService *service);

bool bs_dock_service_rebuild(BsDockService *service, GError **error);

#endif
