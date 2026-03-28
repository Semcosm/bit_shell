#include "frontends/bit_bar/tray_controller.h"

#include "frontends/bit_bar/tray_item_button.h"
#include "frontends/bit_bar/tray_menu_view.h"

struct _BsBarTrayController {
  BsBarViewModel *view_model;
  BsBarTrayControllerOps ops;
  char *open_item_id;
  char *pending_item_id;
  gboolean pending_open;
};

#define BS_BAR_TRAY_MENU_SAFE_EDGE 8
#define BS_BAR_TRAY_MENU_DEFAULT_MIN_WIDTH 220
#define BS_BAR_TRAY_MENU_DEFAULT_MAX_WIDTH 640
#define BS_BAR_TRAY_MENU_DEFAULT_MAX_HEIGHT 480

#ifndef BS_BAR_TRAY_CONTROLLER_COMPUTE_MENU_CONSTRAINTS
#define BS_BAR_TRAY_CONTROLLER_COMPUTE_MENU_CONSTRAINTS(button, out) \
  bs_bar_tray_controller_compute_menu_constraints((button), (out))
#endif

static void bs_bar_tray_controller_set_pending_open(BsBarTrayController *controller,
                                                    const char *item_id);
static void bs_bar_tray_controller_clear_pending_open(BsBarTrayController *controller);
static void bs_bar_tray_controller_set_open_item(BsBarTrayController *controller,
                                                 const char *item_id);
static void bs_bar_tray_controller_clear_open_item(BsBarTrayController *controller);
static BsBarTrayMenuSizeConstraints bs_bar_tray_menu_constraints_default(void);
static BsBarTrayMenuSizeConstraints bs_bar_tray_menu_compute_constraints_for_geometry(int monitor_width,
                                                                                      int monitor_height,
                                                                                      int anchor_y);
static GtkWidget *bs_bar_tray_controller_lookup_button(BsBarTrayController *controller,
                                                       const char *item_id);
static gboolean bs_bar_tray_controller_get_item_anchor(GtkWidget *button, int *out_x, int *out_y);
static gboolean bs_bar_tray_controller_get_menu_tree_ready(const BsTrayMenuTree *tree);
static gboolean bs_bar_tray_controller_compute_menu_constraints(GtkWidget *button,
                                                                BsBarTrayMenuSizeConstraints *out);
static void bs_bar_tray_controller_close_open_menu(BsBarTrayController *controller);
static gboolean bs_bar_tray_controller_present_menu(BsBarTrayController *controller,
                                                    const char *item_id,
                                                    GtkWidget *button,
                                                    const BsTrayMenuTree *tree);
static void bs_bar_tray_controller_on_menu_close_requested(gpointer user_data);
static void bs_bar_tray_controller_on_menu_item_activated(const char *item_id,
                                                          gint32 menu_item_id,
                                                          gpointer user_data);

static void
bs_bar_tray_controller_set_pending_open(BsBarTrayController *controller, const char *item_id) {
  g_return_if_fail(controller != NULL);
  g_return_if_fail(item_id != NULL);

  g_clear_pointer(&controller->pending_item_id, g_free);
  controller->pending_item_id = g_strdup(item_id);
  controller->pending_open = TRUE;
}

static void
bs_bar_tray_controller_clear_pending_open(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  g_clear_pointer(&controller->pending_item_id, g_free);
  controller->pending_open = FALSE;
}

static void
bs_bar_tray_controller_set_open_item(BsBarTrayController *controller, const char *item_id) {
  g_return_if_fail(controller != NULL);
  g_return_if_fail(item_id != NULL);

  g_clear_pointer(&controller->open_item_id, g_free);
  controller->open_item_id = g_strdup(item_id);
}

static void
bs_bar_tray_controller_clear_open_item(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  g_clear_pointer(&controller->open_item_id, g_free);
}

static BsBarTrayMenuSizeConstraints
bs_bar_tray_menu_constraints_default(void) {
  return (BsBarTrayMenuSizeConstraints) {
    .min_width = BS_BAR_TRAY_MENU_DEFAULT_MIN_WIDTH,
    .max_width = BS_BAR_TRAY_MENU_DEFAULT_MAX_WIDTH,
    .max_height = BS_BAR_TRAY_MENU_DEFAULT_MAX_HEIGHT,
  };
}

static BsBarTrayMenuSizeConstraints
bs_bar_tray_menu_compute_constraints_for_geometry(int monitor_width,
                                                  int monitor_height,
                                                  int anchor_y) {
  BsBarTrayMenuSizeConstraints constraints = bs_bar_tray_menu_constraints_default();
  const int max_monitor_width = monitor_width - BS_BAR_TRAY_MENU_SAFE_EDGE * 2;
  const int max_monitor_height = MAX(monitor_height - BS_BAR_TRAY_MENU_SAFE_EDGE * 2, 120);
  const int preferred_max_height = monitor_height - anchor_y - BS_BAR_TRAY_MENU_SAFE_EDGE;
  const int min_preferred_height = MIN(180, max_monitor_height);

  constraints.max_width = max_monitor_width;
  constraints.max_height = CLAMP(preferred_max_height,
                                 min_preferred_height,
                                 max_monitor_height);
  constraints.min_width = MAX(constraints.min_width, 160);
  constraints.max_width = MAX(constraints.max_width, constraints.min_width);
  constraints.max_height = MAX(constraints.max_height, 120);
  return constraints;
}

static GtkWidget *
bs_bar_tray_controller_lookup_button(BsBarTrayController *controller, const char *item_id) {
  g_return_val_if_fail(controller != NULL, NULL);
  g_return_val_if_fail(item_id != NULL, NULL);

  if (controller->ops.lookup_button == NULL) {
    return NULL;
  }

  return controller->ops.lookup_button(item_id, controller->ops.user_data);
}

static gboolean
bs_bar_tray_controller_get_item_anchor(GtkWidget *button, int *out_x, int *out_y) {
  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(out_x != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  return bs_bar_tray_item_button_get_anchor(button, out_x, out_y);
}

static gboolean
bs_bar_tray_controller_get_menu_tree_ready(const BsTrayMenuTree *tree) {
  return bs_bar_tray_menu_tree_has_visible_entries(tree);
}

static gboolean
bs_bar_tray_controller_compute_menu_constraints(GtkWidget *button,
                                                BsBarTrayMenuSizeConstraints *out) {
  GtkNative *native = NULL;
  GdkSurface *surface = NULL;
  GdkDisplay *display = NULL;
  GdkMonitor *monitor = NULL;
  GdkRectangle geometry = {0};
  double surface_y = 0.0;
  int anchor_x = 0;
  int anchor_y = 0;
  int monitor_anchor_y = 0;

  g_return_val_if_fail(out != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(button), FALSE);

  if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
    return FALSE;
  }
  (void) anchor_x;

  native = gtk_widget_get_native(button);
  if (native == NULL) {
    return FALSE;
  }

  surface = gtk_native_get_surface(native);
  if (surface == NULL) {
    return FALSE;
  }

  display = gdk_surface_get_display(surface);
  if (display == NULL) {
    return FALSE;
  }

  monitor = gdk_display_get_monitor_at_surface(display, surface);
  if (monitor == NULL) {
    return FALSE;
  }

  gdk_monitor_get_geometry(monitor, &geometry);
  gtk_native_get_surface_transform(native, NULL, &surface_y);
  monitor_anchor_y = anchor_y + (int) surface_y;
  *out = bs_bar_tray_menu_compute_constraints_for_geometry(geometry.width,
                                                           geometry.height,
                                                           monitor_anchor_y);
  return TRUE;
}

static void
bs_bar_tray_controller_close_open_menu(BsBarTrayController *controller) {
  GtkWidget *button = NULL;

  g_return_if_fail(controller != NULL);

  if (controller->open_item_id != NULL) {
    button = bs_bar_tray_controller_lookup_button(controller, controller->open_item_id);
    if (button != NULL) {
      bs_bar_tray_item_button_close_menu(button);
    }
  }
  bs_bar_tray_controller_clear_open_item(controller);
}

static gboolean
bs_bar_tray_controller_present_menu(BsBarTrayController *controller,
                                    const char *item_id,
                                    GtkWidget *button,
                                    const BsTrayMenuTree *tree) {
  GtkWidget *menu_view = NULL;
  BsBarTrayMenuSizeConstraints constraints = {0};

  g_return_val_if_fail(controller != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);
  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(tree != NULL, FALSE);

  if (!BS_BAR_TRAY_CONTROLLER_COMPUTE_MENU_CONSTRAINTS(button, &constraints)) {
    constraints = bs_bar_tray_menu_constraints_default();
  }

  menu_view = bs_bar_tray_menu_view_new(tree,
                                        &constraints,
                                        bs_bar_tray_controller_on_menu_item_activated,
                                        bs_bar_tray_controller_on_menu_close_requested,
                                        controller);
  if (menu_view == NULL) {
    return FALSE;
  }

  if (controller->open_item_id != NULL && g_strcmp0(controller->open_item_id, item_id) != 0) {
    bs_bar_tray_controller_close_open_menu(controller);
  }

  if (!bs_bar_tray_item_button_present_menu(button, menu_view)) {
    return FALSE;
  }

  bs_bar_tray_controller_set_open_item(controller, item_id);
  bs_bar_tray_controller_clear_pending_open(controller);
  return TRUE;
}

static void
bs_bar_tray_controller_on_menu_close_requested(gpointer user_data) {
  BsBarTrayController *controller = user_data;

  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_close(controller);
}

static void
bs_bar_tray_controller_on_menu_item_activated(const char *item_id,
                                              gint32 menu_item_id,
                                              gpointer user_data) {
  BsBarTrayController *controller = user_data;

  g_return_if_fail(controller != NULL);
  g_return_if_fail(item_id != NULL);

  bs_bar_tray_controller_close(controller);
  if (controller->ops.request_menu_activate != NULL) {
    controller->ops.request_menu_activate(item_id,
                                          menu_item_id,
                                          controller->ops.user_data);
  }
}

BsBarTrayController *
bs_bar_tray_controller_new(BsBarViewModel *view_model, const BsBarTrayControllerOps *ops) {
  BsBarTrayController *controller = NULL;

  g_return_val_if_fail(view_model != NULL, NULL);

  controller = g_new0(BsBarTrayController, 1);
  controller->view_model = view_model;
  if (ops != NULL) {
    controller->ops = *ops;
  }
  return controller;
}

void
bs_bar_tray_controller_free(BsBarTrayController *controller) {
  if (controller == NULL) {
    return;
  }

  bs_bar_tray_controller_close(controller);
  g_free(controller);
}

void
bs_bar_tray_controller_handle_activate(BsBarTrayController *controller,
                                       GtkWidget *button,
                                       const char *item_id) {
  int anchor_x = 0;
  int anchor_y = 0;

  g_return_if_fail(controller != NULL);
  g_return_if_fail(button != NULL);
  g_return_if_fail(item_id != NULL);

  if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
    anchor_x = 0;
    anchor_y = 0;
  }

  bs_bar_tray_controller_close(controller);
  if (controller->ops.request_activate != NULL) {
    controller->ops.request_activate(item_id, anchor_x, anchor_y, controller->ops.user_data);
  }
}

void
bs_bar_tray_controller_handle_menu(BsBarTrayController *controller,
                                   GtkWidget *button,
                                   const char *item_id) {
  const BsBarTrayItemView *item = NULL;
  const BsTrayMenuTree *tree = NULL;
  gboolean has_menu_object = FALSE;
  int anchor_x = 0;
  int anchor_y = 0;

  g_return_if_fail(controller != NULL);
  g_return_if_fail(button != NULL);
  g_return_if_fail(item_id != NULL);

  if (controller->open_item_id != NULL && g_strcmp0(controller->open_item_id, item_id) == 0) {
    bs_bar_tray_item_button_close_menu(button);
    bs_bar_tray_controller_clear_open_item(controller);
    bs_bar_tray_controller_clear_pending_open(controller);
    return;
  }

  item = bs_bar_view_model_lookup_tray_item(controller->view_model, item_id);
  tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, item_id);
  has_menu_object = item != NULL
                    && item->menu_object_path != NULL
                    && *item->menu_object_path != '\0';

  if (bs_bar_tray_controller_get_menu_tree_ready(tree)) {
    bs_bar_tray_controller_clear_pending_open(controller);
    if (!bs_bar_tray_controller_present_menu(controller, item_id, button, tree)
        && controller->ops.request_context_menu != NULL) {
      if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
        anchor_x = 0;
        anchor_y = 0;
      }
      controller->ops.request_context_menu(item_id,
                                           anchor_x,
                                           anchor_y,
                                           controller->ops.user_data);
    }
    return;
  }

  bs_bar_tray_controller_close_open_menu(controller);
  if (has_menu_object) {
    bs_bar_tray_controller_set_pending_open(controller, item_id);
    if (controller->ops.request_menu_refresh != NULL) {
      controller->ops.request_menu_refresh(item_id, controller->ops.user_data);
    }
    return;
  }

  bs_bar_tray_controller_clear_pending_open(controller);
  if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
    anchor_x = 0;
    anchor_y = 0;
  }
  if (controller->ops.request_context_menu != NULL) {
    controller->ops.request_context_menu(item_id, anchor_x, anchor_y, controller->ops.user_data);
  }
}

void
bs_bar_tray_controller_handle_item_menu_closed(BsBarTrayController *controller, const char *item_id) {
  g_return_if_fail(controller != NULL);
  g_return_if_fail(item_id != NULL);

  if (controller->open_item_id != NULL && g_strcmp0(controller->open_item_id, item_id) == 0) {
    bs_bar_tray_controller_clear_open_item(controller);
  }
}

void
bs_bar_tray_controller_sync_from_vm(BsBarTrayController *controller) {
  const BsBarTrayItemView *item = NULL;
  const BsTrayMenuTree *tree = NULL;
  GtkWidget *button = NULL;

  g_return_if_fail(controller != NULL);

  if (controller->open_item_id != NULL) {
    item = bs_bar_view_model_lookup_tray_item(controller->view_model, controller->open_item_id);
    button = bs_bar_tray_controller_lookup_button(controller, controller->open_item_id);
    tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, controller->open_item_id);
    if (item == NULL || button == NULL) {
      if (button != NULL) {
        bs_bar_tray_item_button_close_menu(button);
      }
      bs_bar_tray_controller_clear_open_item(controller);
    } else if (!bs_bar_tray_controller_get_menu_tree_ready(tree)) {
      bs_bar_tray_item_button_close_menu(button);
      bs_bar_tray_controller_clear_open_item(controller);
    } else if (!bs_bar_tray_controller_present_menu(controller,
                                                    controller->open_item_id,
                                                    button,
                                                    tree)) {
      bs_bar_tray_item_button_close_menu(button);
      bs_bar_tray_controller_clear_open_item(controller);
    }
  }

  if (!controller->pending_open || controller->pending_item_id == NULL) {
    return;
  }

  item = bs_bar_view_model_lookup_tray_item(controller->view_model, controller->pending_item_id);
  if (item == NULL) {
    bs_bar_tray_controller_clear_pending_open(controller);
    return;
  }

  tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, controller->pending_item_id);
  if (!bs_bar_tray_controller_get_menu_tree_ready(tree)) {
    return;
  }

  button = bs_bar_tray_controller_lookup_button(controller, controller->pending_item_id);
  if (button == NULL
      || !bs_bar_tray_controller_present_menu(controller,
                                              controller->pending_item_id,
                                              button,
                                              tree)) {
    bs_bar_tray_controller_clear_pending_open(controller);
  }
}

void
bs_bar_tray_controller_handle_shell_reset(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_close(controller);
}

void
bs_bar_tray_controller_close(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_close_open_menu(controller);
  bs_bar_tray_controller_clear_pending_open(controller);
}
