#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>

#include "model/config.h"
#include "shelld/app.h"

static BsShelldConfig
bs_shelld_config_from_env(void) {
  BsShelldConfig config;
  const char *ipc_socket_override = NULL;

  bs_shell_config_init_defaults(&config);

  ipc_socket_override = g_getenv("BIT_SHELL_SOCKET");
  if (ipc_socket_override != NULL && *ipc_socket_override != '\0') {
    g_free(config.paths.ipc_socket_path);
    config.paths.ipc_socket_path = g_strdup(ipc_socket_override);
  }

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
