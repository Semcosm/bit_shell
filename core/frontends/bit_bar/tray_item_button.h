#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_ITEM_BUTTON_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_ITEM_BUTTON_H

#include <gtk/gtk.h>

#include "frontends/bit_bar/bar_view_model.h"

typedef void (*BsBarTrayItemActivateFn)(GtkWidget *button,
                                        const char *item_id,
                                        gpointer user_data);
typedef void (*BsBarTrayItemMenuFn)(GtkWidget *button,
                                    const char *item_id,
                                    gpointer user_data);
typedef void (*BsBarTrayItemMenuClosedFn)(const char *item_id,
                                          gpointer user_data);

GtkWidget *bs_bar_tray_item_button_new(const BsBarTrayItemView *item,
                                       int slot_size,
                                       BsBarTrayItemActivateFn on_activate,
                                       BsBarTrayItemMenuFn on_menu,
                                       BsBarTrayItemMenuClosedFn on_menu_closed,
                                       gpointer user_data);

void bs_bar_tray_item_button_apply_slot_size(GtkWidget *button, int slot_size);
void bs_bar_tray_item_button_update(GtkWidget *button,
                                    const BsBarTrayItemView *item,
                                    int slot_size);
const char *bs_bar_tray_item_button_item_id(GtkWidget *button);
gboolean bs_bar_tray_item_button_present_menu(GtkWidget *item_widget, GtkWidget *content);
void bs_bar_tray_item_button_close_menu(GtkWidget *item_widget);
gboolean bs_bar_tray_item_button_is_menu_open(GtkWidget *item_widget);
gboolean bs_bar_tray_item_button_get_anchor(GtkWidget *item_widget, int *out_x, int *out_y);

#endif
