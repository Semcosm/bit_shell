#include "shelld/config_watcher.h"

#include <glib.h>

struct _BsConfigWatcher {
  char *config_path;
  char *config_dir_path;
  char *config_basename;
  GMainContext *main_context;
  GFile *config_dir_file;
  GFileMonitor *monitor;
  guint debounce_ms;
  guint debounce_source_id;
  BsConfigWatcherReloadFunc reload_func;
  gpointer reload_user_data;
  bool started;
};

static void bs_config_watcher_on_changed(GFileMonitor *monitor,
                                         GFile *file,
                                         GFile *other_file,
                                         GFileMonitorEvent event_type,
                                         gpointer user_data);
static void bs_config_watcher_schedule_reload(BsConfigWatcher *watcher);
static gboolean bs_config_watcher_on_debounce_fire(gpointer user_data);

BsConfigWatcher *
bs_config_watcher_new(const BsConfigWatcherConfig *config) {
  BsConfigWatcher *watcher = g_new0(BsConfigWatcher, 1);

  if (config != NULL) {
    watcher->config_path = g_strdup(config->config_path);
    watcher->debounce_ms = config->debounce_ms > 0 ? config->debounce_ms : 200;
    watcher->reload_func = config->reload_func;
    watcher->reload_user_data = config->reload_user_data;
    watcher->main_context = g_main_context_ref(config->main_context != NULL
                                                 ? config->main_context
                                                 : g_main_context_default());
  } else {
    watcher->debounce_ms = 200;
    watcher->main_context = g_main_context_ref(g_main_context_default());
  }

  return watcher;
}

void
bs_config_watcher_free(BsConfigWatcher *watcher) {
  if (watcher == NULL) {
    return;
  }

  bs_config_watcher_stop(watcher);
  g_clear_object(&watcher->config_dir_file);
  g_clear_pointer(&watcher->config_path, g_free);
  g_clear_pointer(&watcher->config_dir_path, g_free);
  g_clear_pointer(&watcher->config_basename, g_free);
  g_clear_pointer(&watcher->main_context, g_main_context_unref);
  g_free(watcher);
}

bool
bs_config_watcher_start(BsConfigWatcher *watcher, GError **error) {
  g_return_val_if_fail(watcher != NULL, false);

  if (watcher->started) {
    return true;
  }
  if (watcher->config_path == NULL || *watcher->config_path == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing config watcher path");
    return false;
  }
  if (watcher->reload_func == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing config watcher reload callback");
    return false;
  }

  watcher->config_dir_path = g_path_get_dirname(watcher->config_path);
  watcher->config_basename = g_path_get_basename(watcher->config_path);
  watcher->config_dir_file = g_file_new_for_path(watcher->config_dir_path);
  watcher->monitor = g_file_monitor_directory(watcher->config_dir_file,
                                              G_FILE_MONITOR_NONE,
                                              NULL,
                                              error);
  if (watcher->monitor == NULL) {
    return false;
  }

  g_signal_connect(watcher->monitor, "changed", G_CALLBACK(bs_config_watcher_on_changed), watcher);
  watcher->started = true;
  return true;
}

void
bs_config_watcher_stop(BsConfigWatcher *watcher) {
  if (watcher == NULL) {
    return;
  }

  watcher->started = false;
  if (watcher->debounce_source_id != 0) {
    g_source_remove(watcher->debounce_source_id);
    watcher->debounce_source_id = 0;
  }
  if (watcher->monitor != NULL) {
    g_file_monitor_cancel(watcher->monitor);
    g_clear_object(&watcher->monitor);
  }
}

static void
bs_config_watcher_on_changed(GFileMonitor *monitor,
                             GFile *file,
                             GFile *other_file,
                             GFileMonitorEvent event_type,
                             gpointer user_data) {
  BsConfigWatcher *watcher = user_data;
  GFile *target = NULL;
  g_autofree char *basename = NULL;

  (void) monitor;
  g_return_if_fail(watcher != NULL);

  target = file != NULL ? file : other_file;
  if (target == NULL) {
    return;
  }

  switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
      break;
    default:
      return;
  }

  basename = g_file_get_basename(target);
  if (g_strcmp0(basename, watcher->config_basename) != 0) {
    return;
  }

  bs_config_watcher_schedule_reload(watcher);
}

static void
bs_config_watcher_schedule_reload(BsConfigWatcher *watcher) {
  GSource *source = NULL;

  g_return_if_fail(watcher != NULL);

  if (watcher->debounce_source_id != 0) {
    g_source_remove(watcher->debounce_source_id);
    watcher->debounce_source_id = 0;
  }

  source = g_timeout_source_new(watcher->debounce_ms);
  g_source_set_callback(source, bs_config_watcher_on_debounce_fire, watcher, NULL);
  g_source_attach(source, watcher->main_context);
  watcher->debounce_source_id = g_source_get_id(source);
  g_source_unref(source);
}

static gboolean
bs_config_watcher_on_debounce_fire(gpointer user_data) {
  BsConfigWatcher *watcher = user_data;
  g_autoptr(GError) error = NULL;
  BsSettingsReloadResult result;

  g_return_val_if_fail(watcher != NULL, G_SOURCE_REMOVE);

  watcher->debounce_source_id = 0;
  bs_settings_reload_result_init(&result);
  if (!watcher->reload_func(watcher->reload_user_data, &result, &error)) {
    g_warning("[bit_shelld] config watcher reload rejected: %s",
              error != NULL ? error->message : "unknown error");
    bs_settings_reload_result_clear(&result);
    return G_SOURCE_REMOVE;
  }

  if (result.changed == BS_SETTINGS_RELOAD_NONE) {
    g_message("[bit_shelld] config watcher: no effective config change");
  }

  bs_settings_reload_result_clear(&result);
  return G_SOURCE_REMOVE;
}
