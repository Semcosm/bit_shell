#include "shelld/ipc_server.h"

#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "shelld/app.h"
#include "shelld/command_router.h"

typedef struct _BsIpcClient {
  struct _BsIpcServer *server;
  GSocketConnection *connection;
  GDataInputStream *input;
  GOutputStream *output;
  BsTopicSet subscriptions;
  bool closed;
} BsIpcClient;

struct _BsIpcServer {
  BsShelldApp *app;
  char *socket_path;
  bool running;
  GSocketService *socket_service;
  GPtrArray *clients;
};

static void bs_ipc_client_schedule_read(BsIpcClient *client);

static bool
bs_ipc_server_ensure_parent_dir(const char *path, GError **error) {
  g_autofree char *parent = NULL;

  if (path == NULL || *path == '\0') {
    return true;
  }

  parent = g_path_get_dirname(path);
  if (parent == NULL || g_strcmp0(parent, ".") == 0) {
    return true;
  }

  if (g_mkdir_with_parents(parent, 0700) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "failed to create parent directory for %s",
                path);
    return false;
  }

  return true;
}

static bool
bs_ipc_client_write_json(BsIpcClient *client, const char *json) {
  gsize bytes_written = 0;
  const char newline = '\n';

  g_return_val_if_fail(client != NULL, false);
  g_return_val_if_fail(json != NULL, false);

  if (client->closed) {
    return false;
  }

  if (!g_output_stream_write_all(client->output,
                                 json,
                                 strlen(json),
                                 &bytes_written,
                                 NULL,
                                 NULL)) {
    client->closed = true;
    return false;
  }

  if (!g_output_stream_write_all(client->output,
                                 &newline,
                                 1,
                                 &bytes_written,
                                 NULL,
                                 NULL)) {
    client->closed = true;
    return false;
  }

  (void) g_output_stream_flush(client->output, NULL, NULL);
  return true;
}

static void
bs_ipc_client_free(BsIpcClient *client) {
  if (client == NULL) {
    return;
  }

  client->closed = true;
  g_clear_object(&client->input);
  g_clear_object(&client->output);
  g_clear_object(&client->connection);
  g_free(client);
}

static void
bs_ipc_server_remove_client(BsIpcServer *server, BsIpcClient *client) {
  if (server == NULL || client == NULL || server->clients == NULL) {
    return;
  }

  for (guint i = 0; i < server->clients->len; i++) {
    if (g_ptr_array_index(server->clients, i) == client) {
      g_ptr_array_remove_index(server->clients, i);
      return;
    }
  }
}

static void
bs_ipc_client_on_read_line(GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data) {
  BsIpcClient *client = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *line = NULL;
  gsize length = 0;

  line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source_object),
                                              result,
                                              &length,
                                              &error);
  if (error != NULL || line == NULL) {
    bs_ipc_server_remove_client(client->server, client);
    return;
  }

  if (*line != '\0') {
    g_autofree char *response_json = NULL;
    BsCommandRequest request;
    bs_command_request_init(&request);

    if (!bs_command_router_parse_request(bs_shelld_app_command_router(client->server->app),
                                         line,
                                         &request,
                                         &error)) {
      response_json = bs_command_router_build_error_json(BS_COMMAND_INVALID,
                                                         "invalid_argument",
                                                         error != NULL ? error->message : "failed to parse request");
    } else {
      if (request.command == BS_COMMAND_SUBSCRIBE) {
        client->subscriptions = request.topics;
      }
      if (!bs_command_router_handle_request(bs_shelld_app_command_router(client->server->app),
                                            &request,
                                            &response_json,
                                            &error)) {
        response_json = bs_command_router_build_error_json(request.command,
                                                           "internal_error",
                                                           error != NULL ? error->message : "failed to handle request");
      }
    }

    if (response_json != NULL) {
      (void) bs_ipc_client_write_json(client, response_json);
    }
    bs_command_request_clear(&request);
  }

  if (!client->closed) {
    bs_ipc_client_schedule_read(client);
  } else {
    bs_ipc_server_remove_client(client->server, client);
  }
}

static void
bs_ipc_client_schedule_read(BsIpcClient *client) {
  g_return_if_fail(client != NULL);
  g_data_input_stream_read_line_async(client->input,
                                      G_PRIORITY_DEFAULT,
                                      NULL,
                                      bs_ipc_client_on_read_line,
                                      client);
}

static gboolean
bs_ipc_server_on_incoming(GSocketService *service,
                          GSocketConnection *connection,
                          GObject *source_object,
                          gpointer user_data) {
  BsIpcServer *server = user_data;
  BsIpcClient *client = g_new0(BsIpcClient, 1);

  (void) service;
  (void) source_object;

  client->server = server;
  client->connection = g_object_ref(connection);
  client->input = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(connection)));
  client->output = g_object_ref(g_io_stream_get_output_stream(G_IO_STREAM(connection)));
  bs_topic_set_clear(&client->subscriptions);

  g_ptr_array_add(server->clients, client);
  bs_ipc_client_schedule_read(client);
  return TRUE;
}

static void
bs_ipc_server_on_store_changed(BsStateStore *store, BsTopic topic, gpointer user_data) {
  BsIpcServer *server = user_data;
  g_autofree char *event_json = NULL;

  (void) store;
  if (server == NULL || server->clients == NULL || server->clients->len == 0) {
    return;
  }

  event_json = bs_command_router_build_event_json(bs_shelld_app_command_router(server->app), topic);
  if (event_json == NULL) {
    return;
  }

  for (guint i = 0; i < server->clients->len; ) {
    BsIpcClient *client = g_ptr_array_index(server->clients, i);

    if (!bs_topic_set_contains(&client->subscriptions, topic)) {
      i += 1;
      continue;
    }

    if (!bs_ipc_client_write_json(client, event_json)) {
      g_ptr_array_remove_index(server->clients, i);
      continue;
    }

    i += 1;
  }
}

BsIpcServer *
bs_ipc_server_new(BsShelldApp *app, const BsIpcServerConfig *config) {
  BsIpcServer *server = g_new0(BsIpcServer, 1);
  server->app = app;
  server->clients = g_ptr_array_new_with_free_func((GDestroyNotify) bs_ipc_client_free);
  if (config != NULL && config->socket_path != NULL) {
    server->socket_path = g_strdup(config->socket_path);
  }
  return server;
}

void
bs_ipc_server_free(BsIpcServer *server) {
  if (server == NULL) {
    return;
  }

  bs_ipc_server_stop(server);
  g_clear_pointer(&server->clients, g_ptr_array_unref);
  g_free(server->socket_path);
  g_free(server);
}

bool
bs_ipc_server_start(BsIpcServer *server, GError **error) {
  g_autoptr(GSocketAddress) address = NULL;

  g_return_val_if_fail(server != NULL, false);

  if (server->running) {
    return true;
  }
  if (server->socket_path == NULL || *server->socket_path == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing IPC socket path");
    return false;
  }
  if (!bs_ipc_server_ensure_parent_dir(server->socket_path, error)) {
    return false;
  }

  (void) g_unlink(server->socket_path);
  server->socket_service = g_socket_service_new();
  g_signal_connect(server->socket_service, "incoming", G_CALLBACK(bs_ipc_server_on_incoming), server);

  address = g_unix_socket_address_new(server->socket_path);
  if (!g_socket_listener_add_address(G_SOCKET_LISTENER(server->socket_service),
                                     address,
                                     G_SOCKET_TYPE_STREAM,
                                     G_SOCKET_PROTOCOL_DEFAULT,
                                     NULL,
                                     NULL,
                                     error)) {
    g_clear_object(&server->socket_service);
    return false;
  }

  bs_state_store_set_observer(bs_shelld_app_state_store(server->app),
                              bs_ipc_server_on_store_changed,
                              server);
  g_socket_service_start(server->socket_service);
  server->running = true;
  g_message("[bit_shelld] ipc server listening at %s", server->socket_path);
  return true;
}

void
bs_ipc_server_stop(BsIpcServer *server) {
  if (server == NULL || !server->running) {
    return;
  }

  bs_state_store_set_observer(bs_shelld_app_state_store(server->app), NULL, NULL);
  if (server->socket_service != NULL) {
    g_socket_service_stop(server->socket_service);
    g_clear_object(&server->socket_service);
  }
  if (server->clients != NULL) {
    g_ptr_array_set_size(server->clients, 0);
  }
  if (server->socket_path != NULL) {
    (void) g_unlink(server->socket_path);
  }
  server->running = false;
}
