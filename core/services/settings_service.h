#ifndef BIT_SHELL_CORE_SERVICES_SETTINGS_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_SETTINGS_SERVICE_H

#include <glib.h>
#include <gio/gio.h>
#include <stdbool.h>

#include "model/config.h"
#include "state/state_store.h"

typedef struct _BsSettingsService BsSettingsService;

typedef struct {
  const char *config_path;
  const char *state_path;
} BsSettingsServiceConfig;

typedef enum {
  BS_SETTINGS_RELOAD_NONE = 0,
  BS_SETTINGS_RELOAD_DOCK_CHANGED = 1u << 0,
  BS_SETTINGS_RELOAD_AUTO_RECONNECT_NIRI_CHANGED = 1u << 1,
  BS_SETTINGS_RELOAD_TRAY_WATCHER_CHANGED = 1u << 2,
  BS_SETTINGS_RELOAD_PRIMARY_OUTPUT_CHANGED = 1u << 3,
  BS_SETTINGS_RELOAD_BAR_CHANGED = 1u << 4,
  BS_SETTINGS_RELOAD_LAUNCHPAD_CHANGED = 1u << 5,
} BsSettingsReloadFlags;

typedef struct {
  BsSettingsReloadFlags changed;
  gboolean config_loaded;
  gboolean hot_applied;
  GPtrArray *hot_applied_keys;
  GPtrArray *restart_required_keys;
} BsSettingsReloadResult;

typedef struct {
  BsShellConfig next_config;
  BsSettingsReloadFlags changed;
} BsSettingsReloadPlan;

BsSettingsService *bs_settings_service_new(BsStateStore *store,
                                          const BsSettingsServiceConfig *config);
void bs_settings_service_free(BsSettingsService *service);

void bs_settings_reload_result_init(BsSettingsReloadResult *result);
void bs_settings_reload_result_clear(BsSettingsReloadResult *result);
void bs_settings_reload_plan_init(BsSettingsReloadPlan *plan);
void bs_settings_reload_plan_clear(BsSettingsReloadPlan *plan);

bool bs_settings_service_load_all(BsSettingsService *service, GError **error);
bool bs_settings_service_prepare_reload(BsSettingsService *service,
                                        BsSettingsReloadPlan *plan,
                                        GError **error);
bool bs_settings_service_apply_bar_config(BsSettingsService *service,
                                          const BsBarConfig *bar_config);
bool bs_settings_service_apply_dock_config(BsSettingsService *service,
                                           const BsDockConfig *dock_config);
bool bs_settings_service_commit_reload(BsSettingsService *service,
                                       const BsSettingsReloadPlan *plan,
                                       BsSettingsReloadResult *out,
                                       GError **error);
bool bs_settings_service_import_state(BsSettingsService *service, GError **error);
bool bs_settings_service_flush_state(BsSettingsService *service, GError **error);

const BsShellConfig *bs_settings_service_shell_config(const BsSettingsService *service);
void bs_settings_service_mark_state_dirty(BsSettingsService *service);

#endif
