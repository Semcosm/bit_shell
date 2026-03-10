#ifndef BIT_SHELL_CORE_SERVICES_APP_REGISTRY_H
#define BIT_SHELL_CORE_SERVICES_APP_REGISTRY_H

#include <gio/gio.h>
#include <stdbool.h>

#include "model/types.h"
#include "state/state_store.h"

typedef struct _BsAppRegistry BsAppRegistry;

typedef struct {
  bool watch_desktop_entries;
  const char *applications_dir;
} BsAppRegistryConfig;

BsAppRegistry *bs_app_registry_new(BsStateStore *store, const BsAppRegistryConfig *config);
void bs_app_registry_free(BsAppRegistry *registry);

bool bs_app_registry_start(BsAppRegistry *registry, GError **error);
void bs_app_registry_stop(BsAppRegistry *registry);

bool bs_app_registry_rescan(BsAppRegistry *registry, GError **error);
bool bs_app_registry_launch_desktop_id(BsAppRegistry *registry,
                                       const char *desktop_id,
                                       GError **error);
char *bs_app_registry_resolve_window_app_key(BsAppRegistry *registry, const BsWindow *window);
char *bs_app_registry_canonical_desktop_id(BsAppRegistry *registry, const char *app_key);

#endif
