#include "frontends/bit_bar/tray_menu_view.h"

#define BS_BAR_TRAY_MENU_DEFAULT_MIN_WIDTH 220
#define BS_BAR_TRAY_MENU_DEFAULT_MAX_WIDTH 640
#define BS_BAR_TRAY_MENU_DEFAULT_MAX_HEIGHT 480

typedef struct {
  const BsTrayMenuNode *node;
  int selected_index;
} BsBarTrayMenuLevel;

typedef struct {
  GtkWidget *button;
  const BsTrayMenuNode *node;
  gboolean interactive;
  gboolean opens_submenu;
} BsBarTrayMenuRowRef;

typedef struct {
  char *item_id;
  const BsTrayMenuTree *tree;
  BsBarTrayMenuSizeConstraints constraints;
  GtkWidget *root;
  GtkWidget *header;
  GtkWidget *back_button;
  GtkWidget *title_label;
  GtkWidget *scroller;
  GtkWidget *content_box;
  GPtrArray *navigation;
  GPtrArray *rows;
  BsBarTrayMenuActivateFn activate_cb;
  BsBarTrayMenuRequestCloseFn close_cb;
  gpointer user_data;
} BsBarTrayMenuView;

typedef struct {
  BsBarTrayMenuView *view;
  const BsTrayMenuNode *node;
} BsBarTrayMenuRowData;

static void bs_bar_tray_menu_view_free(gpointer data);
static void bs_bar_tray_menu_level_free(gpointer data);
static void bs_bar_tray_menu_row_ref_free(gpointer data);
static void bs_bar_tray_menu_row_data_free(gpointer data);
static gboolean bs_bar_tray_menu_node_is_displayable(const BsTrayMenuNode *node);
static gboolean bs_bar_tray_menu_children_have_visible_entries(GPtrArray *children);
static const BsTrayMenuNode *bs_bar_tray_menu_view_current_node(BsBarTrayMenuView *view);
static BsBarTrayMenuLevel *bs_bar_tray_menu_view_current_level(BsBarTrayMenuView *view);
static char *bs_bar_tray_menu_view_strip_label(const BsTrayMenuNode *node);
static gboolean bs_bar_tray_menu_view_node_opens_submenu(const BsTrayMenuNode *node);
static gboolean bs_bar_tray_menu_view_node_is_interactive(const BsTrayMenuNode *node);
static void bs_bar_tray_menu_view_normalize_constraints(BsBarTrayMenuSizeConstraints *constraints);
static GtkWidget *bs_bar_tray_menu_view_build_row(BsBarTrayMenuView *view,
                                                  const BsTrayMenuNode *node,
                                                  gboolean is_selected);
static void bs_bar_tray_menu_view_apply_selection(BsBarTrayMenuView *view, gboolean grab_focus);
static void bs_bar_tray_menu_view_select_first_interactive(BsBarTrayMenuView *view);
static void bs_bar_tray_menu_view_move_selection(BsBarTrayMenuView *view, int delta);
static void bs_bar_tray_menu_view_activate_selected(BsBarTrayMenuView *view);
static void bs_bar_tray_menu_view_push_submenu(BsBarTrayMenuView *view, const BsTrayMenuNode *node);
static void bs_bar_tray_menu_view_pop_submenu(BsBarTrayMenuView *view);
static void bs_bar_tray_menu_view_represent_ancestor_popover(BsBarTrayMenuView *view);
static void bs_bar_tray_menu_view_rebuild(BsBarTrayMenuView *view);
static gboolean bs_bar_tray_menu_view_on_key_pressed(GtkEventControllerKey *controller,
                                                     guint keyval,
                                                     guint keycode,
                                                     GdkModifierType state,
                                                     gpointer user_data);
static void bs_bar_tray_menu_view_on_back_clicked(GtkButton *button, gpointer user_data);
static void bs_bar_tray_menu_view_on_row_clicked(GtkButton *button, gpointer user_data);

static void
bs_bar_tray_menu_view_free(gpointer data) {
  BsBarTrayMenuView *view = data;

  if (view == NULL) {
    return;
  }

  g_free(view->item_id);
  g_clear_pointer(&view->navigation, g_ptr_array_unref);
  g_clear_pointer(&view->rows, g_ptr_array_unref);
  g_free(view);
}

static void
bs_bar_tray_menu_level_free(gpointer data) {
  g_free(data);
}

static void
bs_bar_tray_menu_row_ref_free(gpointer data) {
  g_free(data);
}

static void
bs_bar_tray_menu_row_data_free(gpointer data) {
  g_free(data);
}

static gboolean
bs_bar_tray_menu_node_is_displayable(const BsTrayMenuNode *node) {
  g_return_val_if_fail(node != NULL, FALSE);

  return node->visible && node->kind != BS_TRAY_MENU_ITEM_SEPARATOR;
}

static gboolean
bs_bar_tray_menu_children_have_visible_entries(GPtrArray *children) {
  g_return_val_if_fail(children != NULL, FALSE);

  for (guint i = 0; i < children->len; i++) {
    const BsTrayMenuNode *node = g_ptr_array_index(children, i);

    if (node != NULL && bs_bar_tray_menu_node_is_displayable(node)) {
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
bs_bar_tray_menu_tree_has_visible_entries(const BsTrayMenuTree *tree) {
  if (tree == NULL || tree->root == NULL || tree->root->children == NULL) {
    return FALSE;
  }

  return bs_bar_tray_menu_children_have_visible_entries(tree->root->children);
}

static const BsTrayMenuNode *
bs_bar_tray_menu_view_current_node(BsBarTrayMenuView *view) {
  BsBarTrayMenuLevel *level = NULL;

  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(view->tree != NULL, NULL);

  level = bs_bar_tray_menu_view_current_level(view);
  return level != NULL ? level->node : view->tree->root;
}

static BsBarTrayMenuLevel *
bs_bar_tray_menu_view_current_level(BsBarTrayMenuView *view) {
  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(view->navigation != NULL, NULL);

  if (view->navigation->len == 0) {
    return NULL;
  }
  return g_ptr_array_index(view->navigation, view->navigation->len - 1);
}

static char *
bs_bar_tray_menu_view_strip_label(const BsTrayMenuNode *node) {
  GString *label = NULL;
  const char *source = NULL;

  g_return_val_if_fail(node != NULL, g_strdup(""));

  source = node->label != NULL ? node->label : "";
  label = g_string_new(NULL);
  for (const char *cursor = source; *cursor != '\0'; cursor++) {
    if (*cursor == '_') {
      if (*(cursor + 1) == '_') {
        g_string_append_c(label, '_');
        cursor++;
      }
      continue;
    }
    g_string_append_c(label, *cursor);
  }

  return g_string_free(label, FALSE);
}

static gboolean
bs_bar_tray_menu_view_node_opens_submenu(const BsTrayMenuNode *node) {
  g_return_val_if_fail(node != NULL, FALSE);

  return node->enabled && node->kind == BS_TRAY_MENU_ITEM_SUBMENU && node->children != NULL
         && node->children->len > 0;
}

static gboolean
bs_bar_tray_menu_view_node_is_interactive(const BsTrayMenuNode *node) {
  g_return_val_if_fail(node != NULL, FALSE);

  if (!node->enabled || node->kind == BS_TRAY_MENU_ITEM_SEPARATOR) {
    return FALSE;
  }

  if (node->kind == BS_TRAY_MENU_ITEM_SUBMENU) {
    return bs_bar_tray_menu_view_node_opens_submenu(node);
  }

  return TRUE;
}

static void
bs_bar_tray_menu_view_normalize_constraints(BsBarTrayMenuSizeConstraints *constraints) {
  g_return_if_fail(constraints != NULL);

  if (constraints->min_width <= 0) {
    constraints->min_width = BS_BAR_TRAY_MENU_DEFAULT_MIN_WIDTH;
  }
  if (constraints->max_width <= 0) {
    constraints->max_width = BS_BAR_TRAY_MENU_DEFAULT_MAX_WIDTH;
  }
  if (constraints->max_height <= 0) {
    constraints->max_height = BS_BAR_TRAY_MENU_DEFAULT_MAX_HEIGHT;
  }

  constraints->min_width = MAX(constraints->min_width, 160);
  constraints->max_width = MAX(constraints->max_width, constraints->min_width);
  constraints->max_height = MAX(constraints->max_height, 120);
}

static GtkWidget *
bs_bar_tray_menu_view_build_row(BsBarTrayMenuView *view,
                                const BsTrayMenuNode *node,
                                gboolean is_selected) {
  GtkWidget *button = NULL;
  GtkWidget *row_box = NULL;
  GtkWidget *indicator = NULL;
  GtkWidget *label = NULL;
  GtkWidget *affordance = NULL;
  BsBarTrayMenuRowData *row_data = NULL;
  g_autofree char *display_label = NULL;

  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(node != NULL, NULL);

  button = gtk_button_new();
  row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  indicator = gtk_label_new(" ");
  label = gtk_label_new(NULL);
  affordance = gtk_label_new(" ");
  row_data = g_new0(BsBarTrayMenuRowData, 1);
  display_label = bs_bar_tray_menu_view_strip_label(node);

  row_data->view = view;
  row_data->node = node;
  g_object_set_data_full(G_OBJECT(button),
                         "bs-bar-tray-menu-row",
                         row_data,
                         bs_bar_tray_menu_row_data_free);

  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(button, TRUE);
  gtk_widget_set_sensitive(button, node->enabled);
  gtk_widget_add_css_class(button, "bit-bar-tray-menu-row");
  if (is_selected) {
    gtk_widget_add_css_class(button, "bit-bar-tray-menu-row-selected");
  }
  if (!node->enabled) {
    gtk_widget_add_css_class(button, "bit-bar-tray-menu-row-disabled");
  }

  gtk_widget_add_css_class(indicator, "bit-bar-tray-menu-indicator");
  gtk_widget_add_css_class(label, "bit-bar-tray-menu-label");
  gtk_widget_add_css_class(affordance, "bit-bar-tray-menu-affordance");
  gtk_widget_set_hexpand(row_box, TRUE);
  gtk_widget_set_halign(row_box, GTK_ALIGN_FILL);
  gtk_widget_set_size_request(indicator, 28, -1);
  gtk_widget_set_size_request(affordance, 18, -1);
  gtk_widget_set_halign(indicator, GTK_ALIGN_START);
  gtk_widget_set_halign(affordance, GTK_ALIGN_END);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_FILL);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_NONE);
  gtk_label_set_single_line_mode(GTK_LABEL(label), FALSE);
  gtk_label_set_text(GTK_LABEL(label), display_label);

  if (node->kind == BS_TRAY_MENU_ITEM_CHECK) {
    gtk_label_set_text(GTK_LABEL(indicator), node->checked ? "[x]" : "[ ]");
  } else if (node->kind == BS_TRAY_MENU_ITEM_RADIO) {
    gtk_label_set_text(GTK_LABEL(indicator), node->checked ? "(o)" : "( )");
  }
  if (bs_bar_tray_menu_view_node_opens_submenu(node)) {
    gtk_label_set_text(GTK_LABEL(affordance), ">");
  }

  gtk_box_append(GTK_BOX(row_box), indicator);
  gtk_box_append(GTK_BOX(row_box), label);
  gtk_box_append(GTK_BOX(row_box), affordance);
  gtk_button_set_child(GTK_BUTTON(button), row_box);
  g_signal_connect(button,
                   "clicked",
                   G_CALLBACK(bs_bar_tray_menu_view_on_row_clicked),
                   view);
  return button;
}

static void
bs_bar_tray_menu_view_apply_selection(BsBarTrayMenuView *view, gboolean grab_focus) {
  BsBarTrayMenuLevel *level = NULL;

  g_return_if_fail(view != NULL);

  level = bs_bar_tray_menu_view_current_level(view);
  if (level == NULL) {
    return;
  }

  for (guint i = 0; i < view->rows->len; i++) {
    BsBarTrayMenuRowRef *row = g_ptr_array_index(view->rows, i);
    const gboolean selected = (int) i == level->selected_index;

    if (row == NULL || row->button == NULL) {
      continue;
    }

    if (selected) {
      gtk_widget_add_css_class(row->button, "bit-bar-tray-menu-row-selected");
      if (grab_focus && row->interactive) {
        gtk_widget_grab_focus(row->button);
      }
    } else {
      gtk_widget_remove_css_class(row->button, "bit-bar-tray-menu-row-selected");
    }
  }
}

static void
bs_bar_tray_menu_view_select_first_interactive(BsBarTrayMenuView *view) {
  BsBarTrayMenuNavItem *items = NULL;
  BsBarTrayMenuLevel *level = NULL;
  int index = -1;

  g_return_if_fail(view != NULL);

  level = bs_bar_tray_menu_view_current_level(view);
  if (level == NULL || view->rows->len == 0) {
    return;
  }

  items = g_new0(BsBarTrayMenuNavItem, view->rows->len);
  for (guint i = 0; i < view->rows->len; i++) {
    BsBarTrayMenuRowRef *row = g_ptr_array_index(view->rows, i);

    if (row == NULL) {
      continue;
    }

    items[i].interactive = row->interactive;
    items[i].opens_submenu = row->opens_submenu;
  }
  index = bs_bar_tray_menu_nav_find_next(items, view->rows->len, -1, 1);
  g_free(items);
  level->selected_index = index;
  bs_bar_tray_menu_view_apply_selection(view, TRUE);
}

static void
bs_bar_tray_menu_view_move_selection(BsBarTrayMenuView *view, int delta) {
  BsBarTrayMenuNavItem *items = NULL;
  BsBarTrayMenuLevel *level = NULL;
  int next_index = -1;

  g_return_if_fail(view != NULL);
  g_return_if_fail(delta != 0);

  level = bs_bar_tray_menu_view_current_level(view);
  if (level == NULL || view->rows->len == 0) {
    return;
  }

  items = g_new0(BsBarTrayMenuNavItem, view->rows->len);
  for (guint i = 0; i < view->rows->len; i++) {
    BsBarTrayMenuRowRef *row = g_ptr_array_index(view->rows, i);

    if (row == NULL) {
      continue;
    }

    items[i].interactive = row->interactive;
    items[i].opens_submenu = row->opens_submenu;
  }
  next_index = bs_bar_tray_menu_nav_find_next(items, view->rows->len, level->selected_index, delta);
  g_free(items);
  if (next_index < 0) {
    return;
  }

  level->selected_index = next_index;
  bs_bar_tray_menu_view_apply_selection(view, TRUE);
}

static void
bs_bar_tray_menu_view_activate_selected(BsBarTrayMenuView *view) {
  BsBarTrayMenuLevel *level = NULL;
  BsBarTrayMenuRowRef *row = NULL;

  g_return_if_fail(view != NULL);

  level = bs_bar_tray_menu_view_current_level(view);
  if (level == NULL || level->selected_index < 0 || level->selected_index >= (int) view->rows->len) {
    return;
  }

  row = g_ptr_array_index(view->rows, level->selected_index);
  if (row == NULL || !row->interactive || row->node == NULL) {
    return;
  }

  if (row->opens_submenu) {
    bs_bar_tray_menu_view_push_submenu(view, row->node);
    return;
  }

  if (view->activate_cb != NULL) {
    view->activate_cb(view->item_id, row->node->id, view->user_data);
  }
  if (view->close_cb != NULL) {
    view->close_cb(view->user_data);
  }
}

static void
bs_bar_tray_menu_view_push_submenu(BsBarTrayMenuView *view, const BsTrayMenuNode *node) {
  BsBarTrayMenuLevel *level = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(node != NULL);

  if (!bs_bar_tray_menu_view_node_opens_submenu(node)) {
    return;
  }

  level = g_new0(BsBarTrayMenuLevel, 1);
  level->node = node;
  level->selected_index = -1;
  g_ptr_array_add(view->navigation, level);
  bs_bar_tray_menu_view_rebuild(view);
}

static void
bs_bar_tray_menu_view_pop_submenu(BsBarTrayMenuView *view) {
  g_return_if_fail(view != NULL);
  g_return_if_fail(view->navigation != NULL);

  if (view->navigation->len <= 1) {
    return;
  }

  g_ptr_array_remove_index(view->navigation, view->navigation->len - 1);
  bs_bar_tray_menu_view_rebuild(view);
}

static void
bs_bar_tray_menu_view_represent_ancestor_popover(BsBarTrayMenuView *view) {
  GtkWidget *popover = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(view->root != NULL);

  popover = gtk_widget_get_ancestor(view->root, GTK_TYPE_POPOVER);
  if (popover != NULL && gtk_widget_get_visible(popover)) {
    gtk_popover_present(GTK_POPOVER(popover));
  }
}

static void
bs_bar_tray_menu_view_rebuild(BsBarTrayMenuView *view) {
  const BsTrayMenuNode *current = NULL;
  BsBarTrayMenuLevel *level = NULL;
  GtkWidget *child = NULL;

  g_return_if_fail(view != NULL);

  current = bs_bar_tray_menu_view_current_node(view);
  level = bs_bar_tray_menu_view_current_level(view);
  gtk_widget_set_visible(view->header, view->navigation->len > 1);

  if (current != NULL && current->label != NULL && *current->label != '\0') {
    g_autofree char *title = bs_bar_tray_menu_view_strip_label(current);
    gtk_label_set_text(GTK_LABEL(view->title_label), title);
  } else {
    gtk_label_set_text(GTK_LABEL(view->title_label), "Menu");
  }
  gtk_widget_set_sensitive(view->back_button, view->navigation->len > 1);

  child = gtk_widget_get_first_child(view->content_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);

    gtk_box_remove(GTK_BOX(view->content_box), child);
    child = next;
  }
  g_ptr_array_set_size(view->rows, 0);

  if (current == NULL || current->children == NULL) {
    bs_bar_tray_menu_view_represent_ancestor_popover(view);
    return;
  }

  for (guint i = 0; i < current->children->len; i++) {
    const BsTrayMenuNode *node = g_ptr_array_index(current->children, i);

    if (node == NULL || !node->visible) {
      continue;
    }

    if (node->kind == BS_TRAY_MENU_ITEM_SEPARATOR) {
      GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

      gtk_widget_add_css_class(separator, "bit-bar-tray-menu-separator");
      gtk_box_append(GTK_BOX(view->content_box), separator);
      continue;
    }

    {
      BsBarTrayMenuRowRef *row = g_new0(BsBarTrayMenuRowRef, 1);
      GtkWidget *button = NULL;

      row->node = node;
      row->interactive = bs_bar_tray_menu_view_node_is_interactive(node);
      row->opens_submenu = bs_bar_tray_menu_view_node_opens_submenu(node);
      button = bs_bar_tray_menu_view_build_row(view,
                                               node,
                                               level != NULL && level->selected_index == (int) view->rows->len);
      row->button = button;
      g_ptr_array_add(view->rows, row);
      gtk_box_append(GTK_BOX(view->content_box), button);
    }
  }

  if (level != NULL && (level->selected_index < 0 || level->selected_index >= (int) view->rows->len
                        || !((BsBarTrayMenuRowRef *) g_ptr_array_index(view->rows, level->selected_index))->interactive)) {
    level->selected_index = -1;
  }

  if (level != NULL && level->selected_index < 0) {
    bs_bar_tray_menu_view_select_first_interactive(view);
  } else {
    bs_bar_tray_menu_view_apply_selection(view, TRUE);
  }
  bs_bar_tray_menu_view_represent_ancestor_popover(view);
}

static gboolean
bs_bar_tray_menu_view_on_key_pressed(GtkEventControllerKey *controller,
                                     guint keyval,
                                     guint keycode,
                                     GdkModifierType state,
                                     gpointer user_data) {
  BsBarTrayMenuView *view = user_data;
  BsBarTrayMenuLevel *level = NULL;

  (void) controller;
  (void) keycode;
  (void) state;

  g_return_val_if_fail(view != NULL, FALSE);

  level = bs_bar_tray_menu_view_current_level(view);
  switch (keyval) {
    case GDK_KEY_Up:
      bs_bar_tray_menu_view_move_selection(view, -1);
      return TRUE;
    case GDK_KEY_Down:
      bs_bar_tray_menu_view_move_selection(view, 1);
      return TRUE;
    case GDK_KEY_Home:
      if (level != NULL) {
        level->selected_index = -1;
      }
      bs_bar_tray_menu_view_select_first_interactive(view);
      return TRUE;
    case GDK_KEY_End:
      if (level != NULL) {
        level->selected_index = (int) view->rows->len;
      }
      bs_bar_tray_menu_view_move_selection(view, -1);
      return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_space:
      bs_bar_tray_menu_view_activate_selected(view);
      return TRUE;
    case GDK_KEY_Right:
      bs_bar_tray_menu_view_activate_selected(view);
      return TRUE;
    case GDK_KEY_Left:
      bs_bar_tray_menu_view_pop_submenu(view);
      return TRUE;
    case GDK_KEY_Escape:
      if (view->close_cb != NULL) {
        view->close_cb(view->user_data);
      }
      return TRUE;
    default:
      return FALSE;
  }
}

static void
bs_bar_tray_menu_view_on_back_clicked(GtkButton *button, gpointer user_data) {
  BsBarTrayMenuView *view = user_data;

  (void) button;
  g_return_if_fail(view != NULL);

  bs_bar_tray_menu_view_pop_submenu(view);
}

static void
bs_bar_tray_menu_view_on_row_clicked(GtkButton *button, gpointer user_data) {
  BsBarTrayMenuView *view = user_data;
  BsBarTrayMenuRowData *row_data = NULL;
  BsBarTrayMenuLevel *level = NULL;
  int selected_index = -1;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_BUTTON(button));

  row_data = g_object_get_data(G_OBJECT(button), "bs-bar-tray-menu-row");
  if (row_data == NULL || row_data->node == NULL) {
    return;
  }

  level = bs_bar_tray_menu_view_current_level(view);
  for (guint i = 0; i < view->rows->len; i++) {
    BsBarTrayMenuRowRef *row = g_ptr_array_index(view->rows, i);

    if (row != NULL && row->node == row_data->node) {
      selected_index = (int) i;
      break;
    }
  }
  if (level != NULL && selected_index >= 0) {
    level->selected_index = selected_index;
    bs_bar_tray_menu_view_apply_selection(view, FALSE);
  }

  bs_bar_tray_menu_view_activate_selected(view);
}

int
bs_bar_tray_menu_nav_find_next(const BsBarTrayMenuNavItem *items,
                               guint len,
                               int current_index,
                               int delta) {
  int index = 0;

  g_return_val_if_fail(items != NULL || len == 0, -1);
  g_return_val_if_fail(delta != 0, -1);

  if (len == 0) {
    return -1;
  }

  index = current_index;
  if (delta > 0 && index < -1) {
    index = -1;
  }
  if (delta < 0 && index > (int) len) {
    index = (int) len;
  }

  while (TRUE) {
    index += delta > 0 ? 1 : -1;
    if (index < 0 || index >= (int) len) {
      return -1;
    }
    if (items[index].interactive) {
      return index;
    }
  }
}

GtkWidget *
bs_bar_tray_menu_view_new(const BsTrayMenuTree *tree,
                          const BsBarTrayMenuSizeConstraints *constraints,
                          BsBarTrayMenuActivateFn activate_cb,
                          BsBarTrayMenuRequestCloseFn close_cb,
                          gpointer user_data) {
  BsBarTrayMenuView *view = NULL;
  BsBarTrayMenuLevel *root_level = NULL;
  GtkWidget *root = NULL;
  GtkWidget *header_row = NULL;
  GtkWidget *back_button = NULL;
  GtkWidget *scroller = NULL;
  GtkEventController *key_controller = NULL;

  g_return_val_if_fail(tree != NULL, NULL);
  g_return_val_if_fail(tree->root != NULL, NULL);
  if (!bs_bar_tray_menu_tree_has_visible_entries(tree)) {
    return NULL;
  }

  view = g_new0(BsBarTrayMenuView, 1);
  view->item_id = g_strdup(tree->item_id);
  view->tree = tree;
  view->constraints = constraints != NULL
                        ? *constraints
                        : (BsBarTrayMenuSizeConstraints) {
                            .min_width = BS_BAR_TRAY_MENU_DEFAULT_MIN_WIDTH,
                            .max_width = BS_BAR_TRAY_MENU_DEFAULT_MAX_WIDTH,
                            .max_height = BS_BAR_TRAY_MENU_DEFAULT_MAX_HEIGHT,
                          };
  bs_bar_tray_menu_view_normalize_constraints(&view->constraints);
  view->activate_cb = activate_cb;
  view->close_cb = close_cb;
  view->user_data = user_data;
  view->navigation = g_ptr_array_new_with_free_func(bs_bar_tray_menu_level_free);
  view->rows = g_ptr_array_new_with_free_func(bs_bar_tray_menu_row_ref_free);

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  back_button = gtk_button_new_with_label("Back");
  scroller = gtk_scrolled_window_new();
  view->root = root;
  view->header = header_row;
  view->back_button = back_button;
  view->title_label = gtk_label_new("Menu");
  view->scroller = scroller;
  view->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

  gtk_widget_add_css_class(root, "bit-bar-tray-menu-view");
  gtk_widget_add_css_class(header_row, "bit-bar-tray-menu-header");
  gtk_widget_add_css_class(back_button, "bit-bar-tray-menu-back");
  gtk_widget_add_css_class(view->content_box, "bit-bar-tray-menu-content");
  gtk_widget_set_margin_top(root, 8);
  gtk_widget_set_margin_bottom(root, 8);
  gtk_widget_set_margin_start(root, 8);
  gtk_widget_set_margin_end(root, 8);
  gtk_widget_set_focusable(root, TRUE);
  gtk_widget_set_hexpand(view->content_box, TRUE);
  gtk_label_set_xalign(GTK_LABEL(view->title_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(view->title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(view->title_label), TRUE);
  gtk_widget_set_hexpand(view->title_label, TRUE);
  gtk_widget_set_halign(view->title_label, GTK_ALIGN_FILL);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller),
                                            view->constraints.min_width);
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(scroller),
                                            view->constraints.max_width);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller),
                                             view->constraints.max_height);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroller), TRUE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view->content_box);

  gtk_box_append(GTK_BOX(header_row), back_button);
  gtk_box_append(GTK_BOX(header_row), view->title_label);
  gtk_box_append(GTK_BOX(root), header_row);
  gtk_box_append(GTK_BOX(root), scroller);

  root_level = g_new0(BsBarTrayMenuLevel, 1);
  root_level->node = tree->root;
  root_level->selected_index = -1;
  g_ptr_array_add(view->navigation, root_level);

  key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller,
                   "key-pressed",
                   G_CALLBACK(bs_bar_tray_menu_view_on_key_pressed),
                   view);
  gtk_widget_add_controller(root, key_controller);
  g_signal_connect(back_button,
                   "clicked",
                   G_CALLBACK(bs_bar_tray_menu_view_on_back_clicked),
                   view);
  g_object_set_data_full(G_OBJECT(root),
                         "bs-bar-tray-menu-view",
                         view,
                         bs_bar_tray_menu_view_free);
  bs_bar_tray_menu_view_rebuild(view);
  return root;
}
