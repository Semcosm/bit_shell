#include "services/app_registry.h"

#include <glib.h>
#include <gio/gdesktopappinfo.h>

struct _BsAppRegistry {
  BsStateStore *store;
  bool watch_desktop_entries;
  char *applications_dir;
  bool running;
  GAppInfoMonitor *monitor;
  gulong monitor_changed_handler_id;
  GHashTable *alias_to_desktop_id;
};

static void bs_app_registry_register_alias(BsAppRegistry *registry,
                                           const char *alias,
                                           const char *desktop_id);
static char *bs_app_registry_strip_desktop_suffix(const char *desktop_id);
static char *bs_app_registry_dup_icon_name(GAppInfo *app_info);
static void bs_app_registry_free_app_state_ptr(gpointer data);
static void bs_app_registry_on_monitor_changed(GAppInfoMonitor *monitor, gpointer user_data);

static void
bs_app_registry_free_app_state_ptr(gpointer data) {
  BsAppState *app_state = data;

  if (app_state == NULL) {
    return;
  }

  bs_app_state_clear(app_state);
  g_free(app_state);
}

BsAppRegistry *
bs_app_registry_new(BsStateStore *store, const BsAppRegistryConfig *config) {
  BsAppRegistry *registry = g_new0(BsAppRegistry, 1);
  registry->store = store;
  registry->watch_desktop_entries = config != NULL ? config->watch_desktop_entries : true;
  registry->alias_to_desktop_id = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
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
  if (registry->monitor != NULL && registry->monitor_changed_handler_id != 0) {
    g_signal_handler_disconnect(registry->monitor, registry->monitor_changed_handler_id);
  }
  g_clear_object(&registry->monitor);
  g_clear_pointer(&registry->alias_to_desktop_id, g_hash_table_unref);
  g_free(registry);
}

bool
bs_app_registry_start(BsAppRegistry *registry, GError **error) {
  g_return_val_if_fail(registry != NULL, false);
  registry->running = true;

  if (registry->watch_desktop_entries && registry->monitor == NULL) {
    registry->monitor = g_object_ref(g_app_info_monitor_get());
    registry->monitor_changed_handler_id = g_signal_connect(registry->monitor,
                                                            "changed",
                                                            G_CALLBACK(bs_app_registry_on_monitor_changed),
                                                            registry);
  }

  return bs_app_registry_rescan(registry, error);
}

void
bs_app_registry_stop(BsAppRegistry *registry) {
  if (registry == NULL) {
    return;
  }

  registry->running = false;
}

static void
bs_app_registry_register_alias(BsAppRegistry *registry,
                               const char *alias,
                               const char *desktop_id) {
  const char *existing = NULL;

  g_return_if_fail(registry != NULL);

  if (alias == NULL || *alias == '\0' || desktop_id == NULL || *desktop_id == '\0') {
    return;
  }

  existing = g_hash_table_lookup(registry->alias_to_desktop_id, alias);
  if (existing == NULL) {
    g_hash_table_insert(registry->alias_to_desktop_id, g_strdup(alias), g_strdup(desktop_id));
    return;
  }

  if (g_strcmp0(existing, desktop_id) != 0) {
    g_hash_table_remove(registry->alias_to_desktop_id, alias);
  }
}

static char *
bs_app_registry_strip_desktop_suffix(const char *desktop_id) {
  const char *suffix = ".desktop";
  gsize desktop_id_len = 0;
  gsize suffix_len = 0;

  if (desktop_id == NULL) {
    return NULL;
  }

  desktop_id_len = strlen(desktop_id);
  suffix_len = strlen(suffix);
  if (desktop_id_len <= suffix_len || !g_str_has_suffix(desktop_id, suffix)) {
    return NULL;
  }

  return g_strndup(desktop_id, desktop_id_len - suffix_len);
}

static char *
bs_app_registry_dup_icon_name(GAppInfo *app_info) {
  GIcon *icon = NULL;

  g_return_val_if_fail(app_info != NULL, NULL);

  icon = g_app_info_get_icon(app_info);
  return icon != NULL ? g_icon_to_string(icon) : NULL;
}

bool
bs_app_registry_rescan(BsAppRegistry *registry, GError **error) {
  g_autoptr(GPtrArray) apps = NULL;
  GList *app_infos = NULL;
  GList *iter = NULL;

  g_return_val_if_fail(registry != NULL, false);
  (void) error;

  g_hash_table_remove_all(registry->alias_to_desktop_id);
  apps = g_ptr_array_new_with_free_func((GDestroyNotify) bs_app_registry_free_app_state_ptr);
  app_infos = g_app_info_get_all();
  for (iter = app_infos; iter != NULL; iter = iter->next) {
    GAppInfo *app_info = G_APP_INFO(iter->data);
    g_autofree char *icon_name = NULL;
    g_autofree char *desktop_basename = NULL;
    BsAppState *app_state = NULL;
    const char *desktop_id = NULL;
    const char *name = NULL;
    const char *startup_wm_class = NULL;

    if (app_info == NULL || !g_app_info_should_show(app_info)) {
      continue;
    }

    desktop_id = g_app_info_get_id(app_info);
    if (desktop_id == NULL || *desktop_id == '\0') {
      continue;
    }

    name = g_app_info_get_display_name(app_info);
    if (name == NULL || *name == '\0') {
      name = g_app_info_get_name(app_info);
    }

    icon_name = bs_app_registry_dup_icon_name(app_info);
    desktop_basename = bs_app_registry_strip_desktop_suffix(desktop_id);
    if (G_IS_DESKTOP_APP_INFO(app_info)) {
      startup_wm_class = g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(app_info));
    }

    app_state = g_new0(BsAppState, 1);
    app_state->desktop_id = g_strdup(desktop_id);
    app_state->app_id = desktop_basename != NULL ? g_strdup(desktop_basename) : NULL;
    app_state->name = g_strdup(name != NULL ? name : desktop_id);
    app_state->icon_name = g_strdup(icon_name);
    g_ptr_array_add(apps, app_state);

    bs_app_registry_register_alias(registry, desktop_id, desktop_id);
    bs_app_registry_register_alias(registry, desktop_basename, desktop_id);
    bs_app_registry_register_alias(registry, startup_wm_class, desktop_id);
  }

  g_list_free_full(app_infos, g_object_unref);
  bs_state_store_begin_update(registry->store);
  bs_state_store_replace_apps(registry->store, apps);
  bs_state_store_finish_update(registry->store);
  return true;
}

bool
bs_app_registry_launch_desktop_id(BsAppRegistry *registry,
                                  const char *desktop_id,
                                  GError **error) {
  g_autoptr(GDesktopAppInfo) app_info = NULL;

  g_return_val_if_fail(registry != NULL, false);
  g_return_val_if_fail(desktop_id != NULL, false);

  app_info = g_desktop_app_info_new(desktop_id);
  if (app_info == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "desktop entry not found: %s",
                desktop_id);
    return false;
  }

  return g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, error);
}

char *
bs_app_registry_resolve_window_app_key(BsAppRegistry *registry, const BsWindow *window) {
  const char *desktop_id = NULL;

  g_return_val_if_fail(registry != NULL, NULL);
  g_return_val_if_fail(window != NULL, NULL);

  if (window->desktop_id != NULL && *window->desktop_id != '\0') {
    desktop_id = g_hash_table_lookup(registry->alias_to_desktop_id, window->desktop_id);
    return g_strdup(desktop_id != NULL ? desktop_id : window->desktop_id);
  }

  if (window->app_id != NULL && *window->app_id != '\0') {
    desktop_id = g_hash_table_lookup(registry->alias_to_desktop_id, window->app_id);
    return g_strdup(desktop_id != NULL ? desktop_id : window->app_id);
  }

  return window->id != NULL ? g_strdup_printf("unknown:%s", window->id) : g_strdup("unknown");
}

char *
bs_app_registry_canonical_desktop_id(BsAppRegistry *registry, const char *app_key) {
  const char *desktop_id = NULL;
  g_autoptr(GDesktopAppInfo) app_info = NULL;

  g_return_val_if_fail(registry != NULL, NULL);
  g_return_val_if_fail(app_key != NULL, NULL);

  if (*app_key == '\0') {
    return NULL;
  }

  desktop_id = g_hash_table_lookup(registry->alias_to_desktop_id, app_key);
  if (desktop_id != NULL) {
    return g_strdup(desktop_id);
  }

  app_info = g_desktop_app_info_new(app_key);
  if (app_info != NULL) {
    return g_strdup(app_key);
  }

  return NULL;
}

static void
bs_app_registry_on_monitor_changed(GAppInfoMonitor *monitor, gpointer user_data) {
  BsAppRegistry *registry = user_data;
  g_autoptr(GError) error = NULL;

  (void) monitor;
  g_return_if_fail(registry != NULL);

  if (!registry->running) {
    return;
  }

  if (!bs_app_registry_rescan(registry, &error)) {
    g_warning("[bit_shelld] app registry rescan failed: %s",
              error != NULL ? error->message : "unknown error");
  }
}
