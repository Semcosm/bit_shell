#ifndef BIT_SHELL_CORE_SERVICES_TRAY_MENU_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_TRAY_MENU_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "model/tray_menu.h"
#include "model/types.h"
#include "state/state_store.h"

typedef struct _BsTrayMenuService BsTrayMenuService;

BsTrayMenuService *bs_tray_menu_service_new(BsStateStore *store);
void bs_tray_menu_service_free(BsTrayMenuService *service);

bool bs_tray_menu_service_start(BsTrayMenuService *service, GError **error);
void bs_tray_menu_service_stop(BsTrayMenuService *service);
bool bs_tray_menu_service_sync_item(BsTrayMenuService *service,
                                    const BsTrayItem *item,
                                    GError **error);
bool bs_tray_menu_service_remove_item(BsTrayMenuService *service,
                                      const char *item_id);
void bs_tray_menu_service_clear_items(BsTrayMenuService *service);
bool bs_tray_menu_service_refresh_item(BsTrayMenuService *service,
                                       const char *item_id,
                                       GError **error);
bool bs_tray_menu_service_activate_menu_item(BsTrayMenuService *service,
                                             const char *item_id,
                                             gint32 menu_item_id,
                                             GError **error);

#endif
