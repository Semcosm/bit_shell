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
  GtkWidget *slot;
  GtkWidget *button;
  GtkWidget *indicator;
  GtkWidget *icon_image;
  GtkWidget *label;
  BsDockItemActionData *action;
} BsDockItemWidgets;

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
  GHashTable *item_widgets_by_app_key;
  char *socket_path;
  guint reconnect_source_id;
  bool ipc_ready;
};

static void bs_dock_item_view_free(gpointer data);
static void bs_dock_item_action_data_free(gpointer data);
static void bs_dock_item_widgets_free(gpointer data);
static void bs_dock_app_close_connection(BsDockApp *app);
static void bs_dock_app_set_status(BsDockApp *app, const char *message);
static void bs_dock_app_update_action_data(BsDockItemActionData *action, const BsDockItemView *item);
static BsDockItemWidgets *bs_dock_app_create_item_widgets(BsDockApp *app, const BsDockItemView *item);
static void bs_dock_app_update_item_widgets(BsDockApp *app,
                                            BsDockItemWidgets *widgets,
                                            const BsDockItemView *item);
static void bs_dock_app_sync_ui(BsDockApp *app);
static void bs_dock_app_apply_dock_payload(BsDockApp *app, JsonObject *payload);
static GPtrArray *bs_dock_app_parse_dock_items(JsonObject *payload);
static void bs_dock_app_log_dock_items(BsDockApp *app, const char *source);
static gboolean bs_dock_app_reconnect_cb(gpointer user_data);
static void bs_dock_app_schedule_reconnect(BsDockApp *app);
static bool bs_dock_app_send_json(BsDockApp *app, const char *json, GError **error);
static bool bs_dock_app_send_app_window_focus_request(BsDockApp *app,
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
static void bs_dock_app_on_item_primary_pressed(GtkGestureClick *gesture,
                                                gint n_press,
                                                gdouble x,
                                                gdouble y,
                                                gpointer user_data);
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

BsDockApp *
bs_dock_app_new(void) {
  BsDockApp *app = g_new0(BsDockApp, 1);
  const char *socket_override = g_getenv("BIT_SHELL_SOCKET");

  app->gtk_app = gtk_application_new(BS_DOCK_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  app->socket_client = g_socket_client_new();
  app->read_cancellable = g_cancellable_new();
  app->items = g_ptr_array_new_with_free_func(bs_dock_item_view_free);
  app->item_widgets_by_app_key = g_hash_table_new_full(g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       bs_dock_item_widgets_free);
  app->socket_path = (socket_override != NULL && *socket_override != '\0')
                       ? g_strdup(socket_override)
                       : bs_dock_app_default_socket_path();

  g_message("[bit_dock] initialized with IPC socket %s", app->socket_path);
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
  g_clear_pointer(&app->item_widgets_by_app_key, g_hash_table_unref);
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
    g_message("[bit_dock] closing IPC connection");
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
bs_dock_item_widgets_free(gpointer data) {
  BsDockItemWidgets *widgets = data;

  if (widgets == NULL) {
    return;
  }

  if (widgets->slot != NULL && gtk_widget_get_parent(widgets->slot) != NULL) {
    gtk_box_remove(GTK_BOX(gtk_widget_get_parent(widgets->slot)), widgets->slot);
  }
  bs_dock_item_action_data_free(widgets->action);
  g_free(widgets);
}

static void
bs_dock_app_update_action_data(BsDockItemActionData *action, const BsDockItemView *item) {
  g_return_if_fail(action != NULL);
  g_return_if_fail(item != NULL);

  action->focused = item->focused;
  g_free(action->app_key);
  action->app_key = g_strdup(item->app_key);
  g_free(action->desktop_id);
  action->desktop_id = g_strdup(item->desktop_id);
  g_clear_pointer(&action->window_ids, g_ptr_array_unref);
  action->window_ids = bs_string_ptr_array_dup(item->window_ids);
  action->pinned = item->pinned;
  action->running = item->running;
}

static void
bs_dock_app_send_primary_action(BsDockApp *app, const BsDockItemActionData *action) {
  g_autoptr(GError) error = NULL;
  g_autofree char *escaped_app_key = NULL;
  g_autofree char *escaped_desktop_id = NULL;
  g_autofree char *request = NULL;

  g_return_if_fail(app != NULL);
  if (action == NULL || !app->ipc_ready) {
    return;
  }

  if (action->running) {
    if (action->focused
        && action->window_ids != NULL
        && action->window_ids->len > 1) {
      g_message("[bit_dock] request focus_next_app_window for %s",
                action->app_key != NULL ? action->app_key : "(null)");
      if (!bs_dock_app_send_app_window_focus_request(app, action, 1, &error)) {
        g_warning("[bit_dock] focus_next_app_window failed: %s",
                  error != NULL ? error->message : "unknown error");
        bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to focus next window");
      }
      return;
    }

    escaped_app_key = g_strescape(action->app_key != NULL ? action->app_key : "", NULL);
    request = g_strdup_printf("{\"op\":\"activate_app\",\"app_key\":\"%s\"}",
                              escaped_app_key != NULL ? escaped_app_key : "");
    g_message("[bit_dock] request activate_app for %s",
              action->app_key != NULL ? action->app_key : "(null)");
  } else if (action->desktop_id != NULL) {
    escaped_desktop_id = g_strescape(action->desktop_id, NULL);
    request = g_strdup_printf("{\"op\":\"launch_app\",\"desktop_id\":\"%s\"}",
                              escaped_desktop_id != NULL ? escaped_desktop_id : "");
    g_message("[bit_dock] request launch_app for %s", action->desktop_id);
  } else {
    bs_dock_app_set_status(app, "Missing desktop_id for launch");
    return;
  }

  if (!bs_dock_app_send_json(app, request, &error)) {
    g_warning("[bit_dock] command send failed: %s",
              error != NULL ? error->message : "unknown error");
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to send command");
  }
}

static void
bs_dock_app_on_item_primary_pressed(GtkGestureClick *gesture,
                                    gint n_press,
                                    gdouble x,
                                    gdouble y,
                                    gpointer user_data) {
  GtkWidget *button = user_data;
  BsDockItemActionData *action = NULL;
  BsDockApp *app = NULL;

  (void) n_press;
  (void) x;
  (void) y;

  g_return_if_fail(GTK_IS_GESTURE_CLICK(gesture));
  g_return_if_fail(button != NULL);

  action = g_object_get_data(G_OBJECT(button), "bs-dock-action");
  app = g_object_get_data(G_OBJECT(button), "bs-dock-app");
  if (action == NULL || app == NULL) {
    return;
  }

  gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  bs_dock_app_send_primary_action(app, action);
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
  g_message("[bit_dock] request %s for %s",
            action->pinned ? "unpin_app" : "pin_app",
            action->desktop_id);
  if (!bs_dock_app_send_json(app, request, &error)) {
    g_warning("[bit_dock] pin command send failed: %s",
              error != NULL ? error->message : "unknown error");
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

  g_message("[bit_dock] request %s for %s",
            direction > 0 ? "focus_next_app_window" : "focus_prev_app_window",
            action->app_key != NULL ? action->app_key : "(null)");
  if (!bs_dock_app_send_app_window_focus_request(app, action, direction, &error)) {
    g_warning("[bit_dock] app window focus request failed: %s",
              error != NULL ? error->message : "unknown error");
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to cycle app windows");
  }

  return GDK_EVENT_STOP;
}

static BsDockItemWidgets *
bs_dock_app_create_item_widgets(BsDockApp *app, const BsDockItemView *item) {
  BsDockItemWidgets *widgets = NULL;
  GtkGesture *primary_click = NULL;
  GtkGesture *secondary_click = NULL;
  GtkEventController *scroll = NULL;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(item != NULL, NULL);

  widgets = g_new0(BsDockItemWidgets, 1);
  widgets->slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_halign(widgets->slot, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(widgets->slot, "dock-slot");

  widgets->button = gtk_button_new();
  gtk_widget_add_css_class(widgets->button, "dock-item");
  gtk_widget_set_size_request(widgets->button, 56, 56);

  widgets->icon_image = gtk_image_new();
  gtk_widget_add_css_class(widgets->icon_image, "dock-item-icon");
  gtk_image_set_pixel_size(GTK_IMAGE(widgets->icon_image), 56);

  widgets->label = gtk_label_new(NULL);
  gtk_widget_add_css_class(widgets->label, "dock-item-label");
  gtk_label_set_wrap(GTK_LABEL(widgets->label), true);
  gtk_label_set_wrap_mode(GTK_LABEL(widgets->label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_justify(GTK_LABEL(widgets->label), GTK_JUSTIFY_CENTER);
  gtk_label_set_max_width_chars(GTK_LABEL(widgets->label), 6);
  gtk_button_set_child(GTK_BUTTON(widgets->button), widgets->icon_image);

  widgets->indicator = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(widgets->indicator, "dock-indicator");
  gtk_widget_set_halign(widgets->indicator, GTK_ALIGN_CENTER);

  widgets->action = g_new0(BsDockItemActionData, 1);
  g_object_set_data(G_OBJECT(widgets->button), "bs-dock-app", app);
  g_object_set_data(G_OBJECT(widgets->button), "bs-dock-action", widgets->action);

  primary_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(primary_click), GDK_BUTTON_PRIMARY);
  g_signal_connect(primary_click,
                   "pressed",
                   G_CALLBACK(bs_dock_app_on_item_primary_pressed),
                   widgets->button);
  gtk_widget_add_controller(widgets->button, GTK_EVENT_CONTROLLER(primary_click));

  secondary_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary_click), GDK_BUTTON_SECONDARY);
  g_signal_connect(secondary_click,
                   "pressed",
                   G_CALLBACK(bs_dock_app_on_item_secondary_pressed),
                   widgets->button);
  gtk_widget_add_controller(widgets->button, GTK_EVENT_CONTROLLER(secondary_click));

  scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL
                                           | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  g_signal_connect(scroll,
                   "scroll",
                   G_CALLBACK(bs_dock_app_on_item_scrolled),
                   widgets->button);
  gtk_widget_add_controller(widgets->button, scroll);

  gtk_box_append(GTK_BOX(widgets->slot), widgets->button);
  gtk_box_append(GTK_BOX(widgets->slot), widgets->indicator);

  bs_dock_app_update_item_widgets(app, widgets, item);
  return widgets;
}

static void
bs_dock_app_update_item_widgets(BsDockApp *app,
                                BsDockItemWidgets *widgets,
                                const BsDockItemView *item) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(widgets != NULL);
  g_return_if_fail(item != NULL);

  bs_dock_app_update_action_data(widgets->action, item);

  gtk_widget_set_tooltip_text(widgets->button, item->name != NULL ? item->name : item->app_key);
  if (item->icon_name != NULL && *item->icon_name != '\0') {
    gtk_image_set_from_icon_name(GTK_IMAGE(widgets->icon_image), item->icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(widgets->icon_image), 56);
    if (gtk_button_get_child(GTK_BUTTON(widgets->button)) != widgets->icon_image) {
      gtk_button_set_child(GTK_BUTTON(widgets->button), widgets->icon_image);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(widgets->label),
                       item->name != NULL ? item->name : item->app_key);
    if (gtk_button_get_child(GTK_BUTTON(widgets->button)) != widgets->label) {
      gtk_button_set_child(GTK_BUTTON(widgets->button), widgets->label);
    }
  }

  if (item->running) {
    gtk_widget_add_css_class(widgets->button, "is-running");
  } else {
    gtk_widget_remove_css_class(widgets->button, "is-running");
  }
  if (item->focused) {
    gtk_widget_add_css_class(widgets->button, "is-focused");
  } else {
    gtk_widget_remove_css_class(widgets->button, "is-focused");
  }
  if (item->pinned && !item->running) {
    gtk_widget_add_css_class(widgets->button, "is-pinned");
  } else {
    gtk_widget_remove_css_class(widgets->button, "is-pinned");
  }

  gtk_widget_set_size_request(widgets->indicator,
                              item->focused ? 7 : 6,
                              item->focused ? 7 : 6);
  if (!item->running) {
    gtk_widget_add_css_class(widgets->indicator, "is-hidden");
    gtk_widget_remove_css_class(widgets->indicator, "is-focused");
  } else if (item->focused) {
    gtk_widget_remove_css_class(widgets->indicator, "is-hidden");
    gtk_widget_add_css_class(widgets->indicator, "is-focused");
  } else {
    gtk_widget_remove_css_class(widgets->indicator, "is-hidden");
    gtk_widget_remove_css_class(widgets->indicator, "is-focused");
  }
}

static void
bs_dock_app_sync_ui(BsDockApp *app) {
  g_autoptr(GHashTable) seen_keys = NULL;
  GtkWidget *previous_slot = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->items_box != NULL);

  seen_keys = g_hash_table_new(g_str_hash, g_str_equal);
  for (guint i = 0; i < app->items->len; i++) {
    const BsDockItemView *item = g_ptr_array_index(app->items, i);
    BsDockItemWidgets *widgets = NULL;

    if (item == NULL || item->app_key == NULL) {
      continue;
    }

    g_hash_table_add(seen_keys, item->app_key);
    widgets = g_hash_table_lookup(app->item_widgets_by_app_key, item->app_key);
    if (widgets == NULL) {
      widgets = bs_dock_app_create_item_widgets(app, item);
      g_hash_table_insert(app->item_widgets_by_app_key, g_strdup(item->app_key), widgets);
      gtk_box_append(GTK_BOX(app->items_box), widgets->slot);
    } else {
      bs_dock_app_update_item_widgets(app, widgets, item);
    }

#if GTK_CHECK_VERSION(4, 10, 0)
    gtk_box_reorder_child_after(GTK_BOX(app->items_box), widgets->slot, previous_slot);
#endif
    previous_slot = widgets->slot;
  }

  if (app->item_widgets_by_app_key != NULL) {
    GHashTableIter iter;
    gpointer key = NULL;

    g_hash_table_iter_init(&iter, app->item_widgets_by_app_key);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
      if (!g_hash_table_contains(seen_keys, key)) {
        g_hash_table_iter_remove(&iter);
      }
    }
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

    if (item->app_key == NULL || *item->app_key == '\0') {
      bs_dock_item_view_free(item);
      continue;
    }

    g_ptr_array_add(items, item);
  }

  return items;
}

static bool
bs_dock_app_send_app_window_focus_request(BsDockApp *app,
                                          const BsDockItemActionData *action,
                                          int direction,
                                          GError **error) {
  const char *op = NULL;
  g_autofree char *escaped_app_key = NULL;
  g_autofree char *request = NULL;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(action != NULL, false);

  if (action->app_key == NULL || *action->app_key == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Missing app_key for app window focus");
    return false;
  }

  op = direction >= 0 ? "focus_next_app_window" : "focus_prev_app_window";
  escaped_app_key = g_strescape(action->app_key, NULL);
  request = g_strdup_printf("{\"op\":\"%s\",\"app_key\":\"%s\"}",
                            op,
                            escaped_app_key != NULL ? escaped_app_key : "");
  return bs_dock_app_send_json(app, request, error);
}

static void
bs_dock_app_log_dock_items(BsDockApp *app, const char *source) {
  const BsDockItemView *focused_item = NULL;

  g_return_if_fail(app != NULL);

  for (guint i = 0; i < app->items->len; i++) {
    const BsDockItemView *item = g_ptr_array_index(app->items, i);

    if (item != NULL) {
      g_message("[bit_dock] dock item[%u] source=%s app_key=%s running=%s focused=%s windows=%u icon=%s",
                i,
                source != NULL ? source : "unknown",
                item->app_key != NULL ? item->app_key : "(null)",
                item->running ? "true" : "false",
                item->focused ? "true" : "false",
                item->window_ids != NULL ? item->window_ids->len : 0,
                item->icon_name != NULL && *item->icon_name != '\0' ? item->icon_name : "(none)");
    }

    if (item != NULL && item->focused) {
      focused_item = item;
      break;
    }
  }

  g_message("[bit_dock] applied dock payload from %s items=%u focused_app=%s focused_windows=%u",
            source != NULL ? source : "unknown",
            app->items != NULL ? app->items->len : 0,
            focused_item != NULL && focused_item->app_key != NULL ? focused_item->app_key : "(none)",
            focused_item != NULL && focused_item->window_ids != NULL ? focused_item->window_ids->len : 0);
}

static void
bs_dock_app_apply_dock_payload(BsDockApp *app, JsonObject *payload) {
  g_autoptr(GPtrArray) items = NULL;

  g_return_if_fail(app != NULL);

  items = bs_dock_app_parse_dock_items(payload);
  g_clear_pointer(&app->items, g_ptr_array_unref);
  app->items = g_steal_pointer(&items);
  bs_dock_app_log_dock_items(app, "update");
  bs_dock_app_sync_ui(app);
}

static gboolean
bs_dock_app_reconnect_cb(gpointer user_data) {
  BsDockApp *app = user_data;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(app != NULL, G_SOURCE_REMOVE);
  app->reconnect_source_id = 0;
  g_message("[bit_dock] retrying IPC connection");

  if (!bs_dock_app_connect_ipc(app, &error)) {
    g_warning("[bit_dock] reconnect failed: %s",
              error != NULL ? error->message : "unknown error");
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

  g_message("[bit_dock] scheduling IPC reconnect in %u ms", BS_DOCK_RECONNECT_DELAY_MS);
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
    g_warning("[bit_dock] failed to parse IPC message: %s", line);
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
    JsonObject *dock = state != NULL ? json_object_get_object_member(state, "dock") : NULL;

    bs_dock_app_apply_dock_payload(app, dock);
    return;
  }

  if (g_strcmp0(kind, "event") == 0) {
    const char *topic = bs_json_string_member(root_object, "topic");
    JsonObject *payload = json_object_get_object_member(root_object, "payload");

    if (g_strcmp0(topic, "dock") == 0 && payload != NULL) {
      bs_dock_app_apply_dock_payload(app, payload);
    }
    return;
  }

  if (g_strcmp0(kind, "error") == 0) {
    const char *message = bs_json_string_member(root_object, "message");
    g_warning("[bit_dock] IPC error: %s", message != NULL ? message : "request failed");
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
    g_warning("[bit_dock] IPC disconnected: %s",
              error != NULL ? error->message : "Dock IPC disconnected");
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
  g_message("[bit_dock] connecting to IPC socket %s", app->socket_path);
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
  if (!bs_dock_app_send_json(app, "{\"op\":\"subscribe\",\"topics\":[\"dock\"]}", error)) {
    bs_dock_app_close_connection(app);
    return false;
  }

  app->ipc_ready = true;
  g_message("[bit_dock] IPC connected and subscribed to dock topic");
  bs_dock_app_set_status(app, "");
  bs_dock_app_begin_read(app);
  return true;
}

static void
bs_dock_app_apply_css(void) {
  GtkCssProvider *provider = NULL;
  const char *css =
    "window.bit-dock-window {"
    "  background: transparent;"
    "  box-shadow: none;"
    "}"
    ".dock-root {"
    "  padding: 10px 14px 8px 14px;"
    "  border-radius: 24px;"
    "  background: rgba(248, 248, 252, 0.82);"
    "  border: 1px solid rgba(255, 255, 255, 0.22);"
    "  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.22),"
    "              inset 0 1px 0 rgba(255, 255, 255, 0.18);"
    "}"
    ".dock-slot {"
    "  margin-left: 1px;"
    "  margin-right: 1px;"
    "}"
    ".dock-item {"
    "  min-width: 56px;"
    "  min-height: 56px;"
    "  padding: 0;"
    "  border-radius: 16px;"
    "  border: 1px solid transparent;"
    "  background: transparent;"
    "  box-shadow: none;"
    "}"
    ".dock-item:hover {"
    "  background: rgba(255, 255, 255, 0.10);"
    "  border: 1px solid rgba(255, 255, 255, 0.10);"
    "}"
    ".dock-item-icon {"
    "  color: rgba(24, 28, 34, 0.94);"
    "}"
    ".dock-item-label {"
    "  color: rgba(24, 28, 34, 0.92);"
    "  font-size: 11px;"
    "  font-weight: 600;"
    "}"
    ".dock-item.is-running {"
    "  background: transparent;"
    "}"
    ".dock-item.is-focused {"
    "  background: rgba(255, 255, 255, 0.12);"
    "  border: 1px solid rgba(255, 255, 255, 0.14);"
    "  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.18);"
    "}"
    ".dock-item.is-pinned {"
    "  background: transparent;"
    "}"
    ".dock-indicator {"
    "  min-width: 6px;"
    "  min-height: 6px;"
    "  border-radius: 999px;"
    "  background: rgba(255, 255, 255, 0.72);"
    "}"
    ".dock-indicator.is-focused {"
    "  background: rgba(255, 255, 255, 0.96);"
    "}"
    ".dock-indicator.is-hidden {"
    "  opacity: 0.0;"
    "}"
    ".dock-status {"
    "  margin-top: 2px;"
    "  color: rgba(255, 255, 255, 0.68);"
    "  font-size: 11px;"
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
  gtk_widget_add_css_class(GTK_WIDGET(app->window), "bit-dock-window");

  app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(app->root_box, "dock-root");

  app->items_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
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

  g_message("[bit_dock] activate");
  bs_dock_app_apply_css();
  bs_dock_app_ensure_window(app);
  gtk_window_present(app->window);

  if (!app->ipc_ready && !bs_dock_app_connect_ipc(app, &error)) {
    g_warning("[bit_dock] initial IPC connect failed: %s",
              error != NULL ? error->message : "unknown error");
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to connect to bit_shelld");
    bs_dock_app_schedule_reconnect(app);
  }
}
