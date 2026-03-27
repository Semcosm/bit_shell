#include "frontends/bit_bar/tray_item_button.h"
#include "frontends/bit_bar/tray_icon_resolver.h"

typedef struct {
  char *item_id;
  BsBarTrayPrimaryAction primary_action;
  gboolean has_context_menu;
  BsBarTrayItemActivateFn on_activate;
  BsBarTrayItemMenuFn on_menu;
  gpointer user_data;
} BsBarTrayItemButtonState;

static const char *BS_BAR_TRAY_ITEM_BUTTON_STATE_KEY = "bs-bar-tray-item-button-state";

static GtkWidget *bs_bar_tray_item_button_build_content(const BsBarTrayItemView *item,
                                                        int slot_size);
static void bs_bar_tray_item_button_apply_affordance_classes(GtkWidget *button,
                                                             const BsBarTrayItemView *item);
static void bs_bar_tray_item_button_clear_affordance_classes(GtkWidget *button);
static gboolean bs_bar_tray_item_button_get_anchor(GtkWidget *button, int *out_x, int *out_y);
static void bs_bar_tray_item_button_state_free(gpointer data);
static void bs_bar_tray_item_button_on_pressed(GtkGestureClick *gesture,
                                               gint n_press,
                                               gdouble x,
                                               gdouble y,
                                               gpointer user_data);

static GtkWidget *
bs_bar_tray_item_button_build_content(const BsBarTrayItemView *item, int slot_size) {
  GtkWidget *box = NULL;
  GtkWidget *image = NULL;
  GtkWidget *picture = NULL;
  GtkWidget *label = NULL;
  BsBarTrayResolvedIcon resolved = {0};

  g_return_val_if_fail(item != NULL, NULL);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(box, slot_size, slot_size);

  bs_bar_tray_icon_resolve(item, slot_size, &resolved);
  if (resolved.kind == BS_BAR_TRAY_ICON_THEME && resolved.icon_name != NULL) {
    image = gtk_image_new_from_icon_name(resolved.icon_name);
    gtk_widget_add_css_class(image, "bit-bar-tray-icon");
    gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
    gtk_box_append(GTK_BOX(box), image);
    bs_bar_tray_resolved_icon_clear(&resolved);
    return box;
  }

  if (resolved.kind == BS_BAR_TRAY_ICON_TEXTURE && resolved.texture != NULL) {
    picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(resolved.texture));
    gtk_widget_add_css_class(picture, "bit-bar-tray-icon");
    gtk_widget_set_size_request(picture, slot_size, slot_size);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), true);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_box_append(GTK_BOX(box), picture);
    bs_bar_tray_resolved_icon_clear(&resolved);
    return box;
  }

  label = gtk_label_new(resolved.fallback_label != NULL ? resolved.fallback_label : "?");
  gtk_widget_add_css_class(label, "bit-bar-tray-fallback");
  gtk_label_set_single_line_mode(GTK_LABEL(label), true);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), label);
  bs_bar_tray_resolved_icon_clear(&resolved);
  return box;
}

static void
bs_bar_tray_item_button_clear_affordance_classes(GtkWidget *button) {
  static const char *classes[] = {
    "bit-bar-tray-item",
    "needs-attention",
    "is-active",
    "is-passive",
    "can-activate",
    "can-context-menu",
    "is-menu-item",
    "is-disabled",
  };

  g_return_if_fail(button != NULL);

  for (guint i = 0; i < G_N_ELEMENTS(classes); i++) {
    gtk_widget_remove_css_class(button, classes[i]);
  }
}

static void
bs_bar_tray_item_button_apply_affordance_classes(GtkWidget *button, const BsBarTrayItemView *item) {
  g_return_if_fail(button != NULL);
  g_return_if_fail(item != NULL);

  gtk_widget_add_css_class(button, "bit-bar-tray-item");
  if (item->visual_state == BS_BAR_TRAY_VISUAL_ATTENTION) {
    gtk_widget_add_css_class(button, "needs-attention");
  } else if (item->visual_state == BS_BAR_TRAY_VISUAL_ACTIVE) {
    gtk_widget_add_css_class(button, "is-active");
  } else {
    gtk_widget_add_css_class(button, "is-passive");
  }
  if (item->primary_action == BS_BAR_TRAY_PRIMARY_ACTIVATE) {
    gtk_widget_add_css_class(button, "can-activate");
  }
  if (item->has_context_menu) {
    gtk_widget_add_css_class(button, "can-context-menu");
  }
  if (item->item_is_menu) {
    gtk_widget_add_css_class(button, "is-menu-item");
  }
  if (item->primary_action == BS_BAR_TRAY_PRIMARY_NONE && !item->has_context_menu) {
    gtk_widget_add_css_class(button, "is-disabled");
  }
}

static gboolean
bs_bar_tray_item_button_get_anchor(GtkWidget *button, int *out_x, int *out_y) {
  GtkNative *native = NULL;
  GtkWidget *native_widget = NULL;
  graphene_point_t src = GRAPHENE_POINT_INIT_ZERO;
  graphene_point_t dest = GRAPHENE_POINT_INIT_ZERO;

  g_return_val_if_fail(button != NULL, false);
  g_return_val_if_fail(out_x != NULL, false);
  g_return_val_if_fail(out_y != NULL, false);

  native = gtk_widget_get_native(button);
  if (native == NULL) {
    return false;
  }

  native_widget = GTK_WIDGET(native);
  src.x = (float) gtk_widget_get_width(button) / 2.0f;
  src.y = (float) gtk_widget_get_height(button);
  if (!gtk_widget_compute_point(button, native_widget, &src, &dest)) {
    return false;
  }

  *out_x = (int) dest.x;
  *out_y = (int) dest.y;
  return true;
}

static void
bs_bar_tray_item_button_state_free(gpointer data) {
  BsBarTrayItemButtonState *state = data;

  if (state == NULL) {
    return;
  }

  g_clear_pointer(&state->item_id, g_free);
  g_free(state);
}

static void
bs_bar_tray_item_button_on_pressed(GtkGestureClick *gesture,
                                   gint n_press,
                                   gdouble x,
                                   gdouble y,
                                   gpointer user_data) {
  GtkWidget *button = NULL;
  BsBarTrayItemButtonState *state = NULL;
  guint mouse_button = 0;
  int anchor_x = 0;
  int anchor_y = 0;

  (void) n_press;
  (void) x;
  (void) y;
  (void) user_data;

  g_return_if_fail(GTK_IS_GESTURE_CLICK(gesture));

  button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  if (button == NULL) {
    return;
  }

  state = g_object_get_data(G_OBJECT(button), BS_BAR_TRAY_ITEM_BUTTON_STATE_KEY);
  if (state == NULL || state->item_id == NULL || *state->item_id == '\0') {
    return;
  }

  mouse_button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  if (!bs_bar_tray_item_button_get_anchor(button, &anchor_x, &anchor_y)) {
    anchor_x = 0;
    anchor_y = 0;
  }

  if (mouse_button == GDK_BUTTON_PRIMARY) {
    if (state->primary_action == BS_BAR_TRAY_PRIMARY_ACTIVATE) {
      if (state->on_activate != NULL) {
        g_object_set_data(G_OBJECT(button), "bs-bar-tray-anchor-x", GINT_TO_POINTER(anchor_x));
        g_object_set_data(G_OBJECT(button), "bs-bar-tray-anchor-y", GINT_TO_POINTER(anchor_y));
        state->on_activate(button, state->item_id, state->user_data);
      }
    } else if (state->primary_action == BS_BAR_TRAY_PRIMARY_MENU) {
      if (state->on_menu != NULL) {
        g_object_set_data(G_OBJECT(button), "bs-bar-tray-anchor-x", GINT_TO_POINTER(anchor_x));
        g_object_set_data(G_OBJECT(button), "bs-bar-tray-anchor-y", GINT_TO_POINTER(anchor_y));
        state->on_menu(button, state->item_id, state->user_data);
      }
    }
  } else if (mouse_button == GDK_BUTTON_SECONDARY && state->has_context_menu) {
    if (state->on_menu != NULL) {
      g_object_set_data(G_OBJECT(button), "bs-bar-tray-anchor-x", GINT_TO_POINTER(anchor_x));
      g_object_set_data(G_OBJECT(button), "bs-bar-tray-anchor-y", GINT_TO_POINTER(anchor_y));
      state->on_menu(button, state->item_id, state->user_data);
    }
  }
}

GtkWidget *
bs_bar_tray_item_button_new(const BsBarTrayItemView *item,
                            int slot_size,
                            BsBarTrayItemActivateFn on_activate,
                            BsBarTrayItemMenuFn on_menu,
                            gpointer user_data) {
  GtkWidget *button = NULL;
  GtkGesture *gesture = NULL;
  BsBarTrayItemButtonState *state = NULL;

  g_return_val_if_fail(item != NULL, NULL);

  button = gtk_button_new();
  state = g_new0(BsBarTrayItemButtonState, 1);
  state->on_activate = on_activate;
  state->on_menu = on_menu;
  state->user_data = user_data;
  g_object_set_data_full(G_OBJECT(button),
                         BS_BAR_TRAY_ITEM_BUTTON_STATE_KEY,
                         state,
                         bs_bar_tray_item_button_state_free);

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
  g_signal_connect(gesture,
                   "pressed",
                   G_CALLBACK(bs_bar_tray_item_button_on_pressed),
                   NULL);
  gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(gesture));

  bs_bar_tray_item_button_update(button, item, slot_size);
  return button;
}

void
bs_bar_tray_item_button_update(GtkWidget *button,
                               const BsBarTrayItemView *item,
                               int slot_size) {
  BsBarTrayItemButtonState *state = NULL;
  GtkWidget *content = NULL;
  GtkWidget *existing_child = NULL;

  g_return_if_fail(button != NULL);
  g_return_if_fail(item != NULL);

  state = g_object_get_data(G_OBJECT(button), BS_BAR_TRAY_ITEM_BUTTON_STATE_KEY);
  g_return_if_fail(state != NULL);

  existing_child = gtk_button_get_child(GTK_BUTTON(button));
  if (existing_child != NULL) {
    gtk_button_set_child(GTK_BUTTON(button), NULL);
  }

  content = bs_bar_tray_item_button_build_content(item, slot_size);
  bs_bar_tray_item_button_clear_affordance_classes(button);
  bs_bar_tray_item_button_apply_affordance_classes(button, item);
  gtk_button_set_child(GTK_BUTTON(button), content);
  gtk_widget_set_size_request(button, slot_size, slot_size);
  gtk_widget_set_tooltip_text(button,
                              item->title != NULL && *item->title != '\0' ? item->title : item->item_id);

  g_clear_pointer(&state->item_id, g_free);
  state->item_id = g_strdup(item->item_id);
  state->primary_action = item->primary_action;
  state->has_context_menu = item->has_context_menu;
}
