#include "niri/niri_backend.h"

#include <gio/gio.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#define BS_NIRI_RECONNECT_DELAY_MS 2000U

static void
bs_niri_backend_free_output_ptr(gpointer data) {
  BsOutput *output = data;
  if (output == NULL) {
    return;
  }
  bs_output_clear(output);
  g_free(output);
}

static void
bs_niri_backend_free_workspace_ptr(gpointer data) {
  BsWorkspace *workspace = data;
  if (workspace == NULL) {
    return;
  }
  bs_workspace_clear(workspace);
  g_free(workspace);
}

static void
bs_niri_backend_free_window_ptr(gpointer data) {
  BsWindow *window = data;
  if (window == NULL) {
    return;
  }
  bs_window_clear(window);
  g_free(window);
}

struct _BsNiriBackend {
  BsStateStore *store;
  char *socket_path;
  bool auto_reconnect;
  bool running;
  GSocketClient *socket_client;
  GSocketConnection *event_connection;
  GDataInputStream *event_input;
  GOutputStream *event_output;
  GCancellable *read_cancellable;
  guint reconnect_source_id;
};

static const char *bs_niri_backend_resolve_socket_path(BsNiriBackend *backend);
static void bs_niri_backend_close_event_connection(BsNiriBackend *backend);
static void bs_niri_backend_schedule_reconnect(BsNiriBackend *backend);
static gboolean bs_niri_backend_reconnect_cb(gpointer user_data);
static bool bs_niri_backend_connect_stream(BsNiriBackend *backend, GError **error);
static bool bs_niri_backend_refresh_outputs(BsNiriBackend *backend, GError **error);
static GSocketConnection *bs_niri_backend_open_connection(BsNiriBackend *backend, GError **error);
static bool bs_niri_backend_write_request(GOutputStream *output, const char *request, GError **error);
static char *bs_niri_backend_read_reply_line(GInputStream *input, GError **error);
static bool bs_niri_backend_request(BsNiriBackend *backend,
                                    const char *request,
                                    JsonNode **root_out,
                                    GError **error);
static JsonNode *bs_niri_backend_parse_json_line(const char *line, GError **error);
static JsonObject *bs_niri_backend_reply_ok_object(JsonNode *root, const char *member_name, GError **error);
static const char *bs_json_object_get_string_member_or(JsonObject *object,
                                                       const char *member_name,
                                                       const char *fallback_member_name);
static bool bs_json_object_get_bool_member_or(JsonObject *object,
                                              const char *member_name,
                                              const char *fallback_member_name,
                                              bool fallback_value);
static gint64 bs_json_object_get_int_member_default(JsonObject *object,
                                                    const char *member_name,
                                                    gint64 fallback_value);
static JsonObject *bs_json_object_get_nested_object(JsonObject *object, const char *member_name);
static bool bs_niri_backend_parse_output(JsonObject *object, BsOutput *output_out);
static bool bs_niri_backend_parse_workspace(JsonObject *object, BsWorkspace *workspace_out);
static bool bs_niri_backend_parse_window(BsNiriBackend *backend, JsonObject *object, BsWindow *window_out);
static char *bs_niri_backend_dup_id_string(JsonObject *object, const char *member_name);
static bool bs_niri_backend_parse_timestamp_ns(JsonObject *object,
                                               const char *member_name,
                                               bool *has_value_out,
                                               uint64_t *value_out);
static void bs_niri_backend_begin_read(BsNiriBackend *backend);
static void bs_niri_backend_on_read_line(GObject *source_object,
                                         GAsyncResult *result,
                                         gpointer user_data);
static bool bs_niri_backend_apply_event(BsNiriBackend *backend, const char *line, GError **error);

static const char *
bs_niri_backend_resolve_socket_path(BsNiriBackend *backend) {
  const char *env_path = NULL;

  g_return_val_if_fail(backend != NULL, NULL);

  if (backend->socket_path != NULL && *backend->socket_path != '\0') {
    return backend->socket_path;
  }

  env_path = g_getenv("NIRI_SOCKET");
  return env_path != NULL && *env_path != '\0' ? env_path : NULL;
}

BsNiriBackend *
bs_niri_backend_new(BsStateStore *store, const BsNiriBackendConfig *config) {
  BsNiriBackend *backend = g_new0(BsNiriBackend, 1);

  backend->store = store;
  if (config != NULL && config->socket_path != NULL) {
    backend->socket_path = g_strdup(config->socket_path);
  }
  backend->auto_reconnect = config != NULL ? config->auto_reconnect : true;
  backend->socket_client = g_socket_client_new();
  backend->read_cancellable = g_cancellable_new();
  return backend;
}

void
bs_niri_backend_free(BsNiriBackend *backend) {
  if (backend == NULL) {
    return;
  }

  bs_niri_backend_stop(backend);
  g_clear_object(&backend->read_cancellable);
  g_clear_object(&backend->socket_client);
  g_free(backend->socket_path);
  g_free(backend);
}

static void
bs_niri_backend_close_event_connection(BsNiriBackend *backend) {
  g_return_if_fail(backend != NULL);

  if (backend->read_cancellable != NULL) {
    g_cancellable_cancel(backend->read_cancellable);
    g_cancellable_reset(backend->read_cancellable);
  }
  g_clear_object(&backend->event_input);
  backend->event_output = NULL;
  if (backend->event_connection != NULL) {
    g_io_stream_close(G_IO_STREAM(backend->event_connection), NULL, NULL);
    g_clear_object(&backend->event_connection);
  }
}

static void
bs_niri_backend_schedule_reconnect(BsNiriBackend *backend) {
  g_return_if_fail(backend != NULL);

  if (!backend->running || !backend->auto_reconnect) {
    return;
  }
  if (backend->reconnect_source_id != 0) {
    return;
  }

  backend->reconnect_source_id = g_timeout_add(BS_NIRI_RECONNECT_DELAY_MS,
                                               bs_niri_backend_reconnect_cb,
                                               backend);
}

static gboolean
bs_niri_backend_reconnect_cb(gpointer user_data) {
  BsNiriBackend *backend = user_data;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(backend != NULL, G_SOURCE_REMOVE);
  backend->reconnect_source_id = 0;

  if (!backend->running) {
    return G_SOURCE_REMOVE;
  }

  if (!bs_niri_backend_connect_stream(backend, &error)) {
    g_warning("[bit_shelld] niri reconnect failed: %s",
              error != NULL ? error->message : "unknown error");
    bs_state_store_set_shell_connection_state(backend->store,
                                              false,
                                              error != NULL ? error->message : "niri reconnect failed");
    bs_niri_backend_schedule_reconnect(backend);
  }

  return G_SOURCE_REMOVE;
}

static GSocketConnection *
bs_niri_backend_open_connection(BsNiriBackend *backend, GError **error) {
  g_autoptr(GSocketAddress) address = NULL;
  const char *socket_path = NULL;

  g_return_val_if_fail(backend != NULL, NULL);

  socket_path = bs_niri_backend_resolve_socket_path(backend);
  if (socket_path == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "missing NIRI_SOCKET");
    return NULL;
  }

  address = g_unix_socket_address_new(socket_path);
  return g_socket_client_connect(backend->socket_client,
                                 G_SOCKET_CONNECTABLE(address),
                                 NULL,
                                 error);
}

static bool
bs_niri_backend_write_request(GOutputStream *output, const char *request, GError **error) {
  g_autofree char *line = NULL;
  gsize bytes_written = 0;

  g_return_val_if_fail(output != NULL, false);
  g_return_val_if_fail(request != NULL, false);

  line = g_strdup_printf("%s\n", request);
  if (!g_output_stream_write_all(output,
                                 line,
                                 strlen(line),
                                 &bytes_written,
                                 NULL,
                                 error)) {
    return false;
  }

  return g_output_stream_flush(output, NULL, error);
}

static char *
bs_niri_backend_read_reply_line(GInputStream *input, GError **error) {
  g_autoptr(GDataInputStream) data_input = NULL;

  g_return_val_if_fail(input != NULL, NULL);

  data_input = g_data_input_stream_new(input);
  return g_data_input_stream_read_line(data_input, NULL, NULL, error);
}

static JsonNode *
bs_niri_backend_parse_json_line(const char *line, GError **error) {
  JsonParser *parser = NULL;
  JsonNode *copy = NULL;

  g_return_val_if_fail(line != NULL, NULL);

  parser = json_parser_new();
  if (!json_parser_load_from_data(parser, line, -1, error)) {
    g_object_unref(parser);
    return NULL;
  }

  copy = json_node_copy(json_parser_get_root(parser));
  g_object_unref(parser);
  return copy;
}

static bool
bs_niri_backend_request(BsNiriBackend *backend,
                        const char *request,
                        JsonNode **root_out,
                        GError **error) {
  g_autoptr(GSocketConnection) connection = NULL;
  g_autofree char *reply_line = NULL;
  JsonNode *root = NULL;
  JsonObject *root_object = NULL;
  JsonNode *err_node = NULL;

  g_return_val_if_fail(backend != NULL, false);
  g_return_val_if_fail(request != NULL, false);

  if (root_out != NULL) {
    *root_out = NULL;
  }

  connection = bs_niri_backend_open_connection(backend, error);
  if (connection == NULL) {
    return false;
  }

  if (!bs_niri_backend_write_request(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                     request,
                                     error)) {
    return false;
  }

  reply_line = bs_niri_backend_read_reply_line(g_io_stream_get_input_stream(G_IO_STREAM(connection)),
                                               error);
  if (reply_line == NULL) {
    return false;
  }

  root = bs_niri_backend_parse_json_line(reply_line, error);
  if (root == NULL) {
    return false;
  }
  if (!JSON_NODE_HOLDS_OBJECT(root)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "niri reply root is not an object");
    json_node_unref(root);
    return false;
  }

  root_object = json_node_get_object(root);
  if (json_object_has_member(root_object, "Err")) {
    err_node = json_object_get_member(root_object, "Err");
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "niri error: %s",
                err_node != NULL && JSON_NODE_HOLDS_VALUE(err_node)
                  ? json_node_get_string(err_node)
                  : "request failed");
    json_node_unref(root);
    return false;
  }

  if (root_out != NULL) {
    *root_out = root;
  } else {
    json_node_unref(root);
  }

  return true;
}

static JsonObject *
bs_niri_backend_reply_ok_object(JsonNode *root, const char *member_name, GError **error) {
  JsonObject *root_object = NULL;
  JsonNode *ok_node = NULL;
  JsonObject *ok_object = NULL;

  g_return_val_if_fail(root != NULL, NULL);

  if (!JSON_NODE_HOLDS_OBJECT(root)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "niri reply root is not an object");
    return NULL;
  }

  root_object = json_node_get_object(root);
  if (json_object_has_member(root_object, "Err")) {
    JsonNode *err_node = json_object_get_member(root_object, "Err");
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "niri error: %s",
                JSON_NODE_HOLDS_VALUE(err_node) ? json_node_get_string(err_node) : "request failed");
    return NULL;
  }

  if (!json_object_has_member(root_object, "Ok")) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "niri reply missing Ok member");
    return NULL;
  }

  ok_node = json_object_get_member(root_object, "Ok");
  if (member_name == NULL) {
    return JSON_NODE_HOLDS_OBJECT(ok_node) ? json_node_get_object(ok_node) : root_object;
  }

  if (!JSON_NODE_HOLDS_OBJECT(ok_node)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "niri reply Ok payload is not an object");
    return NULL;
  }

  ok_object = json_node_get_object(ok_node);
  if (!json_object_has_member(ok_object, member_name)) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "niri reply missing Ok.%s",
                member_name);
    return NULL;
  }

  if (!JSON_NODE_HOLDS_OBJECT(json_object_get_member(ok_object, member_name))
      && !JSON_NODE_HOLDS_ARRAY(json_object_get_member(ok_object, member_name))) {
    return ok_object;
  }

  return ok_object;
}

static const char *
bs_json_object_get_string_member_or(JsonObject *object,
                                    const char *member_name,
                                    const char *fallback_member_name) {
  JsonNode *node = NULL;

  if (object == NULL) {
    return NULL;
  }
  if (member_name != NULL && json_object_has_member(object, member_name)) {
    node = json_object_get_member(object, member_name);
  } else if (fallback_member_name != NULL && json_object_has_member(object, fallback_member_name)) {
    node = json_object_get_member(object, fallback_member_name);
  }

  if (node == NULL || JSON_NODE_HOLDS_NULL(node) || !JSON_NODE_HOLDS_VALUE(node)) {
    return NULL;
  }
  return json_node_get_string(node);
}

static bool
bs_json_object_get_bool_member_or(JsonObject *object,
                                  const char *member_name,
                                  const char *fallback_member_name,
                                  bool fallback_value) {
  JsonNode *node = NULL;

  if (object == NULL) {
    return fallback_value;
  }
  if (member_name != NULL && json_object_has_member(object, member_name)) {
    node = json_object_get_member(object, member_name);
  } else if (fallback_member_name != NULL && json_object_has_member(object, fallback_member_name)) {
    node = json_object_get_member(object, fallback_member_name);
  }

  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node)) {
    return fallback_value;
  }
  return json_node_get_boolean(node);
}

static gint64
bs_json_object_get_int_member_default(JsonObject *object,
                                      const char *member_name,
                                      gint64 fallback_value) {
  JsonNode *node = NULL;

  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return fallback_value;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node)) {
    return fallback_value;
  }
  return json_node_get_int(node);
}

static JsonObject *
bs_json_object_get_nested_object(JsonObject *object, const char *member_name) {
  JsonNode *node = NULL;

  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return NULL;
  }

  node = json_object_get_member(object, member_name);
  return node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
}

static char *
bs_niri_backend_dup_id_string(JsonObject *object, const char *member_name) {
  JsonNode *node = NULL;
  guint64 id = 0;

  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return NULL;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || JSON_NODE_HOLDS_NULL(node)) {
    return NULL;
  }

  if (JSON_NODE_HOLDS_VALUE(node)) {
    if (json_node_get_value_type(node) == G_TYPE_STRING) {
      return g_strdup(json_node_get_string(node));
    }
    id = (guint64) json_node_get_int(node);
    return g_strdup_printf("%" G_GUINT64_FORMAT, id);
  }

  return NULL;
}

static bool
bs_niri_backend_parse_timestamp_ns(JsonObject *object,
                                   const char *member_name,
                                   bool *has_value_out,
                                   uint64_t *value_out) {
  JsonNode *node = NULL;
  JsonObject *ts = NULL;
  guint64 secs = 0;
  guint64 nanos = 0;

  g_return_val_if_fail(has_value_out != NULL, false);
  g_return_val_if_fail(value_out != NULL, false);

  *has_value_out = false;
  *value_out = 0;

  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return true;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || JSON_NODE_HOLDS_NULL(node)) {
    return true;
  }
  if (!JSON_NODE_HOLDS_OBJECT(node)) {
    return false;
  }

  ts = json_node_get_object(node);
  secs = (guint64) bs_json_object_get_int_member_default(ts, "secs", 0);
  nanos = (guint64) bs_json_object_get_int_member_default(ts, "nanos", 0);
  *has_value_out = true;
  *value_out = secs * G_GUINT64_CONSTANT(1000000000) + nanos;
  return true;
}

static bool
bs_niri_backend_parse_output(JsonObject *object, BsOutput *output_out) {
  JsonObject *logical = NULL;

  g_return_val_if_fail(object != NULL, false);
  g_return_val_if_fail(output_out != NULL, false);

  memset(output_out, 0, sizeof(*output_out));
  output_out->name = g_strdup(bs_json_object_get_string_member_or(object, "name", NULL));
  logical = bs_json_object_get_nested_object(object, "logical");
  output_out->width = (int) bs_json_object_get_int_member_default(logical, "width", 0);
  output_out->height = (int) bs_json_object_get_int_member_default(logical, "height", 0);
  if (logical != NULL && json_object_has_member(logical, "scale")) {
    output_out->scale = json_object_get_double_member(logical, "scale");
  } else {
    output_out->scale = 1.0;
  }
  return output_out->name != NULL;
}

static bool
bs_niri_backend_parse_workspace(JsonObject *object, BsWorkspace *workspace_out) {
  g_return_val_if_fail(object != NULL, false);
  g_return_val_if_fail(workspace_out != NULL, false);

  memset(workspace_out, 0, sizeof(*workspace_out));
  workspace_out->id = bs_niri_backend_dup_id_string(object, "id");
  workspace_out->name = g_strdup(bs_json_object_get_string_member_or(object, "name", NULL));
  workspace_out->output_name = g_strdup(bs_json_object_get_string_member_or(object, "output", "output_name"));
  workspace_out->focused = bs_json_object_get_bool_member_or(object, "is_focused", "focused", false);
  workspace_out->empty = true;
  workspace_out->local_index = (int) bs_json_object_get_int_member_default(object, "idx", 0);
  return workspace_out->id != NULL;
}

static bool
bs_niri_backend_parse_window(BsNiriBackend *backend, JsonObject *object, BsWindow *window_out) {
  BsSnapshot *snapshot = NULL;
  BsWorkspace *workspace = NULL;
  bool has_focus_ts = false;
  uint64_t focus_ts = 0;

  g_return_val_if_fail(backend != NULL, false);
  g_return_val_if_fail(object != NULL, false);
  g_return_val_if_fail(window_out != NULL, false);

  memset(window_out, 0, sizeof(*window_out));
  window_out->id = bs_niri_backend_dup_id_string(object, "id");
  window_out->title = g_strdup(bs_json_object_get_string_member_or(object, "title", NULL));
  window_out->app_id = g_strdup(bs_json_object_get_string_member_or(object, "app_id", NULL));
  window_out->workspace_id = bs_niri_backend_dup_id_string(object, "workspace_id");
  window_out->focused = bs_json_object_get_bool_member_or(object, "is_focused", "focused", false);
  window_out->floating = bs_json_object_get_bool_member_or(object, "is_floating", "floating", false);
  window_out->fullscreen = bs_json_object_get_bool_member_or(object, "is_fullscreen", "fullscreen", false)
                           || bs_json_object_get_bool_member_or(object, "is_fullscreened", NULL, false);
  window_out->output_name = g_strdup(bs_json_object_get_string_member_or(object, "output", "output_name"));
  if (!bs_niri_backend_parse_timestamp_ns(object, "focus_timestamp", &has_focus_ts, &focus_ts)) {
    return false;
  }
  if (has_focus_ts) {
    window_out->focus_ts = focus_ts;
  }

  if (window_out->output_name == NULL && window_out->workspace_id != NULL) {
    snapshot = bs_state_store_snapshot(backend->store);
    workspace = snapshot != NULL ? g_hash_table_lookup(snapshot->workspaces, window_out->workspace_id) : NULL;
    if (workspace != NULL) {
      window_out->output_name = g_strdup(workspace->output_name);
    }
  }

  return window_out->id != NULL;
}

static bool
bs_niri_backend_refresh_outputs(BsNiriBackend *backend, GError **error) {
  JsonNode *root = NULL;
  JsonObject *ok_object = NULL;
  JsonArray *outputs_array = NULL;
  g_autoptr(GPtrArray) outputs = NULL;

  g_return_val_if_fail(backend != NULL, false);

  if (!bs_niri_backend_request(backend, "\"Outputs\"", &root, error)) {
    return false;
  }

  ok_object = bs_niri_backend_reply_ok_object(root, "Outputs", error);
  if (ok_object == NULL) {
    return false;
  }

  if (!json_object_has_member(ok_object, "Outputs")) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "niri Outputs reply missing Outputs field");
    return false;
  }

  outputs_array = json_node_get_array(json_object_get_member(ok_object, "Outputs"));
  outputs = g_ptr_array_new_with_free_func(bs_niri_backend_free_output_ptr);
  for (guint i = 0; i < json_array_get_length(outputs_array); i++) {
    JsonObject *item = json_array_get_object_element(outputs_array, i);
    BsOutput *output = g_new0(BsOutput, 1);
    if (!bs_niri_backend_parse_output(item, output)) {
      bs_output_clear(output);
      g_free(output);
      continue;
    }
    g_ptr_array_add(outputs, output);
  }

  bs_state_store_begin_update(backend->store);
  bs_state_store_replace_outputs(backend->store, outputs);
  bs_state_store_finish_update(backend->store);
  if (root != NULL) {
    json_node_unref(root);
  }
  return true;
}

static bool
bs_niri_backend_connect_stream(BsNiriBackend *backend, GError **error) {
  g_autofree char *ack_line = NULL;

  g_return_val_if_fail(backend != NULL, false);

  bs_niri_backend_close_event_connection(backend);

  if (!bs_niri_backend_refresh_outputs(backend, error)) {
    g_warning("[bit_shelld] failed to refresh niri outputs before event-stream: %s",
              error != NULL && *error != NULL ? (*error)->message : "unknown error");
    if (error != NULL) {
      g_clear_error(error);
    }
  }

  backend->event_connection = bs_niri_backend_open_connection(backend, error);
  if (backend->event_connection == NULL) {
    return false;
  }

  backend->event_output = g_io_stream_get_output_stream(G_IO_STREAM(backend->event_connection));
  if (!bs_niri_backend_write_request(backend->event_output, "\"EventStream\"", error)) {
    bs_niri_backend_close_event_connection(backend);
    return false;
  }

  ack_line = bs_niri_backend_read_reply_line(g_io_stream_get_input_stream(G_IO_STREAM(backend->event_connection)),
                                             error);
  if (ack_line == NULL) {
    bs_niri_backend_close_event_connection(backend);
    return false;
  }

  if (g_strstr_len(ack_line, -1, "\"Err\"") != NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "niri event-stream request failed: %s", ack_line);
    bs_niri_backend_close_event_connection(backend);
    return false;
  }

  backend->event_input = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(backend->event_connection)));
  g_data_input_stream_set_newline_type(backend->event_input, G_DATA_STREAM_NEWLINE_TYPE_LF);

  bs_state_store_set_shell_connection_state(backend->store, true, NULL);
  bs_niri_backend_begin_read(backend);
  g_message("[bit_shelld] niri event-stream connected");
  return true;
}

static void
bs_niri_backend_begin_read(BsNiriBackend *backend) {
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->event_input != NULL);

  g_data_input_stream_read_line_async(backend->event_input,
                                      G_PRIORITY_DEFAULT,
                                      backend->read_cancellable,
                                      bs_niri_backend_on_read_line,
                                      backend);
}

static void
bs_niri_backend_on_read_line(GObject *source_object,
                             GAsyncResult *result,
                             gpointer user_data) {
  BsNiriBackend *backend = user_data;
  g_autofree char *line = NULL;
  g_autoptr(GError) error = NULL;

  (void) source_object;
  g_return_if_fail(backend != NULL);

  line = g_data_input_stream_read_line_finish(backend->event_input, result, NULL, &error);
  if (!backend->running) {
    return;
  }

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    return;
  }

  if (line == NULL) {
    const char *message = error != NULL ? error->message : "niri event-stream closed";
    g_warning("[bit_shelld] niri event-stream disconnected: %s", message);
    bs_niri_backend_close_event_connection(backend);
    bs_state_store_set_shell_connection_state(backend->store, false, message);
    bs_niri_backend_schedule_reconnect(backend);
    return;
  }

  if (!bs_niri_backend_apply_event(backend, line, &error)) {
    g_warning("[bit_shelld] ignored niri event: %s (%s)",
              line,
              error != NULL ? error->message : "parse failure");
  }

  if (backend->running && backend->event_input != NULL) {
    bs_niri_backend_begin_read(backend);
  }
}

static bool
bs_niri_backend_apply_event(BsNiriBackend *backend, const char *line, GError **error) {
  JsonNode *root = NULL;
  JsonObject *root_object = NULL;

  g_return_val_if_fail(backend != NULL, false);
  g_return_val_if_fail(line != NULL, false);

  root = bs_niri_backend_parse_json_line(line, error);
  if (root == NULL) {
    return false;
  }
  if (!JSON_NODE_HOLDS_OBJECT(root)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "niri event root is not an object");
    json_node_unref(root);
    return false;
  }

  root_object = json_node_get_object(root);

  if (json_object_has_member(root_object, "WorkspacesChanged")) {
    JsonObject *event = json_object_get_object_member(root_object, "WorkspacesChanged");
    JsonArray *array = json_object_get_array_member(event, "workspaces");
    g_autoptr(GPtrArray) workspaces = g_ptr_array_new_with_free_func(bs_niri_backend_free_workspace_ptr);

    for (guint i = 0; i < json_array_get_length(array); i++) {
      BsWorkspace *workspace = g_new0(BsWorkspace, 1);
      if (!bs_niri_backend_parse_workspace(json_array_get_object_element(array, i), workspace)) {
        bs_workspace_clear(workspace);
        g_free(workspace);
        continue;
      }
      g_ptr_array_add(workspaces, workspace);
    }

    bs_state_store_begin_update(backend->store);
    bs_state_store_replace_workspaces(backend->store, workspaces);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WorkspaceActivated")) {
    JsonObject *event = json_object_get_object_member(root_object, "WorkspaceActivated");
    g_autofree char *workspace_id = bs_niri_backend_dup_id_string(event, "id");
    bool focused = bs_json_object_get_bool_member_or(event, "focused", NULL, false);

    if (workspace_id == NULL) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "WorkspaceActivated missing id");
      return false;
    }

    bs_state_store_begin_update(backend->store);
    bs_state_store_set_workspace_activated(backend->store, workspace_id, focused);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WorkspaceActiveWindowChanged")) {
    JsonObject *event = json_object_get_object_member(root_object, "WorkspaceActiveWindowChanged");
    g_autofree char *workspace_id = bs_niri_backend_dup_id_string(event, "workspace_id");
    g_autofree char *window_id = bs_niri_backend_dup_id_string(event, "active_window_id");

    bs_state_store_begin_update(backend->store);
    bs_state_store_set_workspace_active_window(backend->store, workspace_id, window_id);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WindowsChanged")) {
    JsonObject *event = json_object_get_object_member(root_object, "WindowsChanged");
    JsonArray *array = json_object_get_array_member(event, "windows");
    g_autoptr(GPtrArray) windows = g_ptr_array_new_with_free_func(bs_niri_backend_free_window_ptr);

    for (guint i = 0; i < json_array_get_length(array); i++) {
      BsWindow *window = g_new0(BsWindow, 1);
      if (!bs_niri_backend_parse_window(backend, json_array_get_object_element(array, i), window)) {
        bs_window_clear(window);
        g_free(window);
        continue;
      }
      g_ptr_array_add(windows, window);
    }

    bs_state_store_begin_update(backend->store);
    bs_state_store_replace_windows(backend->store, windows);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WindowOpenedOrChanged")) {
    JsonObject *event = json_object_get_object_member(root_object, "WindowOpenedOrChanged");
    BsWindow window = {0};

    if (!bs_niri_backend_parse_window(backend, json_object_get_object_member(event, "window"), &window)) {
      bs_window_clear(&window);
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "WindowOpenedOrChanged payload invalid");
      json_node_unref(root);
      return false;
    }

    bs_state_store_begin_update(backend->store);
    bs_state_store_upsert_window(backend->store, &window);
    bs_state_store_finish_update(backend->store);
    bs_window_clear(&window);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WindowClosed")) {
    JsonObject *event = json_object_get_object_member(root_object, "WindowClosed");
    g_autofree char *window_id = bs_niri_backend_dup_id_string(event, "id");

    if (window_id == NULL) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "WindowClosed missing id");
      json_node_unref(root);
      return false;
    }

    bs_state_store_begin_update(backend->store);
    bs_state_store_remove_window(backend->store, window_id);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WindowFocusChanged")) {
    JsonObject *event = json_object_get_object_member(root_object, "WindowFocusChanged");
    g_autofree char *window_id = bs_niri_backend_dup_id_string(event, "id");

    bs_state_store_begin_update(backend->store);
    bs_state_store_set_window_focus(backend->store, window_id);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  if (json_object_has_member(root_object, "WindowFocusTimestampChanged")) {
    JsonObject *event = json_object_get_object_member(root_object, "WindowFocusTimestampChanged");
    g_autofree char *window_id = bs_niri_backend_dup_id_string(event, "id");
    bool has_value = false;
    uint64_t focus_ts = 0;

    if (window_id == NULL
        || !bs_niri_backend_parse_timestamp_ns(event, "focus_timestamp", &has_value, &focus_ts)) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "WindowFocusTimestampChanged payload invalid");
      json_node_unref(root);
      return false;
    }

    bs_state_store_begin_update(backend->store);
    bs_state_store_set_window_focus_timestamp(backend->store, window_id, has_value, focus_ts);
    bs_state_store_finish_update(backend->store);
    json_node_unref(root);
    return true;
  }

  json_node_unref(root);
  return true;
}

bool
bs_niri_backend_start(BsNiriBackend *backend, GError **error) {
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail(backend != NULL, false);

  if (backend->running) {
    return true;
  }

  backend->running = true;
  if (!bs_niri_backend_connect_stream(backend, &local_error)) {
    const char *message = local_error != NULL ? local_error->message : "niri connect failed";
    bs_state_store_set_shell_connection_state(backend->store, false, message);
    g_warning("[bit_shelld] niri backend start degraded: %s", message);
    bs_niri_backend_schedule_reconnect(backend);
    if (error != NULL && local_error != NULL) {
      g_propagate_error(error, g_steal_pointer(&local_error));
    }
  }

  return true;
}

void
bs_niri_backend_stop(BsNiriBackend *backend) {
  if (backend == NULL) {
    return;
  }

  backend->running = false;
  if (backend->reconnect_source_id != 0) {
    g_source_remove(backend->reconnect_source_id);
    backend->reconnect_source_id = 0;
  }
  bs_niri_backend_close_event_connection(backend);
}

bool
bs_niri_backend_request_initial_snapshot(BsNiriBackend *backend, GError **error) {
  g_return_val_if_fail(backend != NULL, false);
  (void) error;
  return true;
}

bool
bs_niri_backend_subscribe_event_stream(BsNiriBackend *backend, GError **error) {
  g_return_val_if_fail(backend != NULL, false);
  (void) error;
  return true;
}

bool
bs_niri_backend_focus_window(BsNiriBackend *backend,
                             const char *window_id,
                             GError **error) {
  JsonNode *root = NULL;
  guint64 parsed_id = 0;
  char *end = NULL;
  g_autofree char *request = NULL;

  g_return_val_if_fail(backend != NULL, false);
  g_return_val_if_fail(window_id != NULL, false);

  parsed_id = g_ascii_strtoull(window_id, &end, 10);
  if (end == window_id || (end != NULL && *end != '\0')) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "window_id is not a numeric niri window id: %s",
                window_id);
    return false;
  }

  request = g_strdup_printf("{\"Action\":{\"FocusWindow\":{\"id\":%" G_GUINT64_FORMAT "}}}",
                            parsed_id);
  if (!bs_niri_backend_request(backend, request, &root, error)) {
    return false;
  }

  if (root != NULL) {
    json_node_unref(root);
  }
  return true;
}

bool
bs_niri_backend_focus_workspace(BsNiriBackend *backend,
                                const char *workspace_id,
                                GError **error) {
  JsonNode *root = NULL;
  guint64 parsed_id = 0;
  char *end = NULL;
  g_autofree char *request = NULL;

  g_return_val_if_fail(backend != NULL, false);
  g_return_val_if_fail(workspace_id != NULL, false);

  parsed_id = g_ascii_strtoull(workspace_id, &end, 10);
  if (end == workspace_id || (end != NULL && *end != '\0')) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "workspace_id is not a numeric niri workspace id: %s",
                workspace_id);
    return false;
  }

  request = g_strdup_printf("{\"Action\":{\"FocusWorkspace\":{\"reference\":{\"Id\":%" G_GUINT64_FORMAT "}}}}",
                            parsed_id);
  if (!bs_niri_backend_request(backend, request, &root, error)) {
    return false;
  }

  if (root != NULL) {
    json_node_unref(root);
  }
  return true;
}
