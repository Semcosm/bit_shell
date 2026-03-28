#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_MENU_VIEW_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_MENU_VIEW_H

#include <gtk/gtk.h>

#include "model/tray_menu.h"

typedef void (*BsBarTrayMenuActivateFn)(const char *item_id,
                                        gint32 menu_item_id,
                                        gpointer user_data);
typedef void (*BsBarTrayMenuRequestCloseFn)(gpointer user_data);

typedef struct {
  gboolean interactive;
  gboolean opens_submenu;
} BsBarTrayMenuNavItem;

int bs_bar_tray_menu_nav_find_next(const BsBarTrayMenuNavItem *items,
                                   guint len,
                                   int current_index,
                                   int delta);

gboolean bs_bar_tray_menu_tree_has_visible_entries(const BsTrayMenuTree *tree);

GtkWidget *bs_bar_tray_menu_view_new(const BsTrayMenuTree *tree,
                                     BsBarTrayMenuActivateFn activate_cb,
                                     BsBarTrayMenuRequestCloseFn close_cb,
                                     gpointer user_data);

#endif
