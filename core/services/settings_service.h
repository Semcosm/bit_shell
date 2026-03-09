#ifndef BIT_SHELL_CORE_SERVICES_SETTINGS_SERVICE_H
#define BIT_SHELL_CORE_SERVICES_SETTINGS_SERVICE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "model/config.h"
#include "state/state_store.h"

typedef struct _BsSettingsService BsSettingsService;

typedef struct {
  const char *config_path;
  const char *state_path;
} BsSettingsServiceConfig;

BsSettingsService *bs_settings_service_new(BsStateStore *store, const BsSettingsServiceConfig *config);
void bs_settings_service_free(BsSettingsService *service);

bool bs_settings_service_load(BsSettingsService *service, GError **error);
bool bs_settings_service_flush(BsSettingsService *service, GError **error);

const BsShellConfig *bs_settings_service_shell_config(const BsSettingsService *service);
void bs_settings_service_mark_state_dirty(BsSettingsService *service);

#endif
