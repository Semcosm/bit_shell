#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_CONTROLLER_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_CONTROLLER_H

#include <gtk/gtk.h>

#include "frontends/bit_bar/bar_view_model.h"

typedef struct _BsBarTrayController BsBarTrayController;

typedef struct {
  void (*request_activate)(const char *item_id, int x, int y, gpointer user_data);
  void (*request_context_menu)(const char *item_id, int x, int y, gpointer user_data);
  void (*request_menu_refresh)(const char *item_id, gpointer user_data);
  void (*request_menu_activate)(const char *item_id, gint32 menu_item_id, gpointer user_data);

  GtkWidget *(*lookup_button)(const char *item_id, gpointer user_data);

  gpointer user_data;
} BsBarTrayControllerOps;

BsBarTrayController *bs_bar_tray_controller_new(BsBarViewModel *view_model,
                                                const BsBarTrayControllerOps *ops);
void bs_bar_tray_controller_free(BsBarTrayController *controller);

void bs_bar_tray_controller_handle_activate(BsBarTrayController *controller,
                                            GtkWidget *button,
                                            const char *item_id);
void bs_bar_tray_controller_handle_menu(BsBarTrayController *controller,
                                        GtkWidget *button,
                                        const char *item_id);
void bs_bar_tray_controller_handle_item_menu_closed(BsBarTrayController *controller,
                                                    const char *item_id);
void bs_bar_tray_controller_sync_from_vm(BsBarTrayController *controller);
void bs_bar_tray_controller_handle_shell_reset(BsBarTrayController *controller);
void bs_bar_tray_controller_close(BsBarTrayController *controller);

#endif
