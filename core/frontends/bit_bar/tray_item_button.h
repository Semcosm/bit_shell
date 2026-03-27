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

GtkWidget *bs_bar_tray_item_button_new(const BsBarTrayItemView *item,
                                       int slot_size,
                                       BsBarTrayItemActivateFn on_activate,
                                       BsBarTrayItemMenuFn on_menu,
                                       gpointer user_data);

void bs_bar_tray_item_button_update(GtkWidget *button,
                                    const BsBarTrayItemView *item,
                                    int slot_size);

#endif
