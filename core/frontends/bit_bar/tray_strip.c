#include "frontends/bit_bar/tray_strip.h"

static void bs_bar_tray_strip_clear(GtkWidget *strip);

static void
bs_bar_tray_strip_clear(GtkWidget *strip) {
  GtkWidget *child = NULL;

  g_return_if_fail(strip != NULL);

  child = gtk_widget_get_first_child(strip);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);

    gtk_box_remove(GTK_BOX(strip), child);
    child = next;
  }
}

GtkWidget *
bs_bar_tray_strip_new(void) {
  GtkWidget *strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  gtk_widget_add_css_class(strip, "bit-bar-tray-strip");
  return strip;
}

void
bs_bar_tray_strip_apply_metrics(GtkWidget *strip, int gap, int slot_size) {
  GtkWidget *child = NULL;

  g_return_if_fail(strip != NULL);

  gtk_box_set_spacing(GTK_BOX(strip), gap);
  child = gtk_widget_get_first_child(strip);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);

    bs_bar_tray_item_button_apply_slot_size(child, slot_size);
    child = next;
  }
}

void
bs_bar_tray_strip_rebuild(GtkWidget *strip,
                          const GPtrArray *items,
                          int slot_size,
                          BsBarTrayItemActivateFn on_activate,
                          BsBarTrayItemMenuFn on_menu,
                          gpointer user_data) {
  g_return_if_fail(strip != NULL);

  bs_bar_tray_strip_clear(strip);
  if (items == NULL) {
    return;
  }

  for (guint i = 0; i < items->len; i++) {
    const BsBarTrayItemView *item = g_ptr_array_index((GPtrArray *) items, i);
    GtkWidget *button = NULL;

    if (item == NULL || !item->show_by_default) {
      continue;
    }

    button = bs_bar_tray_item_button_new(item, slot_size, on_activate, on_menu, user_data);
    gtk_box_append(GTK_BOX(strip), button);
  }
}

GtkWidget *
bs_bar_tray_strip_find_item_button(GtkWidget *strip, const char *item_id) {
  GtkWidget *child = NULL;

  g_return_val_if_fail(strip != NULL, NULL);
  g_return_val_if_fail(item_id != NULL, NULL);

  child = gtk_widget_get_first_child(strip);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    const char *button_item_id = NULL;

    button_item_id = bs_bar_tray_item_button_item_id(child);
    if (g_strcmp0(button_item_id, item_id) == 0) {
      return child;
    }
    child = next;
  }

  return NULL;
}
