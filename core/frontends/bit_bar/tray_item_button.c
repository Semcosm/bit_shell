#include "frontends/bit_bar/tray_item_button.h"

#include "frontends/bit_bar/tray_icon_resolver.h"

typedef struct _BsBarTrayItemButtonWidget BsBarTrayItemButtonWidget;
typedef struct _BsBarTrayItemButtonWidgetClass BsBarTrayItemButtonWidgetClass;

#define BS_TYPE_BAR_TRAY_ITEM_BUTTON_WIDGET (bs_bar_tray_item_button_widget_get_type())
#define BS_BAR_TRAY_ITEM_BUTTON_WIDGET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), BS_TYPE_BAR_TRAY_ITEM_BUTTON_WIDGET, BsBarTrayItemButtonWidget))
#define BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), BS_TYPE_BAR_TRAY_ITEM_BUTTON_WIDGET))

struct _BsBarTrayItemButtonWidget {
  GtkWidget parent_instance;
  char *item_id;
  BsBarTrayPrimaryAction primary_action;
  gboolean has_context_menu;
  int slot_size;
  BsBarTrayItemActivateFn on_activate;
  BsBarTrayItemMenuFn on_menu;
  gpointer user_data;
  GtkWidget *button_child;
  GtkPopover *popover;
};

struct _BsBarTrayItemButtonWidgetClass {
  GtkWidgetClass parent_class;
};

G_DEFINE_FINAL_TYPE(BsBarTrayItemButtonWidget,
                    bs_bar_tray_item_button_widget,
                    GTK_TYPE_WIDGET)

static GtkWidget *bs_bar_tray_item_button_build_content(const BsBarTrayItemView *item,
                                                        int slot_size);
static void bs_bar_tray_item_button_apply_affordance_classes(GtkWidget *button,
                                                             const BsBarTrayItemView *item);
static void bs_bar_tray_item_button_clear_affordance_classes(GtkWidget *button);
static void bs_bar_tray_item_button_widget_measure(GtkWidget *widget,
                                                   GtkOrientation orientation,
                                                   int for_size,
                                                   int *minimum,
                                                   int *natural,
                                                   int *minimum_baseline,
                                                   int *natural_baseline);
static void bs_bar_tray_item_button_widget_size_allocate(GtkWidget *widget,
                                                         int width,
                                                         int height,
                                                         int baseline);
static void bs_bar_tray_item_button_widget_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);
static void bs_bar_tray_item_button_widget_dispose(GObject *object);
static void bs_bar_tray_item_button_widget_finalize(GObject *object);
static void bs_bar_tray_item_button_apply_button_geometry(BsBarTrayItemButtonWidget *self);
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

static void
bs_bar_tray_item_button_widget_measure(GtkWidget *widget,
                                       GtkOrientation orientation,
                                       int for_size,
                                       int *minimum,
                                       int *natural,
                                       int *minimum_baseline,
                                       int *natural_baseline) {
  BsBarTrayItemButtonWidget *self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(widget);

  g_return_if_fail(minimum != NULL);
  g_return_if_fail(natural != NULL);

  if (self->button_child == NULL) {
    *minimum = 0;
    *natural = 0;
    if (minimum_baseline != NULL) {
      *minimum_baseline = -1;
    }
    if (natural_baseline != NULL) {
      *natural_baseline = -1;
    }
    return;
  }

  gtk_widget_measure(self->button_child,
                     orientation,
                     for_size,
                     minimum,
                     natural,
                     minimum_baseline,
                     natural_baseline);
}

static void
bs_bar_tray_item_button_widget_size_allocate(GtkWidget *widget,
                                             int width,
                                             int height,
                                             int baseline) {
  BsBarTrayItemButtonWidget *self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(widget);

  if (self->button_child != NULL) {
    gtk_widget_allocate(self->button_child, width, height, baseline, NULL);
  }
  if (self->popover != NULL && gtk_widget_get_visible(GTK_WIDGET(self->popover))) {
    gtk_popover_present(self->popover);
  }
}

static void
bs_bar_tray_item_button_widget_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  BsBarTrayItemButtonWidget *self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(widget);

  g_return_if_fail(snapshot != NULL);

  if (self->button_child != NULL) {
    gtk_widget_snapshot_child(widget, self->button_child, snapshot);
  }
  if (self->popover != NULL && gtk_widget_get_visible(GTK_WIDGET(self->popover))) {
    gtk_widget_snapshot_child(widget, GTK_WIDGET(self->popover), snapshot);
  }
}

static void
bs_bar_tray_item_button_widget_dispose(GObject *object) {
  BsBarTrayItemButtonWidget *self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(object);

  if (self->popover != NULL) {
    gtk_popover_popdown(self->popover);
    if (gtk_popover_get_child(self->popover) != NULL) {
      gtk_popover_set_child(self->popover, NULL);
    }
    gtk_widget_unparent(GTK_WIDGET(self->popover));
    self->popover = NULL;
  }
  if (self->button_child != NULL) {
    gtk_widget_unparent(self->button_child);
    self->button_child = NULL;
  }

  G_OBJECT_CLASS(bs_bar_tray_item_button_widget_parent_class)->dispose(object);
}

static void
bs_bar_tray_item_button_widget_finalize(GObject *object) {
  BsBarTrayItemButtonWidget *self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(object);

  g_clear_pointer(&self->item_id, g_free);
  G_OBJECT_CLASS(bs_bar_tray_item_button_widget_parent_class)->finalize(object);
}

static void
bs_bar_tray_item_button_apply_button_geometry(BsBarTrayItemButtonWidget *self) {
  GtkWidget *content = NULL;

  g_return_if_fail(self != NULL);
  g_return_if_fail(self->button_child != NULL);

  gtk_widget_set_size_request(GTK_WIDGET(self), self->slot_size, self->slot_size);
  gtk_widget_set_size_request(self->button_child, self->slot_size, self->slot_size);
  content = gtk_button_get_child(GTK_BUTTON(self->button_child));
  if (content != NULL) {
    gtk_widget_set_size_request(content, self->slot_size, self->slot_size);
  }
}

static void
bs_bar_tray_item_button_on_pressed(GtkGestureClick *gesture,
                                   gint n_press,
                                   gdouble x,
                                   gdouble y,
                                   gpointer user_data) {
  BsBarTrayItemButtonWidget *self = user_data;
  guint mouse_button = 0;

  (void) n_press;
  (void) x;
  (void) y;

  g_return_if_fail(GTK_IS_GESTURE_CLICK(gesture));
  g_return_if_fail(self != NULL);

  if (self->item_id == NULL || *self->item_id == '\0') {
    return;
  }

  mouse_button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  if (mouse_button == GDK_BUTTON_PRIMARY) {
    if (self->primary_action == BS_BAR_TRAY_PRIMARY_ACTIVATE) {
      if (self->on_activate != NULL) {
        self->on_activate(GTK_WIDGET(self), self->item_id, self->user_data);
      }
    } else if (self->primary_action == BS_BAR_TRAY_PRIMARY_MENU) {
      if (self->on_menu != NULL) {
        self->on_menu(GTK_WIDGET(self), self->item_id, self->user_data);
      }
    }
  } else if (mouse_button == GDK_BUTTON_SECONDARY && self->has_context_menu) {
    if (self->on_menu != NULL) {
      self->on_menu(GTK_WIDGET(self), self->item_id, self->user_data);
    }
  }
}

static void
bs_bar_tray_item_button_widget_class_init(BsBarTrayItemButtonWidgetClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  widget_class->measure = bs_bar_tray_item_button_widget_measure;
  widget_class->size_allocate = bs_bar_tray_item_button_widget_size_allocate;
  widget_class->snapshot = bs_bar_tray_item_button_widget_snapshot;
  object_class->dispose = bs_bar_tray_item_button_widget_dispose;
  object_class->finalize = bs_bar_tray_item_button_widget_finalize;
}

static void
bs_bar_tray_item_button_widget_init(BsBarTrayItemButtonWidget *self) {
  GtkGesture *gesture = NULL;

  self->slot_size = 24;
  self->button_child = gtk_button_new();
  self->popover = GTK_POPOVER(gtk_popover_new());

  gtk_widget_set_focusable(GTK_WIDGET(self), FALSE);
  gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_VISIBLE);
  gtk_widget_set_parent(self->button_child, GTK_WIDGET(self));
  gtk_widget_set_parent(GTK_WIDGET(self->popover), GTK_WIDGET(self));
  gtk_popover_set_position(self->popover, GTK_POS_BOTTOM);
  gtk_popover_set_autohide(self->popover, true);
  gtk_popover_set_cascade_popdown(self->popover, true);
  gtk_popover_set_has_arrow(self->popover, false);
  gtk_widget_add_css_class(GTK_WIDGET(self->popover), "bit-bar-tray-menu-popover");

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
  g_signal_connect(gesture,
                   "pressed",
                   G_CALLBACK(bs_bar_tray_item_button_on_pressed),
                   self);
  gtk_widget_add_controller(self->button_child, GTK_EVENT_CONTROLLER(gesture));

  bs_bar_tray_item_button_apply_button_geometry(self);
}

GtkWidget *
bs_bar_tray_item_button_new(const BsBarTrayItemView *item,
                            int slot_size,
                            BsBarTrayItemActivateFn on_activate,
                            BsBarTrayItemMenuFn on_menu,
                            gpointer user_data) {
  BsBarTrayItemButtonWidget *self = NULL;

  g_return_val_if_fail(item != NULL, NULL);

  self = g_object_new(bs_bar_tray_item_button_widget_get_type(), NULL);
  self->on_activate = on_activate;
  self->on_menu = on_menu;
  self->user_data = user_data;
  bs_bar_tray_item_button_update(GTK_WIDGET(self), item, slot_size);
  return GTK_WIDGET(self);
}

void
bs_bar_tray_item_button_apply_slot_size(GtkWidget *button, int slot_size) {
  BsBarTrayItemButtonWidget *self = NULL;

  if (!BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(button)) {
    return;
  }

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(button);
  self->slot_size = slot_size;
  bs_bar_tray_item_button_apply_button_geometry(self);
  gtk_widget_queue_resize(button);
}

void
bs_bar_tray_item_button_update(GtkWidget *button,
                               const BsBarTrayItemView *item,
                               int slot_size) {
  BsBarTrayItemButtonWidget *self = NULL;
  GtkWidget *content = NULL;
  GtkWidget *existing_child = NULL;

  g_return_if_fail(BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(button));
  g_return_if_fail(item != NULL);

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(button);
  self->slot_size = slot_size;

  existing_child = gtk_button_get_child(GTK_BUTTON(self->button_child));
  if (existing_child != NULL) {
    gtk_button_set_child(GTK_BUTTON(self->button_child), NULL);
  }

  content = bs_bar_tray_item_button_build_content(item, slot_size);
  bs_bar_tray_item_button_clear_affordance_classes(self->button_child);
  bs_bar_tray_item_button_apply_affordance_classes(self->button_child, item);
  gtk_button_set_child(GTK_BUTTON(self->button_child), content);
  gtk_widget_set_tooltip_text(GTK_WIDGET(self),
                              item->title != NULL && *item->title != '\0' ? item->title : item->item_id);
  gtk_widget_set_tooltip_text(self->button_child,
                              item->title != NULL && *item->title != '\0' ? item->title : item->item_id);

  g_clear_pointer(&self->item_id, g_free);
  self->item_id = g_strdup(item->item_id);
  self->primary_action = item->primary_action;
  self->has_context_menu = item->has_context_menu;
  bs_bar_tray_item_button_apply_button_geometry(self);
}

const char *
bs_bar_tray_item_button_item_id(GtkWidget *button) {
  BsBarTrayItemButtonWidget *self = NULL;

  if (!BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(button)) {
    return NULL;
  }

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(button);
  return self->item_id;
}

gboolean
bs_bar_tray_item_button_present_menu(GtkWidget *item_widget, GtkWidget *content) {
  BsBarTrayItemButtonWidget *self = NULL;

  if (!BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget)) {
    return FALSE;
  }
  g_return_val_if_fail(GTK_IS_WIDGET(content), FALSE);

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget);
  gtk_popover_set_child(self->popover, content);
  gtk_popover_popup(self->popover);
  gtk_popover_present(self->popover);
  return TRUE;
}

void
bs_bar_tray_item_button_close_menu(GtkWidget *item_widget) {
  BsBarTrayItemButtonWidget *self = NULL;

  if (!BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget)) {
    return;
  }

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget);
  gtk_popover_popdown(self->popover);
}

gboolean
bs_bar_tray_item_button_is_menu_open(GtkWidget *item_widget) {
  BsBarTrayItemButtonWidget *self = NULL;

  if (!BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget)) {
    return FALSE;
  }

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget);
  return gtk_widget_get_visible(GTK_WIDGET(self->popover));
}

gboolean
bs_bar_tray_item_button_get_anchor(GtkWidget *item_widget, int *out_x, int *out_y) {
  BsBarTrayItemButtonWidget *self = NULL;
  GtkNative *native = NULL;
  GtkWidget *native_widget = NULL;
  graphene_point_t src = GRAPHENE_POINT_INIT_ZERO;
  graphene_point_t dest = GRAPHENE_POINT_INIT_ZERO;
  GtkWidget *anchor_widget = NULL;

  if (!BS_IS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget)) {
    return FALSE;
  }
  g_return_val_if_fail(out_x != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  self = BS_BAR_TRAY_ITEM_BUTTON_WIDGET(item_widget);
  anchor_widget = self->button_child != NULL ? self->button_child : item_widget;
  native = gtk_widget_get_native(anchor_widget);
  if (native == NULL) {
    return FALSE;
  }

  native_widget = GTK_WIDGET(native);
  src.x = (float) gtk_widget_get_width(anchor_widget) / 2.0f;
  src.y = (float) gtk_widget_get_height(anchor_widget);
  if (!gtk_widget_compute_point(anchor_widget, native_widget, &src, &dest)) {
    return FALSE;
  }

  *out_x = (int) dest.x;
  *out_y = (int) dest.y;
  return TRUE;
}
