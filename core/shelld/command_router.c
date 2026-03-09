#include "shelld/command_router.h"

#include <glib.h>
#include <string.h>

struct _BsCommandRouter {
  BsShelldApp *app;
};

static bool
bs_json_extract_op(const char *payload, char *buffer, size_t buffer_len) {
  const char *needle = "\"op\"";
  const char *pos = NULL;
  const char *first_quote = NULL;
  const char *second_quote = NULL;
  size_t value_len = 0;

  if (payload == NULL || buffer == NULL || buffer_len == 0) {
    return false;
  }

  pos = strstr(payload, needle);
  if (pos == NULL) {
    return false;
  }

  first_quote = strchr(pos + strlen(needle), '"');
  if (first_quote == NULL) {
    return false;
  }

  second_quote = strchr(first_quote + 1, '"');
  if (second_quote == NULL) {
    return false;
  }

  value_len = (size_t) (second_quote - (first_quote + 1));
  if (value_len >= buffer_len) {
    return false;
  }

  memcpy(buffer, first_quote + 1, value_len);
  buffer[value_len] = '\0';
  return true;
}

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
bs_command_router_parse_request(BsCommandRouter *router,
                                const char *payload,
                                BsIpcRequest *request,
                                GError **error) {
  char op[64] = {0};

  g_return_val_if_fail(router != NULL, false);
  g_return_val_if_fail(request != NULL, false);

  request->command = BS_COMMAND_INVALID;
  bs_topic_set_clear(&request->topics);

  if (payload == NULL || *payload == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "empty IPC payload");
    return false;
  }

  if (!bs_json_extract_op(payload, op, sizeof(op))) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing op field");
    return false;
  }

  if (!bs_command_from_string(op, &request->command)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "unknown command: %s", op);
    return false;
  }

  if (request->command == BS_COMMAND_SUBSCRIBE) {
    for (int topic = 0; topic < BS_TOPIC_COUNT; topic++) {
      const char *topic_name = bs_topic_to_string((BsTopic) topic);
      if (strstr(payload, topic_name) != NULL) {
        bs_topic_set_add(&request->topics, (BsTopic) topic);
      }
    }
  }

  return true;
}

bool
bs_command_router_handle_json(BsCommandRouter *router,
                              const char *payload,
                              char **response_json,
                              GError **error) {
  BsIpcRequest request = {0};

  g_return_val_if_fail(router != NULL, false);
  g_return_val_if_fail(response_json != NULL, false);

  if (!bs_command_router_parse_request(router, payload, &request, error)) {
    return false;
  }

  switch (request.command) {
    case BS_COMMAND_SNAPSHOT:
      *response_json = g_strdup("{\"ok\":true,\"kind\":\"snapshot\",\"todo\":\"serialize-store\"}");
      return true;
    case BS_COMMAND_SUBSCRIBE:
      *response_json = g_strdup("{\"ok\":true,\"kind\":\"subscribed\",\"todo\":\"track-topics\"}");
      return true;
    default:
      *response_json = g_strdup_printf("{\"ok\":true,\"kind\":\"ack\",\"command\":\"%s\",\"todo\":\"route-command\"}",
                                       bs_command_to_string(request.command));
      return true;
  }
}
