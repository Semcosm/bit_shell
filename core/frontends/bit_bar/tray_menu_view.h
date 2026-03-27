#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_MENU_VIEW_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_MENU_VIEW_H

#include <gtk/gtk.h>

#include "model/tray_menu.h"

typedef void (*BsBarTrayMenuActivateFn)(const char *item_id,
                                        gint32 menu_item_id,
                                        gpointer user_data);

GtkWidget *bs_bar_tray_menu_view_new(const BsTrayMenuTree *tree,
                                     BsBarTrayMenuActivateFn activate_cb,
                                     gpointer user_data);

#endif
