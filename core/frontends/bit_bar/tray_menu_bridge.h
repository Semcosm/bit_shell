#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_MENU_BRIDGE_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_MENU_BRIDGE_H

#include <gtk/gtk.h>

typedef struct _BsBarTrayMenuBridge BsBarTrayMenuBridge;

typedef struct {
  int x;
  int y;
  int width;
  int height;
  int monitor_x;
  int monitor_y;
  int monitor_width;
  int monitor_height;
} BsBarPopupAnchor;

BsBarTrayMenuBridge *bs_bar_tray_menu_bridge_new(GtkWidget *overlay_parent);
void bs_bar_tray_menu_bridge_free(BsBarTrayMenuBridge *bridge);

gboolean bs_bar_tray_menu_bridge_open(BsBarTrayMenuBridge *bridge,
                                      const char *item_id,
                                      const BsBarPopupAnchor *anchor);
gboolean bs_bar_tray_menu_bridge_present(BsBarTrayMenuBridge *bridge,
                                         const char *item_id,
                                         const BsBarPopupAnchor *anchor,
                                         GtkWidget *content);
void bs_bar_tray_menu_bridge_close(BsBarTrayMenuBridge *bridge);
gboolean bs_bar_tray_menu_bridge_toggle(BsBarTrayMenuBridge *bridge,
                                        const char *item_id,
                                        const BsBarPopupAnchor *anchor);
gboolean bs_bar_tray_menu_bridge_is_open_for(BsBarTrayMenuBridge *bridge,
                                             const char *item_id);
const char *bs_bar_tray_menu_bridge_open_item_id(BsBarTrayMenuBridge *bridge);
void bs_bar_tray_menu_bridge_handle_shell_reset(BsBarTrayMenuBridge *bridge);

#endif
