#ifndef BIT_SHELL_CORE_SHELLD_CONFIG_WATCHER_H
#define BIT_SHELL_CORE_SHELLD_CONFIG_WATCHER_H

#include <gio/gio.h>
#include <stdbool.h>

#include "services/settings_service.h"

typedef bool (*BsConfigWatcherReloadFunc)(gpointer user_data,
                                          BsSettingsReloadResult *result,
                                          GError **error);

typedef struct _BsConfigWatcher BsConfigWatcher;

typedef struct {
  const char *config_path;
  GMainContext *main_context;
  guint debounce_ms;
  BsConfigWatcherReloadFunc reload_func;
  gpointer reload_user_data;
} BsConfigWatcherConfig;

BsConfigWatcher *bs_config_watcher_new(const BsConfigWatcherConfig *config);
void bs_config_watcher_free(BsConfigWatcher *watcher);

bool bs_config_watcher_start(BsConfigWatcher *watcher, GError **error);
void bs_config_watcher_stop(BsConfigWatcher *watcher);

#endif
