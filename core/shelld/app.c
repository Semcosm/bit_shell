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
  app->dock_service = bs_dock_service_new(app->state_store);
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
