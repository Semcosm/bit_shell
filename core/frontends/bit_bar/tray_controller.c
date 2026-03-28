#include "frontends/bit_bar/tray_controller.h"

#include "frontends/bit_bar/tray_item_button.h"
#include "frontends/bit_bar/tray_menu_view.h"

struct _BsBarTrayController {
  BsBarViewModel *view_model;
  BsBarTrayControllerOps ops;
  char *open_item_id;
  GWeakRef open_button_ref;
  char *pending_item_id;
  gboolean pending_open;
};

static void bs_bar_tray_controller_set_pending_open(BsBarTrayController *controller,
                                                    const char *item_id);
static void bs_bar_tray_controller_clear_pending_open(BsBarTrayController *controller);
static void bs_bar_tray_controller_set_open_item(BsBarTrayController *controller,
                                                 const char *item_id,
                                                 GtkWidget *button);
static void bs_bar_tray_controller_clear_open_item(BsBarTrayController *controller);
static GtkWidget *bs_bar_tray_controller_lookup_button(BsBarTrayController *controller,
                                                       const char *item_id);
static GtkWidget *bs_bar_tray_controller_get_open_button(BsBarTrayController *controller);
static gboolean bs_bar_tray_controller_get_item_anchor(GtkWidget *button, int *out_x, int *out_y);
static gboolean bs_bar_tray_controller_get_menu_tree_ready(const BsTrayMenuTree *tree);
static void bs_bar_tray_controller_sync_open_state(BsBarTrayController *controller);
static void bs_bar_tray_controller_close_open_menu(BsBarTrayController *controller);
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

static void
bs_bar_tray_controller_set_open_item(BsBarTrayController *controller,
                                     const char *item_id,
                                     GtkWidget *button) {
  g_return_if_fail(controller != NULL);
  g_return_if_fail(item_id != NULL);
  g_return_if_fail(button != NULL);

  g_clear_pointer(&controller->open_item_id, g_free);
  controller->open_item_id = g_strdup(item_id);
  g_weak_ref_set(&controller->open_button_ref, button);
}

static void
bs_bar_tray_controller_clear_open_item(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  g_weak_ref_set(&controller->open_button_ref, NULL);
  g_clear_pointer(&controller->open_item_id, g_free);
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

static GtkWidget *
bs_bar_tray_controller_get_open_button(BsBarTrayController *controller) {
  g_return_val_if_fail(controller != NULL, NULL);

  return g_weak_ref_get(&controller->open_button_ref);
}

static gboolean
bs_bar_tray_controller_get_item_anchor(GtkWidget *button, int *out_x, int *out_y) {
  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(out_x != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  return bs_bar_tray_item_button_get_anchor(button, out_x, out_y);
}

static gboolean
bs_bar_tray_controller_get_menu_tree_ready(const BsTrayMenuTree *tree) {
  return bs_bar_tray_menu_tree_has_visible_entries(tree);
}

static void
bs_bar_tray_controller_sync_open_state(BsBarTrayController *controller) {
  g_autoptr(GtkWidget) open_button = NULL;
  GtkWidget *button = NULL;

  g_return_if_fail(controller != NULL);

  if (controller->open_item_id == NULL) {
    return;
  }

  open_button = bs_bar_tray_controller_get_open_button(controller);
  button = bs_bar_tray_controller_lookup_button(controller, controller->open_item_id);
  if (button == NULL) {
    bs_bar_tray_controller_clear_open_item(controller);
    return;
  }
  if (open_button != NULL
      && button == open_button
      && !bs_bar_tray_item_button_is_menu_open(button)) {
    bs_bar_tray_controller_clear_open_item(controller);
  }
}

static void
bs_bar_tray_controller_close_open_menu(BsBarTrayController *controller) {
  GtkWidget *button = NULL;

  g_return_if_fail(controller != NULL);

  if (controller->open_item_id != NULL) {
    button = bs_bar_tray_controller_lookup_button(controller, controller->open_item_id);
    if (button != NULL) {
      bs_bar_tray_item_button_close_menu(button);
    }
  }
  bs_bar_tray_controller_clear_open_item(controller);
}

static gboolean
bs_bar_tray_controller_present_menu(BsBarTrayController *controller,
                                    const char *item_id,
                                    GtkWidget *button,
                                    const BsTrayMenuTree *tree) {
  GtkWidget *menu_view = NULL;

  g_return_val_if_fail(controller != NULL, FALSE);
  g_return_val_if_fail(item_id != NULL, FALSE);
  g_return_val_if_fail(button != NULL, FALSE);
  g_return_val_if_fail(tree != NULL, FALSE);

  menu_view = bs_bar_tray_menu_view_new(tree,
                                        bs_bar_tray_controller_on_menu_item_activated,
                                        bs_bar_tray_controller_on_menu_close_requested,
                                        controller);
  if (menu_view == NULL) {
    return FALSE;
  }

  bs_bar_tray_controller_sync_open_state(controller);
  if (controller->open_item_id != NULL && g_strcmp0(controller->open_item_id, item_id) != 0) {
    bs_bar_tray_controller_close_open_menu(controller);
  }

  if (!bs_bar_tray_item_button_present_menu(button, menu_view)) {
    return FALSE;
  }

  bs_bar_tray_controller_set_open_item(controller, item_id, button);
  bs_bar_tray_controller_clear_pending_open(controller);
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
bs_bar_tray_controller_new(BsBarViewModel *view_model, const BsBarTrayControllerOps *ops) {
  BsBarTrayController *controller = NULL;

  g_return_val_if_fail(view_model != NULL, NULL);

  controller = g_new0(BsBarTrayController, 1);
  controller->view_model = view_model;
  g_weak_ref_init(&controller->open_button_ref, NULL);
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

  bs_bar_tray_controller_close_open_menu(controller);
  g_weak_ref_clear(&controller->open_button_ref);
  g_clear_pointer(&controller->open_item_id, g_free);
  g_clear_pointer(&controller->pending_item_id, g_free);
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
    anchor_x = 0;
    anchor_y = 0;
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

  bs_bar_tray_controller_sync_open_state(controller);
  if (controller->open_item_id != NULL
      && g_strcmp0(controller->open_item_id, item_id) == 0
      && bs_bar_tray_item_button_is_menu_open(button)) {
    bs_bar_tray_controller_close(controller);
    return;
  }

  item = bs_bar_view_model_lookup_tray_item(controller->view_model, item_id);
  tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, item_id);
  has_menu_object = item != NULL
                    && item->menu_object_path != NULL
                    && *item->menu_object_path != '\0';

  if (bs_bar_tray_controller_get_menu_tree_ready(tree)) {
    if (!bs_bar_tray_controller_present_menu(controller, item_id, button, tree)
        && controller->ops.request_context_menu != NULL) {
      if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
        anchor_x = 0;
        anchor_y = 0;
      }
      controller->ops.request_context_menu(item_id,
                                           anchor_x,
                                           anchor_y,
                                           controller->ops.user_data);
    }
    return;
  }

  bs_bar_tray_controller_close_open_menu(controller);
  if (has_menu_object) {
    bs_bar_tray_controller_set_pending_open(controller, item_id);
    if (controller->ops.request_menu_refresh != NULL) {
      controller->ops.request_menu_refresh(item_id, controller->ops.user_data);
    }
    return;
  }

  bs_bar_tray_controller_clear_pending_open(controller);
  if (!bs_bar_tray_controller_get_item_anchor(button, &anchor_x, &anchor_y)) {
    anchor_x = 0;
    anchor_y = 0;
  }
  if (controller->ops.request_context_menu != NULL) {
    controller->ops.request_context_menu(item_id, anchor_x, anchor_y, controller->ops.user_data);
  }
}

void
bs_bar_tray_controller_sync_from_vm(BsBarTrayController *controller) {
  const BsBarTrayItemView *open_item = NULL;
  const BsTrayMenuTree *open_tree = NULL;
  GtkWidget *button = NULL;

  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_sync_open_state(controller);
  if (controller->open_item_id != NULL) {
    open_item = bs_bar_view_model_lookup_tray_item(controller->view_model, controller->open_item_id);
    open_tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, controller->open_item_id);
    button = bs_bar_tray_controller_lookup_button(controller, controller->open_item_id);
    if (open_item == NULL
        || button == NULL
        || !bs_bar_tray_controller_get_menu_tree_ready(open_tree)
        || !bs_bar_tray_controller_present_menu(controller, controller->open_item_id, button, open_tree)) {
      bs_bar_tray_controller_close_open_menu(controller);
    }
  }

  if (!controller->pending_open || controller->pending_item_id == NULL) {
    return;
  }

  if (bs_bar_view_model_lookup_tray_item(controller->view_model, controller->pending_item_id) == NULL) {
    bs_bar_tray_controller_clear_pending_open(controller);
    return;
  }

  open_tree = bs_bar_view_model_lookup_tray_menu(controller->view_model, controller->pending_item_id);
  if (!bs_bar_tray_controller_get_menu_tree_ready(open_tree)) {
    return;
  }

  button = bs_bar_tray_controller_lookup_button(controller, controller->pending_item_id);
  if (button == NULL
      || !bs_bar_tray_controller_present_menu(controller,
                                              controller->pending_item_id,
                                              button,
                                              open_tree)) {
    bs_bar_tray_controller_clear_pending_open(controller);
  }
}

void
bs_bar_tray_controller_handle_shell_reset(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_close(controller);
}

void
bs_bar_tray_controller_close(BsBarTrayController *controller) {
  g_return_if_fail(controller != NULL);

  bs_bar_tray_controller_clear_pending_open(controller);
  bs_bar_tray_controller_close_open_menu(controller);
}
