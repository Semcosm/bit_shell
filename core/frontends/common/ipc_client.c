#include "frontends/common/ipc_client.h"

#include <gio/gunixsocketaddress.h>

struct _BsFrontendIpcClient {
  GSocketClient *socket_client;
  GSocketConnection *connection;
  GDataInputStream *input;
  GOutputStream *output;
  GCancellable *read_cancellable;
  char *socket_path;
  char *last_error;
  guint reconnect_delay_ms;
  guint reconnect_source_id;
  bool ready;
  bool running;
  BsFrontendIpcStateFn on_connected;
  BsFrontendIpcStateFn on_disconnected;
  BsFrontendIpcLineFn on_line;
  gpointer user_data;
};

static char *bs_frontend_ipc_client_default_socket_path(void);
static void bs_frontend_ipc_client_set_last_error(BsFrontendIpcClient *client,
                                                  const char *message);
static void bs_frontend_ipc_client_close_connection(BsFrontendIpcClient *client,
                                                    bool notify_disconnected);
static void bs_frontend_ipc_client_begin_read(BsFrontendIpcClient *client);
static void bs_frontend_ipc_client_schedule_reconnect(BsFrontendIpcClient *client);
static bool bs_frontend_ipc_client_connect(BsFrontendIpcClient *client, GError **error);
static void bs_frontend_ipc_client_on_read_line(GObject *source_object,
                                                GAsyncResult *result,
                                                gpointer user_data);
static gboolean bs_frontend_ipc_client_reconnect_cb(gpointer user_data);

static char *
bs_frontend_ipc_client_default_socket_path(void) {
  const char *runtime_dir = g_get_user_runtime_dir();

  if (runtime_dir == NULL || *runtime_dir == '\0') {
    runtime_dir = g_get_tmp_dir();
  }

  return g_build_filename(runtime_dir, "bit_shell", "bit_shelld.sock", NULL);
}

static void
bs_frontend_ipc_client_set_last_error(BsFrontendIpcClient *client, const char *message) {
  g_return_if_fail(client != NULL);

  g_free(client->last_error);
  client->last_error = g_strdup(message);
}

static void
bs_frontend_ipc_client_close_connection(BsFrontendIpcClient *client,
                                        bool notify_disconnected) {
  bool was_ready = false;

  g_return_if_fail(client != NULL);

  was_ready = client->ready;
  client->ready = false;
  if (client->read_cancellable != NULL) {
    g_cancellable_cancel(client->read_cancellable);
    g_cancellable_reset(client->read_cancellable);
  }
  g_clear_object(&client->input);
  client->output = NULL;
  if (client->connection != NULL) {
    (void) g_io_stream_close(G_IO_STREAM(client->connection), NULL, NULL);
    g_clear_object(&client->connection);
  }

  if (notify_disconnected && was_ready && client->on_disconnected != NULL) {
    client->on_disconnected(client, client->user_data);
  }
}

static void
bs_frontend_ipc_client_begin_read(BsFrontendIpcClient *client) {
  g_return_if_fail(client != NULL);
  g_return_if_fail(client->input != NULL);

  g_data_input_stream_read_line_async(client->input,
                                      G_PRIORITY_DEFAULT,
                                      client->read_cancellable,
                                      bs_frontend_ipc_client_on_read_line,
                                      client);
}

static gboolean
bs_frontend_ipc_client_reconnect_cb(gpointer user_data) {
  BsFrontendIpcClient *client = user_data;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(client != NULL, G_SOURCE_REMOVE);

  client->reconnect_source_id = 0;
  if (!client->running) {
    return G_SOURCE_REMOVE;
  }

  if (!bs_frontend_ipc_client_connect(client, &error)) {
    bs_frontend_ipc_client_set_last_error(client,
                                          error != NULL ? error->message : "IPC reconnect failed");
    bs_frontend_ipc_client_schedule_reconnect(client);
  }

  return G_SOURCE_REMOVE;
}

static void
bs_frontend_ipc_client_schedule_reconnect(BsFrontendIpcClient *client) {
  g_return_if_fail(client != NULL);

  if (!client->running || client->reconnect_delay_ms == 0 || client->reconnect_source_id != 0) {
    return;
  }

  client->reconnect_source_id = g_timeout_add(client->reconnect_delay_ms,
                                              bs_frontend_ipc_client_reconnect_cb,
                                              client);
}

static bool
bs_frontend_ipc_client_connect(BsFrontendIpcClient *client, GError **error) {
  g_autoptr(GSocketAddress) address = NULL;

  g_return_val_if_fail(client != NULL, false);

  bs_frontend_ipc_client_close_connection(client, false);

  address = g_unix_socket_address_new(client->socket_path);
  client->connection = g_socket_client_connect(client->socket_client,
                                               G_SOCKET_CONNECTABLE(address),
                                               NULL,
                                               error);
  if (client->connection == NULL) {
    bs_frontend_ipc_client_set_last_error(client,
                                          (error != NULL && *error != NULL) ? (*error)->message : "IPC connect failed");
    return false;
  }

  client->output = g_io_stream_get_output_stream(G_IO_STREAM(client->connection));
  client->input = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(client->connection)));
  g_data_input_stream_set_newline_type(client->input, G_DATA_STREAM_NEWLINE_TYPE_LF);
  client->ready = true;
  bs_frontend_ipc_client_set_last_error(client, NULL);

  if (client->on_connected != NULL) {
    client->on_connected(client, client->user_data);
    if (!client->ready) {
      return false;
    }
  }

  bs_frontend_ipc_client_begin_read(client);
  return true;
}

static void
bs_frontend_ipc_client_on_read_line(GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data) {
  BsFrontendIpcClient *client = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *line = NULL;

  g_return_if_fail(client != NULL);

  line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source_object),
                                              result,
                                              NULL,
                                              &error);
  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    return;
  }

  if (line == NULL) {
    bs_frontend_ipc_client_set_last_error(client,
                                          error != NULL ? error->message : "IPC disconnected");
    bs_frontend_ipc_client_close_connection(client, true);
    bs_frontend_ipc_client_schedule_reconnect(client);
    return;
  }

  if (client->on_line != NULL) {
    client->on_line(client, line, client->user_data);
  }
  if (client->running && client->input != NULL && client->ready) {
    bs_frontend_ipc_client_begin_read(client);
  }
}

BsFrontendIpcClient *
bs_frontend_ipc_client_new(const BsFrontendIpcClientConfig *config) {
  BsFrontendIpcClient *client = g_new0(BsFrontendIpcClient, 1);

  client->socket_client = g_socket_client_new();
  client->read_cancellable = g_cancellable_new();
  client->reconnect_delay_ms = config != NULL ? config->reconnect_delay_ms : 0;
  client->on_connected = config != NULL ? config->on_connected : NULL;
  client->on_disconnected = config != NULL ? config->on_disconnected : NULL;
  client->on_line = config != NULL ? config->on_line : NULL;
  client->user_data = config != NULL ? config->user_data : NULL;
  client->socket_path = (config != NULL && config->socket_path != NULL && *config->socket_path != '\0')
                          ? g_strdup(config->socket_path)
                          : bs_frontend_ipc_client_default_socket_path();
  return client;
}

void
bs_frontend_ipc_client_free(BsFrontendIpcClient *client) {
  if (client == NULL) {
    return;
  }

  bs_frontend_ipc_client_stop(client);
  g_clear_object(&client->read_cancellable);
  g_clear_object(&client->socket_client);
  g_free(client->socket_path);
  g_free(client->last_error);
  g_free(client);
}

bool
bs_frontend_ipc_client_start(BsFrontendIpcClient *client, GError **error) {
  g_return_val_if_fail(client != NULL, false);

  client->running = true;
  if (client->reconnect_source_id != 0) {
    g_source_remove(client->reconnect_source_id);
    client->reconnect_source_id = 0;
  }

  if (!bs_frontend_ipc_client_connect(client, error)) {
    bs_frontend_ipc_client_schedule_reconnect(client);
    return false;
  }

  return true;
}

void
bs_frontend_ipc_client_stop(BsFrontendIpcClient *client) {
  g_return_if_fail(client != NULL);

  client->running = false;
  if (client->reconnect_source_id != 0) {
    g_source_remove(client->reconnect_source_id);
    client->reconnect_source_id = 0;
  }
  bs_frontend_ipc_client_close_connection(client, false);
}

void
bs_frontend_ipc_client_disconnect(BsFrontendIpcClient *client) {
  g_return_if_fail(client != NULL);

  bs_frontend_ipc_client_close_connection(client, true);
  bs_frontend_ipc_client_schedule_reconnect(client);
}

bool
bs_frontend_ipc_client_send_line(BsFrontendIpcClient *client,
                                 const char *json_line,
                                 GError **error) {
  gsize bytes_written = 0;
  const char newline = '\n';

  g_return_val_if_fail(client != NULL, false);
  g_return_val_if_fail(json_line != NULL, false);

  if (client->output == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "IPC socket is not connected");
    bs_frontend_ipc_client_set_last_error(client, "IPC socket is not connected");
    return false;
  }

  if (!g_output_stream_write_all(client->output,
                                 json_line,
                                 strlen(json_line),
                                 &bytes_written,
                                 NULL,
                                 error)) {
    bs_frontend_ipc_client_set_last_error(client,
                                          (error != NULL && *error != NULL) ? (*error)->message : "IPC write failed");
    return false;
  }
  if (!g_output_stream_write_all(client->output,
                                 &newline,
                                 1,
                                 &bytes_written,
                                 NULL,
                                 error)) {
    bs_frontend_ipc_client_set_last_error(client,
                                          (error != NULL && *error != NULL) ? (*error)->message : "IPC write failed");
    return false;
  }
  if (!g_output_stream_flush(client->output, NULL, error)) {
    bs_frontend_ipc_client_set_last_error(client,
                                          (error != NULL && *error != NULL) ? (*error)->message : "IPC flush failed");
    return false;
  }

  return true;
}

const char *
bs_frontend_ipc_client_socket_path(BsFrontendIpcClient *client) {
  g_return_val_if_fail(client != NULL, NULL);
  return client->socket_path;
}

const char *
bs_frontend_ipc_client_last_error(BsFrontendIpcClient *client) {
  g_return_val_if_fail(client != NULL, NULL);
  return client->last_error;
}

bool
bs_frontend_ipc_client_ready(BsFrontendIpcClient *client) {
  g_return_val_if_fail(client != NULL, false);
  return client->ready;
}
