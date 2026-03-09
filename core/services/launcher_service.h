#ifndef BIT_SHELL_CORE_SERVICES_LAUNCHER_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_LAUNCHER_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "state/state_store.h"

typedef struct _BsLauncherService BsLauncherService;

BsLauncherService *bs_launcher_service_new(BsStateStore *store);
void bs_launcher_service_free(BsLauncherService *service);

bool bs_launcher_service_refresh(BsLauncherService *service, GError **error);

#endif
