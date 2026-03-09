#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>

#include "model/config.h"
#include "shelld/app.h"

static BsShelldConfig
bs_shelld_config_from_env(void) {
  BsShelldConfig config;

  bs_shell_config_init_defaults(&config);
  g_free(config.paths.niri_socket_path);
  g_free(config.paths.ipc_socket_path);
  g_free(config.paths.config_path);
  g_free(config.paths.state_path);

  config.paths.niri_socket_path = g_strdup(g_getenv("NIRI_SOCKET"));
  config.paths.ipc_socket_path = g_strdup(g_getenv("BIT_SHELL_SOCKET"));
  config.paths.config_path = g_strdup(g_get_user_config_dir());
  config.paths.state_path = g_strdup(g_get_user_state_dir());
  config.paths.applications_dir = NULL;

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
    bs_shell_config_clear(&config);
    return EXIT_FAILURE;
  }

  exit_code = bs_shelld_app_run(app, &error);
  if (error != NULL) {
    g_printerr("[bit_shelld] %s\n", error->message);
  }

  bs_shelld_app_free(app);
  bs_shell_config_clear(&config);
  return exit_code;
}
