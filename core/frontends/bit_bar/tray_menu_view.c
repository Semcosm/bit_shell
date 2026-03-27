#include "frontends/bit_bar/tray_menu_view.h"

typedef struct {
  char *item_id;
  const BsTrayMenuTree *tree;
  GtkWidget *root;
  GtkWidget *header;
  GtkWidget *title_label;
  GtkWidget *content_box;
  GPtrArray *navigation;
  BsBarTrayMenuActivateFn activate_cb;
  gpointer user_data;
} BsBarTrayMenuView;

typedef struct {
  BsBarTrayMenuView *view;
  const BsTrayMenuNode *node;
} BsBarTrayMenuRowData;

static void bs_bar_tray_menu_view_free(gpointer data);
static void bs_bar_tray_menu_row_data_free(gpointer data);
static const BsTrayMenuNode *bs_bar_tray_menu_view_current_node(BsBarTrayMenuView *view);
static char *bs_bar_tray_menu_view_display_label(const BsTrayMenuNode *node);
static void bs_bar_tray_menu_view_rebuild(BsBarTrayMenuView *view);
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
  g_free(view);
}

static void
bs_bar_tray_menu_row_data_free(gpointer data) {
  g_free(data);
}

static const BsTrayMenuNode *
bs_bar_tray_menu_view_current_node(BsBarTrayMenuView *view) {
  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(view->tree != NULL, NULL);

  if (view->navigation == NULL || view->navigation->len == 0) {
    return view->tree->root;
  }
  return g_ptr_array_index(view->navigation, view->navigation->len - 1);
}

static char *
bs_bar_tray_menu_view_display_label(const BsTrayMenuNode *node) {
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

  if (node->kind == BS_TRAY_MENU_ITEM_CHECK) {
    g_string_prepend(label, node->checked ? "[x] " : "[ ] ");
  } else if (node->kind == BS_TRAY_MENU_ITEM_RADIO) {
    g_string_prepend(label, node->checked ? "(*) " : "( ) ");
  } else if (node->kind == BS_TRAY_MENU_ITEM_SUBMENU) {
    g_string_append(label, "  >");
  }

  return g_string_free(label, FALSE);
}

static void
bs_bar_tray_menu_view_rebuild(BsBarTrayMenuView *view) {
  const BsTrayMenuNode *current = NULL;
  GtkWidget *child = NULL;

  g_return_if_fail(view != NULL);

  current = bs_bar_tray_menu_view_current_node(view);
  gtk_widget_set_visible(view->header, current != NULL && current != view->tree->root);
  gtk_label_set_text(GTK_LABEL(view->title_label),
                     current != NULL && current->label != NULL && *current->label != '\0'
                       ? current->label
                       : "Menu");

  child = gtk_widget_get_first_child(view->content_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(view->content_box), child);
    child = next;
  }

  if (current == NULL || current->children == NULL) {
    return;
  }

  for (guint i = 0; i < current->children->len; i++) {
    const BsTrayMenuNode *node = g_ptr_array_index(current->children, i);

    if (node == NULL || !node->visible) {
      continue;
    }

    if (node->kind == BS_TRAY_MENU_ITEM_SEPARATOR) {
      GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
      gtk_box_append(GTK_BOX(view->content_box), separator);
      continue;
    }

    {
      GtkWidget *button = gtk_button_new();
      GtkWidget *label = gtk_label_new(NULL);
      BsBarTrayMenuRowData *row_data = g_new0(BsBarTrayMenuRowData, 1);
      g_autofree char *display = bs_bar_tray_menu_view_display_label(node);

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
      gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
      gtk_label_set_text(GTK_LABEL(label), display);
      gtk_button_set_child(GTK_BUTTON(button), label);
      g_signal_connect(button,
                       "clicked",
                       G_CALLBACK(bs_bar_tray_menu_view_on_row_clicked),
                       view);
      gtk_box_append(GTK_BOX(view->content_box), button);
    }
  }
}

static void
bs_bar_tray_menu_view_on_back_clicked(GtkButton *button, gpointer user_data) {
  BsBarTrayMenuView *view = user_data;

  (void) button;
  g_return_if_fail(view != NULL);

  if (view->navigation == NULL || view->navigation->len <= 1) {
    return;
  }

  g_ptr_array_remove_index(view->navigation, view->navigation->len - 1);
  bs_bar_tray_menu_view_rebuild(view);
}

static void
bs_bar_tray_menu_view_on_row_clicked(GtkButton *button, gpointer user_data) {
  BsBarTrayMenuView *view = user_data;
  BsBarTrayMenuRowData *row_data = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_BUTTON(button));

  row_data = g_object_get_data(G_OBJECT(button), "bs-bar-tray-menu-row");
  if (row_data == NULL || row_data->node == NULL || !row_data->node->enabled) {
    return;
  }

  if (row_data->node->kind == BS_TRAY_MENU_ITEM_SUBMENU
      && row_data->node->children != NULL
      && row_data->node->children->len > 0) {
    g_ptr_array_add(view->navigation, (gpointer) row_data->node);
    bs_bar_tray_menu_view_rebuild(view);
    return;
  }

  if (view->activate_cb != NULL) {
    view->activate_cb(view->item_id, row_data->node->id, view->user_data);
  }
}

GtkWidget *
bs_bar_tray_menu_view_new(const BsTrayMenuTree *tree,
                          BsBarTrayMenuActivateFn activate_cb,
                          gpointer user_data) {
  BsBarTrayMenuView *view = NULL;
  GtkWidget *root = NULL;
  GtkWidget *header_row = NULL;
  GtkWidget *back_button = NULL;

  g_return_val_if_fail(tree != NULL, NULL);
  g_return_val_if_fail(tree->root != NULL, NULL);

  view = g_new0(BsBarTrayMenuView, 1);
  view->item_id = g_strdup(tree->item_id);
  view->tree = tree;
  view->activate_cb = activate_cb;
  view->user_data = user_data;
  view->navigation = g_ptr_array_new();

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  back_button = gtk_button_new_with_label("Back");
  view->root = root;
  view->header = header_row;
  view->title_label = gtk_label_new("Menu");
  view->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

  gtk_widget_add_css_class(root, "bit-bar-tray-menu-view");
  gtk_widget_add_css_class(view->content_box, "bit-bar-tray-menu-content");
  gtk_widget_add_css_class(header_row, "bit-bar-tray-menu-header");
  gtk_widget_set_margin_top(root, 8);
  gtk_widget_set_margin_bottom(root, 8);
  gtk_widget_set_margin_start(root, 8);
  gtk_widget_set_margin_end(root, 8);
  gtk_widget_set_size_request(root, 180, -1);
  gtk_widget_set_hexpand(view->content_box, TRUE);
  gtk_label_set_xalign(GTK_LABEL(view->title_label), 0.0f);

  gtk_box_append(GTK_BOX(header_row), back_button);
  gtk_box_append(GTK_BOX(header_row), view->title_label);
  gtk_box_append(GTK_BOX(root), header_row);
  gtk_box_append(GTK_BOX(root), view->content_box);

  g_ptr_array_add(view->navigation, (gpointer) tree->root);
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
