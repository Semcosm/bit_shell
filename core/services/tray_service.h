#ifndef BIT_SHELL_CORE_SERVICES_TRAY_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_TRAY_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "state/state_store.h"

typedef struct _BsTrayService BsTrayService;

typedef struct {
  const char *watcher_name;
} BsTrayServiceConfig;

BsTrayService *bs_tray_service_new(BsStateStore *store, const BsTrayServiceConfig *config);
void bs_tray_service_free(BsTrayService *service);

bool bs_tray_service_start(BsTrayService *service, GError **error);
void bs_tray_service_stop(BsTrayService *service);

#endif
