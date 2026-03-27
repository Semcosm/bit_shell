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
    GtkWidget *content = NULL;
    GtkWidget *next = gtk_widget_get_next_sibling(child);

    gtk_widget_set_size_request(child, slot_size, slot_size);
    content = GTK_IS_BUTTON(child) ? gtk_button_get_child(GTK_BUTTON(child)) : child;
    if (content != NULL) {
      gtk_widget_set_size_request(content, slot_size, slot_size);
    }
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
