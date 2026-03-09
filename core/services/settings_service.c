#include "services/settings_service.h"

#include <glib.h>

struct _BsSettingsService {
  BsStateStore *store;
  char *config_path;
  char *state_path;
  BsShellConfig shell_config;
  bool dirty_state;
};

BsSettingsService *
bs_settings_service_new(BsStateStore *store, const BsSettingsServiceConfig *config) {
  BsSettingsService *service = g_new0(BsSettingsService, 1);
  service->store = store;
  bs_shell_config_init_defaults(&service->shell_config);
  if (config != NULL && config->config_path != NULL) {
    service->config_path = g_strdup(config->config_path);
    g_free(service->shell_config.paths.config_path);
    service->shell_config.paths.config_path = g_strdup(config->config_path);
  }
  if (config != NULL && config->state_path != NULL) {
    service->state_path = g_strdup(config->state_path);
    g_free(service->shell_config.paths.state_path);
    service->shell_config.paths.state_path = g_strdup(config->state_path);
  }
  return service;
}

void
bs_settings_service_free(BsSettingsService *service) {
  if (service == NULL) {
    return;
  }

  g_free(service->config_path);
  g_free(service->state_path);
  bs_shell_config_clear(&service->shell_config);
  g_free(service);
}

bool
bs_settings_service_load(BsSettingsService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  (void) error;
  bs_state_store_mark_topic_changed(service->store, BS_TOPIC_SETTINGS);
  return true;
}

bool
bs_settings_service_flush(BsSettingsService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  (void) error;
  service->dirty_state = false;
  return true;
}

const BsShellConfig *
bs_settings_service_shell_config(const BsSettingsService *service) {
  g_return_val_if_fail(service != NULL, NULL);
  return &service->shell_config;
}

void
bs_settings_service_mark_state_dirty(BsSettingsService *service) {
  g_return_if_fail(service != NULL);
  service->dirty_state = true;
}
