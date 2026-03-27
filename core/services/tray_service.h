#ifndef BIT_SHELL_CORE_SERVICES_TRAY_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_TRAY_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "services/tray_menu_service.h"
#include "state/state_store.h"

typedef struct _BsTrayService BsTrayService;

typedef struct {
  const char *watcher_name;
  BsTrayMenuService *menu_service;
} BsTrayServiceConfig;

BsTrayService *bs_tray_service_new(BsStateStore *store, const BsTrayServiceConfig *config);
void bs_tray_service_free(BsTrayService *service);

bool bs_tray_service_start(BsTrayService *service, GError **error);
void bs_tray_service_stop(BsTrayService *service);
bool bs_tray_service_activate_item(BsTrayService *service,
                                   const char *item_id,
                                   int32_t x,
                                   int32_t y,
                                   GError **error);
bool bs_tray_service_context_menu_item(BsTrayService *service,
                                       const char *item_id,
                                       int32_t x,
                                       int32_t y,
                                       GError **error);
const char *bs_tray_service_watcher_name(const BsTrayService *service);
bool bs_tray_service_running(const BsTrayService *service);

#endif
