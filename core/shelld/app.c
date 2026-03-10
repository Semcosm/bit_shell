#include "shelld/app.h"

#include <glib.h>

struct _BsShelldApp {
  BsShelldConfig config;
  GMainLoop *main_loop;
  BsStateStore *state_store;
  BsNiriBackend *niri_backend;
  BsAppRegistry *app_registry;
  BsWorkspaceService *workspace_service;
  BsDockService *dock_service;
  BsLauncherService *launcher_service;
  BsTrayService *tray_service;
  BsSettingsService *settings_service;
  BsCommandRouter *command_router;
  BsIpcServer *ipc_server;
  bool running;
};

static void bs_shelld_app_rebuild_derived_state(BsStateStore *store, gpointer user_data);
static const BsWindow *bs_shelld_app_find_best_window_for_app(BsShelldApp *app, const char *app_key);
static const BsWindow *bs_shelld_app_find_best_window_by_field(BsSnapshot *snapshot,
                                                               const char *app_key,
                                                               bool match_desktop_id);
static bool bs_shelld_app_set_app_pinned(BsShelldApp *app,
                                         const char *app_key,
                                         bool pinned,
                                         GError **error);

BsShelldApp *
bs_shelld_app_new(const BsShelldConfig *config) {
  BsShelldApp *app = g_new0(BsShelldApp, 1);
  BsNiriBackendConfig niri_config = {0};
  BsAppRegistryConfig app_registry_config = {0};
  BsTrayServiceConfig tray_config = {0};
  BsSettingsServiceConfig settings_config = {0};
  BsIpcServerConfig ipc_config = {0};

  bs_shell_config_init_defaults(&app->config);
  if (config != NULL) {
    bs_shell_config_clear(&app->config);
    bs_shell_config_copy(&app->config, config);
  }

  app->main_loop = g_main_loop_new(NULL, false);
  app->state_store = bs_state_store_new();

  niri_config.socket_path = app->config.paths.niri_socket_path;
  niri_config.auto_reconnect = app->config.auto_reconnect_niri;
  app->niri_backend = bs_niri_backend_new(app->state_store, &niri_config);

  app_registry_config.watch_desktop_entries = true;
  app_registry_config.applications_dir = app->config.paths.applications_dir;
  app->app_registry = bs_app_registry_new(app->state_store, &app_registry_config);

  app->workspace_service = bs_workspace_service_new(app->state_store);
  app->dock_service = bs_dock_service_new(app->state_store, app->app_registry);
  bs_state_store_set_derived_updater(app->state_store,
                                     bs_shelld_app_rebuild_derived_state,
                                     app);
  app->launcher_service = bs_launcher_service_new(app->state_store);

  tray_config.watcher_name = app->config.tray_watcher_name;
  app->tray_service = bs_tray_service_new(app->state_store, &tray_config);

  settings_config.config_path = app->config.paths.config_path;
  settings_config.state_path = app->config.paths.state_path;
  app->settings_service = bs_settings_service_new(app->state_store, &settings_config);

  app->command_router = bs_command_router_new(app);

  ipc_config.socket_path = app->config.paths.ipc_socket_path;
  app->ipc_server = bs_ipc_server_new(app, &ipc_config);

  return app;
}

void
bs_shelld_app_free(BsShelldApp *app) {
  if (app == NULL) {
    return;
  }

  bs_shelld_app_stop(app);
  bs_ipc_server_free(app->ipc_server);
  bs_command_router_free(app->command_router);
  bs_settings_service_free(app->settings_service);
  bs_tray_service_free(app->tray_service);
  bs_launcher_service_free(app->launcher_service);
  bs_dock_service_free(app->dock_service);
  bs_workspace_service_free(app->workspace_service);
  bs_app_registry_free(app->app_registry);
  bs_niri_backend_free(app->niri_backend);
  bs_state_store_free(app->state_store);

  if (app->main_loop != NULL) {
    g_main_loop_unref(app->main_loop);
  }

  bs_shell_config_clear(&app->config);
  g_free(app);
}

bool
bs_shelld_app_start(BsShelldApp *app, GError **error) {
  g_autoptr(GError) niri_error = NULL;

  g_return_val_if_fail(app != NULL, false);

  if (!bs_settings_service_load(app->settings_service, error)) {
    return false;
  }

  if (!bs_app_registry_start(app->app_registry, error)) {
    return false;
  }

  if (!bs_launcher_service_refresh(app->launcher_service, error)) {
    return false;
  }

  if (!bs_workspace_service_rebuild(app->workspace_service, error)) {
    return false;
  }

  if (!bs_dock_service_rebuild(app->dock_service, error)) {
    return false;
  }

  if (!bs_tray_service_start(app->tray_service, error)) {
    return false;
  }

  if (!bs_ipc_server_start(app->ipc_server, error)) {
    return false;
  }

  if (!bs_niri_backend_start(app->niri_backend, &niri_error)) {
    return false;
  }
  if (niri_error != NULL) {
    g_warning("[bit_shelld] started with degraded niri backend: %s", niri_error->message);
  }

  app->running = true;
  return true;
}

void
bs_shelld_app_stop(BsShelldApp *app) {
  if (app == NULL || !app->running) {
    return;
  }

  bs_ipc_server_stop(app->ipc_server);
  bs_tray_service_stop(app->tray_service);
  bs_niri_backend_stop(app->niri_backend);
  bs_app_registry_stop(app->app_registry);
  (void) bs_settings_service_flush(app->settings_service, NULL);
  app->running = false;
}

int
bs_shelld_app_run(BsShelldApp *app, GError **error) {
  g_return_val_if_fail(app != NULL, EXIT_FAILURE);

  if (!bs_shelld_app_start(app, error)) {
    return EXIT_FAILURE;
  }

  g_message("[bit_shelld] started");
  g_main_loop_run(app->main_loop);
  bs_shelld_app_stop(app);
  return EXIT_SUCCESS;
}

BsStateStore *
bs_shelld_app_state_store(BsShelldApp *app) {
  g_return_val_if_fail(app != NULL, NULL);
  return app->state_store;
}

BsCommandRouter *
bs_shelld_app_command_router(BsShelldApp *app) {
  g_return_val_if_fail(app != NULL, NULL);
  return app->command_router;
}

const BsShelldConfig *
bs_shelld_app_config(const BsShelldApp *app) {
  g_return_val_if_fail(app != NULL, NULL);
  return &app->config;
}

static void
bs_shelld_app_rebuild_derived_state(BsStateStore *store, gpointer user_data) {
  BsShelldApp *app = user_data;
  g_autoptr(GError) error = NULL;

  (void) store;
  g_return_if_fail(app != NULL);

  if (!bs_dock_service_rebuild(app->dock_service, &error)) {
    g_warning("[bit_shelld] failed to rebuild dock state: %s",
              error != NULL ? error->message : "unknown error");
  }
}

static const BsWindow *
bs_shelld_app_find_best_window_for_app(BsShelldApp *app, const char *app_key) {
  BsSnapshot *snapshot = NULL;
  const BsWindow *best = NULL;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(app_key != NULL, NULL);

  snapshot = bs_state_store_snapshot(app->state_store);
  if (snapshot == NULL) {
    return NULL;
  }

  /* `app_key` is canonicalized to desktop_id when known; app_id is fallback only. */
  best = bs_shelld_app_find_best_window_by_field(snapshot, app_key, true);
  if (best != NULL) {
    return best;
  }

  return bs_shelld_app_find_best_window_by_field(snapshot, app_key, false);
}

static const BsWindow *
bs_shelld_app_find_best_window_by_field(BsSnapshot *snapshot,
                                        const char *app_key,
                                        bool match_desktop_id) {
  GHashTableIter iter;
  gpointer value = NULL;
  const BsWindow *best = NULL;

  g_return_val_if_fail(snapshot != NULL, NULL);
  g_return_val_if_fail(app_key != NULL, NULL);

  g_hash_table_iter_init(&iter, snapshot->windows);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    const BsWindow *window = value;
    const char *candidate = match_desktop_id ? window->desktop_id : window->app_id;

    if (g_strcmp0(candidate, app_key) != 0) {
      continue;
    }

    if (best == NULL
        || (window->focused && !best->focused)
        || (window->focused == best->focused && window->focus_ts > best->focus_ts)) {
      best = window;
    }
  }

  return best;
}

bool
bs_shelld_app_launch_app(BsShelldApp *app,
                         const char *desktop_id,
                         GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(desktop_id != NULL, false);

  return bs_app_registry_launch_desktop_id(app->app_registry, desktop_id, error);
}

bool
bs_shelld_app_activate_app(BsShelldApp *app,
                           const char *app_key,
                           GError **error) {
  const BsWindow *window = NULL;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app_key != NULL, false);

  window = bs_shelld_app_find_best_window_for_app(app, app_key);
  if (window == NULL || window->id == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "no running window found for app_key: %s",
                app_key);
    return false;
  }

  return bs_niri_backend_focus_window(app->niri_backend, window->id, error);
}

bool
bs_shelld_app_focus_window(BsShelldApp *app,
                           const char *window_id,
                           GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(window_id != NULL, false);

  return bs_niri_backend_focus_window(app->niri_backend, window_id, error);
}

bool
bs_shelld_app_switch_workspace(BsShelldApp *app,
                               const char *workspace_id,
                               GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(workspace_id != NULL, false);

  return bs_niri_backend_focus_workspace(app->niri_backend, workspace_id, error);
}

static bool
bs_shelld_app_set_app_pinned(BsShelldApp *app,
                             const char *app_key,
                             bool pinned,
                             GError **error) {
  BsSnapshot *snapshot = NULL;
  g_autofree char *desktop_id = NULL;
  g_autoptr(GPtrArray) pinned_app_ids = NULL;
  bool already_present = false;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app_key != NULL, false);

  desktop_id = bs_app_registry_canonical_desktop_id(app->app_registry, app_key);
  if (desktop_id == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "app_key cannot be canonicalized to desktop_id: %s",
                app_key);
    return false;
  }

  snapshot = bs_state_store_snapshot(app->state_store);
  pinned_app_ids = g_ptr_array_new_with_free_func(g_free);
  if (snapshot != NULL && snapshot->pinned_app_ids != NULL) {
    for (guint i = 0; i < snapshot->pinned_app_ids->len; i++) {
      const char *existing = g_ptr_array_index(snapshot->pinned_app_ids, i);

      if (g_strcmp0(existing, desktop_id) == 0) {
        already_present = true;
        if (!pinned) {
          continue;
        }
      }

      if (existing != NULL && *existing != '\0') {
        g_ptr_array_add(pinned_app_ids, g_strdup(existing));
      }
    }
  }

  if (pinned && !already_present) {
    g_ptr_array_add(pinned_app_ids, g_strdup(desktop_id));
  }

  bs_state_store_begin_update(app->state_store);
  bs_state_store_replace_pinned_app_ids(app->state_store, pinned_app_ids);
  bs_state_store_finish_update(app->state_store);
  return bs_settings_service_flush(app->settings_service, error);
}

bool
bs_shelld_app_pin_app(BsShelldApp *app,
                      const char *app_key,
                      GError **error) {
  return bs_shelld_app_set_app_pinned(app, app_key, true, error);
}

bool
bs_shelld_app_unpin_app(BsShelldApp *app,
                        const char *app_key,
                        GError **error) {
  return bs_shelld_app_set_app_pinned(app, app_key, false, error);
}
