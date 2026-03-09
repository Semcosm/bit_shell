#include "services/app_registry.h"

#include <glib.h>

struct _BsAppRegistry {
  BsStateStore *store;
  bool watch_desktop_entries;
  char *applications_dir;
  bool running;
};

BsAppRegistry *
bs_app_registry_new(BsStateStore *store, const BsAppRegistryConfig *config) {
  BsAppRegistry *registry = g_new0(BsAppRegistry, 1);
  registry->store = store;
  registry->watch_desktop_entries = config != NULL ? config->watch_desktop_entries : true;
  if (config != NULL && config->applications_dir != NULL) {
    registry->applications_dir = g_strdup(config->applications_dir);
  }
  return registry;
}

void
bs_app_registry_free(BsAppRegistry *registry) {
  if (registry == NULL) {
    return;
  }

  g_free(registry->applications_dir);
  g_free(registry);
}

bool
bs_app_registry_start(BsAppRegistry *registry, GError **error) {
  g_return_val_if_fail(registry != NULL, false);
  registry->running = true;
  return bs_app_registry_rescan(registry, error);
}

void
bs_app_registry_stop(BsAppRegistry *registry) {
  if (registry == NULL) {
    return;
  }

  registry->running = false;
}

bool
bs_app_registry_rescan(BsAppRegistry *registry, GError **error) {
  g_return_val_if_fail(registry != NULL, false);
  bs_state_store_mark_topic_changed(registry->store, BS_TOPIC_SETTINGS);
  return true;
}
