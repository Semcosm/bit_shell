#include "frontends/bit_dock/dock_app.h"

#include <gio/gio.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <gtk4-layer-shell.h>

#define BS_DOCK_APP_ID "io.bit_shell.bit_dock"
#define BS_DOCK_RECONNECT_DELAY_MS 2000U
#define BS_DOCK_NAMESPACE "bit-dock"

typedef struct {
  char *app_key;
  char *desktop_id;
  char *name;
  char *icon_name;
  GPtrArray *window_ids;
  bool pinned;
  bool running;
  bool focused;
  int pinned_index;
  uint64_t last_focus_ts;
} BsDockItemView;

typedef struct {
  bool focused;
  char *app_key;
  char *desktop_id;
  GPtrArray *window_ids;
  bool pinned;
  bool running;
} BsDockItemActionData;

typedef struct {
  bool focused;
  uint64_t focus_ts;
} BsWindowView;

struct _BsDockApp {
  GtkApplication *gtk_app;
  GtkWindow *window;
  GtkWidget *root_box;
  GtkWidget *items_box;
  GtkWidget *status_label;
  GSocketClient *socket_client;
  GSocketConnection *connection;
  GDataInputStream *input;
  GOutputStream *output;
  GCancellable *read_cancellable;
  GPtrArray *items;
  GHashTable *windows_by_id;
  char *socket_path;
  guint reconnect_source_id;
  bool ipc_ready;
};

static void bs_dock_item_view_free(gpointer data);
static void bs_dock_item_action_data_free(gpointer data);
static void bs_window_view_free(gpointer data);
static void bs_dock_app_close_connection(BsDockApp *app);
static void bs_dock_app_set_status(BsDockApp *app, const char *message);
static void bs_dock_app_rebuild_ui(BsDockApp *app);
static void bs_dock_app_apply_dock_payload(BsDockApp *app, JsonObject *payload);
static void bs_dock_app_apply_windows_payload(BsDockApp *app, JsonObject *payload);
static GPtrArray *bs_dock_app_parse_dock_items(JsonObject *payload);
static GHashTable *bs_dock_app_parse_windows_payload(JsonObject *payload);
static gboolean bs_dock_app_reconnect_cb(gpointer user_data);
static void bs_dock_app_schedule_reconnect(BsDockApp *app);
static bool bs_dock_app_send_json(BsDockApp *app, const char *json, GError **error);
static bool bs_dock_app_focus_window_for_action(BsDockApp *app,
                                                const BsDockItemActionData *action,
                                                int direction,
                                                GError **error);
static void bs_dock_app_begin_read(BsDockApp *app);
static void bs_dock_app_on_read_line(GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data);
static bool bs_dock_app_connect_ipc(BsDockApp *app, GError **error);
static void bs_dock_app_ensure_window(BsDockApp *app);
static void bs_dock_app_apply_css(void);
static void bs_dock_app_on_activate(GtkApplication *gtk_app, gpointer user_data);
static void bs_dock_app_on_item_clicked(GtkButton *button, gpointer user_data);
static void bs_dock_app_on_item_secondary_pressed(GtkGestureClick *gesture,
                                                  gint n_press,
                                                  gdouble x,
                                                  gdouble y,
                                                  gpointer user_data);
static gboolean bs_dock_app_on_item_scrolled(GtkEventControllerScroll *controller,
                                             gdouble dx,
                                             gdouble dy,
                                             gpointer user_data);
static GPtrArray *bs_string_ptr_array_dup(GPtrArray *values);
static GPtrArray *bs_json_string_array_member(JsonObject *object, const char *member_name);
static int bs_dock_app_find_active_window_index(BsDockApp *app, GPtrArray *window_ids);

static char *
bs_dock_app_default_socket_path(void) {
  const char *runtime_dir = g_get_user_runtime_dir();

  if (runtime_dir == NULL || *runtime_dir == '\0') {
    runtime_dir = g_get_tmp_dir();
  }

  return g_build_filename(runtime_dir, "bit_shell", "bit_shelld.sock", NULL);
}

static void
bs_dock_item_view_free(gpointer data) {
  BsDockItemView *item = data;

  if (item == NULL) {
    return;
  }

  g_free(item->app_key);
  g_free(item->desktop_id);
  g_free(item->name);
  g_free(item->icon_name);
  g_clear_pointer(&item->window_ids, g_ptr_array_unref);
  g_free(item);
}

static void
bs_dock_item_action_data_free(gpointer data) {
  BsDockItemActionData *action = data;

  if (action == NULL) {
    return;
  }

  g_free(action->app_key);
  g_free(action->desktop_id);
  g_clear_pointer(&action->window_ids, g_ptr_array_unref);
  g_free(action);
}

static void
bs_window_view_free(gpointer data) {
  g_free(data);
}

BsDockApp *
bs_dock_app_new(void) {
  BsDockApp *app = g_new0(BsDockApp, 1);
  const char *socket_override = g_getenv("BIT_SHELL_SOCKET");

  app->gtk_app = gtk_application_new(BS_DOCK_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  app->socket_client = g_socket_client_new();
  app->read_cancellable = g_cancellable_new();
  app->items = g_ptr_array_new_with_free_func(bs_dock_item_view_free);
  app->windows_by_id = g_hash_table_new_full(g_str_hash,
                                             g_str_equal,
                                             g_free,
                                             bs_window_view_free);
  app->socket_path = (socket_override != NULL && *socket_override != '\0')
                       ? g_strdup(socket_override)
                       : bs_dock_app_default_socket_path();

  g_signal_connect(app->gtk_app, "activate", G_CALLBACK(bs_dock_app_on_activate), app);
  return app;
}

void
bs_dock_app_free(BsDockApp *app) {
  if (app == NULL) {
    return;
  }

  if (app->reconnect_source_id != 0) {
    g_source_remove(app->reconnect_source_id);
  }
  bs_dock_app_close_connection(app);
  g_clear_pointer(&app->items, g_ptr_array_unref);
  g_clear_pointer(&app->windows_by_id, g_hash_table_unref);
  g_clear_object(&app->read_cancellable);
  g_clear_object(&app->socket_client);
  g_clear_object(&app->gtk_app);
  g_free(app->socket_path);
  g_free(app);
}

int
bs_dock_app_run(BsDockApp *app, int argc, char **argv) {
  g_return_val_if_fail(app != NULL, 1);
  return g_application_run(G_APPLICATION(app->gtk_app), argc, argv);
}

static void
bs_dock_app_close_connection(BsDockApp *app) {
  g_return_if_fail(app != NULL);

  app->ipc_ready = false;
  if (app->read_cancellable != NULL) {
    g_cancellable_cancel(app->read_cancellable);
    g_cancellable_reset(app->read_cancellable);
  }
  g_clear_object(&app->input);
  app->output = NULL;
  if (app->connection != NULL) {
    (void) g_io_stream_close(G_IO_STREAM(app->connection), NULL, NULL);
    g_clear_object(&app->connection);
  }
}

static void
bs_dock_app_set_status(BsDockApp *app, const char *message) {
  g_return_if_fail(app != NULL);

  if (app->status_label != NULL) {
    gtk_label_set_text(GTK_LABEL(app->status_label), message != NULL ? message : "");
    gtk_widget_set_visible(app->status_label, message != NULL && *message != '\0');
  }
}

static void
bs_dock_app_clear_items_box(BsDockApp *app) {
  GtkWidget *child = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->items_box != NULL);

  child = gtk_widget_get_first_child(app->items_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(app->items_box), child);
    child = next;
  }
}

static void
bs_dock_app_on_item_clicked(GtkButton *button, gpointer user_data) {
  BsDockApp *app = user_data;
  BsDockItemActionData *action = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *escaped_app_key = NULL;
  g_autofree char *escaped_desktop_id = NULL;
  g_autofree char *request = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(button != NULL);

  action = g_object_get_data(G_OBJECT(button), "bs-dock-action");
  if (action == NULL || !app->ipc_ready) {
    return;
  }

  if (action->running) {
    if (action->focused
        && action->window_ids != NULL
        && action->window_ids->len > 1) {
      if (!bs_dock_app_focus_window_for_action(app, action, 1, &error)) {
        bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to focus next window");
      }
      return;
    }

    escaped_app_key = g_strescape(action->app_key != NULL ? action->app_key : "", NULL);
    request = g_strdup_printf("{\"op\":\"activate_app\",\"app_key\":\"%s\"}",
                              escaped_app_key != NULL ? escaped_app_key : "");
  } else if (action->desktop_id != NULL) {
    escaped_desktop_id = g_strescape(action->desktop_id, NULL);
    request = g_strdup_printf("{\"op\":\"launch_app\",\"desktop_id\":\"%s\"}",
                              escaped_desktop_id != NULL ? escaped_desktop_id : "");
  } else {
    bs_dock_app_set_status(app, "Missing desktop_id for launch");
    return;
  }

  if (!bs_dock_app_send_json(app, request, &error)) {
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to send command");
  }
}

static void
bs_dock_app_on_item_secondary_pressed(GtkGestureClick *gesture,
                                      gint n_press,
                                      gdouble x,
                                      gdouble y,
                                      gpointer user_data) {
  GtkWidget *button = user_data;
  BsDockItemActionData *action = NULL;
  BsDockApp *app = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *escaped_app_key = NULL;
  g_autofree char *request = NULL;

  (void) gesture;
  (void) n_press;
  (void) x;
  (void) y;

  g_return_if_fail(button != NULL);

  action = g_object_get_data(G_OBJECT(button), "bs-dock-action");
  app = g_object_get_data(G_OBJECT(button), "bs-dock-app");
  if (action == NULL || app == NULL || !app->ipc_ready) {
    return;
  }
  if (action->desktop_id == NULL || *action->desktop_id == '\0') {
    bs_dock_app_set_status(app, "Only desktop-backed items can be pinned");
    return;
  }

  escaped_app_key = g_strescape(action->desktop_id, NULL);
  request = g_strdup_printf("{\"op\":\"%s\",\"app_key\":\"%s\"}",
                            action->pinned ? "unpin_app" : "pin_app",
                            escaped_app_key != NULL ? escaped_app_key : "");
  if (!bs_dock_app_send_json(app, request, &error)) {
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to send pin command");
  }
}

static gboolean
bs_dock_app_on_item_scrolled(GtkEventControllerScroll *controller,
                             gdouble dx,
                             gdouble dy,
                             gpointer user_data) {
  GtkWidget *button = user_data;
  BsDockItemActionData *action = NULL;
  BsDockApp *app = NULL;
  g_autoptr(GError) error = NULL;
  int direction = 0;

  (void) controller;

  g_return_val_if_fail(button != NULL, GDK_EVENT_PROPAGATE);

  action = g_object_get_data(G_OBJECT(button), "bs-dock-action");
  app = g_object_get_data(G_OBJECT(button), "bs-dock-app");
  if (action == NULL || app == NULL || !app->ipc_ready) {
    return GDK_EVENT_PROPAGATE;
  }
  if (action->window_ids == NULL || action->window_ids->len < 2) {
    return GDK_EVENT_PROPAGATE;
  }

  if (dy > 0 || dx > 0) {
    direction = 1;
  } else if (dy < 0 || dx < 0) {
    direction = -1;
  } else {
    return GDK_EVENT_PROPAGATE;
  }

  if (!bs_dock_app_focus_window_for_action(app, action, direction, &error)) {
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to cycle app windows");
  }

  return GDK_EVENT_STOP;
}

static void
bs_dock_app_rebuild_ui(BsDockApp *app) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->items_box != NULL);

  bs_dock_app_clear_items_box(app);

  for (guint i = 0; i < app->items->len; i++) {
    const BsDockItemView *item = g_ptr_array_index(app->items, i);
    BsDockItemActionData *action = NULL;
    GtkWidget *slot = NULL;
    GtkWidget *button = NULL;
    GtkWidget *indicator = NULL;
    GtkGesture *secondary_click = NULL;

    slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(slot, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(slot, "dock-slot");

    button = gtk_button_new();
    gtk_widget_add_css_class(button, "dock-item");
    gtk_widget_set_size_request(button, 60, 60);
    gtk_widget_set_tooltip_text(button, item->name != NULL ? item->name : item->app_key);
    if (item->icon_name != NULL && *item->icon_name != '\0') {
      gtk_button_set_icon_name(GTK_BUTTON(button), item->icon_name);
    } else {
      gtk_button_set_label(GTK_BUTTON(button), item->name != NULL ? item->name : item->app_key);
    }
    if (item->running) {
      gtk_widget_add_css_class(button, "is-running");
    }
    if (item->focused) {
      gtk_widget_add_css_class(button, "is-focused");
    }
    if (item->pinned && !item->running) {
      gtk_widget_add_css_class(button, "is-pinned");
    }

    action = g_new0(BsDockItemActionData, 1);
    action->focused = item->focused;
    action->app_key = g_strdup(item->app_key);
    action->desktop_id = g_strdup(item->desktop_id);
    action->window_ids = bs_string_ptr_array_dup(item->window_ids);
    action->pinned = item->pinned;
    action->running = item->running;
    g_object_set_data_full(G_OBJECT(button),
                           "bs-dock-action",
                           action,
                           bs_dock_item_action_data_free);
    g_object_set_data(G_OBJECT(button), "bs-dock-app", app);
    g_signal_connect(button, "clicked", G_CALLBACK(bs_dock_app_on_item_clicked), app);

    secondary_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(secondary_click,
                     "pressed",
                     G_CALLBACK(bs_dock_app_on_item_secondary_pressed),
                     button);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(secondary_click));

    if (item->window_ids != NULL && item->window_ids->len > 1) {
      GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL
                                                                   | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
      g_signal_connect(scroll,
                       "scroll",
                       G_CALLBACK(bs_dock_app_on_item_scrolled),
                       button);
      gtk_widget_add_controller(button, scroll);
    }

    indicator = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(indicator, "dock-indicator");
    gtk_widget_set_halign(indicator, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(indicator, item->focused ? 22 : 14, 4);
    if (!item->running) {
      gtk_widget_add_css_class(indicator, "is-hidden");
    } else if (item->focused) {
      gtk_widget_add_css_class(indicator, "is-focused");
    }

    gtk_box_append(GTK_BOX(slot), button);
    gtk_box_append(GTK_BOX(slot), indicator);
    gtk_box_append(GTK_BOX(app->items_box), slot);
  }

  if (app->items->len == 0) {
    bs_dock_app_set_status(app, "Waiting for dock items");
  } else if (gtk_widget_get_visible(app->status_label)) {
    bs_dock_app_set_status(app, "");
  }
}

static const char *
bs_json_string_member(JsonObject *object, const char *member_name) {
  JsonNode *node = NULL;

  if (object == NULL || !json_object_has_member(object, member_name)) {
    return NULL;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || JSON_NODE_HOLDS_NULL(node) || !JSON_NODE_HOLDS_VALUE(node)) {
    return NULL;
  }

  return json_node_get_string(node);
}

static bool
bs_json_bool_member(JsonObject *object, const char *member_name, bool fallback_value) {
  JsonNode *node = NULL;

  if (object == NULL || !json_object_has_member(object, member_name)) {
    return fallback_value;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node)) {
    return fallback_value;
  }

  return json_node_get_boolean(node);
}

static gint64
bs_json_int_member(JsonObject *object, const char *member_name, gint64 fallback_value) {
  JsonNode *node = NULL;

  if (object == NULL || !json_object_has_member(object, member_name)) {
    return fallback_value;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node)) {
    return fallback_value;
  }

  return json_node_get_int(node);
}

static GPtrArray *
bs_string_ptr_array_dup(GPtrArray *values) {
  GPtrArray *copy = g_ptr_array_new_with_free_func(g_free);

  if (values == NULL) {
    return copy;
  }

  for (guint i = 0; i < values->len; i++) {
    const char *value = g_ptr_array_index(values, i);
    g_ptr_array_add(copy, g_strdup(value));
  }

  return copy;
}

static GPtrArray *
bs_json_string_array_member(JsonObject *object, const char *member_name) {
  GPtrArray *values = g_ptr_array_new_with_free_func(g_free);
  JsonArray *array = NULL;

  if (object == NULL || !json_object_has_member(object, member_name)) {
    return values;
  }

  array = json_object_get_array_member(object, member_name);
  if (array == NULL) {
    return values;
  }

  for (guint i = 0; i < json_array_get_length(array); i++) {
    const char *value = json_array_get_string_element(array, i);

    if (value == NULL || *value == '\0') {
      continue;
    }

    g_ptr_array_add(values, g_strdup(value));
  }

  return values;
}

static GPtrArray *
bs_dock_app_parse_dock_items(JsonObject *payload) {
  GPtrArray *items = NULL;
  JsonArray *items_array = NULL;

  items = g_ptr_array_new_with_free_func(bs_dock_item_view_free);
  if (payload == NULL || !json_object_has_member(payload, "items")) {
    return items;
  }

  items_array = json_object_get_array_member(payload, "items");
  if (items_array == NULL) {
    return items;
  }

  for (guint i = 0; i < json_array_get_length(items_array); i++) {
    JsonObject *item_object = json_array_get_object_element(items_array, i);
    BsDockItemView *item = NULL;

    if (item_object == NULL) {
      continue;
    }

    item = g_new0(BsDockItemView, 1);
    item->app_key = g_strdup(bs_json_string_member(item_object, "app_key"));
    item->desktop_id = g_strdup(bs_json_string_member(item_object, "desktop_id"));
    item->name = g_strdup(bs_json_string_member(item_object, "name"));
    item->icon_name = g_strdup(bs_json_string_member(item_object, "icon_name"));
    item->window_ids = bs_json_string_array_member(item_object, "window_ids");
    item->pinned = bs_json_bool_member(item_object, "pinned", false);
    item->running = bs_json_bool_member(item_object, "running", false);
    item->focused = bs_json_bool_member(item_object, "focused", false);
    item->pinned_index = (int) bs_json_int_member(item_object, "pinned_index", -1);
    item->last_focus_ts = (uint64_t) bs_json_int_member(item_object, "last_focus_ts", 0);

    if (item->app_key == NULL || *item->app_key == '\0') {
      bs_dock_item_view_free(item);
      continue;
    }

    g_ptr_array_add(items, item);
  }

  return items;
}

static void
bs_dock_app_apply_windows_payload(BsDockApp *app, JsonObject *payload) {
  g_autoptr(GHashTable) windows_by_id = NULL;

  g_return_if_fail(app != NULL);

  windows_by_id = bs_dock_app_parse_windows_payload(payload);
  g_clear_pointer(&app->windows_by_id, g_hash_table_unref);
  app->windows_by_id = g_steal_pointer(&windows_by_id);
}

static GHashTable *
bs_dock_app_parse_windows_payload(JsonObject *payload) {
  GHashTable *windows_by_id = g_hash_table_new_full(g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    bs_window_view_free);
  JsonArray *windows_array = NULL;

  if (payload == NULL || !json_object_has_member(payload, "windows")) {
    return windows_by_id;
  }

  windows_array = json_object_get_array_member(payload, "windows");
  if (windows_array == NULL) {
    return windows_by_id;
  }

  for (guint i = 0; i < json_array_get_length(windows_array); i++) {
    JsonObject *window_object = json_array_get_object_element(windows_array, i);
    const char *window_id = NULL;
    BsWindowView *window = NULL;

    if (window_object == NULL) {
      continue;
    }

    window_id = bs_json_string_member(window_object, "id");
    if (window_id == NULL || *window_id == '\0') {
      continue;
    }

    window = g_new0(BsWindowView, 1);
    window->focused = bs_json_bool_member(window_object, "focused", false);
    window->focus_ts = (uint64_t) bs_json_int_member(window_object, "focus_ts", 0);
    g_hash_table_replace(windows_by_id, g_strdup(window_id), window);
  }

  return windows_by_id;
}

static int
bs_dock_app_find_active_window_index(BsDockApp *app, GPtrArray *window_ids) {
  int best_index = -1;
  uint64_t best_focus_ts = 0;

  g_return_val_if_fail(app != NULL, -1);
  g_return_val_if_fail(window_ids != NULL, -1);

  for (guint i = 0; i < window_ids->len; i++) {
    const char *window_id = g_ptr_array_index(window_ids, i);
    const BsWindowView *window = NULL;

    if (window_id == NULL) {
      continue;
    }

    window = g_hash_table_lookup(app->windows_by_id, window_id);
    if (window == NULL) {
      continue;
    }
    if (window->focused) {
      return (int) i;
    }
    if (best_index < 0 || window->focus_ts > best_focus_ts) {
      best_index = (int) i;
      best_focus_ts = window->focus_ts;
    }
  }

  if (best_index >= 0) {
    return best_index;
  }
  return window_ids->len > 0 ? 0 : -1;
}

static bool
bs_dock_app_focus_window_for_action(BsDockApp *app,
                                    const BsDockItemActionData *action,
                                    int direction,
                                    GError **error) {
  int current_index = -1;
  int next_index = 0;
  const char *window_id = NULL;
  g_autofree char *escaped_window_id = NULL;
  g_autofree char *request = NULL;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(action != NULL, false);

  if (action->window_ids == NULL || action->window_ids->len == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No window is available for this dock item");
    return false;
  }

  current_index = bs_dock_app_find_active_window_index(app, action->window_ids);
  if (current_index < 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Failed to resolve the active window");
    return false;
  }

  next_index = (current_index + direction) % (int) action->window_ids->len;
  if (next_index < 0) {
    next_index += (int) action->window_ids->len;
  }

  window_id = g_ptr_array_index(action->window_ids, next_index);
  if (window_id == NULL || *window_id == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Resolved window_id is empty");
    return false;
  }

  escaped_window_id = g_strescape(window_id, NULL);
  request = g_strdup_printf("{\"op\":\"focus_window\",\"window_id\":\"%s\"}",
                            escaped_window_id != NULL ? escaped_window_id : "");
  return bs_dock_app_send_json(app, request, error);
}

static void
bs_dock_app_apply_dock_payload(BsDockApp *app, JsonObject *payload) {
  g_autoptr(GPtrArray) items = NULL;

  g_return_if_fail(app != NULL);

  items = bs_dock_app_parse_dock_items(payload);
  g_clear_pointer(&app->items, g_ptr_array_unref);
  app->items = g_steal_pointer(&items);
  bs_dock_app_rebuild_ui(app);
}

static gboolean
bs_dock_app_reconnect_cb(gpointer user_data) {
  BsDockApp *app = user_data;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(app != NULL, G_SOURCE_REMOVE);
  app->reconnect_source_id = 0;

  if (!bs_dock_app_connect_ipc(app, &error)) {
    bs_dock_app_set_status(app, error != NULL ? error->message : "Reconnect failed");
    bs_dock_app_schedule_reconnect(app);
  }

  return G_SOURCE_REMOVE;
}

static void
bs_dock_app_schedule_reconnect(BsDockApp *app) {
  g_return_if_fail(app != NULL);

  if (app->reconnect_source_id != 0) {
    return;
  }

  app->reconnect_source_id = g_timeout_add(BS_DOCK_RECONNECT_DELAY_MS,
                                           bs_dock_app_reconnect_cb,
                                           app);
}

static bool
bs_dock_app_send_json(BsDockApp *app, const char *json, GError **error) {
  gsize bytes_written = 0;
  const char newline = '\n';

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(json != NULL, false);

  if (app->output == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "IPC socket is not connected");
    return false;
  }

  if (!g_output_stream_write_all(app->output,
                                 json,
                                 strlen(json),
                                 &bytes_written,
                                 NULL,
                                 error)) {
    return false;
  }
  if (!g_output_stream_write_all(app->output,
                                 &newline,
                                 1,
                                 &bytes_written,
                                 NULL,
                                 error)) {
    return false;
  }

  return g_output_stream_flush(app->output, NULL, error);
}

static void
bs_dock_app_handle_message(BsDockApp *app, const char *line) {
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *root = NULL;
  JsonObject *root_object = NULL;
  const char *kind = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(line != NULL);

  parser = json_parser_new();
  if (!json_parser_load_from_data(parser, line, -1, NULL)) {
    bs_dock_app_set_status(app, "Failed to parse IPC message");
    return;
  }

  root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
    return;
  }

  root_object = json_node_get_object(root);
  kind = bs_json_string_member(root_object, "kind");
  if (g_strcmp0(kind, "snapshot") == 0) {
    JsonObject *state = json_object_get_object_member(root_object, "state");
    JsonObject *windows = state != NULL ? json_object_get_object_member(state, "windows") : NULL;
    JsonObject *dock = state != NULL ? json_object_get_object_member(state, "dock") : NULL;

    bs_dock_app_apply_windows_payload(app, windows);
    bs_dock_app_apply_dock_payload(app, dock);
    return;
  }

  if (g_strcmp0(kind, "event") == 0) {
    const char *topic = bs_json_string_member(root_object, "topic");
    JsonObject *payload = json_object_get_object_member(root_object, "payload");

    if (g_strcmp0(topic, "dock") == 0 && payload != NULL) {
      bs_dock_app_apply_dock_payload(app, payload);
    } else if (g_strcmp0(topic, "windows") == 0 && payload != NULL) {
      bs_dock_app_apply_windows_payload(app, payload);
    }
    return;
  }

  if (g_strcmp0(kind, "error") == 0) {
    const char *message = bs_json_string_member(root_object, "message");
    bs_dock_app_set_status(app, message != NULL ? message : "IPC request failed");
  }
}

static void
bs_dock_app_begin_read(BsDockApp *app) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->input != NULL);

  g_data_input_stream_read_line_async(app->input,
                                      G_PRIORITY_DEFAULT,
                                      app->read_cancellable,
                                      bs_dock_app_on_read_line,
                                      app);
}

static void
bs_dock_app_on_read_line(GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data) {
  BsDockApp *app = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *line = NULL;

  g_return_if_fail(app != NULL);

  line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source_object),
                                              result,
                                              NULL,
                                              &error);
  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    return;
  }

  if (line == NULL) {
    bs_dock_app_close_connection(app);
    bs_dock_app_set_status(app, error != NULL ? error->message : "Dock IPC disconnected");
    bs_dock_app_schedule_reconnect(app);
    return;
  }

  bs_dock_app_handle_message(app, line);
  if (app->input != NULL) {
    bs_dock_app_begin_read(app);
  }
}

static bool
bs_dock_app_connect_ipc(BsDockApp *app, GError **error) {
  g_autoptr(GSocketAddress) address = NULL;

  g_return_val_if_fail(app != NULL, false);

  bs_dock_app_close_connection(app);
  address = g_unix_socket_address_new(app->socket_path);
  app->connection = g_socket_client_connect(app->socket_client,
                                            G_SOCKET_CONNECTABLE(address),
                                            NULL,
                                            error);
  if (app->connection == NULL) {
    return false;
  }

  app->output = g_io_stream_get_output_stream(G_IO_STREAM(app->connection));
  app->input = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(app->connection)));
  g_data_input_stream_set_newline_type(app->input, G_DATA_STREAM_NEWLINE_TYPE_LF);

  if (!bs_dock_app_send_json(app, "{\"op\":\"snapshot\"}", error)) {
    bs_dock_app_close_connection(app);
    return false;
  }
  if (!bs_dock_app_send_json(app, "{\"op\":\"subscribe\",\"topics\":[\"dock\",\"windows\"]}", error)) {
    bs_dock_app_close_connection(app);
    return false;
  }

  app->ipc_ready = true;
  bs_dock_app_set_status(app, "");
  bs_dock_app_begin_read(app);
  return true;
}

static void
bs_dock_app_apply_css(void) {
  GtkCssProvider *provider = NULL;
  const char *css =
    ".dock-root {"
    "  padding: 14px 18px;"
    "  border-radius: 22px;"
    "  background: rgba(12, 18, 27, 0.88);"
    "  border: 1px solid rgba(255, 255, 255, 0.08);"
    "}"
    ".dock-item {"
    "  min-width: 60px;"
    "  min-height: 60px;"
    "  border-radius: 18px;"
    "  background: rgba(255, 255, 255, 0.06);"
    "}"
    ".dock-item.is-running {"
    "  background: rgba(78, 126, 255, 0.18);"
    "}"
    ".dock-item.is-focused {"
    "  background: rgba(110, 170, 255, 0.32);"
    "}"
    ".dock-item.is-pinned {"
    "  background: rgba(255, 255, 255, 0.10);"
    "}"
    ".dock-indicator {"
    "  min-height: 4px;"
    "  border-radius: 999px;"
    "  background: rgba(255, 255, 255, 0.42);"
    "}"
    ".dock-indicator.is-focused {"
    "  background: rgba(156, 212, 255, 0.92);"
    "}"
    ".dock-indicator.is-hidden {"
    "  opacity: 0.0;"
    "}"
    ".dock-status {"
    "  color: rgba(255, 255, 255, 0.72);"
    "}";

  provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
  gtk_css_provider_load_from_string(provider, css);
#else
  gtk_css_provider_load_from_data(provider, css, -1);
#endif
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void
bs_dock_app_configure_window(BsDockApp *app) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->window != NULL);

  gtk_window_set_title(app->window, "bit_dock");
  gtk_window_set_decorated(app->window, false);
  gtk_window_set_resizable(app->window, false);
  gtk_window_set_default_size(app->window, 1, 1);

  if (gtk_layer_is_supported()) {
    gtk_layer_init_for_window(app->window);
    gtk_layer_set_namespace(app->window, BS_DOCK_NAMESPACE);
    gtk_layer_set_layer(app->window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM, true);
    gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_margin(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM, 14);
    gtk_layer_set_exclusive_zone(app->window, 0);
  }
}

static void
bs_dock_app_ensure_window(BsDockApp *app) {
  g_return_if_fail(app != NULL);

  if (app->window != NULL) {
    return;
  }

  app->window = GTK_WINDOW(gtk_application_window_new(app->gtk_app));
  bs_dock_app_configure_window(app);

  app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(app->root_box, "dock-root");
  gtk_widget_set_margin_start(app->root_box, 10);
  gtk_widget_set_margin_end(app->root_box, 10);
  gtk_widget_set_margin_top(app->root_box, 8);
  gtk_widget_set_margin_bottom(app->root_box, 8);

  app->items_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_halign(app->items_box, GTK_ALIGN_CENTER);

  app->status_label = gtk_label_new("Connecting to bit_shelld...");
  gtk_widget_add_css_class(app->status_label, "dock-status");
  gtk_widget_set_halign(app->status_label, GTK_ALIGN_CENTER);

  gtk_box_append(GTK_BOX(app->root_box), app->items_box);
  gtk_box_append(GTK_BOX(app->root_box), app->status_label);
  gtk_window_set_child(app->window, app->root_box);
}

static void
bs_dock_app_on_activate(GtkApplication *gtk_app, gpointer user_data) {
  BsDockApp *app = user_data;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(gtk_app != NULL);

  bs_dock_app_apply_css();
  bs_dock_app_ensure_window(app);
  gtk_window_present(app->window);

  if (!app->ipc_ready && !bs_dock_app_connect_ipc(app, &error)) {
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to connect to bit_shelld");
    bs_dock_app_schedule_reconnect(app);
  }
}
