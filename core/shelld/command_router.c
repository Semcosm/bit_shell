#include "shelld/command_router.h"

#include <glib.h>
#include <string.h>

struct _BsCommandRouter {
  BsShelldApp *app;
};

BsCommandRouter *
bs_command_router_new(BsShelldApp *app) {
  BsCommandRouter *router = g_new0(BsCommandRouter, 1);
  router->app = app;
  return router;
}

void
bs_command_router_free(BsCommandRouter *router) {
  g_free(router);
}

bool
bs_command_router_handle_json(BsCommandRouter *router,
                              const char *payload,
                              char **response_json,
                              GError **error) {
  g_return_val_if_fail(router != NULL, false);
  g_return_val_if_fail(response_json != NULL, false);

  if (payload == NULL || *payload == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "empty IPC payload");
    return false;
  }

  if (g_str_has_prefix(payload, "{ \"op\": \"snapshot\"")) {
    *response_json = g_strdup("{\"ok\":true,\"kind\":\"snapshot\",\"todo\":\"serialize-store\"}");
    return true;
  }

  *response_json = g_strdup("{\"ok\":true,\"kind\":\"ack\",\"todo\":\"route-command\"}");
  return true;
}
