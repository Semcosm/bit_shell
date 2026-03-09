#include "services/settings_service.h"

#include <glib.h>

struct _BsSettingsService {
  BsStateStore *store;
  char *config_path;
  char *state_path;
};

BsSettingsService *
bs_settings_service_new(BsStateStore *store, const BsSettingsServiceConfig *config) {
  BsSettingsService *service = g_new0(BsSettingsService, 1);
  service->store = store;
  if (config != NULL && config->config_path != NULL) {
    service->config_path = g_strdup(config->config_path);
  }
  if (config != NULL && config->state_path != NULL) {
    service->state_path = g_strdup(config->state_path);
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
  g_free(service);
}

bool
bs_settings_service_load(BsSettingsService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  bs_state_store_mark_topic_changed(service->store, BS_TOPIC_SETTINGS);
  return true;
}

bool
bs_settings_service_flush(BsSettingsService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);
  return true;
}
