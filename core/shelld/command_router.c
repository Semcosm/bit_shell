#include "shelld/command_router.h"

#include <glib.h>
#include <string.h>

#include "model/snapshot.h"
#include "shelld/app.h"

struct _BsCommandRouter {
  BsShelldApp *app;
};

static bool
bs_json_extract_string_field(const char *payload,
                             const char *key,
                             char *buffer,
                             size_t buffer_len) {
  char needle[64] = {0};
  const char *pos = NULL;
  const char *first_quote = NULL;
  const char *second_quote = NULL;
  size_t value_len = 0;

  if (payload == NULL || key == NULL || buffer == NULL || buffer_len == 0) {
    return false;
  }

  g_snprintf(needle, sizeof(needle), "\"%s\"", key);
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

static bool
bs_json_extract_int_field(const char *payload,
                          const char *key,
                          int32_t *value_out) {
  char needle[64] = {0};
  const char *pos = NULL;
  char *end = NULL;
  long parsed = 0;

  if (payload == NULL || key == NULL || value_out == NULL) {
    return false;
  }

  g_snprintf(needle, sizeof(needle), "\"%s\"", key);
  pos = strstr(payload, needle);
  if (pos == NULL) {
    return false;
  }

  pos = strchr(pos + strlen(needle), ':');
  if (pos == NULL) {
    return false;
  }

  parsed = strtol(pos + 1, &end, 10);
  if (end == pos + 1) {
    return false;
  }

  *value_out = (int32_t) parsed;
  return true;
}

static bool
bs_json_extract_topics_field(const char *payload, BsTopicSet *topics_out) {
  char needle[] = "\"topics\"";
  const char *pos = NULL;
  const char *cursor = NULL;
  const char *end = NULL;

  if (payload == NULL || topics_out == NULL) {
    return false;
  }

  pos = strstr(payload, needle);
  if (pos == NULL) {
    return false;
  }

  cursor = strchr(pos + strlen(needle), '[');
  if (cursor == NULL) {
    return false;
  }

  end = strchr(cursor, ']');
  if (end == NULL) {
    return false;
  }

  bs_topic_set_clear(topics_out);
  cursor += 1;
  while (cursor < end) {
    const char *first_quote = strchr(cursor, '"');
    const char *second_quote = NULL;
    char topic_name[64] = {0};
    size_t topic_len = 0;
    BsTopic topic = BS_TOPIC_SHELL;

    if (first_quote == NULL || first_quote >= end) {
      break;
    }
    second_quote = strchr(first_quote + 1, '"');
    if (second_quote == NULL || second_quote > end) {
      break;
    }

    topic_len = (size_t) (second_quote - (first_quote + 1));
    if (topic_len >= sizeof(topic_name)) {
      return false;
    }

    memcpy(topic_name, first_quote + 1, topic_len);
    topic_name[topic_len] = '\0';
    if (!bs_topic_from_string(topic_name, &topic)) {
      return false;
    }

    bs_topic_set_add(topics_out, topic);
    cursor = second_quote + 1;
  }

  return bs_topic_set_count(topics_out) > 0;
}

static char *
bs_command_request_params_json(const BsCommandRequest *request) {
  GString *json = g_string_new("{");
  bool first = true;

  if (request->desktop_id != NULL) {
    g_string_append_printf(json, "\"desktop_id\":\"%s\"", request->desktop_id);
    first = false;
  }
  if (request->app_key != NULL) {
    g_string_append_printf(json,
                           "%s\"app_key\":\"%s\"",
                           first ? "" : ",",
                           request->app_key);
    first = false;
  }
  if (request->window_id != NULL) {
    g_string_append_printf(json,
                           "%s\"window_id\":\"%s\"",
                           first ? "" : ",",
                           request->window_id);
    first = false;
  }
  if (request->workspace_id != NULL) {
    g_string_append_printf(json,
                           "%s\"workspace_id\":\"%s\"",
                           first ? "" : ",",
                           request->workspace_id);
    first = false;
  }
  if (request->item_id != NULL) {
    g_string_append_printf(json,
                           "%s\"item_id\":\"%s\"",
                           first ? "" : ",",
                           request->item_id);
    first = false;
  }
  if (request->x != BS_IPC_COORD_UNSET) {
    g_string_append_printf(json,
                           "%s\"x\":%d",
                           first ? "" : ",",
                           request->x);
    first = false;
  }
  if (request->y != BS_IPC_COORD_UNSET) {
    g_string_append_printf(json,
                           "%s\"y\":%d",
                           first ? "" : ",",
                           request->y);
    first = false;
  }
  if (bs_topic_set_count(&request->topics) > 0) {
    g_autofree char *topics_json = bs_topic_set_to_json(&request->topics);
    g_string_append_printf(json,
                           "%s\"topics\":%s",
                           first ? "" : ",",
                           topics_json);
  }

  g_string_append(json, "}");
  return g_string_free(json, false);
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
                                BsCommandRequest *request,
                                GError **error) {
  char op[64] = {0};
  char value_buf[256] = {0};

  g_return_val_if_fail(router != NULL, false);
  g_return_val_if_fail(request != NULL, false);

  bs_command_request_init(request);

  if (payload == NULL || *payload == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "empty IPC payload");
    return false;
  }

  if (!bs_json_extract_string_field(payload, "op", op, sizeof(op))) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing op field");
    return false;
  }

  if (!bs_command_from_string(op, &request->command)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "unknown command: %s", op);
    return false;
  }

  switch (request->command) {
    case BS_COMMAND_SUBSCRIBE:
      if (!bs_json_extract_topics_field(payload, &request->topics)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing or empty topics field");
        return false;
      }
      break;
    case BS_COMMAND_LAUNCH_APP:
      if (!bs_json_extract_string_field(payload, "desktop_id", value_buf, sizeof(value_buf))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing desktop_id");
        return false;
      }
      request->desktop_id = g_strdup(value_buf);
      break;
    case BS_COMMAND_ACTIVATE_APP:
    case BS_COMMAND_PIN_APP:
    case BS_COMMAND_UNPIN_APP:
      if (!bs_json_extract_string_field(payload, "app_key", value_buf, sizeof(value_buf))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing app_key");
        return false;
      }
      request->app_key = g_strdup(value_buf);
      break;
    case BS_COMMAND_FOCUS_WINDOW:
      if (!bs_json_extract_string_field(payload, "window_id", value_buf, sizeof(value_buf))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing window_id");
        return false;
      }
      request->window_id = g_strdup(value_buf);
      break;
    case BS_COMMAND_SWITCH_WORKSPACE:
      if (!bs_json_extract_string_field(payload, "workspace_id", value_buf, sizeof(value_buf))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing workspace_id");
        return false;
      }
      request->workspace_id = g_strdup(value_buf);
      break;
    case BS_COMMAND_TRAY_ACTIVATE:
    case BS_COMMAND_TRAY_CONTEXT_MENU:
      if (!bs_json_extract_string_field(payload, "item_id", value_buf, sizeof(value_buf))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing item_id");
        return false;
      }
      request->item_id = g_strdup(value_buf);
      (void) bs_json_extract_int_field(payload, "x", &request->x);
      (void) bs_json_extract_int_field(payload, "y", &request->y);
      break;
    case BS_COMMAND_SNAPSHOT:
    case BS_COMMAND_TOGGLE_LAUNCHPAD:
      break;
    default:
      break;
  }

  return true;
}

bool
bs_command_router_handle_request(BsCommandRouter *router,
                                 const BsCommandRequest *request,
                                 char **response_json,
                                 GError **error) {
  BsStateStore *store = NULL;
  BsSnapshot *snapshot = NULL;

  g_return_val_if_fail(router != NULL, false);
  g_return_val_if_fail(request != NULL, false);
  g_return_val_if_fail(response_json != NULL, false);

  store = bs_shelld_app_state_store(router->app);
  snapshot = bs_state_store_snapshot(store);

  switch (request->command) {
    case BS_COMMAND_SNAPSHOT:
      *response_json = bs_snapshot_serialize_json(snapshot);
      return true;
    case BS_COMMAND_SUBSCRIBE: {
      g_autofree char *topics_json = bs_topic_set_to_json(&request->topics);
      g_autofree char *topic_versions_json = bs_snapshot_serialize_topic_versions_json(snapshot);
      *response_json = g_strdup_printf("{\"ok\":true,\"kind\":\"subscribed\",\"topics\":%s,\"topic_versions\":%s}",
                                       topics_json,
                                       topic_versions_json);
      return true;
    }
    default: {
      g_autofree char *params_json = bs_command_request_params_json(request);
      *response_json = g_strdup_printf("{\"ok\":true,\"kind\":\"ack\",\"command\":\"%s\",\"params\":%s,\"todo\":\"route-command\"}",
                                       bs_command_to_string(request->command),
                                       params_json);
      return true;
    }
  }

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to handle request");
  return false;
}

bool
bs_command_router_handle_json(BsCommandRouter *router,
                              const char *payload,
                              char **response_json,
                              GError **error) {
  BsCommandRequest request;
  bool ok = false;

  bs_command_request_init(&request);
  ok = bs_command_router_parse_request(router, payload, &request, error)
       && bs_command_router_handle_request(router, &request, response_json, error);
  bs_command_request_clear(&request);
  return ok;
}

char *
bs_command_router_build_event_json(BsCommandRouter *router, BsTopic topic) {
  BsStateStore *store = NULL;
  BsSnapshot *snapshot = NULL;
  g_autofree char *payload_json = NULL;

  g_return_val_if_fail(router != NULL, NULL);

  store = bs_shelld_app_state_store(router->app);
  snapshot = bs_state_store_snapshot(store);
  payload_json = bs_snapshot_serialize_topic_payload_json(snapshot, topic);

  return g_strdup_printf("{\"kind\":\"event\",\"topic\":\"%s\",\"version\":%" G_GUINT64_FORMAT ",\"generation\":%" G_GUINT64_FORMAT ",\"payload\":%s}",
                         bs_topic_to_string(topic),
                         bs_state_store_topic_generation(store, topic),
                         bs_state_store_generation(store),
                         payload_json);
}

char *
bs_command_router_build_error_json(BsCommand command,
                                   const char *code,
                                   const char *message) {
  g_autofree char *escaped_code = g_strescape(code != NULL ? code : "internal_error", NULL);
  g_autofree char *escaped_message = g_strescape(message != NULL ? message : "unknown error", NULL);

  return g_strdup_printf("{\"ok\":false,\"kind\":\"error\",\"command\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}",
                         bs_command_to_string(command),
                         escaped_code,
                         escaped_message);
}
