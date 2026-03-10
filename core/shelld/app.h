#ifndef BIT_SHELL_CORE_SHELLD_APP_H
#define BIT_SHELL_CORE_SHELLD_APP_H

#include <gio/gio.h>
#include <stdbool.h>

#include "model/config.h"
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
typedef BsShellConfig BsShelldConfig;

BsShelldApp *bs_shelld_app_new(const BsShelldConfig *config);
void bs_shelld_app_free(BsShelldApp *app);

bool bs_shelld_app_start(BsShelldApp *app, GError **error);
void bs_shelld_app_stop(BsShelldApp *app);
int bs_shelld_app_run(BsShelldApp *app, GError **error);

BsStateStore *bs_shelld_app_state_store(BsShelldApp *app);
BsCommandRouter *bs_shelld_app_command_router(BsShelldApp *app);
const BsShelldConfig *bs_shelld_app_config(const BsShelldApp *app);
bool bs_shelld_app_launch_app(BsShelldApp *app,
                              const char *desktop_id,
                              GError **error);
bool bs_shelld_app_activate_app(BsShelldApp *app,
                                const char *app_key,
                                GError **error);
bool bs_shelld_app_focus_window(BsShelldApp *app,
                                const char *window_id,
                                GError **error);
bool bs_shelld_app_switch_workspace(BsShelldApp *app,
                                    const char *workspace_id,
                                    GError **error);
bool bs_shelld_app_pin_app(BsShelldApp *app,
                           const char *app_key,
                           GError **error);
bool bs_shelld_app_unpin_app(BsShelldApp *app,
                             const char *app_key,
                             GError **error);

#endif
