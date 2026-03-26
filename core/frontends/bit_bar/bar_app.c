#include "frontends/bit_bar/bar_app.h"
#include "frontends/bit_bar/clock_widget.h"
#include "frontends/bit_bar/bar_view_model.h"
#include "frontends/common/ipc_client.h"

#include <glib.h>
#include <gtk4-layer-shell.h>

#define BS_BAR_APP_ID "io.bit_shell.bit_bar"
#define BS_BAR_NAMESPACE "bit-shell-bar"
#define BS_BAR_RECONNECT_DELAY_MS 2000U

typedef struct {
  int content_margin_x;
  int content_margin_y;
  int section_gap;
  int workspace_gap;
  int tray_gap;
  int trailing_cluster_gap;
  int title_list_gap;
  int title_min_height;
  int workspace_min_height;
  int tray_slot_size;
  int clock_min_width;
  int clock_button_height;
  int center_max_width_chars;
} BsBarMetrics;

struct _BsBarApp {
  GtkApplication *gtk_app;
  GtkWindow *window;
  GtkWidget *surface_box;
  GtkWidget *root_box;
  GtkWidget *content_box;
  GtkWidget *left_box;
  GtkWidget *workspace_strip_box;
  GtkWidget *center_box;
  GtkWidget *right_box;
  GtkWidget *tray_cluster;
  GtkWidget *clock_cluster;
  GtkWidget *title_button;
  GtkWidget *title_button_box;
  GtkWidget *title_app_badge;
  GtkWidget *title_label;
  GtkWidget *title_popover;
  GtkWidget *title_list_box;
  GtkWidget *tray_strip_box;
  GtkWidget *clock_button;
  GtkWidget *clock_popover;
  BsClockWidget *clock_widget;
  BsFrontendIpcClient *ipc_client;
  BsBarViewModel *view_model;
  BsBarConfig config;
  BsBarMetrics metrics;
  guint pending_dirty_flags;
  guint render_source_id;
  bool snapshot_in_flight;
  bool subscribe_sent;
  char *pending_workspace_switch_id;
  char *pending_focus_window_id;
};

static BsBarMetrics bs_bar_metrics_from_height(guint32 height_px);
static int bs_bar_app_fallback_window_width(void);
static void bs_bar_app_apply_bar_config(BsBarApp *app, const BsBarConfig *config);
static void bs_bar_app_apply_metrics(BsBarApp *app, const BsBarMetrics *metrics);
static void bs_bar_app_configure_window(BsBarApp *app);
static void bs_bar_app_ensure_window(BsBarApp *app);
static void bs_bar_app_apply_layout_from_vm(BsBarApp *app);
static void bs_bar_app_render_left_from_vm(BsBarApp *app);
static void bs_bar_app_render_center_from_vm(BsBarApp *app);
static void bs_bar_app_render_right_from_vm(BsBarApp *app);
static GtkWidget *bs_bar_app_build_loading_capsule(BsBarApp *app,
                                                   const char *css_role,
                                                   int min_width,
                                                   int min_height);
static GtkWidget *bs_bar_app_build_empty_capsule(BsBarApp *app,
                                                 const char *css_role,
                                                 int min_width,
                                                 int min_height);
static void bs_bar_app_compute_center_display(BsBarApp *app,
                                              const char **out_badge,
                                              const char **out_title,
                                              gboolean *out_interactive);
static void bs_bar_app_build_center_placeholder(BsBarApp *app,
                                                const char *badge,
                                                const char *text,
                                                bool interactive);
static GtkWidget *bs_bar_app_build_workspace_empty_placeholder(BsBarApp *app);
static GtkWidget *bs_bar_app_build_workspace_button(BsBarApp *app,
                                                    const BsBarWorkspaceStripItem *item);
static GtkWidget *bs_bar_app_build_window_candidate_row(BsBarApp *app,
                                                        const BsBarWindowCandidate *item);
static GtkWidget *bs_bar_app_build_tray_content(BsBarApp *app, const BsBarTrayItemView *item);
static void bs_bar_app_apply_tray_affordance_classes(GtkWidget *button,
                                                     const BsBarTrayItemView *item);
static GtkWidget *bs_bar_app_build_tray_item_widget(BsBarApp *app,
                                                    const BsBarTrayItemView *item);
static void bs_bar_app_request_switch_workspace(BsBarApp *app, const char *workspace_id);
static void bs_bar_app_request_focus_window(BsBarApp *app, const char *window_id);
static void bs_bar_app_request_tray_activate(BsBarApp *app,
                                             const char *item_id,
                                             int x,
                                             int y);
static void bs_bar_app_request_tray_context_menu(BsBarApp *app,
                                                 const char *item_id,
                                                 int x,
                                                 int y);
static bool bs_bar_app_get_tray_item_anchor(BsBarApp *app,
                                            GtkWidget *widget,
                                            int *out_x,
                                            int *out_y);
static void bs_bar_app_on_workspace_button_clicked(GtkButton *button, gpointer user_data);
static void bs_bar_app_on_title_button_clicked(GtkButton *button, gpointer user_data);
static void bs_bar_app_on_clock_button_clicked(GtkButton *button, gpointer user_data);
static void bs_bar_app_on_window_candidate_clicked(GtkButton *button, gpointer user_data);
static void bs_bar_app_on_tray_item_pressed(GtkGestureClick *gesture,
                                            gint n_press,
                                            gdouble x,
                                            gdouble y,
                                            gpointer user_data);
static void bs_bar_app_rebuild_title_popover(BsBarApp *app);
static void bs_bar_app_rebuild_clock_popover(BsBarApp *app);
static gboolean bs_bar_app_render_idle_cb(gpointer user_data);
static void bs_bar_app_schedule_render(BsBarApp *app, guint dirty_flags);
static void bs_bar_app_on_vm_changed(BsBarViewModel *vm,
                                     guint dirty_flags,
                                     gpointer user_data);
static void bs_bar_app_on_ipc_connected(BsFrontendIpcClient *client, gpointer user_data);
static void bs_bar_app_on_ipc_disconnected(BsFrontendIpcClient *client, gpointer user_data);
static void bs_bar_app_on_ipc_line(BsFrontendIpcClient *client,
                                   const char *line,
                                   gpointer user_data);
static void bs_bar_app_on_activate(GtkApplication *gtk_app, gpointer user_data);

BsBarApp *
bs_bar_app_new(void) {
  BsBarApp *app = g_new0(BsBarApp, 1);
  BsShellConfig shell_defaults = {0};
  BsFrontendIpcClientConfig ipc_config = {0};

  app->gtk_app = gtk_application_new(BS_BAR_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  bs_shell_config_init_defaults(&shell_defaults);
  app->config = shell_defaults.bar;
  app->metrics = bs_bar_metrics_from_height(app->config.height_px);
  bs_shell_config_clear(&shell_defaults);
  app->view_model = bs_bar_view_model_new();
  bs_bar_view_model_set_changed_cb(app->view_model, bs_bar_app_on_vm_changed, app);
  ipc_config.reconnect_delay_ms = BS_BAR_RECONNECT_DELAY_MS;
  ipc_config.on_connected = bs_bar_app_on_ipc_connected;
  ipc_config.on_disconnected = bs_bar_app_on_ipc_disconnected;
  ipc_config.on_line = bs_bar_app_on_ipc_line;
  ipc_config.user_data = app;
  app->ipc_client = bs_frontend_ipc_client_new(&ipc_config);

  g_signal_connect(app->gtk_app, "activate", G_CALLBACK(bs_bar_app_on_activate), app);
  return app;
}

void
bs_bar_app_free(BsBarApp *app) {
  if (app == NULL) {
    return;
  }

  if (app->render_source_id != 0) {
    g_source_remove(app->render_source_id);
  }
  g_clear_pointer(&app->pending_workspace_switch_id, g_free);
  g_clear_pointer(&app->pending_focus_window_id, g_free);
  bs_clock_widget_free(app->clock_widget);
  bs_frontend_ipc_client_free(app->ipc_client);
  bs_bar_view_model_free(app->view_model);
  g_clear_object(&app->gtk_app);
  g_free(app);
}

void
bs_bar_app_set_config(BsBarApp *app, const BsBarConfig *config) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(config != NULL);

  bs_bar_app_apply_bar_config(app, config);
}

void
bs_bar_app_get_config(BsBarApp *app, BsBarConfig *out_config) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(out_config != NULL);

  *out_config = app->config;
}

int
bs_bar_app_run(BsBarApp *app, int argc, char **argv) {
  g_return_val_if_fail(app != NULL, 1);
  return g_application_run(G_APPLICATION(app->gtk_app), argc, argv);
}

static void
bs_bar_app_apply_size_to_children(GtkWidget *container, int min_width, int min_height) {
  GtkWidget *child = NULL;

  g_return_if_fail(container != NULL);

  child = gtk_widget_get_first_child(container);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    const int child_width = min_width >= 0 && GTK_IS_BUTTON(child) ? min_width : -1;

    gtk_widget_set_size_request(child, child_width, min_height);
    child = next;
  }
}

static GtkWidget *
bs_bar_app_build_loading_capsule(BsBarApp *app, const char *css_role, int min_width, int min_height) {
  GtkWidget *box = NULL;
  GtkWidget *label = NULL;

  g_return_val_if_fail(app != NULL, NULL);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new(NULL);
  gtk_widget_add_css_class(box, "bit-bar-placeholder");
  gtk_widget_add_css_class(box, "bit-bar-placeholder-loading");
  if (css_role != NULL && *css_role != '\0') {
    gtk_widget_add_css_class(box, css_role);
  }
  gtk_widget_set_size_request(box, min_width, min_height);
  gtk_widget_set_opacity(box, 0.55);
  gtk_box_append(GTK_BOX(box), label);
  return box;
}

static GtkWidget *
bs_bar_app_build_empty_capsule(BsBarApp *app, const char *css_role, int min_width, int min_height) {
  GtkWidget *box = NULL;
  GtkWidget *label = NULL;

  g_return_val_if_fail(app != NULL, NULL);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new(NULL);
  gtk_widget_add_css_class(box, "bit-bar-placeholder");
  gtk_widget_add_css_class(box, "bit-bar-placeholder-empty");
  if (css_role != NULL && *css_role != '\0') {
    gtk_widget_add_css_class(box, css_role);
  }
  gtk_widget_set_size_request(box, min_width, min_height);
  gtk_widget_set_opacity(box, 0.22);
  gtk_box_append(GTK_BOX(box), label);
  return box;
}

static void
bs_bar_app_compute_center_display(BsBarApp *app,
                                  const char **out_badge,
                                  const char **out_title,
                                  gboolean *out_interactive) {
  BsBarVmCenterState state = BS_BAR_VM_CENTER_CONNECTING;
  const char *title = NULL;
  const char *app_name = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(out_badge != NULL);
  g_return_if_fail(out_title != NULL);
  g_return_if_fail(out_interactive != NULL);

  *out_badge = NULL;
  *out_title = "Connecting";
  *out_interactive = FALSE;

  state = bs_bar_view_model_center_state(app->view_model);
  if (state == BS_BAR_VM_CENTER_CONNECTING) {
    return;
  }
  if (state == BS_BAR_VM_CENTER_SYNCING_WINDOWS) {
    *out_title = "Syncing windows";
    return;
  }
  if (state == BS_BAR_VM_CENTER_READY_NO_FOCUSED_WINDOW) {
    *out_title = "No focused window";
    return;
  }

  title = bs_bar_view_model_focused_title(app->view_model);
  app_name = bs_bar_view_model_focused_app_name(app->view_model);
  *out_badge = app_name != NULL && *app_name != '\0' ? app_name : NULL;
  *out_title = title != NULL && *title != '\0' ? title : "No focused window";
  *out_interactive = bs_bar_view_model_can_open_window_list(app->view_model);
}

static void
bs_bar_app_build_center_placeholder(BsBarApp *app, const char *badge, const char *text, bool interactive) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->title_app_badge != NULL);
  g_return_if_fail(app->title_label != NULL);
  g_return_if_fail(app->title_button != NULL);

  if (badge != NULL && *badge != '\0') {
    gtk_label_set_text(GTK_LABEL(app->title_app_badge), badge);
    gtk_widget_set_visible(app->title_app_badge, true);
  } else {
    gtk_label_set_text(GTK_LABEL(app->title_app_badge), "");
    gtk_widget_set_visible(app->title_app_badge, false);
  }
  gtk_label_set_text(GTK_LABEL(app->title_label), text != NULL ? text : "");
  gtk_widget_set_sensitive(app->title_button, interactive);
  if (interactive) {
    gtk_widget_remove_css_class(app->title_button, "bit-bar-segment-disabled");
    gtk_widget_remove_css_class(app->title_label, "bit-bar-title-placeholder");
  } else {
    gtk_widget_add_css_class(app->title_button, "bit-bar-segment-disabled");
    gtk_widget_add_css_class(app->title_label, "bit-bar-title-placeholder");
    if (app->title_popover != NULL) {
      gtk_popover_popdown(GTK_POPOVER(app->title_popover));
    }
  }
}

static GtkWidget *
bs_bar_app_build_workspace_empty_placeholder(BsBarApp *app) {
  g_return_val_if_fail(app != NULL, NULL);

  return bs_bar_app_build_empty_capsule(app,
                                        "workspace-placeholder",
                                        MAX(app->metrics.workspace_min_height / 2, 10),
                                        app->metrics.workspace_min_height);
}

static GtkWidget *
bs_bar_app_build_tray_content(BsBarApp *app, const BsBarTrayItemView *item) {
  GtkWidget *box = NULL;
  GtkWidget *image = NULL;
  GtkWidget *label = NULL;
  GdkDisplay *display = NULL;
  GtkIconTheme *icon_theme = NULL;
  gboolean use_icon = FALSE;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(item != NULL, NULL);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(box, app->metrics.tray_slot_size, app->metrics.tray_slot_size);

  display = gdk_display_get_default();
  if (display != NULL && item->effective_icon_name != NULL && *item->effective_icon_name != '\0') {
    icon_theme = gtk_icon_theme_get_for_display(display);
    use_icon = icon_theme != NULL && gtk_icon_theme_has_icon(icon_theme, item->effective_icon_name);
  }

  if (use_icon) {
    image = gtk_image_new_from_icon_name(item->effective_icon_name);
    gtk_widget_add_css_class(image, "bit-bar-tray-icon");
    gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
    gtk_box_append(GTK_BOX(box), image);
    return box;
  }

  label = gtk_label_new(item->fallback_label != NULL ? item->fallback_label : "?");
  gtk_widget_add_css_class(label, "bit-bar-tray-fallback");
  gtk_label_set_single_line_mode(GTK_LABEL(label), true);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), label);
  return box;
}

static void
bs_bar_app_apply_tray_affordance_classes(GtkWidget *button, const BsBarTrayItemView *item) {
  g_return_if_fail(button != NULL);
  g_return_if_fail(item != NULL);

  gtk_widget_add_css_class(button, "bit-bar-tray-item");
  if (item->visual_state == BS_BAR_TRAY_VISUAL_ATTENTION) {
    gtk_widget_add_css_class(button, "needs-attention");
  } else if (item->visual_state == BS_BAR_TRAY_VISUAL_ACTIVE) {
    gtk_widget_add_css_class(button, "is-active");
  } else {
    gtk_widget_add_css_class(button, "is-passive");
  }
  if (item->primary_action == BS_BAR_TRAY_PRIMARY_ACTIVATE) {
    gtk_widget_add_css_class(button, "can-activate");
  }
  if (item->has_context_menu) {
    gtk_widget_add_css_class(button, "can-context-menu");
  }
  if (item->item_is_menu) {
    gtk_widget_add_css_class(button, "is-menu-item");
  }
  if (item->primary_action == BS_BAR_TRAY_PRIMARY_NONE && !item->has_context_menu) {
    gtk_widget_add_css_class(button, "is-disabled");
  }
}

static BsBarMetrics
bs_bar_metrics_from_height(guint32 height_px) {
  const int height = MAX((int) height_px, 20);
  BsBarMetrics metrics = {0};

  metrics.content_margin_y = CLAMP(height / 8, 2, 8);
  metrics.content_margin_x = CLAMP((height * 3) / 8, 8, 24);
  metrics.section_gap = CLAMP(height / 4, 4, 12);
  metrics.workspace_gap = CLAMP(height / 5, 4, 10);
  metrics.tray_gap = CLAMP(height / 6, 4, 8);
  metrics.trailing_cluster_gap = CLAMP(height / 5, 6, 12);
  metrics.title_list_gap = CLAMP(height / 8, 4, 8);
  metrics.title_min_height = MAX(height - (metrics.content_margin_y * 2), 18);
  metrics.workspace_min_height = MAX(height - (metrics.content_margin_y * 2), 18);
  metrics.tray_slot_size = MAX(height - (metrics.content_margin_y * 2), 16);
  metrics.clock_min_width = CLAMP((height * 5) / 2, 56, 120);
  metrics.clock_button_height = metrics.title_min_height;
  metrics.center_max_width_chars = CLAMP((height / 2) + 18, 18, 40);
  return metrics;
}

static void
bs_bar_app_apply_metrics(BsBarApp *app, const BsBarMetrics *metrics) {
  g_return_if_fail(app != NULL);
  g_return_if_fail(metrics != NULL);

  app->metrics = *metrics;

  if (app->root_box != NULL) {
    gtk_widget_set_margin_start(app->root_box, metrics->content_margin_x);
    gtk_widget_set_margin_end(app->root_box, metrics->content_margin_x);
    gtk_widget_set_margin_top(app->root_box, metrics->content_margin_y);
    gtk_widget_set_margin_bottom(app->root_box, metrics->content_margin_y);
  }
  if (app->left_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->left_box), metrics->section_gap);
    gtk_widget_set_margin_end(app->left_box, metrics->section_gap);
  }
  if (app->center_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->center_box), metrics->section_gap);
    gtk_widget_set_margin_start(app->center_box, metrics->section_gap);
    gtk_widget_set_margin_end(app->center_box, metrics->section_gap);
  }
  if (app->right_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->right_box), metrics->trailing_cluster_gap);
    gtk_widget_set_margin_start(app->right_box, metrics->section_gap);
  }
  if (app->tray_cluster != NULL) {
    gtk_widget_set_halign(app->tray_cluster, GTK_ALIGN_END);
    gtk_widget_set_valign(app->tray_cluster, GTK_ALIGN_CENTER);
  }
  if (app->clock_cluster != NULL) {
    gtk_widget_set_halign(app->clock_cluster, GTK_ALIGN_END);
    gtk_widget_set_valign(app->clock_cluster, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(app->clock_cluster,
                                metrics->clock_min_width,
                                metrics->clock_button_height);
  }
  if (app->workspace_strip_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->workspace_strip_box), metrics->workspace_gap);
    bs_bar_app_apply_size_to_children(app->workspace_strip_box, -1, metrics->workspace_min_height);
  }
  if (app->title_list_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->title_list_box), metrics->title_list_gap);
    bs_bar_app_apply_size_to_children(app->title_list_box, -1, metrics->title_min_height);
  }
  if (app->tray_strip_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->tray_strip_box), metrics->tray_gap);
    bs_bar_app_apply_size_to_children(app->tray_strip_box,
                                      metrics->tray_slot_size,
                                      metrics->tray_slot_size);
  }
  if (app->clock_button != NULL) {
    gtk_widget_set_size_request(app->clock_button,
                                metrics->clock_min_width,
                                metrics->clock_button_height);
  }
  if (app->title_button != NULL) {
    gtk_widget_set_size_request(app->title_button, -1, metrics->title_min_height);
    gtk_widget_set_hexpand(app->title_button, false);
  }
  if (app->title_button_box != NULL) {
    gtk_box_set_spacing(GTK_BOX(app->title_button_box), metrics->title_list_gap);
  }
  if (app->title_app_badge != NULL) {
    gtk_widget_set_size_request(app->title_app_badge, -1, metrics->title_min_height);
  }
  if (app->center_box != NULL) {
    gtk_widget_set_size_request(app->center_box, -1, metrics->title_min_height);
  }
  if (app->title_label != NULL) {
    gtk_label_set_max_width_chars(GTK_LABEL(app->title_label), metrics->center_max_width_chars);
  }
}

static void
bs_bar_app_apply_bar_config(BsBarApp *app, const BsBarConfig *config) {
  const gboolean layer_shell_supported = gtk_layer_is_supported();

  g_return_if_fail(app != NULL);
  g_return_if_fail(config != NULL);

  app->config = *config;
  app->metrics = bs_bar_metrics_from_height(app->config.height_px);

  if (app->window != NULL) {
    gtk_window_set_default_size(app->window,
                                layer_shell_supported ? 1 : bs_bar_app_fallback_window_width(),
                                (int) app->config.height_px);
    if (layer_shell_supported) {
      gtk_layer_set_exclusive_zone(app->window, (int) app->config.height_px);
    }
    bs_bar_app_apply_metrics(app, &app->metrics);
  }
}

static void
bs_bar_app_configure_window(BsBarApp *app) {
  const gboolean layer_shell_supported = gtk_layer_is_supported();

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->window != NULL);

  gtk_window_set_title(app->window, "bit_bar");
  gtk_window_set_decorated(app->window, false);
  gtk_window_set_resizable(app->window, false);
  gtk_window_set_default_size(app->window,
                              layer_shell_supported ? 1 : bs_bar_app_fallback_window_width(),
                              (int) app->config.height_px);
  g_message("[bit_bar] gtk_layer_is_supported=%d", layer_shell_supported ? 1 : 0);

  if (layer_shell_supported) {
    gtk_layer_init_for_window(app->window);
    gtk_layer_set_namespace(app->window, BS_BAR_NAMESPACE);
    gtk_layer_set_layer(app->window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_TOP, true);
    gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_LEFT, true);
    gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_RIGHT, true);
    gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_exclusive_zone(app->window, (int) app->config.height_px);
  } else {
    g_warning("[bit_bar] gtk-layer-shell unavailable; falling back to monitor-sized GTK window");
  }
}

static int
bs_bar_app_fallback_window_width(void) {
  GdkDisplay *display = NULL;
  GListModel *monitors = NULL;
  gpointer item = NULL;
  GdkRectangle geometry = {0};

  display = gdk_display_get_default();
  if (display == NULL) {
    return 1280;
  }

  monitors = gdk_display_get_monitors(display);
  if (monitors == NULL || g_list_model_get_n_items(monitors) == 0) {
    return 1280;
  }

  item = g_list_model_get_item(monitors, 0);
  if (item == NULL) {
    return 1280;
  }

  gdk_monitor_get_geometry(GDK_MONITOR(item), &geometry);
  g_object_unref(item);
  return geometry.width > 0 ? geometry.width : 1280;
}

static void
bs_bar_app_ensure_window(BsBarApp *app) {
  g_return_if_fail(app != NULL);

  if (app->window != NULL) {
    return;
  }

  app->window = GTK_WINDOW(gtk_application_window_new(app->gtk_app));
  bs_bar_app_configure_window(app);
  gtk_widget_add_css_class(GTK_WIDGET(app->window), "bit-bar-window");

  app->surface_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  app->content_box = gtk_center_box_new();
  gtk_widget_add_css_class(app->surface_box, "bit-bar-root");
  gtk_widget_add_css_class(app->root_box, "bit-bar-content");
  gtk_widget_set_hexpand(app->surface_box, true);
  gtk_widget_set_halign(app->surface_box, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(app->root_box, true);
  gtk_widget_set_halign(app->root_box, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(app->content_box, true);
  gtk_widget_set_halign(app->content_box, GTK_ALIGN_FILL);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(app->content_box), GTK_ORIENTATION_HORIZONTAL);

  app->left_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->workspace_strip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->tray_cluster = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->clock_cluster = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->title_button = gtk_button_new();
  app->title_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->title_app_badge = gtk_label_new(NULL);
  app->title_label = gtk_label_new("Connecting");
  app->title_popover = gtk_popover_new();
  app->title_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  app->tray_strip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  app->clock_button = gtk_button_new();
  app->clock_popover = gtk_popover_new();
  app->clock_widget = bs_clock_widget_new();

  gtk_widget_add_css_class(app->left_box, "bit-bar-left");
  gtk_widget_add_css_class(app->workspace_strip_box, "bit-bar-workspace-strip");
  gtk_widget_add_css_class(app->center_box, "bit-bar-center");
  gtk_widget_add_css_class(app->right_box, "bit-bar-right");
  gtk_widget_add_css_class(app->tray_cluster, "bit-bar-tray-cluster");
  gtk_widget_add_css_class(app->clock_cluster, "bit-bar-clock-cluster");
  gtk_widget_add_css_class(app->title_button, "bit-bar-title-button");
  gtk_widget_add_css_class(app->title_button_box, "bit-bar-title-content");
  gtk_widget_add_css_class(app->title_app_badge, "bit-bar-title-badge");
  gtk_widget_add_css_class(app->title_label, "bit-bar-title-label");
  gtk_widget_add_css_class(app->title_label, "bit-bar-title-text");
  gtk_widget_add_css_class(app->title_list_box, "bit-bar-title-list");
  gtk_widget_add_css_class(app->tray_strip_box, "bit-bar-tray-strip");
  gtk_widget_add_css_class(app->clock_button, "bit-bar-clock-button");
  gtk_widget_add_css_class(app->clock_popover, "bit-bar-clock-popover-surface");

  gtk_widget_set_halign(app->left_box, GTK_ALIGN_START);
  gtk_widget_set_halign(app->center_box, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(app->right_box, GTK_ALIGN_END);
  gtk_widget_set_hexpand(app->left_box, false);
  gtk_widget_set_hexpand(app->center_box, false);
  gtk_widget_set_hexpand(app->right_box, false);
  gtk_widget_set_hexpand(app->tray_cluster, false);
  gtk_widget_set_hexpand(app->clock_cluster, false);
  gtk_widget_set_hexpand(app->title_button_box, false);
  gtk_widget_set_halign(app->title_button_box, GTK_ALIGN_CENTER);
  gtk_label_set_ellipsize(GTK_LABEL(app->title_app_badge), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(app->title_app_badge), true);
  gtk_label_set_wrap(GTK_LABEL(app->title_app_badge), false);
  gtk_label_set_xalign(GTK_LABEL(app->title_app_badge), 0.5f);
  gtk_label_set_ellipsize(GTK_LABEL(app->title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(app->title_label), true);
  gtk_label_set_wrap(GTK_LABEL(app->title_label), false);
  gtk_label_set_xalign(GTK_LABEL(app->title_label), 0.5f);
  gtk_widget_set_hexpand(app->title_label, true);
  gtk_widget_set_halign(app->title_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(app->title_button_box), app->title_app_badge);
  gtk_box_append(GTK_BOX(app->title_button_box), app->title_label);
  gtk_button_set_child(GTK_BUTTON(app->title_button), app->title_button_box);

  gtk_widget_set_parent(app->title_popover, app->title_button);
  gtk_popover_set_position(GTK_POPOVER(app->title_popover), GTK_POS_BOTTOM);
  gtk_popover_set_autohide(GTK_POPOVER(app->title_popover), true);
  gtk_popover_set_child(GTK_POPOVER(app->title_popover), app->title_list_box);
  gtk_button_set_child(GTK_BUTTON(app->clock_button), bs_clock_widget_root(app->clock_widget));
  gtk_widget_set_parent(app->clock_popover, app->clock_button);
  gtk_popover_set_position(GTK_POPOVER(app->clock_popover), GTK_POS_BOTTOM);
  gtk_popover_set_autohide(GTK_POPOVER(app->clock_popover), true);
  g_signal_connect(app->title_button,
                   "clicked",
                   G_CALLBACK(bs_bar_app_on_title_button_clicked),
                   app);
  g_signal_connect(app->clock_button,
                   "clicked",
                   G_CALLBACK(bs_bar_app_on_clock_button_clicked),
                   app);

  gtk_box_append(GTK_BOX(app->left_box), app->workspace_strip_box);
  gtk_box_append(GTK_BOX(app->center_box), app->title_button);
  gtk_box_append(GTK_BOX(app->tray_cluster), app->tray_strip_box);
  gtk_box_append(GTK_BOX(app->clock_cluster), app->clock_button);
  gtk_box_append(GTK_BOX(app->right_box), app->tray_cluster);
  gtk_box_append(GTK_BOX(app->right_box), app->clock_cluster);

  gtk_center_box_set_start_widget(GTK_CENTER_BOX(app->content_box), app->left_box);
  gtk_center_box_set_center_widget(GTK_CENTER_BOX(app->content_box), app->center_box);
  gtk_center_box_set_end_widget(GTK_CENTER_BOX(app->content_box), app->right_box);
  gtk_box_append(GTK_BOX(app->surface_box), app->root_box);
  gtk_box_append(GTK_BOX(app->root_box), app->content_box);
  bs_bar_app_apply_metrics(app, &app->metrics);

  gtk_window_set_child(app->window, app->surface_box);
}

static void
bs_bar_app_apply_layout_from_vm(BsBarApp *app) {
  const BsBarConfig *config = NULL;

  g_return_if_fail(app != NULL);

  config = bs_bar_view_model_bar_config(app->view_model);
  if (config == NULL) {
    return;
  }

  bs_bar_app_apply_bar_config(app, config);
  if (app->left_box != NULL) {
    gtk_widget_set_visible(app->left_box, config->show_workspace_strip);
  }
  if (app->center_box != NULL) {
    gtk_widget_set_visible(app->center_box, config->show_focused_title);
  }
  if (app->tray_strip_box != NULL) {
    gtk_widget_set_visible(app->tray_strip_box, config->show_tray);
  }
  if (app->tray_cluster != NULL) {
    gtk_widget_set_visible(app->tray_cluster, config->show_tray);
  }
  if (app->clock_widget != NULL) {
    bs_clock_widget_set_visible_enabled(app->clock_widget, config->show_clock);
  }
  if (app->clock_cluster != NULL) {
    gtk_widget_set_visible(app->clock_cluster, config->show_clock);
  }
  if (!config->show_clock && app->clock_popover != NULL) {
    gtk_popover_popdown(GTK_POPOVER(app->clock_popover));
  }
  if (app->right_box != NULL) {
    gtk_widget_set_visible(app->right_box, config->show_tray || config->show_clock);
  }
}

static void
bs_bar_app_render_left_from_vm(BsBarApp *app) {
  GPtrArray *items = NULL;
  GtkWidget *child = NULL;
  BsBarVmWorkspaceStripState state = BS_BAR_VM_WORKSPACE_STRIP_LOADING;

  g_return_if_fail(app != NULL);

  items = bs_bar_view_model_workspace_items(app->view_model);
  state = bs_bar_view_model_workspace_strip_state(app->view_model);
  if (app->workspace_strip_box == NULL) {
    return;
  }

  child = gtk_widget_get_first_child(app->workspace_strip_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(app->workspace_strip_box), child);
    child = next;
  }

  if (state == BS_BAR_VM_WORKSPACE_STRIP_LOADING) {
    for (int i = 0; i < 3; i++) {
      GtkWidget *placeholder = bs_bar_app_build_loading_capsule(app,
                                                                "workspace-placeholder",
                                                                app->metrics.workspace_min_height,
                                                                app->metrics.workspace_min_height);

      gtk_box_append(GTK_BOX(app->workspace_strip_box), placeholder);
    }
    return;
  }
  if (state == BS_BAR_VM_WORKSPACE_STRIP_READY_EMPTY || items == NULL || items->len == 0) {
    GtkWidget *placeholder = bs_bar_app_build_workspace_empty_placeholder(app);

    gtk_box_append(GTK_BOX(app->workspace_strip_box), placeholder);
    return;
  }

  for (guint i = 0; i < items->len; i++) {
    const BsBarWorkspaceStripItem *item = g_ptr_array_index(items, i);
    GtkWidget *button = NULL;

    if (item == NULL) {
      continue;
    }

    button = bs_bar_app_build_workspace_button(app, item);
    gtk_box_append(GTK_BOX(app->workspace_strip_box), button);
  }
}

static void
bs_bar_app_render_center_from_vm(BsBarApp *app) {
  const char *badge = NULL;
  const char *title = NULL;
  gboolean interactive = FALSE;

  g_return_if_fail(app != NULL);

  if (app->title_button == NULL) {
    return;
  }

  bs_bar_app_compute_center_display(app, &badge, &title, &interactive);
  bs_bar_app_build_center_placeholder(app, badge, title, interactive);
  if (interactive && gtk_widget_get_visible(app->title_popover)) {
    bs_bar_app_rebuild_title_popover(app);
  }
}

static void
bs_bar_app_render_right_from_vm(BsBarApp *app) {
  GPtrArray *items = NULL;
  GtkWidget *child = NULL;
  BsBarVmTrayState state = BS_BAR_VM_TRAY_CONNECTING;

  g_return_if_fail(app != NULL);

  if (app->tray_strip_box == NULL) {
    return;
  }

  items = bs_bar_view_model_tray_items(app->view_model);
  state = bs_bar_view_model_tray_state(app->view_model);
  child = gtk_widget_get_first_child(app->tray_strip_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(app->tray_strip_box), child);
    child = next;
  }

  if (!bs_bar_view_model_show_tray(app->view_model)) {
    return;
  }
  if (state == BS_BAR_VM_TRAY_CONNECTING) {
    for (int i = 0; i < 2; i++) {
      GtkWidget *placeholder = bs_bar_app_build_loading_capsule(app,
                                                                "tray-placeholder",
                                                                app->metrics.tray_slot_size,
                                                                app->metrics.tray_slot_size);

      gtk_box_append(GTK_BOX(app->tray_strip_box), placeholder);
    }
    return;
  }
  if (state == BS_BAR_VM_TRAY_READY_EMPTY || items == NULL || items->len == 0) {
    GtkWidget *placeholder = bs_bar_app_build_empty_capsule(app,
                                                            "tray-placeholder",
                                                            MAX(app->metrics.tray_slot_size / 3, 8),
                                                            app->metrics.tray_slot_size);

    gtk_box_append(GTK_BOX(app->tray_strip_box), placeholder);
    return;
  }

  for (guint i = 0; i < items->len; i++) {
    const BsBarTrayItemView *item = g_ptr_array_index(items, i);
    GtkWidget *widget = NULL;

    if (item == NULL) {
      continue;
    }

    widget = bs_bar_app_build_tray_item_widget(app, item);
    gtk_box_append(GTK_BOX(app->tray_strip_box), widget);
  }
}

static GtkWidget *
bs_bar_app_build_workspace_button(BsBarApp *app, const BsBarWorkspaceStripItem *item) {
  GtkWidget *button = NULL;
  GtkWidget *content = NULL;
  GtkWidget *label = NULL;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(item != NULL, NULL);

  button = gtk_button_new();
  content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new(item->display_label != NULL ? item->display_label : "");
  gtk_widget_add_css_class(button, "bit-bar-workspace");
  gtk_widget_add_css_class(content, "bit-bar-workspace-content");
  gtk_widget_add_css_class(label, "bit-bar-workspace-label");
  if (item->focused) {
    gtk_widget_add_css_class(button, "is-focused");
  }
  if (item->empty) {
    gtk_widget_add_css_class(button, "is-empty");
  }
  if (item->presentation == BS_BAR_WORKSPACE_PRESENTATION_FULL) {
    gtk_widget_add_css_class(button, "is-full");
  } else if (item->presentation == BS_BAR_WORKSPACE_PRESENTATION_COMPACT) {
    gtk_widget_add_css_class(button, "is-compact");
  } else {
    gtk_widget_add_css_class(button, "is-minimal");
  }
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(label), true);
  gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
  gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(content), label);
  gtk_button_set_child(GTK_BUTTON(button), content);
  gtk_widget_set_size_request(button, -1, app->metrics.workspace_min_height);
  gtk_widget_set_tooltip_text(button,
                              item->tooltip_label != NULL && *item->tooltip_label != '\0'
                                ? item->tooltip_label
                                : item->id);
  g_object_set_data_full(G_OBJECT(button), "bs-bar-workspace-id", g_strdup(item->id), g_free);
  g_signal_connect(button,
                   "clicked",
                   G_CALLBACK(bs_bar_app_on_workspace_button_clicked),
                   app);
  return button;
}

static GtkWidget *
bs_bar_app_build_window_candidate_row(BsBarApp *app, const BsBarWindowCandidate *item) {
  GtkWidget *button = NULL;
  GtkWidget *content = NULL;
  GtkWidget *title_label = NULL;
  GtkWidget *subtitle_label = NULL;
  const char *title = NULL;
  const char *subtitle = NULL;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(item != NULL, NULL);

  title = item->title != NULL && *item->title != '\0' ? item->title : "Untitled window";
  subtitle = item->desktop_id != NULL && *item->desktop_id != '\0'
               ? item->desktop_id
               : (item->app_id != NULL ? item->app_id : "");
  button = gtk_button_new();
  content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  title_label = gtk_label_new(title);
  subtitle_label = gtk_label_new(subtitle);
  gtk_widget_add_css_class(button, "bit-bar-window-candidate");
  gtk_widget_add_css_class(content, "bit-bar-window-candidate-content");
  gtk_widget_add_css_class(title_label, "bit-bar-window-candidate-title");
  gtk_widget_add_css_class(subtitle_label, "bit-bar-window-candidate-subtitle");
  if (item->focused) {
    gtk_widget_add_css_class(button, "is-focused");
  }
  gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(title_label), true);
  gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(subtitle_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(subtitle_label), true);
  gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
  gtk_widget_set_halign(content, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(content, true);
  gtk_box_append(GTK_BOX(content), title_label);
  gtk_box_append(GTK_BOX(content), subtitle_label);
  gtk_button_set_child(GTK_BUTTON(button), content);
  gtk_widget_set_size_request(button, -1, app->metrics.title_min_height);
  g_object_set_data_full(G_OBJECT(button), "bs-bar-window-id", g_strdup(item->window_id), g_free);
  g_signal_connect(button,
                   "clicked",
                   G_CALLBACK(bs_bar_app_on_window_candidate_clicked),
                   app);
  return button;
}

static GtkWidget *
bs_bar_app_build_tray_item_widget(BsBarApp *app, const BsBarTrayItemView *item) {
  GtkWidget *button = NULL;
  GtkWidget *content = NULL;
  GtkGesture *gesture = NULL;

  g_return_val_if_fail(app != NULL, NULL);
  g_return_val_if_fail(item != NULL, NULL);

  button = gtk_button_new();
  content = bs_bar_app_build_tray_content(app, item);
  bs_bar_app_apply_tray_affordance_classes(button, item);
  gtk_button_set_child(GTK_BUTTON(button), content);
  gtk_widget_set_size_request(button, app->metrics.tray_slot_size, app->metrics.tray_slot_size);
  gtk_widget_set_tooltip_text(button,
                              item->title != NULL && *item->title != '\0' ? item->title : item->item_id);
  g_object_set_data_full(G_OBJECT(button), "bs-bar-tray-item-id", g_strdup(item->item_id), g_free);
  g_object_set_data(G_OBJECT(button),
                    "bs-bar-tray-primary-action",
                    GINT_TO_POINTER((gint) item->primary_action));
  g_object_set_data(G_OBJECT(button), "bs-bar-tray-has-context-menu", GINT_TO_POINTER(item->has_context_menu));

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
  g_signal_connect(gesture,
                   "pressed",
                   G_CALLBACK(bs_bar_app_on_tray_item_pressed),
                   app);
  gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(gesture));
  return button;
}

static void
bs_bar_app_request_switch_workspace(BsBarApp *app, const char *workspace_id) {
  const char *focused_workspace_id = NULL;
  g_autofree char *escaped_workspace_id = NULL;
  g_autofree char *request = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);

  if (workspace_id == NULL || *workspace_id == '\0') {
    return;
  }
  if (!bs_frontend_ipc_client_ready(app->ipc_client)) {
    return;
  }

  focused_workspace_id = bs_bar_view_model_focused_workspace_id(app->view_model);
  if (g_strcmp0(focused_workspace_id, workspace_id) == 0) {
    return;
  }
  if (g_strcmp0(app->pending_workspace_switch_id, workspace_id) == 0) {
    return;
  }

  escaped_workspace_id = g_strescape(workspace_id, NULL);
  request = g_strdup_printf("{\"op\":\"switch_workspace\",\"workspace_id\":\"%s\"}",
                            escaped_workspace_id != NULL ? escaped_workspace_id : "");
  if (!bs_frontend_ipc_client_send_line(app->ipc_client, request, &error)) {
    g_warning("[bit_bar] switch_workspace request failed: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_pointer(&app->pending_workspace_switch_id, g_free);
    return;
  }

  g_free(app->pending_workspace_switch_id);
  app->pending_workspace_switch_id = g_strdup(workspace_id);
}

static void
bs_bar_app_request_focus_window(BsBarApp *app, const char *window_id) {
  const char *focused_window_id = NULL;
  g_autofree char *escaped_window_id = NULL;
  g_autofree char *request = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);

  if (window_id == NULL || *window_id == '\0') {
    return;
  }
  if (!bs_frontend_ipc_client_ready(app->ipc_client)) {
    return;
  }

  focused_window_id = bs_bar_view_model_focused_window_id(app->view_model);
  if (g_strcmp0(focused_window_id, window_id) == 0) {
    return;
  }
  if (g_strcmp0(app->pending_focus_window_id, window_id) == 0) {
    return;
  }

  escaped_window_id = g_strescape(window_id, NULL);
  request = g_strdup_printf("{\"op\":\"focus_window\",\"window_id\":\"%s\"}",
                            escaped_window_id != NULL ? escaped_window_id : "");
  if (!bs_frontend_ipc_client_send_line(app->ipc_client, request, &error)) {
    g_warning("[bit_bar] focus_window request failed: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_pointer(&app->pending_focus_window_id, g_free);
    return;
  }

  g_free(app->pending_focus_window_id);
  app->pending_focus_window_id = g_strdup(window_id);
}

static void
bs_bar_app_request_tray_activate(BsBarApp *app, const char *item_id, int x, int y) {
  g_autofree char *escaped_item_id = NULL;
  g_autofree char *request = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);

  if (item_id == NULL || *item_id == '\0' || !bs_frontend_ipc_client_ready(app->ipc_client)) {
    return;
  }

  escaped_item_id = g_strescape(item_id, NULL);
  request = g_strdup_printf("{\"op\":\"tray_activate\",\"item_id\":\"%s\",\"x\":%d,\"y\":%d}",
                            escaped_item_id != NULL ? escaped_item_id : "",
                            x,
                            y);
  if (!bs_frontend_ipc_client_send_line(app->ipc_client, request, &error)) {
    g_warning("[bit_bar] tray_activate request failed: %s",
              error != NULL ? error->message : "unknown error");
  }
}

static void
bs_bar_app_request_tray_context_menu(BsBarApp *app, const char *item_id, int x, int y) {
  g_autofree char *escaped_item_id = NULL;
  g_autofree char *request = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);

  if (item_id == NULL || *item_id == '\0' || !bs_frontend_ipc_client_ready(app->ipc_client)) {
    return;
  }

  escaped_item_id = g_strescape(item_id, NULL);
  request = g_strdup_printf("{\"op\":\"tray_context_menu\",\"item_id\":\"%s\",\"x\":%d,\"y\":%d}",
                            escaped_item_id != NULL ? escaped_item_id : "",
                            x,
                            y);
  if (!bs_frontend_ipc_client_send_line(app->ipc_client, request, &error)) {
    g_warning("[bit_bar] tray_context_menu request failed: %s",
              error != NULL ? error->message : "unknown error");
  }
}

static bool
bs_bar_app_get_tray_item_anchor(BsBarApp *app, GtkWidget *widget, int *out_x, int *out_y) {
  GtkNative *native = NULL;
  GtkWidget *native_widget = NULL;
  graphene_point_t src = GRAPHENE_POINT_INIT_ZERO;
  graphene_point_t dest = GRAPHENE_POINT_INIT_ZERO;

  g_return_val_if_fail(app != NULL, false);
  g_return_val_if_fail(widget != NULL, false);
  g_return_val_if_fail(out_x != NULL, false);
  g_return_val_if_fail(out_y != NULL, false);

  native = gtk_widget_get_native(widget);
  if (native == NULL) {
    return false;
  }

  native_widget = GTK_WIDGET(native);
  src.x = (float) gtk_widget_get_width(widget) / 2.0f;
  src.y = (float) gtk_widget_get_height(widget);
  if (!gtk_widget_compute_point(widget, native_widget, &src, &dest)) {
    return false;
  }

  *out_x = (int) dest.x;
  *out_y = (int) dest.y;
  return true;
}

static void
bs_bar_app_on_workspace_button_clicked(GtkButton *button, gpointer user_data) {
  BsBarApp *app = user_data;
  const char *workspace_id = NULL;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(app != NULL);

  workspace_id = g_object_get_data(G_OBJECT(button), "bs-bar-workspace-id");
  bs_bar_app_request_switch_workspace(app, workspace_id);
}

static void
bs_bar_app_on_title_button_clicked(GtkButton *button, gpointer user_data) {
  BsBarApp *app = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->title_popover != NULL);

  if (!bs_bar_view_model_can_open_window_list(app->view_model)) {
    return;
  }

  bs_bar_app_rebuild_title_popover(app);
  gtk_popover_popup(GTK_POPOVER(app->title_popover));
}

static void
bs_bar_app_on_clock_button_clicked(GtkButton *button, gpointer user_data) {
  BsBarApp *app = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(app != NULL);
  g_return_if_fail(app->clock_popover != NULL);

  if (!app->config.show_clock) {
    return;
  }

  if (gtk_widget_get_visible(app->clock_popover)) {
    gtk_popover_popdown(GTK_POPOVER(app->clock_popover));
    return;
  }

  bs_bar_app_rebuild_clock_popover(app);
  gtk_popover_popup(GTK_POPOVER(app->clock_popover));
}

static void
bs_bar_app_on_window_candidate_clicked(GtkButton *button, gpointer user_data) {
  BsBarApp *app = user_data;
  const char *window_id = NULL;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(app != NULL);

  window_id = g_object_get_data(G_OBJECT(button), "bs-bar-window-id");
  bs_bar_app_request_focus_window(app, window_id);
  if (app->title_popover != NULL) {
    gtk_popover_popdown(GTK_POPOVER(app->title_popover));
  }
}

static void
bs_bar_app_on_tray_item_pressed(GtkGestureClick *gesture,
                                gint n_press,
                                gdouble x,
                                gdouble y,
                                gpointer user_data) {
  BsBarApp *app = user_data;
  GtkWidget *widget = NULL;
  const char *item_id = NULL;
  guint button = 0;
  BsBarTrayPrimaryAction primary_action = BS_BAR_TRAY_PRIMARY_NONE;
  bool has_context_menu = false;
  int anchor_x = 0;
  int anchor_y = 0;

  (void) n_press;
  (void) x;
  (void) y;

  g_return_if_fail(GTK_IS_GESTURE_CLICK(gesture));
  g_return_if_fail(app != NULL);

  widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  if (widget == NULL) {
    return;
  }

  item_id = g_object_get_data(G_OBJECT(widget), "bs-bar-tray-item-id");
  primary_action = (BsBarTrayPrimaryAction) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                                                              "bs-bar-tray-primary-action"));
  has_context_menu = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                                       "bs-bar-tray-has-context-menu")) != 0;
  button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  if (!bs_bar_app_get_tray_item_anchor(app, widget, &anchor_x, &anchor_y)) {
    anchor_x = 0;
    anchor_y = 0;
  }

  if (button == GDK_BUTTON_PRIMARY) {
    if (primary_action == BS_BAR_TRAY_PRIMARY_ACTIVATE) {
      bs_bar_app_request_tray_activate(app, item_id, anchor_x, anchor_y);
    } else if (primary_action == BS_BAR_TRAY_PRIMARY_MENU || has_context_menu) {
      bs_bar_app_request_tray_context_menu(app, item_id, anchor_x, anchor_y);
    }
  } else if (button == GDK_BUTTON_SECONDARY) {
    if (has_context_menu) {
      bs_bar_app_request_tray_context_menu(app, item_id, anchor_x, anchor_y);
    }
  }
}

static void
bs_bar_app_rebuild_title_popover(BsBarApp *app) {
  GPtrArray *items = NULL;
  GtkWidget *child = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->title_list_box != NULL);

  items = bs_bar_view_model_window_candidates(app->view_model);
  child = gtk_widget_get_first_child(app->title_list_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(app->title_list_box), child);
    child = next;
  }

  if (items == NULL || items->len == 0) {
    GtkWidget *placeholder = gtk_label_new("No windows on current workspace");

    gtk_box_append(GTK_BOX(app->title_list_box), placeholder);
    return;
  }

  for (guint i = 0; i < items->len; i++) {
    const BsBarWindowCandidate *item = g_ptr_array_index(items, i);
    GtkWidget *row = NULL;

    if (item == NULL) {
      continue;
    }
    row = bs_bar_app_build_window_candidate_row(app, item);
    gtk_box_append(GTK_BOX(app->title_list_box), row);
  }
}

static void
bs_bar_app_rebuild_clock_popover(BsBarApp *app) {
  GtkWidget *child = NULL;
  GtkWidget *content = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(app->clock_popover != NULL);
  g_return_if_fail(app->clock_widget != NULL);

  child = gtk_popover_get_child(GTK_POPOVER(app->clock_popover));
  if (child != NULL) {
    gtk_popover_set_child(GTK_POPOVER(app->clock_popover), NULL);
  }

  content = bs_clock_widget_build_popover_content(app->clock_widget);
  gtk_popover_set_child(GTK_POPOVER(app->clock_popover), content);
}

static gboolean
bs_bar_app_render_idle_cb(gpointer user_data) {
  BsBarApp *app = user_data;
  guint dirty_flags = 0;

  g_return_val_if_fail(app != NULL, G_SOURCE_REMOVE);

  app->render_source_id = 0;
  dirty_flags = app->pending_dirty_flags;
  app->pending_dirty_flags = 0;

  if ((dirty_flags & BS_BAR_VM_DIRTY_LAYOUT) != 0) {
    bs_bar_app_apply_layout_from_vm(app);
  }
  if ((dirty_flags & BS_BAR_VM_DIRTY_LEFT) != 0) {
    bs_bar_app_render_left_from_vm(app);
  }
  if ((dirty_flags & BS_BAR_VM_DIRTY_CENTER) != 0) {
    bs_bar_app_render_center_from_vm(app);
  }
  if ((dirty_flags & BS_BAR_VM_DIRTY_RIGHT) != 0) {
    bs_bar_app_render_right_from_vm(app);
  }

  return G_SOURCE_REMOVE;
}

static void
bs_bar_app_schedule_render(BsBarApp *app, guint dirty_flags) {
  g_return_if_fail(app != NULL);

  app->pending_dirty_flags |= dirty_flags;
  if (app->render_source_id != 0) {
    return;
  }

  app->render_source_id = g_idle_add(bs_bar_app_render_idle_cb, app);
}

static void
bs_bar_app_on_vm_changed(BsBarViewModel *vm, guint dirty_flags, gpointer user_data) {
  BsBarApp *app = user_data;

  g_return_if_fail(vm != NULL);
  g_return_if_fail(app != NULL);

  bs_bar_app_schedule_render(app, dirty_flags);
}

static void
bs_bar_app_on_ipc_connected(BsFrontendIpcClient *client, gpointer user_data) {
  BsBarApp *app = user_data;
  g_autofree char *snapshot_request = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(client != NULL);
  g_return_if_fail(app != NULL);

  app->snapshot_in_flight = true;
  app->subscribe_sent = false;
  bs_bar_view_model_reset_connection(app->view_model);
  snapshot_request = bs_bar_view_model_build_snapshot_request();
  if (!bs_frontend_ipc_client_send_line(client, snapshot_request, &error)) {
    g_warning("[bit_bar] initial snapshot request failed: %s",
              error != NULL ? error->message : "unknown error");
    bs_frontend_ipc_client_disconnect(client);
  }
}

static void
bs_bar_app_on_ipc_disconnected(BsFrontendIpcClient *client, gpointer user_data) {
  BsBarApp *app = user_data;

  g_return_if_fail(client != NULL);
  g_return_if_fail(app != NULL);

  app->snapshot_in_flight = false;
  app->subscribe_sent = false;
}

static void
bs_bar_app_on_ipc_line(BsFrontendIpcClient *client,
                       const char *line,
                       gpointer user_data) {
  BsBarApp *app = user_data;
  BsBarVmPhase phase = BS_BAR_VM_PHASE_DISCONNECTED;
  g_autofree char *subscribe_request = NULL;
  g_autofree char *snapshot_request = NULL;
  g_autoptr(GError) parse_error = NULL;
  g_autoptr(GError) send_error = NULL;

  g_return_if_fail(client != NULL);
  g_return_if_fail(line != NULL);
  g_return_if_fail(app != NULL);

  if (!bs_bar_view_model_consume_json_line(app->view_model, line, &parse_error)) {
    g_warning("[bit_bar] failed to parse IPC payload: %s",
              parse_error != NULL ? parse_error->message : "unknown error");
    return;
  }

  if (strstr(line, "\"kind\":\"error\"") != NULL) {
    g_clear_pointer(&app->pending_workspace_switch_id, g_free);
    g_clear_pointer(&app->pending_focus_window_id, g_free);
  }
  if (app->pending_workspace_switch_id != NULL
      && g_strcmp0(app->pending_workspace_switch_id,
                   bs_bar_view_model_focused_workspace_id(app->view_model)) == 0) {
    g_clear_pointer(&app->pending_workspace_switch_id, g_free);
  }
  if (app->pending_focus_window_id != NULL
      && g_strcmp0(app->pending_focus_window_id,
                   bs_bar_view_model_focused_window_id(app->view_model)) == 0) {
    g_clear_pointer(&app->pending_focus_window_id, g_free);
  }

  phase = bs_bar_view_model_phase(app->view_model);
  if (phase != BS_BAR_VM_PHASE_WAITING_SNAPSHOT) {
    app->snapshot_in_flight = false;
  }

  if (phase == BS_BAR_VM_PHASE_WAITING_SUBSCRIBE_ACK && !app->subscribe_sent) {
    subscribe_request = bs_bar_view_model_build_subscribe_request();
    if (!bs_frontend_ipc_client_send_line(client, subscribe_request, &send_error)) {
      g_warning("[bit_bar] subscribe request failed: %s",
                send_error != NULL ? send_error->message : "unknown error");
      bs_frontend_ipc_client_disconnect(client);
      return;
    }
    app->subscribe_sent = true;
  }

  if (bs_bar_view_model_needs_resnapshot(app->view_model) && !app->snapshot_in_flight) {
    snapshot_request = bs_bar_view_model_build_snapshot_request();
    if (!bs_frontend_ipc_client_send_line(client, snapshot_request, &send_error)) {
      g_warning("[bit_bar] resnapshot request failed: %s",
                send_error != NULL ? send_error->message : "unknown error");
      bs_frontend_ipc_client_disconnect(client);
      return;
    }
    app->snapshot_in_flight = true;
  }
}

static void
bs_bar_app_on_activate(GtkApplication *gtk_app, gpointer user_data) {
  BsBarApp *app = user_data;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(app != NULL);
  g_return_if_fail(gtk_app != NULL);

  bs_bar_app_ensure_window(app);
  gtk_window_present(app->window);
  bs_bar_app_schedule_render(app, BS_BAR_VM_DIRTY_ALL);
  if (app->clock_widget != NULL) {
    bs_clock_widget_start(app->clock_widget);
  }

  if (!bs_frontend_ipc_client_ready(app->ipc_client)
      && !bs_frontend_ipc_client_start(app->ipc_client, &error)) {
    g_warning("[bit_bar] initial IPC connect failed: %s",
              error != NULL ? error->message : "unknown error");
  }
}
