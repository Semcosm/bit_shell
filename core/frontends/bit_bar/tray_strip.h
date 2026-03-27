#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_STRIP_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_STRIP_H

#include <gtk/gtk.h>

#include "frontends/bit_bar/tray_item_button.h"

GtkWidget *bs_bar_tray_strip_new(void);

void bs_bar_tray_strip_apply_metrics(GtkWidget *strip,
                                     int gap,
                                     int slot_size);

void bs_bar_tray_strip_rebuild(GtkWidget *strip,
                               const GPtrArray *items,
                               int slot_size,
                               BsBarTrayItemActivateFn on_activate,
                               BsBarTrayItemMenuFn on_menu,
                               gpointer user_data);

#endif
