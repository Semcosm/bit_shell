#include "frontends/bit_dock/dock_app.h"
#include "frontends/bit_dock/dock_layout.h"

#include <gio/gio.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <gtk4-layer-shell.h>

#define BS_DOCK_APP_ID "io.bit_shell.bit_dock"
#define BS_DOCK_RECONNECT_DELAY_MS 2000U
#define BS_DOCK_HOVER_CLEAR_DELAY_MS 16U
#define BS_DOCK_ACTIVE_WEIGHT_EPSILON 0.0025
#define BS_DOCK_TICK_EPSILON_SCALE 0.0005
#define BS_DOCK_TICK_EPSILON_OFFSET 0.01
#define BS_DOCK_LAYOUT_EPSILON 0.01
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
  GtkWidget *content_box;
  GtkWidget *button;
  GtkWidget *indicator;
  GtkWidget *icon_image;
  GtkWidget *label;
  char *slot_css_name;
  char *content_css_name;
  char *button_css_name;
  BsDockItemActionData *action;
  guint visual_index;
  double base_center_x;
  double target_offset_x_px;
  double target_lift_px;
  double target_scale;
  double x_offset_px;
  double y_offset_px;
  double visual_scale;
  double slot_width_px;
} BsDockItemWidgets;

struct _BsDockApp {
  GtkApplication *gtk_app;
  GtkWindow *window;
  GtkWidget *layout_box;
  GtkWidget *root_box;
  GtkWidget *items_box;
  GtkWidget *status_label;
  GtkEventController *items_motion;
  GtkCssProvider *base_css_provider;
  GtkCssProvider *dynamic_css_provider;
  GSocketClient *socket_client;
  GSocketConnection *connection;
  GDataInputStream *input;
  GOutputStream *output;
  GCancellable *read_cancellable;
  GPtrArray *items;
  GPtrArray *ordered_item_widgets;
  GHashTable *item_widgets_by_app_key;
  char *socket_path;
  double pointer_x;
  double pointer_y;
  gint64 last_frame_time_us;
  guint tick_callback_id;
  guint reconnect_source_id;
  guint next_slot_css_id;
  int root_box_margin_top_px;
  int items_box_margin_start_px;
  int items_box_margin_end_px;
  BsDockConfig config;
  BsDockMetrics metrics;
  bool dynamic_css_structure_dirty;
  bool hover_active;
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
static void bs_dock_app_recompute_visual_indices(BsDockApp *app);
static void bs_dock_app_refresh_magnification(BsDockApp *app);
static void bs_dock_app_update_hover_from_items_box(BsDockApp *app, double x, double y);
static bool bs_dock_app_compute_items_box_point_in_layout(BsDockApp *app,
                                                          double x,
                                                          double y,
                                                          double *layout_x,
                                                          double *layout_y);
static bool bs_dock_app_update_root_box_top_margin(BsDockApp *app, int margin_top);
static bool bs_dock_app_update_items_box_margins(BsDockApp *app,
                                                 int margin_start,
                                                 int margin_end);
static double bs_dock_app_base_step(BsDockApp *app);
static double bs_dock_app_base_gap(BsDockApp *app);
static double bs_dock_app_base_center_for_index(BsDockApp *app, guint index);
static double bs_dock_app_hover_range(BsDockApp *app, guint item_count);
static double bs_dock_app_magnification_weight(double distance_px, double radius_px);
static double bs_dock_app_animation_alpha(double dt_seconds, double tau_seconds);
static void bs_dock_app_solve_isotonic(double *values, const double *weights, guint len);
static void bs_dock_app_update_targets(BsDockApp *app);
static bool bs_dock_app_apply_current_visuals(BsDockApp *app, bool force_dynamic_css);
static gboolean bs_dock_app_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data);
static void bs_dock_app_ensure_tick(BsDockApp *app);
static void bs_dock_app_stop_tick(BsDockApp *app);
static void bs_dock_app_apply_layout(BsDockApp *app, BsDockItemWidgets *widgets);
static void bs_dock_app_update_dynamic_css(BsDockApp *app);
static void bs_dock_app_apply_dock_payload(BsDockApp *app, JsonObject *payload);
static void bs_dock_app_apply_settings_payload(BsDockApp *app, JsonObject *payload);
static GPtrArray *bs_dock_app_parse_dock_items(JsonObject *payload);
static void bs_dock_app_log_dock_items(BsDockApp *app, const char *source);
static gboolean bs_dock_app_reconnect_cb(gpointer user_data);
static void bs_dock_app_schedule_reconnect(BsDockApp *app);
static bool bs_dock_app_send_json(BsDockApp *app, const char *json, GError **error);
static bool bs_dock_app_send_app_window_focus_request(BsDockApp *app,
                                                      const BsDockItemActionData *action,
                                                      int direction,
                                                      GError **error);
static void bs_dock_app_apply_dock_config(BsDockApp *app, const BsDockConfig *config);
static void bs_dock_app_begin_read(BsDockApp *app);
static void bs_dock_app_on_read_line(GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data);
static bool bs_dock_app_connect_ipc(BsDockApp *app, GError **error);
static void bs_dock_app_ensure_window(BsDockApp *app);
static void bs_dock_app_apply_css(BsDockApp *app);
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
static void bs_dock_app_on_items_enter(GtkEventControllerMotion *motion,
                                       gdouble x,
                                       gdouble y,
                                       gpointer user_data);
static void bs_dock_app_on_items_motion(GtkEventControllerMotion *motion,
                                        gdouble x,
                                        gdouble y,
                                        gpointer user_data);
static void bs_dock_app_on_items_leave(GtkEventControllerMotion *motion,
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
  app->ordered_item_widgets = g_ptr_array_new();
  app->item_widgets_by_app_key = g_hash_table_new_full(g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       bs_dock_item_widgets_free);
  bs_dock_config_init_defaults(&app->config);
  bs_dock_metrics_derive(&app->metrics, &app->config);
  app->root_box_margin_top_px = -1;
  app->items_box_margin_start_px = -1;
  app->items_box_margin_end_px = -1;
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

  bs_dock_app_stop_tick(app);
  if (app->reconnect_source_id != 0) {
    g_source_remove(app->reconnect_source_id);
  }
  bs_dock_app_close_connection(app);
  g_clear_pointer(&app->items, g_ptr_array_unref);
  g_clear_pointer(&app->ordered_item_widgets, g_ptr_array_unref);
  g_clear_pointer(&app->item_widgets_by_app_key, g_hash_table_unref);
  g_clear_object(&app->read_cancellable);
  g_clear_object(&app->socket_client);
  g_clear_object(&app->base_css_provider);
  g_clear_object(&app->dynamic_css_provider);
  g_clear_object(&app->gtk_app);
  g_free(app->socket_path);
  g_free(app);
}

void
bs_dock_app_set_config(BsDockApp *app, const BsDockConfig *config) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(config != NULL);

  bs_dock_app_apply_dock_config(app, config);
}

void
bs_dock_app_get_config(BsDockApp *app, BsDockConfig *out_config) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(out_config != NULL);

  *out_config = app->config;
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
  g_free(widgets->slot_css_name);
  g_free(widgets->content_css_name);
  g_free(widgets->button_css_name);
  bs_dock_item_action_data_free(widgets->action);
  g_free(widgets);
}

static void
bs_dock_app_stop_tick(BsDockApp *app) {
  g_return_if_fail(app != NULL);

  if (app->tick_callback_id != 0 && app->items_box != NULL) {
    gtk_widget_remove_tick_callback(app->items_box, app->tick_callback_id);
    app->tick_callback_id = 0;
  }
  app->last_frame_time_us = 0;
}

static void
bs_dock_app_ensure_tick(BsDockApp *app) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->items_box != NULL);

  if (app->tick_callback_id != 0) {
    return;
  }

  app->last_frame_time_us = 0;
  app->tick_callback_id = gtk_widget_add_tick_callback(app->items_box,
                                                       bs_dock_app_tick_cb,
                                                       app,
                                                       NULL);
}

static void
bs_dock_app_update_hover_from_items_box(BsDockApp *app, double x, double y) {
  double layout_x = 0.0;
  double layout_y = 0.0;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->items_box != NULL);

  if (!bs_dock_app_compute_items_box_point_in_layout(app, x, y, &layout_x, &layout_y)) {
    return;
  }

  if (app->hover_active
      && fabs(app->pointer_x - layout_x) < 0.01
      && fabs(app->pointer_y - layout_y) < 0.01) {
    return;
  }

  app->hover_active = true;
  app->pointer_x = layout_x;
  app->pointer_y = layout_y;
  bs_dock_app_refresh_magnification(app);
}

static double
bs_dock_app_base_gap(BsDockApp *app) {
  g_return_val_if_fail(app != NULL, 0.0);
  return MAX(bs_dock_app_base_step(app) - app->metrics.item_size_px, 0.0);
}

static bool
bs_dock_app_compute_items_box_point_in_layout(BsDockApp *app,
                                              double x,
                                              double y,
                                              double *layout_x,
                                              double *layout_y) {
  graphene_point_t local_point;
  graphene_point_t layout_point;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app->items_box != NULL, false);
  g_return_val_if_fail(app->layout_box != NULL, false);
  g_return_val_if_fail(layout_x != NULL, false);
  g_return_val_if_fail(layout_y != NULL, false);

  local_point = GRAPHENE_POINT_INIT((float) x, (float) y);
  if (!gtk_widget_compute_point(app->items_box, app->layout_box, &local_point, &layout_point)) {
    return false;
  }

  *layout_x = layout_point.x;
  *layout_y = layout_point.y;
  return true;
}

static bool
bs_dock_app_update_root_box_top_margin(BsDockApp *app, int margin_top) {
  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app->root_box != NULL, false);

  margin_top = MAX(margin_top, 0);
  if (app->root_box_margin_top_px == margin_top) {
    return false;
  }

  gtk_widget_set_margin_top(app->root_box, margin_top);
  app->root_box_margin_top_px = margin_top;
  return true;
}

static bool
bs_dock_app_update_items_box_margins(BsDockApp *app, int margin_start, int margin_end) {
  bool changed = false;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app->items_box != NULL, false);
  g_return_val_if_fail(app->root_box != NULL, false);

  margin_start = MAX(margin_start, 0);
  margin_end = MAX(margin_end, 0);

  if (app->items_box_margin_start_px != margin_start) {
    gtk_widget_set_margin_start(app->items_box, margin_start);
    app->items_box_margin_start_px = margin_start;
    changed = true;
  }
  if (app->items_box_margin_end_px != margin_end) {
    gtk_widget_set_margin_end(app->items_box, margin_end);
    app->items_box_margin_end_px = margin_end;
    changed = true;
  }

  if (gtk_widget_get_margin_top(app->root_box) != app->root_box_margin_top_px) {
    gtk_widget_set_margin_top(app->root_box, MAX(app->root_box_margin_top_px, 0));
    changed = true;
  }
  if (gtk_widget_get_margin_start(app->root_box) != app->metrics.edge_reserve_px) {
    gtk_widget_set_margin_start(app->root_box, app->metrics.edge_reserve_px);
    changed = true;
  }
  if (gtk_widget_get_margin_end(app->root_box) != app->metrics.edge_reserve_px) {
    gtk_widget_set_margin_end(app->root_box, app->metrics.edge_reserve_px);
    changed = true;
  }

  return changed;
}

static double
bs_dock_app_base_step(BsDockApp *app) {
  g_return_val_if_fail(app != NULL, 68.0);

  return (double) app->metrics.slot_width_px
         + (double) (app->items_box != NULL
                       ? gtk_box_get_spacing(GTK_BOX(app->items_box))
                       : app->metrics.items_spacing_px);
}

static double
bs_dock_app_base_center_for_index(BsDockApp *app, guint index) {
  g_return_val_if_fail(app != NULL, 0.0);

  return (app->metrics.slot_width_px * 0.5) + (bs_dock_app_base_step(app) * (double) index);
}

static double
bs_dock_app_hover_range(BsDockApp *app, guint item_count) {
  g_return_val_if_fail(app != NULL, 0.0);
  return bs_dock_metrics_hover_range(&app->metrics, item_count);
}

static double
bs_dock_app_magnification_weight(double distance_px, double radius_px) {
  double normalized = 0.0;

  if (radius_px <= 0.0 || distance_px >= radius_px) {
    return 0.0;
  }

  normalized = distance_px / radius_px;
  return 0.5 * (1.0 + cos(G_PI * normalized));
}

static double
bs_dock_app_animation_alpha(double dt_seconds, double tau_seconds) {
  if (tau_seconds <= 0.0) {
    return 1.0;
  }

  return 1.0 - exp(-(dt_seconds / tau_seconds));
}

static void
bs_dock_app_solve_isotonic(double *values, const double *weights, guint len) {
  guint *block_starts = NULL;
  guint *block_ends = NULL;
  double *block_weight_sums = NULL;
  double *block_value_sums = NULL;
  guint block_count = 0;

  g_return_if_fail(values != NULL);
  g_return_if_fail(weights != NULL);

  if (len == 0) {
    return;
  }

  block_starts = g_newa(guint, len);
  block_ends = g_newa(guint, len);
  block_weight_sums = g_newa(double, len);
  block_value_sums = g_newa(double, len);

  for (guint i = 0; i < len; i++) {
    double weight = MAX(weights[i], 0.0001);

    block_starts[block_count] = i;
    block_ends[block_count] = i;
    block_weight_sums[block_count] = weight;
    block_value_sums[block_count] = values[i] * weight;
    block_count++;

    while (block_count > 1) {
      double previous_mean =
        block_value_sums[block_count - 2] / block_weight_sums[block_count - 2];
      double current_mean =
        block_value_sums[block_count - 1] / block_weight_sums[block_count - 1];

      if (previous_mean <= current_mean + 1e-12) {
        break;
      }

      block_ends[block_count - 2] = block_ends[block_count - 1];
      block_weight_sums[block_count - 2] += block_weight_sums[block_count - 1];
      block_value_sums[block_count - 2] += block_value_sums[block_count - 1];
      block_count--;
    }
  }

  for (guint block_index = 0; block_index < block_count; block_index++) {
    double mean = block_value_sums[block_index] / block_weight_sums[block_index];

    for (guint i = block_starts[block_index]; i <= block_ends[block_index]; i++) {
      values[i] = mean;
    }
  }
}


static void
bs_dock_app_apply_layout(BsDockApp *app, BsDockItemWidgets *widgets) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(widgets != NULL);

  gtk_widget_set_size_request(widgets->slot,
                              widgets->slot_width_px > 0.0
                                ? (int) (widgets->slot_width_px + 0.5)
                                : app->metrics.slot_width_px,
                              -1);
  gtk_widget_set_size_request(widgets->button,
                              app->metrics.item_size_px,
                              app->metrics.item_size_px);
  gtk_image_set_pixel_size(GTK_IMAGE(widgets->icon_image), app->metrics.item_size_px);
}

static void
bs_dock_app_update_dynamic_css(BsDockApp *app) {
  GString *css = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->dynamic_css_provider != NULL);
  g_return_if_fail(app->ordered_item_widgets != NULL);

  css = g_string_new("");
  for (guint i = 0; i < app->ordered_item_widgets->len; i++) {
    BsDockItemWidgets *widgets = g_ptr_array_index(app->ordered_item_widgets, i);

    if (widgets == NULL
        || widgets->slot_css_name == NULL
        || widgets->content_css_name == NULL
        || widgets->button_css_name == NULL) {
      continue;
    }

    g_string_append_printf(css,
                           "#%s { transform: translateX(%.2fpx); }\n",
                           widgets->slot_css_name,
                           widgets->x_offset_px);
    g_string_append_printf(css,
                           "#%s { transform: translateY(%.2fpx); }\n",
                           widgets->content_css_name,
                           widgets->y_offset_px);
    g_string_append_printf(css,
                           "#%s { transform: scale(%.4f); }\n",
                           widgets->button_css_name,
                           widgets->visual_scale);
  }

#if GTK_CHECK_VERSION(4, 12, 0)
  gtk_css_provider_load_from_string(app->dynamic_css_provider, css->str);
#else
  gtk_css_provider_load_from_data(app->dynamic_css_provider, css->str, -1);
#endif
  g_string_free(css, TRUE);
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

static void
bs_dock_app_on_items_enter(GtkEventControllerMotion *motion,
                           gdouble x,
                           gdouble y,
                           gpointer user_data) {
  BsDockApp *app = user_data;

  (void) motion;
  g_return_if_fail(app != NULL);

  bs_dock_app_update_hover_from_items_box(app, x, y);
}

static void
bs_dock_app_on_items_motion(GtkEventControllerMotion *motion,
                            gdouble x,
                            gdouble y,
                            gpointer user_data) {
  BsDockApp *app = user_data;

  (void) motion;
  g_return_if_fail(app != NULL);

  bs_dock_app_update_hover_from_items_box(app, x, y);
}

static void
bs_dock_app_on_items_leave(GtkEventControllerMotion *motion, gpointer user_data) {
  BsDockApp *app = user_data;

  (void) motion;
  g_return_if_fail(app != NULL);

  app->hover_active = false;
  bs_dock_app_refresh_magnification(app);
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
  widgets->slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign(widgets->slot, GTK_ALIGN_CENTER);
  gtk_widget_set_overflow(widgets->slot, GTK_OVERFLOW_VISIBLE);
  gtk_widget_add_css_class(widgets->slot, "dock-slot");
  widgets->slot_css_name = g_strdup_printf("dock-slot-%u", ++app->next_slot_css_id);
  gtk_widget_set_name(widgets->slot, widgets->slot_css_name);

  widgets->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, app->metrics.content_gap_px);
  gtk_widget_set_halign(widgets->content_box, GTK_ALIGN_CENTER);
  gtk_widget_set_overflow(widgets->content_box, GTK_OVERFLOW_VISIBLE);
  gtk_widget_add_css_class(widgets->content_box, "dock-slot-content");
  widgets->content_css_name = g_strdup_printf("dock-slot-content-%u", app->next_slot_css_id);
  gtk_widget_set_name(widgets->content_box, widgets->content_css_name);

  widgets->button = gtk_button_new();
  gtk_widget_set_overflow(widgets->button, GTK_OVERFLOW_VISIBLE);
  gtk_widget_add_css_class(widgets->button, "dock-item");
  widgets->button_css_name = g_strdup_printf("dock-item-%u", app->next_slot_css_id);
  gtk_widget_set_name(widgets->button, widgets->button_css_name);

  widgets->icon_image = gtk_image_new();
  gtk_widget_add_css_class(widgets->icon_image, "dock-item-icon");

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
  gtk_widget_set_can_target(widgets->indicator, false);

  widgets->action = g_new0(BsDockItemActionData, 1);
  widgets->base_center_x = 0.0;
  widgets->target_offset_x_px = 0.0;
  widgets->target_lift_px = 0.0;
  widgets->target_scale = 1.0;
  widgets->slot_width_px = app->metrics.slot_width_px;
  widgets->x_offset_px = 0.0;
  widgets->y_offset_px = 0.0;
  widgets->visual_scale = 1.0;
  bs_dock_app_apply_layout(app, widgets);
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

  gtk_box_append(GTK_BOX(widgets->content_box), widgets->button);
  gtk_box_append(GTK_BOX(widgets->content_box), widgets->indicator);
  gtk_box_append(GTK_BOX(widgets->slot), widgets->content_box);

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

  gtk_widget_set_size_request(widgets->indicator,
                              bs_dock_metrics_indicator_size(&app->metrics, item->focused),
                              bs_dock_metrics_indicator_size(&app->metrics, item->focused));
  if (!app->config.show_running_indicator || !item->running) {
    gtk_widget_add_css_class(widgets->indicator, "is-hidden");
    gtk_widget_remove_css_class(widgets->indicator, "is-focused");
  } else if (item->focused) {
    gtk_widget_remove_css_class(widgets->indicator, "is-hidden");
    gtk_widget_add_css_class(widgets->indicator, "is-focused");
  } else {
    gtk_widget_remove_css_class(widgets->indicator, "is-hidden");
    gtk_widget_remove_css_class(widgets->indicator, "is-focused");
  }

  bs_dock_app_apply_layout(app, widgets);
}

static void
bs_dock_app_recompute_visual_indices(BsDockApp *app) {
  double items_origin_x = 0.0;
  double items_origin_y = 0.0;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->ordered_item_widgets != NULL);

  g_ptr_array_set_size(app->ordered_item_widgets, 0);
  if (!bs_dock_app_compute_items_box_point_in_layout(app, 0.0, 0.0, &items_origin_x, &items_origin_y)) {
    items_origin_x = 0.0;
  }

  for (guint i = 0; i < app->items->len; i++) {
    const BsDockItemView *item = g_ptr_array_index(app->items, i);
    BsDockItemWidgets *widgets = NULL;

    if (item == NULL || item->app_key == NULL) {
      continue;
    }

    widgets = g_hash_table_lookup(app->item_widgets_by_app_key, item->app_key);
    if (widgets == NULL) {
      continue;
    }

    widgets->visual_index = i;
    widgets->base_center_x = items_origin_x + bs_dock_app_base_center_for_index(app, i);
    g_ptr_array_add(app->ordered_item_widgets, widgets);
  }
}

static void
bs_dock_app_refresh_magnification(BsDockApp *app) {
  g_return_if_fail(app != NULL);

  bs_dock_app_update_targets(app);
  bs_dock_app_ensure_tick(app);
}

static void
bs_dock_app_update_targets(BsDockApp *app) {
  guint item_count = 0;
  double hover_range = 0.0;
  double base_gap = 0.0;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->ordered_item_widgets != NULL);

  item_count = app->ordered_item_widgets->len;
  if (item_count == 0) {
    return;
  }

  hover_range = bs_dock_app_hover_range(app, item_count);
  base_gap = bs_dock_app_base_gap(app);

  if (app->hover_active) {
    double first_center = ((BsDockItemWidgets *) g_ptr_array_index(app->ordered_item_widgets, 0))->base_center_x;
    double last_center =
      ((BsDockItemWidgets *) g_ptr_array_index(app->ordered_item_widgets, item_count - 1))->base_center_x;

    app->pointer_x = CLAMP(app->pointer_x, first_center, last_center);
  }

  {
    double *weights = g_newa(double, item_count);
    double *half_visual = g_newa(double, item_count);
    double *minimum_distances = g_newa(double, item_count);
    double *prefix_offsets = g_newa(double, item_count);
    double *projected_centers = g_newa(double, item_count);
    double *projection_weights = g_newa(double, item_count);
    double *target_centers = g_newa(double, item_count);
    bool any_active = false;

    for (guint i = 0; i < item_count; i++) {
      BsDockItemWidgets *widgets = g_ptr_array_index(app->ordered_item_widgets, i);
      const BsDockItemView *item = g_ptr_array_index(app->items, i);
      double weight = 0.0;
      double target_scale = 1.0;
      double target_lift = 0.0;

      weights[i] = 0.0;
      half_visual[i] = app->metrics.item_size_px * 0.5;
      target_centers[i] = widgets->base_center_x;

      if (app->hover_active && app->config.magnification_enabled) {
        weight = bs_dock_app_magnification_weight(fabs(app->pointer_x - widgets->base_center_x),
                                                  hover_range);
      }

      if (weight > BS_DOCK_ACTIVE_WEIGHT_EPSILON) {
        any_active = true;
      }

      target_scale = 1.0 + ((app->metrics.max_visual_scale - 1.0) * weight);
      target_lift = (item != NULL && item->focused ? app->metrics.focused_lift_px : 0.0)
                    + (app->metrics.max_hover_lift_px * pow(weight, 0.85));

      widgets->target_scale = target_scale;
      widgets->target_lift_px = target_lift;
      widgets->target_offset_x_px = 0.0;
      weights[i] = weight;
      half_visual[i] = (app->metrics.item_size_px * target_scale) * 0.5;
    }

    if (!app->hover_active || !any_active) {
      return;
    }

    prefix_offsets[0] = 0.0;
    projected_centers[0] =
      ((BsDockItemWidgets *) g_ptr_array_index(app->ordered_item_widgets, 0))->base_center_x;
    projection_weights[0] = 1.0 + ((1.0 - weights[0]) * 6.0);

    for (guint i = 1; i < item_count; i++) {
      minimum_distances[i - 1] = half_visual[i - 1] + base_gap + half_visual[i];
      prefix_offsets[i] = prefix_offsets[i - 1] + minimum_distances[i - 1];
      projected_centers[i] =
        ((BsDockItemWidgets *) g_ptr_array_index(app->ordered_item_widgets, i))->base_center_x
        - prefix_offsets[i];
      projection_weights[i] = 1.0 + ((1.0 - weights[i]) * 6.0);
    }

    bs_dock_app_solve_isotonic(projected_centers, projection_weights, item_count);

    for (guint i = 0; i < item_count; i++) {
      BsDockItemWidgets *widgets = g_ptr_array_index(app->ordered_item_widgets, i);

      target_centers[i] = projected_centers[i] + prefix_offsets[i];
      widgets->target_offset_x_px = target_centers[i] - widgets->base_center_x;
      if (fabs(widgets->target_offset_x_px) <= BS_DOCK_LAYOUT_EPSILON) {
        widgets->target_offset_x_px = 0.0;
      }
    }
  }
}

static bool
bs_dock_app_apply_current_visuals(BsDockApp *app, bool force_dynamic_css) {
  guint item_count = 0;
  double base_left_min = 0.0;
  double base_right_max = 0.0;
  double max_top_overflow = 0.0;
  double visual_left_min = G_MAXDOUBLE;
  double visual_right_max = -G_MAXDOUBLE;
  bool dynamic_css_dirty = force_dynamic_css;
  bool layout_changed = false;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(app->ordered_item_widgets != NULL, false);

  item_count = app->ordered_item_widgets->len;
  if (item_count == 0) {
    if (force_dynamic_css) {
      bs_dock_app_update_dynamic_css(app);
      app->dynamic_css_structure_dirty = false;
    }
    (void) bs_dock_app_update_root_box_top_margin(app, 0);
    (void) bs_dock_app_update_items_box_margins(app, 0, 0);
    return false;
  }

  base_left_min = ((BsDockItemWidgets *) g_ptr_array_index(app->ordered_item_widgets, 0))->base_center_x
                  - (app->metrics.item_size_px * 0.5);
  base_right_max =
    ((BsDockItemWidgets *) g_ptr_array_index(app->ordered_item_widgets, item_count - 1))->base_center_x
    + (app->metrics.item_size_px * 0.5);

  for (guint i = 0; i < item_count; i++) {
    BsDockItemWidgets *widgets = g_ptr_array_index(app->ordered_item_widgets, i);
    double center = widgets->base_center_x + widgets->x_offset_px;
    double half_visual = (app->metrics.item_size_px * widgets->visual_scale) * 0.5;
    double top_overflow = MAX(0.0,
                              (-widgets->y_offset_px)
                                + (app->metrics.item_size_px * (widgets->visual_scale - 1.0)));

    visual_left_min = MIN(visual_left_min, center - half_visual);
    visual_right_max = MAX(visual_right_max, center + half_visual);
    max_top_overflow = MAX(max_top_overflow, top_overflow);
  }

  layout_changed =
    bs_dock_app_update_root_box_top_margin(app, (int) ceil(max_top_overflow))
    || layout_changed;
  layout_changed =
    bs_dock_app_update_items_box_margins(app,
                                         (int) ceil(MAX(0.0, base_left_min - visual_left_min)),
                                         (int) ceil(MAX(0.0, visual_right_max - base_right_max)))
    || layout_changed;

  if (dynamic_css_dirty || layout_changed) {
    bs_dock_app_update_dynamic_css(app);
    app->dynamic_css_structure_dirty = false;
  }

  return dynamic_css_dirty || layout_changed;
}

static gboolean
bs_dock_app_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data) {
  BsDockApp *app = user_data;
  gint64 frame_time_us = 0;
  double dt_seconds = 1.0 / 60.0;
  double alpha_scale = 0.0;
  double alpha_lift = 0.0;
  double alpha_offset = 0.0;
  bool any_animating = false;
  bool force_dynamic_css = false;
  bool dynamic_css_dirty = false;

  (void) widget;
  g_return_val_if_fail(app != NULL, G_SOURCE_REMOVE);
  g_return_val_if_fail(frame_clock != NULL, G_SOURCE_REMOVE);

  frame_time_us = gdk_frame_clock_get_frame_time(frame_clock);
  if (app->last_frame_time_us > 0 && frame_time_us > app->last_frame_time_us) {
    dt_seconds = (double) (frame_time_us - app->last_frame_time_us) / 1000000.0;
    dt_seconds = CLAMP(dt_seconds, 1.0 / 240.0, 0.05);
  }
  app->last_frame_time_us = frame_time_us;

  bs_dock_app_update_targets(app);
  force_dynamic_css = app->dynamic_css_structure_dirty;
  alpha_scale = bs_dock_app_animation_alpha(dt_seconds, app->metrics.tau_scale_s);
  alpha_lift = bs_dock_app_animation_alpha(dt_seconds, app->metrics.tau_lift_s);
  alpha_offset = bs_dock_app_animation_alpha(dt_seconds, app->metrics.tau_offset_s);

  for (guint i = 0; i < app->ordered_item_widgets->len; i++) {
    BsDockItemWidgets *widgets = g_ptr_array_index(app->ordered_item_widgets, i);
    double scale_delta = widgets->target_scale - widgets->visual_scale;
    double lift_delta = widgets->target_lift_px - (-widgets->y_offset_px);
    double offset_delta = widgets->target_offset_x_px - widgets->x_offset_px;

    if (fabs(scale_delta) <= BS_DOCK_TICK_EPSILON_SCALE) {
      widgets->visual_scale = widgets->target_scale;
    } else {
      widgets->visual_scale += scale_delta * alpha_scale;
      any_animating = true;
      dynamic_css_dirty = true;
    }

    if (fabs(lift_delta) <= BS_DOCK_TICK_EPSILON_OFFSET) {
      widgets->y_offset_px = -widgets->target_lift_px;
    } else {
      widgets->y_offset_px -= lift_delta * alpha_lift;
      any_animating = true;
      dynamic_css_dirty = true;
    }

    if (fabs(offset_delta) <= BS_DOCK_TICK_EPSILON_OFFSET) {
      widgets->x_offset_px = widgets->target_offset_x_px;
    } else {
      widgets->x_offset_px += offset_delta * alpha_offset;
      any_animating = true;
      dynamic_css_dirty = true;
    }
  }

  (void) bs_dock_app_apply_current_visuals(app, dynamic_css_dirty || force_dynamic_css);
  if (!app->hover_active && !any_animating) {
    app->tick_callback_id = 0;
    app->last_frame_time_us = 0;
    return G_SOURCE_REMOVE;
  }

  if (!any_animating && !force_dynamic_css) {
    app->tick_callback_id = 0;
    app->last_frame_time_us = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static void
bs_dock_app_sync_ui(BsDockApp *app) {
  g_autoptr(GHashTable) seen_keys = NULL;
  GtkWidget *previous_slot = NULL;
  guint previous_item_count = 0;
  bool structure_changed = false;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->items_box != NULL);

  previous_item_count = g_hash_table_size(app->item_widgets_by_app_key);
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
      structure_changed = true;
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
    gpointer value = NULL;

    g_hash_table_iter_init(&iter, app->item_widgets_by_app_key);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      if (!g_hash_table_contains(seen_keys, key)) {
        structure_changed = true;
        g_hash_table_iter_remove(&iter);
      }
    }
  }

  bs_dock_app_recompute_visual_indices(app);
  if (previous_item_count != app->items->len) {
    structure_changed = true;
  }
  app->dynamic_css_structure_dirty = app->dynamic_css_structure_dirty || structure_changed;
  bs_dock_app_refresh_magnification(app);

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

static void
bs_dock_app_apply_dock_config(BsDockApp *app, const BsDockConfig *config) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(config != NULL);

  app->config = *config;
  bs_dock_config_normalize(&app->config);
  bs_dock_metrics_derive(&app->metrics, &app->config);
  if (app->window != NULL && gtk_layer_is_supported()) {
    gtk_layer_set_margin(app->window,
                         GTK_LAYER_SHELL_EDGE_BOTTOM,
                         app->metrics.bottom_margin_px);
  }
  if (app->items_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->items_box), app->metrics.items_spacing_px);
  }
  if (app->base_css_provider != NULL) {
    bs_dock_app_apply_css(app);
  }
  if (app->items_box != NULL) {
    app->dynamic_css_structure_dirty = true;
    bs_dock_app_sync_ui(app);
  }
}

static void
bs_dock_app_apply_settings_payload(BsDockApp *app, JsonObject *payload) {
  BsDockConfig config;
  JsonObject *dock = NULL;
  const char *display_mode = NULL;

  g_return_if_fail(app != NULL);

  bs_dock_config_init_defaults(&config);
  if (payload == NULL || !json_object_has_member(payload, "dock")) {
    bs_dock_app_apply_dock_config(app, &config);
    return;
  }

  dock = json_object_get_object_member(payload, "dock");
  if (dock == NULL) {
    bs_dock_app_apply_dock_config(app, &config);
    return;
  }

  config.icon_size_px = (uint32_t) bs_json_int_member(dock, "icon_size_px", config.icon_size_px);
  config.magnification_enabled = bs_json_bool_member(dock,
                                                     "magnification_enabled",
                                                     config.magnification_enabled);
  if (json_object_has_member(dock, "magnification_scale")) {
    config.magnification_scale = json_object_get_double_member(dock, "magnification_scale");
  }
  config.hover_range_cap_units =
    (uint32_t) bs_json_int_member(dock,
                                  "hover_range_cap_units",
                                  config.hover_range_cap_units);
  config.spacing_px = (uint32_t) bs_json_int_member(dock, "spacing_px", config.spacing_px);
  config.bottom_margin_px = (uint32_t) bs_json_int_member(dock, "bottom_margin_px", config.bottom_margin_px);
  config.show_running_indicator = bs_json_bool_member(dock,
                                                      "show_running_indicator",
                                                      config.show_running_indicator);
  config.animate_opening_apps = bs_json_bool_member(dock,
                                                    "animate_opening_apps",
                                                    config.animate_opening_apps);
  config.center_on_primary_output = bs_json_bool_member(dock,
                                                        "center_on_primary_output",
                                                        config.center_on_primary_output);
  display_mode = bs_json_string_member(dock, "display_mode");
  if (display_mode != NULL) {
    (void) bs_dock_display_mode_from_string(display_mode, &config.display_mode);
  }

  bs_dock_app_apply_dock_config(app, &config);
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
    JsonObject *settings = state != NULL ? json_object_get_object_member(state, "settings") : NULL;

    bs_dock_app_apply_settings_payload(app, settings);
    bs_dock_app_apply_dock_payload(app, dock);
    return;
  }

  if (g_strcmp0(kind, "event") == 0) {
    const char *topic = bs_json_string_member(root_object, "topic");
    JsonObject *payload = json_object_get_object_member(root_object, "payload");

    if (g_strcmp0(topic, "dock") == 0 && payload != NULL) {
      bs_dock_app_apply_dock_payload(app, payload);
    } else if (g_strcmp0(topic, "settings") == 0) {
      bs_dock_app_apply_settings_payload(app, payload);
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
  if (!bs_dock_app_send_json(app, "{\"op\":\"subscribe\",\"topics\":[\"dock\",\"settings\"]}", error)) {
    bs_dock_app_close_connection(app);
    return false;
  }

  app->ipc_ready = true;
  g_message("[bit_dock] IPC connected and subscribed to dock/settings topics");
  bs_dock_app_set_status(app, "");
  bs_dock_app_begin_read(app);
  return true;
}

static void
bs_dock_app_apply_css(BsDockApp *app) {
  g_autofree char *css = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->base_css_provider != NULL);

  css = bs_dock_metrics_build_css(&app->metrics);
#if GTK_CHECK_VERSION(4, 12, 0)
  gtk_css_provider_load_from_string(app->base_css_provider, css);
#else
  gtk_css_provider_load_from_data(app->base_css_provider, css, -1);
#endif
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
    gtk_layer_set_margin(app->window,
                         GTK_LAYER_SHELL_EDGE_BOTTOM,
                         app->metrics.bottom_margin_px);
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
  app->base_css_provider = gtk_css_provider_new();
  app->dynamic_css_provider = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(app->base_css_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(app->dynamic_css_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

  app->layout_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_overflow(app->layout_box, GTK_OVERFLOW_VISIBLE);
  gtk_widget_add_css_class(app->layout_box, "dock-layout");
  gtk_widget_set_halign(app->layout_box, GTK_ALIGN_CENTER);

  app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_overflow(app->root_box, GTK_OVERFLOW_VISIBLE);
  gtk_widget_add_css_class(app->root_box, "dock-root");
  gtk_widget_set_halign(app->root_box, GTK_ALIGN_CENTER);

  app->items_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, app->metrics.items_spacing_px);
  gtk_widget_set_overflow(app->items_box, GTK_OVERFLOW_VISIBLE);
  gtk_widget_add_css_class(app->items_box, "dock-items");
  gtk_widget_set_halign(app->items_box, GTK_ALIGN_CENTER);
  app->items_motion = gtk_event_controller_motion_new();
  gtk_event_controller_set_propagation_phase(app->items_motion, GTK_PHASE_CAPTURE);
  g_signal_connect(app->items_motion,
                   "enter",
                   G_CALLBACK(bs_dock_app_on_items_enter),
                   app);
  g_signal_connect(app->items_motion,
                   "motion",
                   G_CALLBACK(bs_dock_app_on_items_motion),
                   app);
  g_signal_connect(app->items_motion,
                   "leave",
                   G_CALLBACK(bs_dock_app_on_items_leave),
                   app);
  gtk_widget_add_controller(app->items_box, app->items_motion);

  app->status_label = gtk_label_new("Connecting to bit_shelld...");
  gtk_widget_add_css_class(app->status_label, "dock-status");
  gtk_widget_set_halign(app->status_label, GTK_ALIGN_CENTER);

  gtk_box_append(GTK_BOX(app->root_box), app->items_box);
  gtk_box_append(GTK_BOX(app->root_box), app->status_label);
  gtk_box_append(GTK_BOX(app->layout_box), app->root_box);
  gtk_window_set_child(app->window, app->layout_box);
  (void) bs_dock_app_update_root_box_top_margin(app, 0);
  bs_dock_app_update_items_box_margins(app, 0, 0);
}

static void
bs_dock_app_on_activate(GtkApplication *gtk_app, gpointer user_data) {
  BsDockApp *app = user_data;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(gtk_app != NULL);

  g_message("[bit_dock] activate");
  bs_dock_app_ensure_window(app);
  bs_dock_app_apply_css(app);
  gtk_window_present(app->window);

  if (!app->ipc_ready && !bs_dock_app_connect_ipc(app, &error)) {
    g_warning("[bit_dock] initial IPC connect failed: %s",
              error != NULL ? error->message : "unknown error");
    bs_dock_app_set_status(app, error != NULL ? error->message : "Failed to connect to bit_shelld");
    bs_dock_app_schedule_reconnect(app);
  }
}
