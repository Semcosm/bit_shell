#include "frontends/bit_bar/tray_menu_bridge.h"

struct _BsBarTrayMenuBridge {
  GtkWidget *overlay_parent;
  GtkWidget *popover;
  GtkWidget *shell_box;
  char *open_item_id;
  BsBarPopupAnchor anchor;
};

static void bs_bar_tray_menu_bridge_apply_anchor(BsBarTrayMenuBridge *bridge,
                                                 const BsBarPopupAnchor *anchor);
static void bs_bar_tray_menu_bridge_clear_open_state(BsBarTrayMenuBridge *bridge);
static void bs_bar_tray_menu_bridge_on_closed(GtkPopover *popover, gpointer user_data);
static gboolean bs_bar_tray_menu_bridge_on_key_pressed(GtkEventControllerKey *controller,
                                                       guint keyval,
                                                       guint keycode,
                                                       GdkModifierType state,
                                                       gpointer user_data);

static void
bs_bar_tray_menu_bridge_apply_anchor(BsBarTrayMenuBridge *bridge, const BsBarPopupAnchor *anchor) {
  GdkRectangle rect = {0};

  g_return_if_fail(bridge != NULL);
  g_return_if_fail(anchor != NULL);

  bridge->anchor = *anchor;
  rect.x = anchor->x;
  rect.y = anchor->y;
  rect.width = MAX(anchor->width, 1);
  rect.height = MAX(anchor->height, 1);
  gtk_popover_set_pointing_to(GTK_POPOVER(bridge->popover), &rect);
}

static void
bs_bar_tray_menu_bridge_clear_open_state(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  g_clear_pointer(&bridge->open_item_id, g_free);
  bridge->anchor = (BsBarPopupAnchor) {0};
}

static void
bs_bar_tray_menu_bridge_on_closed(GtkPopover *popover, gpointer user_data) {
  BsBarTrayMenuBridge *bridge = user_data;

  g_return_if_fail(GTK_IS_POPOVER(popover));
  g_return_if_fail(bridge != NULL);

  bs_bar_tray_menu_bridge_clear_open_state(bridge);
}

static gboolean
bs_bar_tray_menu_bridge_on_key_pressed(GtkEventControllerKey *controller,
                                       guint keyval,
                                       guint keycode,
                                       GdkModifierType state,
                                       gpointer user_data) {
  BsBarTrayMenuBridge *bridge = user_data;

  (void) controller;
  (void) keycode;
  (void) state;

  g_return_val_if_fail(bridge != NULL, FALSE);

  if (keyval != GDK_KEY_Escape) {
    return FALSE;
  }

  bs_bar_tray_menu_bridge_close(bridge);
  return TRUE;
}

BsBarTrayMenuBridge *
bs_bar_tray_menu_bridge_new(GtkWidget *overlay_parent) {
  BsBarTrayMenuBridge *bridge = NULL;
  GtkEventController *key_controller = NULL;

  g_return_val_if_fail(GTK_IS_WIDGET(overlay_parent), NULL);
  g_return_val_if_fail(GTK_IS_OVERLAY(overlay_parent), NULL);

  bridge = g_new0(BsBarTrayMenuBridge, 1);
  bridge->overlay_parent = overlay_parent;
  bridge->popover = gtk_popover_new();
  bridge->shell_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  gtk_widget_add_css_class(bridge->popover, "bit-bar-tray-menu-popover");
  gtk_widget_add_css_class(bridge->shell_box, "bit-bar-tray-menu-shell");
  gtk_widget_set_size_request(bridge->shell_box, 1, 1);
  gtk_widget_set_opacity(bridge->shell_box, 0.0);
  gtk_widget_set_focusable(bridge->popover, TRUE);
  gtk_popover_set_autohide(GTK_POPOVER(bridge->popover), true);
  gtk_popover_set_cascade_popdown(GTK_POPOVER(bridge->popover), true);
  gtk_popover_set_has_arrow(GTK_POPOVER(bridge->popover), false);
  gtk_popover_set_position(GTK_POPOVER(bridge->popover), GTK_POS_BOTTOM);
  gtk_popover_set_child(GTK_POPOVER(bridge->popover), bridge->shell_box);
  gtk_overlay_add_overlay(GTK_OVERLAY(bridge->overlay_parent), bridge->popover);
  gtk_widget_set_visible(bridge->popover, false);
  g_signal_connect(bridge->popover,
                   "closed",
                   G_CALLBACK(bs_bar_tray_menu_bridge_on_closed),
                   bridge);

  key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller,
                   "key-pressed",
                   G_CALLBACK(bs_bar_tray_menu_bridge_on_key_pressed),
                   bridge);
  gtk_widget_add_controller(bridge->popover, key_controller);
  return bridge;
}

void
bs_bar_tray_menu_bridge_free(BsBarTrayMenuBridge *bridge) {
  if (bridge == NULL) {
    return;
  }

  if (bridge->popover != NULL) {
    gtk_popover_popdown(GTK_POPOVER(bridge->popover));
    if (gtk_widget_get_parent(bridge->popover) != NULL) {
      gtk_widget_unparent(bridge->popover);
    }
  }

  g_clear_pointer(&bridge->open_item_id, g_free);
  g_free(bridge);
}

gboolean
bs_bar_tray_menu_bridge_open(BsBarTrayMenuBridge *bridge,
                             const char *item_id,
                             const BsBarPopupAnchor *anchor) {
  g_return_val_if_fail(bridge != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);
  g_return_val_if_fail(anchor != NULL, FALSE);

  if (bridge->open_item_id != NULL && g_strcmp0(bridge->open_item_id, item_id) != 0) {
    gtk_popover_popdown(GTK_POPOVER(bridge->popover));
  }

  bs_bar_tray_menu_bridge_apply_anchor(bridge, anchor);
  g_free(bridge->open_item_id);
  bridge->open_item_id = g_strdup(item_id);
  gtk_popover_popup(GTK_POPOVER(bridge->popover));
  gtk_widget_grab_focus(bridge->popover);
  return true;
}

void
bs_bar_tray_menu_bridge_close(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  if (bridge->popover != NULL) {
    gtk_popover_popdown(GTK_POPOVER(bridge->popover));
  }
  bs_bar_tray_menu_bridge_clear_open_state(bridge);
}

gboolean
bs_bar_tray_menu_bridge_toggle(BsBarTrayMenuBridge *bridge,
                               const char *item_id,
                               const BsBarPopupAnchor *anchor) {
  g_return_val_if_fail(bridge != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);
  g_return_val_if_fail(anchor != NULL, FALSE);

  if (bs_bar_tray_menu_bridge_is_open_for(bridge, item_id)) {
    bs_bar_tray_menu_bridge_close(bridge);
    return false;
  }

  return bs_bar_tray_menu_bridge_open(bridge, item_id, anchor);
}

gboolean
bs_bar_tray_menu_bridge_is_open_for(BsBarTrayMenuBridge *bridge, const char *item_id) {
  g_return_val_if_fail(bridge != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);

  return bridge->open_item_id != NULL
         && g_strcmp0(bridge->open_item_id, item_id) == 0
         && gtk_widget_get_visible(bridge->popover);
}

void
bs_bar_tray_menu_bridge_handle_shell_reset(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  bs_bar_tray_menu_bridge_close(bridge);
}
