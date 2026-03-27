#include "shelld/app.h"

#include <glib.h>

#include "shelld/config_watcher.h"

#define BS_WINDOW_CYCLE_CONTEXT_TIMEOUT_US (3 * G_TIME_SPAN_SECOND)

struct _BsShelldApp {
  BsShelldConfig config;
  GMainLoop *main_loop;
  BsStateStore *state_store;
  BsNiriBackend *niri_backend;
  BsAppRegistry *app_registry;
  BsWorkspaceService *workspace_service;
  BsDockService *dock_service;
  BsLauncherService *launcher_service;
  BsTrayMenuService *tray_menu_service;
  BsTrayService *tray_service;
  BsSettingsService *settings_service;
  BsConfigWatcher *config_watcher;
  BsCommandRouter *command_router;
  BsIpcServer *ipc_server;
  bool running;
  struct {
    char *app_key;
    GPtrArray *window_ids;
    guint current_index;
    gint64 last_request_time_us;
  } window_cycle_context;
};

static void bs_shelld_app_rebuild_derived_state(BsStateStore *store, gpointer user_data);
static void bs_shelld_app_reset_window_cycle_context(BsShelldApp *app);
static GPtrArray *bs_shelld_app_collect_windows_for_app(BsShelldApp *app, const char *app_key);
static gint bs_shelld_app_compare_windows_by_focus_ts(gconstpointer lhs, gconstpointer rhs);
static void bs_shelld_app_log_app_windows(const char *prefix, const char *app_key, GPtrArray *windows);
static const BsWindow *bs_shelld_app_find_focused_window(GPtrArray *windows);
static const BsWindow *bs_shelld_app_find_window_by_id(GPtrArray *windows, const char *window_id);
static GPtrArray *bs_shelld_app_build_cycle_window_ids(GPtrArray *windows);
static bool bs_shelld_app_window_set_matches_cycle_context(GPtrArray *windows, GPtrArray *window_ids);
static bool bs_shelld_app_prepare_window_cycle_context(BsShelldApp *app,
                                                       const char *app_key,
                                                       GPtrArray *windows);
static bool bs_shelld_app_focus_adjacent_app_window(BsShelldApp *app,
                                                    const char *app_key,
                                                    int direction,
                                                    GError **error);
static bool bs_shelld_app_set_app_pinned(BsShelldApp *app,
                                         const char *app_key,
                                         bool pinned,
                                         GError **error);
static bool bs_shelld_app_recreate_tray_service(BsShelldApp *app,
                                                const char *watcher_name,
                                                GError **error);
static void bs_shelld_app_append_reload_key(GPtrArray *values, const char *value);
static bool bs_shelld_app_reload_settings_from_watcher(gpointer user_data,
                                                       BsSettingsReloadResult *result,
                                                       GError **error);

BsShelldApp *
bs_shelld_app_new(const BsShelldConfig *config) {
  BsShelldApp *app = g_new0(BsShelldApp, 1);
  BsNiriBackendConfig niri_config = {0};
  BsAppRegistryConfig app_registry_config = {0};
  BsTrayServiceConfig tray_config = {0};
  BsSettingsServiceConfig settings_config = {0};
  BsConfigWatcherConfig watcher_config = {0};
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
  app->tray_menu_service = bs_tray_menu_service_new(app->state_store);
  tray_config.menu_service = app->tray_menu_service;
  app->tray_service = bs_tray_service_new(app->state_store, &tray_config);

  settings_config.config_path = app->config.paths.config_path;
  settings_config.state_path = app->config.paths.state_path;
  app->settings_service = bs_settings_service_new(app->state_store, &settings_config);

  watcher_config.config_path = app->config.paths.config_path;
  watcher_config.main_context = g_main_loop_get_context(app->main_loop);
  watcher_config.debounce_ms = 200;
  watcher_config.reload_func = bs_shelld_app_reload_settings_from_watcher;
  watcher_config.reload_user_data = app;
  app->config_watcher = bs_config_watcher_new(&watcher_config);

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
  bs_config_watcher_free(app->config_watcher);
  bs_settings_service_free(app->settings_service);
  bs_tray_service_free(app->tray_service);
  bs_tray_menu_service_free(app->tray_menu_service);
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
  bs_shelld_app_reset_window_cycle_context(app);
  g_free(app);
}

bool
bs_shelld_app_start(BsShelldApp *app, GError **error) {
  g_autoptr(GError) niri_error = NULL;
  g_autoptr(GError) watcher_error = NULL;

  g_return_val_if_fail(app != NULL, false);

  if (!bs_settings_service_load_all(app->settings_service, error)) {
    return false;
  }
  bs_shell_config_clear(&app->config);
  bs_shell_config_copy(&app->config, bs_settings_service_shell_config(app->settings_service));

  if (!bs_app_registry_start(app->app_registry, error)) {
    return false;
  }

  if (!bs_launcher_service_refresh(app->launcher_service, error)) {
    return false;
  }

  if (!bs_workspace_service_rebuild(app->workspace_service, error)) {
    return false;
  }

  if (!bs_tray_menu_service_start(app->tray_menu_service, error)) {
    return false;
  }

  if (!bs_tray_service_start(app->tray_service, error)) {
    return false;
  }

  if (!bs_niri_backend_start(app->niri_backend, &niri_error)) {
    return false;
  }
  if (niri_error != NULL) {
    g_warning("[bit_shelld] started with degraded niri backend: %s", niri_error->message);
  }

  if (!bs_ipc_server_start(app->ipc_server, error)) {
    return false;
  }
  if (app->config_watcher != NULL && !bs_config_watcher_start(app->config_watcher, &watcher_error)) {
    g_warning("[bit_shelld] config watcher disabled: %s",
              watcher_error != NULL ? watcher_error->message : "unknown error");
  }

  app->running = true;
  return true;
}

void
bs_shelld_app_stop(BsShelldApp *app) {
  if (app == NULL || !app->running) {
    return;
  }

  if (app->config_watcher != NULL) {
    bs_config_watcher_stop(app->config_watcher);
  }
  bs_ipc_server_stop(app->ipc_server);
  bs_tray_service_stop(app->tray_service);
  bs_tray_menu_service_stop(app->tray_menu_service);
  bs_niri_backend_stop(app->niri_backend);
  bs_app_registry_stop(app->app_registry);
  (void) bs_settings_service_flush_state(app->settings_service, NULL);
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

bool
bs_shelld_app_reload_settings(BsShelldApp *app,
                              BsSettingsReloadResult *result,
                              GError **error) {
  BsSettingsReloadPlan plan = {0};
  const BsShellConfig *current_config = NULL;
  bool tray_recreated = false;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(result != NULL, false);

  bs_settings_reload_plan_init(&plan);
  if (!bs_settings_service_prepare_reload(app->settings_service, &plan, error)) {
    return false;
  }
  result->changed = plan.changed;
  result->config_loaded = true;
  current_config = bs_settings_service_shell_config(app->settings_service);

  if ((plan.changed & BS_SETTINGS_RELOAD_TRAY_WATCHER_CHANGED) != 0) {
    if (!bs_shelld_app_recreate_tray_service(app, plan.next_config.tray_watcher_name, error)) {
      bs_settings_reload_plan_clear(&plan);
      return false;
    }
    tray_recreated = true;
    bs_shelld_app_append_reload_key(result->hot_applied_keys, "shell.tray_watcher_name");
    result->hot_applied = true;
  }

  if ((plan.changed & BS_SETTINGS_RELOAD_AUTO_RECONNECT_NIRI_CHANGED) != 0) {
    if (!bs_niri_backend_set_auto_reconnect(app->niri_backend,
                                            plan.next_config.auto_reconnect_niri,
                                            error)) {
      if (tray_recreated) {
        g_autoptr(GError) rollback_error = NULL;

        if (!bs_shelld_app_recreate_tray_service(app,
                                                 current_config->tray_watcher_name,
                                                 &rollback_error)) {
          g_warning("[bit_shelld] failed to rollback tray watcher after reload failure: %s",
                    rollback_error != NULL ? rollback_error->message : "unknown error");
        }
      }
      bs_settings_reload_plan_clear(&plan);
      return false;
    }
    bs_shelld_app_append_reload_key(result->hot_applied_keys, "shell.auto_reconnect_niri");
    result->hot_applied = true;
  }

  if ((plan.changed & BS_SETTINGS_RELOAD_DOCK_CHANGED) != 0) {
    if (!bs_settings_service_apply_dock_config(app->settings_service, &plan.next_config.dock)) {
      if (tray_recreated) {
        g_autoptr(GError) rollback_error = NULL;

        if (!bs_shelld_app_recreate_tray_service(app,
                                                 current_config->tray_watcher_name,
                                                 &rollback_error)) {
          g_warning("[bit_shelld] failed to rollback tray watcher after dock apply failure: %s",
                    rollback_error != NULL ? rollback_error->message : "unknown error");
        }
      }
      bs_settings_reload_plan_clear(&plan);
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to apply dock config");
      return false;
    }
    bs_shelld_app_append_reload_key(result->hot_applied_keys, "dock.*");
    result->hot_applied = true;
  }

  if ((plan.changed & BS_SETTINGS_RELOAD_BAR_CHANGED) != 0) {
    if (!bs_settings_service_apply_bar_config(app->settings_service, &plan.next_config.bar)) {
      if (tray_recreated) {
        g_autoptr(GError) rollback_error = NULL;

        if (!bs_shelld_app_recreate_tray_service(app,
                                                 current_config->tray_watcher_name,
                                                 &rollback_error)) {
          g_warning("[bit_shelld] failed to rollback tray watcher after bar apply failure: %s",
                    rollback_error != NULL ? rollback_error->message : "unknown error");
        }
      }
      bs_settings_reload_plan_clear(&plan);
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to apply bar config");
      return false;
    }
    bs_shelld_app_append_reload_key(result->hot_applied_keys, "bar.*");
    result->hot_applied = true;
  }

  if ((plan.changed & BS_SETTINGS_RELOAD_PRIMARY_OUTPUT_CHANGED) != 0) {
    bs_shelld_app_append_reload_key(result->restart_required_keys, "shell.primary_output");
  }
  if ((plan.changed & BS_SETTINGS_RELOAD_LAUNCHPAD_CHANGED) != 0) {
    bs_shelld_app_append_reload_key(result->restart_required_keys, "launchpad.*");
  }

  if (!bs_settings_service_commit_reload(app->settings_service, &plan, result, error)) {
    bs_settings_reload_plan_clear(&plan);
    return false;
  }
  bs_shell_config_clear(&app->config);
  bs_shell_config_copy(&app->config, bs_settings_service_shell_config(app->settings_service));
  bs_settings_reload_plan_clear(&plan);

  g_message("[bit_shelld] settings reloaded: flags=%u hot=%s restart_required=%u",
            (unsigned int) result->changed,
            result->hot_applied ? "true" : "false",
            result->restart_required_keys != NULL ? result->restart_required_keys->len : 0);
  return true;
}

static bool
bs_shelld_app_recreate_tray_service(BsShelldApp *app,
                                    const char *watcher_name,
                                    GError **error) {
  BsTrayServiceConfig tray_config = {0};
  BsTrayService *next = NULL;
  BsTrayService *previous = NULL;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(watcher_name != NULL, false);

  tray_config.watcher_name = watcher_name;
  next = bs_tray_service_new(app->state_store, &tray_config);
  if (next == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to allocate tray service");
    return false;
  }

  previous = app->tray_service;
  if (previous != NULL) {
    bs_tray_service_stop(previous);
  }

  if (!bs_tray_service_start(next, error)) {
    g_autoptr(GError) rollback_error = NULL;

    if (previous != NULL && !bs_tray_service_start(previous, &rollback_error)) {
      g_warning("[bit_shelld] failed to restore previous tray service: %s",
                rollback_error != NULL ? rollback_error->message : "unknown error");
    }
    bs_tray_service_free(next);
    return false;
  }

  app->tray_service = next;
  if (previous != NULL) {
    bs_tray_service_free(previous);
  }
  return true;
}

static void
bs_shelld_app_append_reload_key(GPtrArray *values, const char *value) {
  g_return_if_fail(values != NULL);
  g_return_if_fail(value != NULL);

  g_ptr_array_add(values, g_strdup(value));
}

static bool
bs_shelld_app_reload_settings_from_watcher(gpointer user_data,
                                           BsSettingsReloadResult *result,
                                           GError **error) {
  return bs_shelld_app_reload_settings(user_data, result, error);
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
  g_autoptr(GPtrArray) windows = NULL;
  const BsWindow *window = NULL;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app_key != NULL, false);

  windows = bs_shelld_app_collect_windows_for_app(app, app_key);
  window = windows != NULL ? bs_shelld_app_find_focused_window(windows) : NULL;
  if (window == NULL && windows != NULL && windows->len > 0) {
    window = g_ptr_array_index(windows, 0);
  }
  if (window == NULL || window->id == NULL) {
    bs_shelld_app_log_app_windows("activate_app missing target", app_key, windows);
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "no running window found for app_key: %s",
                app_key);
    return false;
  }

  bs_shelld_app_log_app_windows("activate_app resolved windows", app_key, windows);
  bs_shelld_app_reset_window_cycle_context(app);
  return bs_niri_backend_focus_window(app->niri_backend, window->id, error);
}

static void
bs_shelld_app_reset_window_cycle_context(BsShelldApp *app) {
  g_return_if_fail(app != NULL);

  g_clear_pointer(&app->window_cycle_context.app_key, g_free);
  g_clear_pointer(&app->window_cycle_context.window_ids, g_ptr_array_unref);
  app->window_cycle_context.current_index = 0;
  app->window_cycle_context.last_request_time_us = 0;
}

static GPtrArray *
bs_shelld_app_collect_windows_for_app(BsShelldApp *app, const char *app_key) {
  GPtrArray *windows = NULL;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(app_key != NULL, NULL);

  windows = bs_state_store_list_app_windows(app->state_store, app_key);
  if (windows == NULL) {
    return NULL;
  }
  g_ptr_array_sort(windows, bs_shelld_app_compare_windows_by_focus_ts);
  return windows;
}

static gint
bs_shelld_app_compare_windows_by_focus_ts(gconstpointer lhs, gconstpointer rhs) {
  const BsWindow *a = *(const BsWindow * const *) lhs;
  const BsWindow *b = *(const BsWindow * const *) rhs;

  if (a->focus_ts != b->focus_ts) {
    return a->focus_ts > b->focus_ts ? -1 : 1;
  }
  return g_strcmp0(a->id, b->id);
}

static void
bs_shelld_app_log_app_windows(const char *prefix, const char *app_key, GPtrArray *windows) {
  GString *message = NULL;

  g_return_if_fail(app_key != NULL);

  if (windows == NULL || windows->len == 0) {
    g_message("[bit_shelld] %s app_key=%s windows=[]",
              prefix != NULL ? prefix : "app windows",
              app_key);
    return;
  }

  message = g_string_new(NULL);
  for (guint i = 0; i < windows->len; i++) {
    const BsWindow *window = g_ptr_array_index(windows, i);

    if (window == NULL) {
      continue;
    }
    if (message->len > 0) {
      g_string_append(message, ", ");
    }
    g_string_append_printf(message,
                           "{id=%s focused=%s desktop_id=%s app_id=%s focus_ts=%" G_GUINT64_FORMAT "}",
                           window->id != NULL ? window->id : "(null)",
                           window->focused ? "true" : "false",
                           window->desktop_id != NULL ? window->desktop_id : "(null)",
                           window->app_id != NULL ? window->app_id : "(null)",
                           window->focus_ts);
  }

  g_message("[bit_shelld] %s app_key=%s windows=[%s]",
            prefix != NULL ? prefix : "app windows",
            app_key,
            message->str);
  g_string_free(message, true);
}

static const BsWindow *
bs_shelld_app_find_focused_window(GPtrArray *windows) {
  g_return_val_if_fail(windows != NULL, NULL);

  for (guint i = 0; i < windows->len; i++) {
    const BsWindow *window = g_ptr_array_index(windows, i);

    if (window != NULL && window->focused) {
      return window;
    }
  }

  return NULL;
}

static const BsWindow *
bs_shelld_app_find_window_by_id(GPtrArray *windows, const char *window_id) {
  g_return_val_if_fail(windows != NULL, NULL);
  g_return_val_if_fail(window_id != NULL, NULL);

  for (guint i = 0; i < windows->len; i++) {
    const BsWindow *window = g_ptr_array_index(windows, i);

    if (window != NULL && g_strcmp0(window->id, window_id) == 0) {
      return window;
    }
  }

  return NULL;
}

static GPtrArray *
bs_shelld_app_build_cycle_window_ids(GPtrArray *windows) {
  g_autoptr(GPtrArray) window_ids = NULL;
  const BsWindow *focused_window = NULL;

  g_return_val_if_fail(windows != NULL, NULL);

  window_ids = g_ptr_array_new_with_free_func(g_free);
  focused_window = bs_shelld_app_find_focused_window(windows);
  if (focused_window != NULL && focused_window->id != NULL) {
    g_ptr_array_add(window_ids, g_strdup(focused_window->id));
  }

  for (guint i = 0; i < windows->len; i++) {
    const BsWindow *window = g_ptr_array_index(windows, i);

    if (window == NULL
        || window->id == NULL
        || (focused_window != NULL && g_strcmp0(window->id, focused_window->id) == 0)) {
      continue;
    }

    g_ptr_array_add(window_ids, g_strdup(window->id));
  }

  return g_steal_pointer(&window_ids);
}

static bool
bs_shelld_app_window_set_matches_cycle_context(GPtrArray *windows, GPtrArray *window_ids) {
  g_return_val_if_fail(windows != NULL, false);
  g_return_val_if_fail(window_ids != NULL, false);

  if (windows->len != window_ids->len) {
    return false;
  }

  for (guint i = 0; i < window_ids->len; i++) {
    const char *window_id = g_ptr_array_index(window_ids, i);

    if (window_id == NULL || bs_shelld_app_find_window_by_id(windows, window_id) == NULL) {
      return false;
    }
  }

  return true;
}

static bool
bs_shelld_app_prepare_window_cycle_context(BsShelldApp *app,
                                           const char *app_key,
                                           GPtrArray *windows) {
  const gint64 now_us = g_get_monotonic_time();

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app_key != NULL, false);
  g_return_val_if_fail(windows != NULL, false);

  if (app->window_cycle_context.app_key != NULL
      && g_strcmp0(app->window_cycle_context.app_key, app_key) == 0
      && app->window_cycle_context.window_ids != NULL
      && app->window_cycle_context.last_request_time_us > 0
      && (now_us - app->window_cycle_context.last_request_time_us) < BS_WINDOW_CYCLE_CONTEXT_TIMEOUT_US
      && bs_shelld_app_window_set_matches_cycle_context(windows,
                                                        app->window_cycle_context.window_ids)) {
    return true;
  }

  bs_shelld_app_reset_window_cycle_context(app);
  app->window_cycle_context.app_key = g_strdup(app_key);
  app->window_cycle_context.window_ids = bs_shelld_app_build_cycle_window_ids(windows);
  app->window_cycle_context.current_index = 0;
  app->window_cycle_context.last_request_time_us = 0;
  return app->window_cycle_context.window_ids != NULL
         && app->window_cycle_context.window_ids->len > 0;
}

static bool
bs_shelld_app_focus_adjacent_app_window(BsShelldApp *app,
                                        const char *app_key,
                                        int direction,
                                        GError **error) {
  g_autoptr(GPtrArray) windows = NULL;
  GPtrArray *cycle_window_ids = NULL;
  guint current_index = 0;
  guint target_index = 0;
  const BsWindow *current_window = NULL;
  const BsWindow *target_window = NULL;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app_key != NULL, false);
  g_return_val_if_fail(direction == 1 || direction == -1, false);

  windows = bs_shelld_app_collect_windows_for_app(app, app_key);
  if (windows == NULL || windows->len == 0) {
    bs_shelld_app_reset_window_cycle_context(app);
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "no running windows found for app_key: %s",
                app_key);
    return false;
  }

  if (!bs_shelld_app_prepare_window_cycle_context(app, app_key, windows)) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "failed to build window cycle context for app_key: %s",
                app_key);
    return false;
  }

  cycle_window_ids = app->window_cycle_context.window_ids;
  current_index = MIN(app->window_cycle_context.current_index, cycle_window_ids->len - 1);
  current_window = bs_shelld_app_find_window_by_id(windows,
                                                   g_ptr_array_index(cycle_window_ids, current_index));
  if (cycle_window_ids->len > 1) {
    if (direction > 0) {
      target_index = (current_index + 1) % cycle_window_ids->len;
    } else {
      target_index = (current_index + cycle_window_ids->len - 1) % cycle_window_ids->len;
    }
  }

  target_window = bs_shelld_app_find_window_by_id(windows,
                                                  g_ptr_array_index(cycle_window_ids, target_index));
  if (target_window == NULL || target_window->id == NULL) {
    bs_shelld_app_reset_window_cycle_context(app);
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "failed to resolve adjacent window for app_key: %s",
                app_key);
    return false;
  }

  g_message("[bit_shelld] %s app_key=%s windows=%u current_window=%s current_focus_ts=%" G_GUINT64_FORMAT " target_window=%s target_focus_ts=%" G_GUINT64_FORMAT,
            direction > 0 ? "focus_next_app_window" : "focus_prev_app_window",
            app_key,
            windows->len,
            current_window != NULL && current_window->id != NULL ? current_window->id : "(null)",
            current_window != NULL ? current_window->focus_ts : 0,
            target_window->id,
            target_window->focus_ts);
  if (!bs_niri_backend_focus_window(app->niri_backend, target_window->id, error)) {
    return false;
  }

  app->window_cycle_context.current_index = target_index;
  app->window_cycle_context.last_request_time_us = g_get_monotonic_time();
  return true;
}

bool
bs_shelld_app_focus_next_app_window(BsShelldApp *app,
                                    const char *app_key,
                                    GError **error) {
  return bs_shelld_app_focus_adjacent_app_window(app, app_key, 1, error);
}

bool
bs_shelld_app_focus_prev_app_window(BsShelldApp *app,
                                    const char *app_key,
                                    GError **error) {
  return bs_shelld_app_focus_adjacent_app_window(app, app_key, -1, error);
}

bool
bs_shelld_app_focus_window(BsShelldApp *app,
                           const char *window_id,
                           GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(window_id != NULL, false);

  bs_shelld_app_reset_window_cycle_context(app);
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
  return bs_settings_service_flush_state(app->settings_service, error);
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

bool
bs_shelld_app_tray_activate_item(BsShelldApp *app,
                                 const char *item_id,
                                 int32_t x,
                                 int32_t y,
                                 GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);

  return bs_tray_service_activate_item(app->tray_service, item_id, x, y, error);
}

bool
bs_shelld_app_tray_context_menu_item(BsShelldApp *app,
                                     const char *item_id,
                                     int32_t x,
                                     int32_t y,
                                     GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);

  return bs_tray_service_context_menu_item(app->tray_service, item_id, x, y, error);
}

bool
bs_shelld_app_tray_menu_activate_item(BsShelldApp *app,
                                      const char *item_id,
                                      int32_t menu_item_id,
                                      GError **error) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);

  return bs_tray_menu_service_activate_menu_item(app->tray_menu_service,
                                                 item_id,
                                                 menu_item_id,
                                                 error);
}
