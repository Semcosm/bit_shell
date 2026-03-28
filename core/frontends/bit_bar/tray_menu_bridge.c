#include "frontends/bit_bar/tray_menu_bridge.h"

struct _BsBarTrayMenuBridge {
  GtkWidget *overlay_parent;
  GtkWidget *popover;
  GtkWidget *shell_box;
  GtkWidget *content_widget;
  gboolean has_real_content;
  char *open_item_id;
  BsBarPopupAnchor anchor;
};

static void bs_bar_tray_menu_bridge_apply_anchor(BsBarTrayMenuBridge *bridge,
                                                 const BsBarPopupAnchor *anchor);
static void bs_bar_tray_menu_bridge_measure_content(BsBarTrayMenuBridge *bridge,
                                                    int *out_width,
                                                    int *out_height);
static void bs_bar_tray_menu_bridge_clear_open_state(BsBarTrayMenuBridge *bridge);
static void bs_bar_tray_menu_bridge_on_closed(GtkPopover *popover, gpointer user_data);
static gboolean bs_bar_tray_menu_bridge_on_key_pressed(GtkEventControllerKey *controller,
                                                       guint keyval,
                                                       guint keycode,
                                                       GdkModifierType state,
                                                       gpointer user_data);

static void
bs_bar_tray_menu_bridge_apply_anchor(BsBarTrayMenuBridge *bridge, const BsBarPopupAnchor *anchor) {
  BsBarPopupPlacement placement = {0};
  GdkRectangle rect = {0};
  int menu_width = 0;
  int menu_height = 0;

  g_return_if_fail(bridge != NULL);
  g_return_if_fail(anchor != NULL);

  bs_bar_tray_menu_bridge_measure_content(bridge, &menu_width, &menu_height);
  placement = bs_bar_popup_compute_placement(anchor, menu_width, menu_height);
  bridge->anchor = *anchor;
  rect.x = anchor->local_x;
  rect.y = anchor->local_y;
  rect.width = MAX(anchor->width, 1);
  rect.height = MAX(anchor->height, 1);
  g_debug("[bit_bar] tray menu anchor local_rect=(%d,%d %dx%d) bounds=(%d,%d %dx%d) placement=(%d,%d %dx%d) side=%s",
          rect.x,
          rect.y,
          rect.width,
          rect.height,
          anchor->monitor_x,
          anchor->monitor_y,
          anchor->monitor_width,
          anchor->monitor_height,
          placement.x,
          placement.y,
          placement.width,
          placement.height,
          placement.side == BS_BAR_POPUP_SIDE_TOP ? "top" : "bottom");
  gtk_popover_set_position(GTK_POPOVER(bridge->popover),
                           placement.side == BS_BAR_POPUP_SIDE_TOP ? GTK_POS_TOP
                                                                   : GTK_POS_BOTTOM);
  gtk_popover_set_pointing_to(GTK_POPOVER(bridge->popover), &rect);
  gtk_widget_set_halign(bridge->popover, GTK_ALIGN_START);
  gtk_widget_set_valign(bridge->popover, GTK_ALIGN_START);
  gtk_widget_set_margin_start(bridge->popover, placement.x);
  gtk_widget_set_margin_top(bridge->popover, placement.y);
  gtk_widget_set_size_request(bridge->popover,
                              MAX(placement.width, 1),
                              MAX(placement.height, 1));
}

static void
bs_bar_tray_menu_bridge_measure_content(BsBarTrayMenuBridge *bridge, int *out_width, int *out_height) {
  GtkWidget *widget = NULL;
  int min_width = 0;
  int nat_width = 0;
  int min_height = 0;
  int nat_height = 0;
  int width = 0;

  g_return_if_fail(bridge != NULL);
  g_return_if_fail(out_width != NULL);
  g_return_if_fail(out_height != NULL);

  *out_width = 1;
  *out_height = 1;
  widget = bridge->content_widget != NULL ? bridge->content_widget : bridge->shell_box;
  if (widget == NULL) {
    return;
  }

  gtk_widget_measure(widget,
                     GTK_ORIENTATION_HORIZONTAL,
                     -1,
                     &min_width,
                     &nat_width,
                     NULL,
                     NULL);
  width = MAX(MAX(min_width, nat_width), 1);
  gtk_widget_measure(widget,
                     GTK_ORIENTATION_VERTICAL,
                     width,
                     &min_height,
                     &nat_height,
                     NULL,
                     NULL);
  *out_width = width;
  *out_height = MAX(MAX(min_height, nat_height), 1);
}

static void
bs_bar_tray_menu_bridge_clear_open_state(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  g_clear_pointer(&bridge->open_item_id, g_free);
  bridge->anchor = (BsBarPopupAnchor) {0};
}

void
bs_bar_tray_menu_bridge_set_content(BsBarTrayMenuBridge *bridge, GtkWidget *content) {
  GtkWidget *child = NULL;

  g_return_if_fail(bridge != NULL);

  child = gtk_widget_get_first_child(bridge->shell_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(bridge->shell_box), child);
    child = next;
  }

  bridge->content_widget = content;
  bridge->has_real_content = content != NULL;
  gtk_widget_set_size_request(bridge->shell_box, content != NULL ? -1 : 1, content != NULL ? -1 : 1);
  gtk_widget_set_opacity(bridge->shell_box, content != NULL ? 1.0 : 0.0);
  if (content != NULL) {
    gtk_box_append(GTK_BOX(bridge->shell_box), content);
  }
}

void
bs_bar_tray_menu_bridge_clear_content(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  bs_bar_tray_menu_bridge_set_content(bridge, NULL);
}

static void
bs_bar_tray_menu_bridge_on_closed(GtkPopover *popover, gpointer user_data) {
  BsBarTrayMenuBridge *bridge = user_data;

  g_return_if_fail(GTK_IS_POPOVER(popover));
  g_return_if_fail(bridge != NULL);

  bs_bar_tray_menu_bridge_clear_content(bridge);
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
  gtk_widget_set_halign(bridge->popover, GTK_ALIGN_START);
  gtk_widget_set_valign(bridge->popover, GTK_ALIGN_START);
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

  bs_bar_tray_menu_bridge_clear_content(bridge);
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

  if (!bridge->has_real_content || bridge->content_widget == NULL) {
    g_debug("[bit_bar] tray menu bridge refused to open item=%s without real content has_real_content=%d content=%p",
            item_id,
            bridge->has_real_content ? 1 : 0,
            bridge->content_widget);
    return FALSE;
  }

  bs_bar_tray_menu_bridge_apply_anchor(bridge, anchor);
  g_free(bridge->open_item_id);
  bridge->open_item_id = g_strdup(item_id);
  gtk_popover_popup(GTK_POPOVER(bridge->popover));
  gtk_widget_grab_focus(bridge->content_widget != NULL ? bridge->content_widget : bridge->popover);
  return true;
}

gboolean
bs_bar_tray_menu_bridge_present(BsBarTrayMenuBridge *bridge,
                                const char *item_id,
                                const BsBarPopupAnchor *anchor,
                                GtkWidget *content) {
  g_return_val_if_fail(bridge != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);
  g_return_val_if_fail(anchor != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(content), FALSE);

  if (bs_bar_tray_menu_bridge_is_open_for(bridge, item_id)) {
    bs_bar_tray_menu_bridge_close(bridge);
    return FALSE;
  }

  bs_bar_tray_menu_bridge_set_content(bridge, content);
  return bs_bar_tray_menu_bridge_open(bridge, item_id, anchor);
}

void
bs_bar_tray_menu_bridge_close(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  if (bridge->popover != NULL) {
    gtk_popover_popdown(GTK_POPOVER(bridge->popover));
  }
  bs_bar_tray_menu_bridge_clear_content(bridge);
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

  if (!bridge->has_real_content || bridge->content_widget == NULL) {
    g_debug("[bit_bar] tray menu bridge refused to toggle item=%s without real content has_real_content=%d content=%p",
            item_id,
            bridge->has_real_content ? 1 : 0,
            bridge->content_widget);
    return FALSE;
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

const char *
bs_bar_tray_menu_bridge_open_item_id(BsBarTrayMenuBridge *bridge) {
  g_return_val_if_fail(bridge != NULL, NULL);
  return bridge->open_item_id;
}

void
bs_bar_tray_menu_bridge_handle_shell_reset(BsBarTrayMenuBridge *bridge) {
  g_return_if_fail(bridge != NULL);

  bs_bar_tray_menu_bridge_close(bridge);
}

BsBarPopupPlacement
bs_bar_popup_compute_placement(const BsBarPopupAnchor *anchor, int menu_width, int menu_height) {
  BsBarPopupPlacement placement = {0};
  int monitor_x = 0;
  int monitor_y = 0;
  int monitor_width = 0;
  int monitor_height = 0;
  int anchor_x = 0;
  int anchor_y = 0;
  int anchor_width = 0;
  int anchor_height = 0;
  int space_above = 0;
  int space_below = 0;
  int desired_x = 0;
  int desired_y = 0;
  int max_x = 0;
  int max_y = 0;

  g_return_val_if_fail(anchor != NULL, placement);

  anchor_x = anchor->local_x;
  anchor_y = anchor->local_y;
  anchor_width = MAX(anchor->width, 1);
  anchor_height = MAX(anchor->height, 1);
  monitor_x = anchor->monitor_x;
  monitor_y = anchor->monitor_y;
  monitor_width = anchor->monitor_width > 0 ? anchor->monitor_width : anchor_x + MAX(menu_width, anchor_width);
  monitor_height = anchor->monitor_height > 0 ? anchor->monitor_height : anchor_y + anchor_height + MAX(menu_height, 1);

  placement.width = CLAMP(MAX(menu_width, anchor_width), 1, MAX(monitor_width, 1));
  placement.height = CLAMP(MAX(menu_height, 1), 1, MAX(monitor_height, 1));
  space_above = anchor_y - monitor_y;
  space_below = (monitor_y + monitor_height) - (anchor_y + anchor_height);
  placement.side = BS_BAR_POPUP_SIDE_BOTTOM;
  if (placement.height > space_below && space_above > space_below) {
    placement.side = BS_BAR_POPUP_SIDE_TOP;
  }

  desired_x = anchor_x + (anchor_width / 2) - (placement.width / 2);
  max_x = monitor_x + monitor_width - placement.width;
  placement.x = CLAMP(desired_x, monitor_x, MAX(max_x, monitor_x));

  if (placement.side == BS_BAR_POPUP_SIDE_TOP) {
    desired_y = anchor_y - placement.height;
  } else {
    desired_y = anchor_y + anchor_height;
  }
  max_y = monitor_y + monitor_height - placement.height;
  placement.y = CLAMP(desired_y, monitor_y, MAX(max_y, monitor_y));
  return placement;
}
