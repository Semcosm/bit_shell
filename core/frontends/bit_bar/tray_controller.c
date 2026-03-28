#include "frontends/bit_bar/tray_controller.h"

#include "frontends/bit_bar/tray_menu_view.h"

struct _BsBarTrayController {
  BsBarViewModel *view_model;
  BsBarTrayMenuBridge *menu_bridge;
  BsBarTrayControllerOps ops;
  char *pending_item_id;
  gboolean pending_open;
};

static void bs_bar_tray_controller_set_pending_open(BsBarTrayController *controller,
                                                    const char *item_id);
static void bs_bar_tray_controller_clear_pending_open(BsBarTrayController *controller);
static gboolean bs_bar_tray_controller_get_item_anchor(GtkWidget *button, int *out_x, int *out_y);
static gboolean bs_bar_tray_controller_get_popup_anchor(BsBarTrayController *controller,
                                                        GtkWidget *button,
                                                        BsBarPopupAnchor *out_anchor);
static GtkWidget *bs_bar_tray_controller_lookup_button(BsBarTrayController *controller,
                                                       const char *item_id);
static gboolean bs_bar_tray_controller_get_menu_tree_ready(const BsTrayMenuTree *tree);
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

static gboolean
bs_bar_tray_controller_get_item_anchor(GtkWidget *button, int *out_x, int *out_y) {
  GtkNative *native = NULL;
  GtkWidget *native_widget = NULL;
  graphene_point_t src = GRAPHENE_POINT_INIT_ZERO;
  graphene_point_t dest = GRAPHENE_POINT_INIT_ZERO;

  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(out_x != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  native = gtk_widget_get_native(button);
  if (native == NULL) {
    return FALSE;
  }

  native_widget = GTK_WIDGET(native);
  src.x = (float) gtk_widget_get_width(button) / 2.0f;
  src.y = (float) gtk_widget_get_height(button);
  if (!gtk_widget_compute_point(button, native_widget, &src, &dest)) {
    return FALSE;
  }

  *out_x = (int) dest.x;
  *out_y = (int) dest.y;
  return TRUE;
}

static gboolean
bs_bar_tray_controller_get_popup_anchor(BsBarTrayController *controller,
                                        GtkWidget *button,
                                        BsBarPopupAnchor *out_anchor) {
  g_return_val_if_fail(controller != NULL, FALSE);
  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(out_anchor != NULL, FALSE);

  *out_anchor = (BsBarPopupAnchor) {0};
  if (controller->ops.resolve_popup_anchor == NULL) {
    return FALSE;
  }

  if (controller->ops.resolve_popup_anchor(button, out_anchor, controller->ops.user_data)) {
    return TRUE;
  }

  out_anchor->width = MAX(gtk_widget_get_width(button), 1);
  out_anchor->height = MAX(gtk_widget_get_height(button), 1);
  return FALSE;
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
bs_bar_tray_controller_get_menu_tree_ready(const BsTrayMenuTree *tree) {
  return bs_bar_tray_menu_tree_has_visible_entries(tree);
}

static gboolean
bs_bar_tray_controller_present_menu(BsBarTrayController *controller,
                                    const char *item_id,
                                    GtkWidget *button,
                                    const BsTrayMenuTree *tree) {
  BsBarPopupAnchor popup_anchor = {0};
  GtkWidget *menu_view = NULL;
  gboolean anchor_ready = FALSE;

  g_return_val_if_fail(controller != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);
  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(tree != NULL, FALSE);

  anchor_ready = bs_bar_tray_controller_get_popup_anchor(controller, button, &popup_anchor);
  menu_view = bs_bar_tray_menu_view_new(tree,
                                        bs_bar_tray_controller_on_menu_item_activated,
                                        bs_bar_tray_controller_on_menu_close_requested,
                                        controller);
  if (menu_view == NULL) {
    g_debug("[bit_bar] tray controller refused empty menu item=%s revision=%u",
            item_id,
            tree->revision);
    bs_bar_tray_menu_bridge_clear_content(controller->menu_bridge);
    return FALSE;
  }

  if (!anchor_ready) {
    g_debug("[bit_bar] tray controller using fallback popup bounds item=%s size=%dx%d",
            item_id,
            popup_anchor.width,
            popup_anchor.height);
  }

  bs_bar_tray_controller_clear_pending_open(controller);
  if (bs_bar_tray_menu_bridge_is_open_for(controller->menu_bridge, item_id)) {
    bs_bar_tray_menu_bridge_set_content(controller->menu_bridge, menu_view);
    if (!bs_bar_tray_menu_bridge_open(controller->menu_bridge, item_id, &popup_anchor)) {
      g_debug("[bit_bar] tray controller failed to reopen menu item=%s", item_id);
      return FALSE;
    }
    return TRUE;
  }

  if (!bs_bar_tray_menu_bridge_present(controller->menu_bridge, item_id, &popup_anchor, menu_view)) {
    g_debug("[bit_bar] tray controller failed to present menu item=%s", item_id);
    return FALSE;
  }

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
bs_bar_tray_controller_new(GtkWidget *overlay_parent,
                           BsBarViewModel *view_model,
                           const BsBarTrayControllerOps *ops) {
  BsBarTrayController *controller = NULL;

  g_return_val_if_fail(GTK_IS_WIDGET(overlay_parent), NULL);
  g_return_val_if_fail(view_model != NULL, NULL);

  controller = g_new0(BsBarTrayController, 1);
  controller->view_model = view_model;
  controller->menu_bridge = bs_bar_tray_menu_bridge_new(overlay_parent);
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

  g_clear_pointer(&controller->pending_item_id, g_free);
  bs_bar_tray_menu_bridge_free(controller->menu_bridge);
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
    anchor_x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "bs-bar-tray-anchor-x"));
    anchor_y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "bs-bar-tray-anchor-y"));
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

  if (bs_bar_tray_menu_bridge_is_open_for(controller->menu_bridge, item_id)) {
    bs_bar_tray_controller_close(controller);
    return;
  }

  item = bs_bar_view_model_lookup_tray_item(controller->view_model, item_id);
  tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, item_id);
  has_menu_object = item != NULL
                    && item->menu_object_path != NULL
                    && *item->menu_object_path != '\0';

  if (bs_bar_tray_controller_get_menu_tree_ready(tree)) {
    bs_bar_tray_controller_present_menu(controller, item_id, button, tree);
    return;
  }

  bs_bar_tray_menu_bridge_close(controller->menu_bridge);
  if (has_menu_object) {
    bs_bar_tray_controller_set_pending_open(controller, item_id);
    if (controller->ops.request_menu_refresh != NULL) {
      controller->ops.request_menu_refresh(item_id, controller->ops.user_data);
    }
    return;
  }

  bs_bar_tray_controller_clear_pending_open(controller);
  if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
    anchor_x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "bs-bar-tray-anchor-x"));
    anchor_y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "bs-bar-tray-anchor-y"));
  }
  if (controller->ops.request_context_menu != NULL) {
    controller->ops.request_context_menu(item_id, anchor_x, anchor_y, controller->ops.user_data);
  }
}

void
bs_bar_tray_controller_sync_from_vm(BsBarTrayController *controller) {
  const char *open_item_id = NULL;
  const BsBarTrayItemView *open_item = NULL;
  const BsTrayMenuTree *open_tree = NULL;
  GtkWidget *button = NULL;

  g_return_if_fail(controller != NULL);

  open_item_id = bs_bar_tray_menu_bridge_open_item_id(controller->menu_bridge);
  if (open_item_id != NULL) {
    open_item = bs_bar_view_model_lookup_tray_item(controller->view_model, open_item_id);
    open_tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, open_item_id);
    if (open_item == NULL || !bs_bar_tray_controller_get_menu_tree_ready(open_tree)) {
      bs_bar_tray_menu_bridge_close(controller->menu_bridge);
    } else {
      button = bs_bar_tray_controller_lookup_button(controller, open_item_id);
      if (button == NULL || !bs_bar_tray_controller_present_menu(controller, open_item_id, button, open_tree)) {
        bs_bar_tray_menu_bridge_close(controller->menu_bridge);
      }
    }
  }

  if (!controller->pending_open || controller->pending_item_id == NULL) {
    return;
  }

  if (bs_bar_view_model_lookup_tray_item(controller->view_model, controller->pending_item_id) == NULL) {
    g_debug("[bit_bar] pending tray menu item disappeared item=%s", controller->pending_item_id);
    bs_bar_tray_controller_clear_pending_open(controller);
    return;
  }

  open_tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, controller->pending_item_id);
  if (!bs_bar_tray_controller_get_menu_tree_ready(open_tree)) {
    return;
  }

  button = bs_bar_tray_controller_lookup_button(controller, controller->pending_item_id);
  if (button == NULL) {
    g_debug("[bit_bar] pending tray menu lost button before present item=%s",
            controller->pending_item_id);
    bs_bar_tray_controller_clear_pending_open(controller);
    return;
  }

  bs_bar_tray_controller_present_menu(controller,
                                      controller->pending_item_id,
                                      button,
                                      open_tree);
}

void
bs_bar_tray_controller_handle_shell_reset(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_clear_pending_open(controller);
  bs_bar_tray_menu_bridge_handle_shell_reset(controller->menu_bridge);
}

void
bs_bar_tray_controller_close(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_clear_pending_open(controller);
  bs_bar_tray_menu_bridge_close(controller->menu_bridge);
}
