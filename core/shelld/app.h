#ifndef BIT_SHELL_CORE_SHELLD_APP_H
#define BIT_SHELL_CORE_SHELLD_APP_H

#include <gio/gio.h>
#include <stdbool.h>

#include "niri/niri_backend.h"
#include "services/app_registry.h"
#include "services/dock_service.h"
#include "services/launcher_service.h"
#include "services/settings_service.h"
#include "services/tray_service.h"
#include "services/workspace_service.h"
#include "shelld/command_router.h"
#include "shelld/ipc_server.h"
#include "state/state_store.h"

typedef struct _BsShelldApp BsShelldApp;

typedef struct {
  const char *niri_socket_path;
  const char *ipc_socket_path;
  const char *config_path;
  const char *state_path;
  const char *applications_dir;
  const char *tray_watcher_name;
  bool auto_reconnect_niri;
} BsShelldConfig;

BsShelldApp *bs_shelld_app_new(const BsShelldConfig *config);
void bs_shelld_app_free(BsShelldApp *app);

bool bs_shelld_app_start(BsShelldApp *app, GError **error);
void bs_shelld_app_stop(BsShelldApp *app);
int bs_shelld_app_run(BsShelldApp *app, GError **error);

BsStateStore *bs_shelld_app_state_store(BsShelldApp *app);
BsCommandRouter *bs_shelld_app_command_router(BsShelldApp *app);

#endif
