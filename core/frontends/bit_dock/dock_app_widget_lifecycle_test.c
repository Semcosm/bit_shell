#include "frontends/bit_dock/dock_layout.h"
#include "model/config.h"

static gboolean test_gtk_available = FALSE;

#include "frontends/bit_dock/dock_app.c"

static gboolean
test_require_gtk(void) {
  if (!test_gtk_available) {
    g_test_skip("GTK display is not available");
    return FALSE;
  }

  return TRUE;
}

static void
test_dock_app_init_test_state(BsDockApp *app) {
  g_return_if_fail(app != NULL);

  bs_dock_config_init_defaults(&app->config);
  bs_dock_metrics_derive(&app->metrics, &app->config);
}

static BsDockItemView
test_dock_item_view(const char *app_key,
                    const char *name,
                    const char *icon_name) {
  return (BsDockItemView) {
    .app_key = (char *) app_key,
    .name = (char *) name,
    .icon_name = (char *) icon_name,
  };
}

static void
test_dock_item_widget_tree_stays_stable_across_mode_switches(void) {
  BsDockApp app = {0};
  BsDockItemView icon_item = {0};
  BsDockItemView label_item = {0};
  BsDockItemView icon_again_item = {0};
  BsDockItemWidgets *widgets = NULL;

  if (!test_require_gtk()) {
    return;
  }

  test_dock_app_init_test_state(&app);
  icon_item = test_dock_item_view("org.test.App", "Test App", "folder");
  label_item = test_dock_item_view("org.test.App", "Fallback", NULL);
  icon_again_item = test_dock_item_view("org.test.App", "Test App", "edit-copy");

  widgets = bs_dock_app_create_item_widgets(&app, &icon_item);
  g_assert_nonnull(widgets);
  g_assert_true(GTK_IS_STACK(widgets->content_stack));
  g_assert_true(GTK_IS_IMAGE(widgets->icon_image));
  g_assert_true(GTK_IS_LABEL(widgets->label));
  g_assert_true(gtk_button_get_child(GTK_BUTTON(widgets->button)) == widgets->content_stack);
  g_assert_cmpstr(gtk_stack_get_visible_child_name(GTK_STACK(widgets->content_stack)), ==, "icon");

  bs_dock_app_update_item_widgets(&app, widgets, &label_item);
  g_assert_true(GTK_IS_IMAGE(widgets->icon_image));
  g_assert_true(GTK_IS_LABEL(widgets->label));
  g_assert_true(GTK_IS_STACK(widgets->content_stack));
  g_assert_true(gtk_button_get_child(GTK_BUTTON(widgets->button)) == widgets->content_stack);
  g_assert_cmpstr(gtk_stack_get_visible_child_name(GTK_STACK(widgets->content_stack)), ==, "label");

  bs_dock_app_apply_layout(&app, widgets);
  g_assert_true(GTK_IS_IMAGE(widgets->icon_image));

  bs_dock_app_update_item_widgets(&app, widgets, &icon_again_item);
  g_assert_true(GTK_IS_IMAGE(widgets->icon_image));
  g_assert_true(GTK_IS_LABEL(widgets->label));
  g_assert_true(gtk_button_get_child(GTK_BUTTON(widgets->button)) == widgets->content_stack);
  g_assert_cmpstr(gtk_stack_get_visible_child_name(GTK_STACK(widgets->content_stack)), ==, "icon");

  bs_dock_item_widgets_free(widgets);
}

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  test_gtk_available = gtk_init_check();

  g_test_add_func("/dock_app/widget_tree_stays_stable_across_mode_switches",
                  test_dock_item_widget_tree_stays_stable_across_mode_switches);
  return g_test_run();
}
