#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>

#include "shelld/app.h"

static BsShelldConfig
bs_shelld_config_from_env(void) {
  BsShelldConfig config = {
    .niri_socket_path = g_getenv("NIRI_SOCKET"),
    .ipc_socket_path = g_getenv("BIT_SHELL_SOCKET"),
    .config_path = g_get_user_config_dir(),
    .state_path = g_get_user_state_dir(),
    .applications_dir = NULL,
    .tray_watcher_name = "org.kde.StatusNotifierWatcher",
    .auto_reconnect_niri = true,
  };

  return config;
}

int
main(void) {
  g_autoptr(GError) error = NULL;
  BsShelldConfig config = bs_shelld_config_from_env();
  BsShelldApp *app = bs_shelld_app_new(&config);
  int exit_code = 1;

  if (app == NULL) {
    g_printerr("[bit_shelld] failed to allocate application context\n");
    return EXIT_FAILURE;
  }

  exit_code = bs_shelld_app_run(app, &error);
  if (error != NULL) {
    g_printerr("[bit_shelld] %s\n", error->message);
  }

  bs_shelld_app_free(app);
  return exit_code;
}
