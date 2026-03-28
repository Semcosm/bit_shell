#include "frontends/bit_bar/bar_view_model.h"
#include "frontends/bit_bar/tray_controller.h"
#include "frontends/bit_bar/tray_menu_view.h"

#include "model/snapshot.h"
#include "model/tray_menu.h"
#include "model/types.h"

typedef struct {
  char *item_id;
  gboolean menu_open;
  guint present_count;
  guint close_count;
} TestTrayButton;

typedef struct {
  GHashTable *buttons_by_item_id;
  guint refresh_request_count;
  guint context_menu_request_count;
  guint menu_view_new_count;
  gboolean should_compute_constraints;
  BsBarTrayMenuSizeConstraints computed_constraints;
  BsBarTrayMenuSizeConstraints last_constraints;
  char *last_refresh_item_id;
  char *last_context_menu_item_id;
} TestTrayHarness;

typedef struct {
  gboolean include_item_a;
  gboolean include_item_b;
  gboolean menu_object_a;
  gboolean menu_object_b;
  gboolean menu_tree_a;
  gboolean menu_tree_b;
} TestTraySnapshotSpec;

typedef struct {
  BsBarViewModel *vm;
  BsBarTrayController *controller;
  TestTrayHarness harness;
} TestTrayFixture;

static gboolean test_gtk_available = FALSE;
static TestTrayHarness *test_active_harness = NULL;

static GtkWidget *test_bs_bar_tray_menu_view_new(const BsTrayMenuTree *tree,
                                                 const BsBarTrayMenuSizeConstraints *constraints,
                                                 BsBarTrayMenuActivateFn activate_cb,
                                                 BsBarTrayMenuRequestCloseFn close_cb,
                                                 gpointer user_data);
static gboolean test_bs_bar_tray_controller_compute_menu_constraints(GtkWidget *button,
                                                                     BsBarTrayMenuSizeConstraints *out);
static gboolean test_bs_bar_tray_item_button_present_menu(GtkWidget *item_widget, GtkWidget *content);
static void test_bs_bar_tray_item_button_close_menu(GtkWidget *item_widget);
static gboolean test_bs_bar_tray_item_button_get_anchor(GtkWidget *item_widget,
                                                        int *out_x,
                                                        int *out_y);

#define bs_bar_tray_menu_view_new test_bs_bar_tray_menu_view_new
#define BS_BAR_TRAY_CONTROLLER_COMPUTE_MENU_CONSTRAINTS(button, out) \
  test_bs_bar_tray_controller_compute_menu_constraints((button), (out))
#define bs_bar_tray_item_button_present_menu test_bs_bar_tray_item_button_present_menu
#define bs_bar_tray_item_button_close_menu test_bs_bar_tray_item_button_close_menu
#define bs_bar_tray_item_button_get_anchor test_bs_bar_tray_item_button_get_anchor
#include "frontends/bit_bar/tray_controller.c"
#undef bs_bar_tray_menu_view_new
#undef BS_BAR_TRAY_CONTROLLER_COMPUTE_MENU_CONSTRAINTS
#undef bs_bar_tray_item_button_present_menu
#undef bs_bar_tray_item_button_close_menu
#undef bs_bar_tray_item_button_get_anchor

G_GNUC_UNUSED static gboolean (*const test_keep_tray_constraint_helper)(GtkWidget *,
                                                                        BsBarTrayMenuSizeConstraints *) =
  bs_bar_tray_controller_compute_menu_constraints;

static BsTrayMenuTree *make_test_tree(BsTrayMenuItemKind child_kind, gboolean visible);
static BsTrayMenuNode *test_tray_menu_node_new(BsTrayMenuItemKind kind,
                                               const char *label,
                                               gboolean visible,
                                               gboolean enabled);
static BsTrayMenuTree *make_menu_tree_with_label(const char *label);
static BsTrayMenuTree *make_menu_tree_with_submenu(const char *submenu_label,
                                                   const char *child_label);
static BsTrayMenuTree *make_controller_menu_tree(const char *item_id, guint revision);
static TestTrayButton *test_tray_button_new(const char *item_id);
static void test_tray_button_free(gpointer data);
static void test_tray_harness_init(TestTrayHarness *harness);
static void test_tray_harness_clear(TestTrayHarness *harness);
static void test_tray_fixture_init(TestTrayFixture *fixture);
static void test_tray_fixture_clear(TestTrayFixture *fixture);
static TestTrayButton *test_tray_fixture_add_button(TestTrayFixture *fixture, const char *item_id);
static void test_tray_snapshot_add_item(BsSnapshot *snapshot,
                                        const char *item_id,
                                        gboolean has_menu_object);
static void test_tray_snapshot_fill(BsSnapshot *snapshot, gpointer user_data);
static void test_tray_fixture_apply_snapshot(TestTrayFixture *fixture,
                                             const TestTraySnapshotSpec *spec);
static void test_request_activate(const char *item_id, int x, int y, gpointer user_data);
static void test_request_context_menu(const char *item_id, int x, int y, gpointer user_data);
static void test_request_menu_refresh(const char *item_id, gpointer user_data);
static void test_request_menu_activate(const char *item_id,
                                       gint32 menu_item_id,
                                       gpointer user_data);
static GtkWidget *test_lookup_button(const char *item_id, gpointer user_data);
static GtkWidget *test_widget_nth_child(GtkWidget *parent, guint index);
static GtkWidget *test_menu_view_get_header(GtkWidget *menu_view);
static GtkWidget *test_menu_view_get_scroller(GtkWidget *menu_view);
static GtkWidget *test_menu_view_get_content_box(GtkWidget *menu_view);
static GtkWidget *test_menu_view_get_row_button(GtkWidget *menu_view, guint index);
static GtkWidget *test_menu_view_get_row_label(GtkWidget *menu_view, guint index);
static gboolean test_require_gtk(void);

static BsTrayMenuTree *
make_test_tree(BsTrayMenuItemKind child_kind, gboolean visible) {
  BsTrayMenuTree *tree = g_new0(BsTrayMenuTree, 1);
  BsTrayMenuNode *root = g_new0(BsTrayMenuNode, 1);
  BsTrayMenuNode *child = g_new0(BsTrayMenuNode, 1);

  root->kind = BS_TRAY_MENU_ITEM_SUBMENU;
  root->visible = TRUE;
  root->enabled = TRUE;
  root->children = g_ptr_array_new_with_free_func((GDestroyNotify) bs_tray_menu_node_free);

  child->kind = child_kind;
  child->visible = visible;
  child->enabled = TRUE;
  child->children = g_ptr_array_new_with_free_func((GDestroyNotify) bs_tray_menu_node_free);
  g_ptr_array_add(root->children, child);

  tree->root = root;
  return tree;
}

static BsTrayMenuNode *
test_tray_menu_node_new(BsTrayMenuItemKind kind,
                        const char *label,
                        gboolean visible,
                        gboolean enabled) {
  BsTrayMenuNode *node = g_new0(BsTrayMenuNode, 1);

  node->id = 1;
  node->kind = kind;
  node->label = g_strdup(label);
  node->visible = visible;
  node->enabled = enabled;
  node->children = g_ptr_array_new_with_free_func((GDestroyNotify) bs_tray_menu_node_free);
  return node;
}

static BsTrayMenuTree *
make_menu_tree_with_label(const char *label) {
  BsTrayMenuTree *tree = g_new0(BsTrayMenuTree, 1);
  BsTrayMenuNode *root = NULL;

  root = test_tray_menu_node_new(BS_TRAY_MENU_ITEM_SUBMENU, "root", TRUE, TRUE);
  g_ptr_array_add(root->children,
                  test_tray_menu_node_new(BS_TRAY_MENU_ITEM_NORMAL, label, TRUE, TRUE));
  tree->item_id = g_strdup("item-a");
  tree->root = root;
  return tree;
}

static BsTrayMenuTree *
make_menu_tree_with_submenu(const char *submenu_label, const char *child_label) {
  BsTrayMenuTree *tree = g_new0(BsTrayMenuTree, 1);
  BsTrayMenuNode *root = NULL;
  BsTrayMenuNode *submenu = NULL;

  root = test_tray_menu_node_new(BS_TRAY_MENU_ITEM_SUBMENU, "root", TRUE, TRUE);
  submenu = test_tray_menu_node_new(BS_TRAY_MENU_ITEM_SUBMENU, submenu_label, TRUE, TRUE);
  submenu->id = 11;
  g_ptr_array_add(submenu->children,
                  test_tray_menu_node_new(BS_TRAY_MENU_ITEM_NORMAL, child_label, TRUE, TRUE));
  g_ptr_array_add(root->children, submenu);
  tree->item_id = g_strdup("item-a");
  tree->root = root;
  return tree;
}

static BsTrayMenuTree *
make_controller_menu_tree(const char *item_id, guint revision) {
  BsTrayMenuTree *tree = make_test_tree(BS_TRAY_MENU_ITEM_NORMAL, TRUE);

  tree->item_id = g_strdup(item_id);
  tree->revision = revision;
  return tree;
}

static TestTrayButton *
test_tray_button_new(const char *item_id) {
  TestTrayButton *button = g_new0(TestTrayButton, 1);

  button->item_id = g_strdup(item_id);
  return button;
}

static void
test_tray_button_free(gpointer data) {
  TestTrayButton *button = data;

  if (button == NULL) {
    return;
  }

  g_clear_pointer(&button->item_id, g_free);
  g_free(button);
}

static void
test_tray_harness_init(TestTrayHarness *harness) {
  g_return_if_fail(harness != NULL);

  harness->buttons_by_item_id = g_hash_table_new_full(g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      test_tray_button_free);
  harness->should_compute_constraints = TRUE;
  harness->computed_constraints = (BsBarTrayMenuSizeConstraints) {
    .min_width = 240,
    .max_width = 520,
    .max_height = 420,
  };
}

static void
test_tray_harness_clear(TestTrayHarness *harness) {
  if (harness == NULL) {
    return;
  }

  g_clear_pointer(&harness->buttons_by_item_id, g_hash_table_unref);
  g_clear_pointer(&harness->last_refresh_item_id, g_free);
  g_clear_pointer(&harness->last_context_menu_item_id, g_free);
}

static void
test_tray_fixture_init(TestTrayFixture *fixture) {
  BsBarTrayControllerOps ops = {0};

  g_return_if_fail(fixture != NULL);

  test_tray_harness_init(&fixture->harness);
  test_active_harness = &fixture->harness;
  fixture->vm = bs_bar_view_model_new();
  ops.request_activate = test_request_activate;
  ops.request_context_menu = test_request_context_menu;
  ops.request_menu_refresh = test_request_menu_refresh;
  ops.request_menu_activate = test_request_menu_activate;
  ops.lookup_button = test_lookup_button;
  ops.user_data = &fixture->harness;
  fixture->controller = bs_bar_tray_controller_new(fixture->vm, &ops);
}

static void
test_tray_fixture_clear(TestTrayFixture *fixture) {
  if (fixture == NULL) {
    return;
  }

  bs_bar_tray_controller_free(fixture->controller);
  bs_bar_view_model_free(fixture->vm);
  test_tray_harness_clear(&fixture->harness);
  if (test_active_harness == &fixture->harness) {
    test_active_harness = NULL;
  }
}

static TestTrayButton *
test_tray_fixture_add_button(TestTrayFixture *fixture, const char *item_id) {
  TestTrayButton *button = NULL;

  g_return_val_if_fail(fixture != NULL, NULL);
  g_return_val_if_fail(item_id != NULL, NULL);

  button = test_tray_button_new(item_id);
  g_hash_table_replace(fixture->harness.buttons_by_item_id,
                       g_strdup(item_id),
                       button);
  return button;
}

static void
test_tray_snapshot_add_item(BsSnapshot *snapshot, const char *item_id, gboolean has_menu_object) {
  BsTrayItem *item = NULL;

  g_return_if_fail(snapshot != NULL);
  g_return_if_fail(item_id != NULL);

  item = g_new0(BsTrayItem, 1);
  item->item_id = g_strdup(item_id);
  item->id = g_strdup(item_id);
  item->title = g_strdup(item_id);
  item->status = BS_TRAY_ITEM_STATUS_ACTIVE;
  item->menu_object_path = has_menu_object ? g_strdup("/StatusNotifierItem/Menu") : NULL;
  item->item_is_menu = TRUE;
  item->has_activate = FALSE;
  item->has_context_menu = TRUE;
  item->presentation_seq = 1;
  g_hash_table_replace(snapshot->tray_items, g_strdup(item->item_id), item);
}

static void
test_tray_snapshot_fill(BsSnapshot *snapshot, gpointer user_data) {
  const TestTraySnapshotSpec *spec = user_data;

  g_return_if_fail(snapshot != NULL);
  g_return_if_fail(spec != NULL);

  if (spec->include_item_a) {
    test_tray_snapshot_add_item(snapshot, "item-a", spec->menu_object_a);
    if (spec->menu_tree_a) {
      g_hash_table_replace(snapshot->tray_menus,
                           g_strdup("item-a"),
                           make_controller_menu_tree("item-a", 1));
    }
  }
  if (spec->include_item_b) {
    test_tray_snapshot_add_item(snapshot, "item-b", spec->menu_object_b);
    if (spec->menu_tree_b) {
      g_hash_table_replace(snapshot->tray_menus,
                           g_strdup("item-b"),
                           make_controller_menu_tree("item-b", 2));
    }
  }
}

static void
test_tray_fixture_apply_snapshot(TestTrayFixture *fixture, const TestTraySnapshotSpec *spec) {
  BsSnapshot snapshot = {0};
  g_autofree char *json = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail(fixture != NULL);
  g_return_if_fail(spec != NULL);

  bs_snapshot_init(&snapshot);
  test_tray_snapshot_fill(&snapshot, (gpointer) spec);
  json = bs_snapshot_serialize_json(&snapshot);
  g_assert_true(bs_bar_view_model_consume_json_line(fixture->vm, json, &error));
  g_assert_no_error(error);
  bs_snapshot_clear(&snapshot);
}

static void
test_request_activate(const char *item_id, int x, int y, gpointer user_data) {
  (void) item_id;
  (void) x;
  (void) y;
  (void) user_data;
}

static void
test_request_context_menu(const char *item_id, int x, int y, gpointer user_data) {
  TestTrayHarness *harness = user_data;

  (void) x;
  (void) y;

  g_return_if_fail(harness != NULL);
  g_return_if_fail(item_id != NULL);

  harness->context_menu_request_count++;
  g_clear_pointer(&harness->last_context_menu_item_id, g_free);
  harness->last_context_menu_item_id = g_strdup(item_id);
}

static void
test_request_menu_refresh(const char *item_id, gpointer user_data) {
  TestTrayHarness *harness = user_data;

  g_return_if_fail(harness != NULL);
  g_return_if_fail(item_id != NULL);

  harness->refresh_request_count++;
  g_clear_pointer(&harness->last_refresh_item_id, g_free);
  harness->last_refresh_item_id = g_strdup(item_id);
}

static void
test_request_menu_activate(const char *item_id, gint32 menu_item_id, gpointer user_data) {
  (void) item_id;
  (void) menu_item_id;
  (void) user_data;
}

static GtkWidget *
test_lookup_button(const char *item_id, gpointer user_data) {
  TestTrayHarness *harness = user_data;

  g_return_val_if_fail(harness != NULL, NULL);
  g_return_val_if_fail(item_id != NULL, NULL);

  return g_hash_table_lookup(harness->buttons_by_item_id, item_id);
}

static GtkWidget *
test_bs_bar_tray_menu_view_new(const BsTrayMenuTree *tree,
                               const BsBarTrayMenuSizeConstraints *constraints,
                               BsBarTrayMenuActivateFn activate_cb,
                               BsBarTrayMenuRequestCloseFn close_cb,
                               gpointer user_data) {
  static int dummy = 0;
  BsBarTrayController *controller = user_data;
  TestTrayHarness *harness = NULL;

  (void) tree;
  (void) activate_cb;
  (void) close_cb;

  g_return_val_if_fail(controller != NULL, NULL);
  harness = controller->ops.user_data;
  g_return_val_if_fail(harness != NULL, NULL);

  harness->menu_view_new_count++;
  if (constraints != NULL) {
    harness->last_constraints = *constraints;
  }

  return (GtkWidget *) &dummy;
}

static gboolean
test_bs_bar_tray_controller_compute_menu_constraints(GtkWidget *button,
                                                     BsBarTrayMenuSizeConstraints *out) {
  (void) button;

  g_return_val_if_fail(out != NULL, FALSE);
  g_return_val_if_fail(test_active_harness != NULL, FALSE);

  if (!test_active_harness->should_compute_constraints) {
    return FALSE;
  }

  *out = test_active_harness->computed_constraints;
  return TRUE;
}

static gboolean
test_bs_bar_tray_item_button_present_menu(GtkWidget *item_widget, GtkWidget *content) {
  TestTrayButton *button = (TestTrayButton *) item_widget;

  if (button == NULL || content == NULL) {
    return FALSE;
  }

  button->menu_open = TRUE;
  button->present_count++;
  return TRUE;
}

static void
test_bs_bar_tray_item_button_close_menu(GtkWidget *item_widget) {
  TestTrayButton *button = (TestTrayButton *) item_widget;

  if (button == NULL) {
    return;
  }

  button->menu_open = FALSE;
  button->close_count++;
}

static gboolean
test_bs_bar_tray_item_button_get_anchor(GtkWidget *item_widget, int *out_x, int *out_y) {
  TestTrayButton *button = (TestTrayButton *) item_widget;

  if (button == NULL || out_x == NULL || out_y == NULL) {
    return FALSE;
  }

  *out_x = 16;
  *out_y = 24;
  return TRUE;
}

static GtkWidget *
test_widget_nth_child(GtkWidget *parent, guint index) {
  GtkWidget *child = NULL;

  g_return_val_if_fail(GTK_IS_WIDGET(parent), NULL);

  child = gtk_widget_get_first_child(parent);
  for (guint i = 0; child != NULL && i < index; i++) {
    child = gtk_widget_get_next_sibling(child);
  }
  return child;
}

static GtkWidget *
test_menu_view_get_header(GtkWidget *menu_view) {
  return test_widget_nth_child(menu_view, 0);
}

static GtkWidget *
test_menu_view_get_scroller(GtkWidget *menu_view) {
  return test_widget_nth_child(menu_view, 1);
}

static GtkWidget *
test_menu_view_get_content_box(GtkWidget *menu_view) {
  GtkWidget *scroller = test_menu_view_get_scroller(menu_view);

  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(scroller), NULL);
  return gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scroller));
}

static GtkWidget *
test_menu_view_get_row_button(GtkWidget *menu_view, guint index) {
  GtkWidget *content_box = test_menu_view_get_content_box(menu_view);

  g_return_val_if_fail(GTK_IS_BOX(content_box), NULL);
  return test_widget_nth_child(content_box, index);
}

static GtkWidget *
test_menu_view_get_row_label(GtkWidget *menu_view, guint index) {
  GtkWidget *button = test_menu_view_get_row_button(menu_view, index);
  GtkWidget *row_box = NULL;
  GtkWidget *indicator = NULL;
  GtkWidget *label = NULL;

  g_return_val_if_fail(GTK_IS_BUTTON(button), NULL);

  row_box = gtk_button_get_child(GTK_BUTTON(button));
  g_return_val_if_fail(GTK_IS_WIDGET(row_box), NULL);
  indicator = gtk_widget_get_first_child(row_box);
  g_return_val_if_fail(GTK_IS_WIDGET(indicator), NULL);
  label = gtk_widget_get_next_sibling(indicator);
  g_return_val_if_fail(GTK_IS_LABEL(label), NULL);
  return label;
}

static gboolean
test_require_gtk(void) {
  if (test_gtk_available) {
    return TRUE;
  }

  g_test_skip("GTK display is not available");
  return FALSE;
}

static void
test_tray_menu_navigation_skips_noninteractive_rows(void) {
  const BsBarTrayMenuNavItem items[] = {
    {.interactive = FALSE, .opens_submenu = FALSE},
    {.interactive = TRUE, .opens_submenu = FALSE},
    {.interactive = FALSE, .opens_submenu = TRUE},
    {.interactive = TRUE, .opens_submenu = TRUE},
  };

  g_assert_cmpint(bs_bar_tray_menu_nav_find_next(items, G_N_ELEMENTS(items), -1, 1), ==, 1);
  g_assert_cmpint(bs_bar_tray_menu_nav_find_next(items, G_N_ELEMENTS(items), 1, 1), ==, 3);
  g_assert_cmpint(bs_bar_tray_menu_nav_find_next(items, G_N_ELEMENTS(items), 3, 1), ==, -1);
  g_assert_cmpint(bs_bar_tray_menu_nav_find_next(items, G_N_ELEMENTS(items), 3, -1), ==, 1);
}

static void
test_tray_menu_tree_ready_rejects_separator_only(void) {
  BsTrayMenuTree *tree = make_test_tree(BS_TRAY_MENU_ITEM_SEPARATOR, TRUE);

  g_assert_false(bs_bar_tray_menu_tree_has_visible_entries(tree));
  bs_tray_menu_tree_free(tree);
}

static void
test_tray_menu_tree_ready_accepts_visible_action(void) {
  BsTrayMenuTree *tree = make_test_tree(BS_TRAY_MENU_ITEM_NORMAL, TRUE);

  g_assert_true(bs_bar_tray_menu_tree_has_visible_entries(tree));
  bs_tray_menu_tree_free(tree);
}

static void
test_tray_menu_constraints_follow_monitor_geometry(void) {
  BsBarTrayMenuSizeConstraints constraints =
    bs_bar_tray_menu_compute_constraints_for_geometry(1280, 800, 32);

  g_assert_cmpint(constraints.min_width, ==, 220);
  g_assert_cmpint(constraints.max_width, ==, 1264);
  g_assert_cmpint(constraints.max_height, ==, 760);
}

static void
test_tray_menu_constraints_normalize_extreme_geometry(void) {
  BsBarTrayMenuSizeConstraints constraints =
    bs_bar_tray_menu_compute_constraints_for_geometry(200, 100, 300);

  g_assert_cmpint(constraints.min_width, ==, 220);
  g_assert_cmpint(constraints.max_width, ==, 220);
  g_assert_cmpint(constraints.max_height, ==, 120);
}

static void
test_tray_popup_passes_computed_size_constraints(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };

  test_tray_fixture_init(&fixture);
  fixture.harness.computed_constraints = (BsBarTrayMenuSizeConstraints) {
    .min_width = 260,
    .max_width = 580,
    .max_height = 440,
  };
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");

  g_assert_cmpuint(fixture.harness.menu_view_new_count, ==, 1);
  g_assert_cmpint(fixture.harness.last_constraints.min_width, ==, 260);
  g_assert_cmpint(fixture.harness.last_constraints.max_width, ==, 580);
  g_assert_cmpint(fixture.harness.last_constraints.max_height, ==, 440);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_uses_fallback_size_constraints(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };

  test_tray_fixture_init(&fixture);
  fixture.harness.should_compute_constraints = FALSE;
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");

  g_assert_cmpuint(fixture.harness.menu_view_new_count, ==, 1);
  g_assert_cmpint(fixture.harness.last_constraints.min_width, ==, 220);
  g_assert_cmpint(fixture.harness.last_constraints.max_width, ==, 640);
  g_assert_cmpint(fixture.harness.last_constraints.max_height, ==, 480);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_menu_view_applies_runtime_constraints(void) {
  BsTrayMenuTree *tree = NULL;
  GtkWidget *menu_view = NULL;
  GtkWidget *scroller = NULL;
  GtkPolicyType hpolicy = GTK_POLICY_AUTOMATIC;
  GtkPolicyType vpolicy = GTK_POLICY_NEVER;
  const BsBarTrayMenuSizeConstraints constraints = {
    .min_width = 230,
    .max_width = 510,
    .max_height = 390,
  };

  if (!test_require_gtk()) {
    return;
  }

  tree = make_menu_tree_with_label("Short item");
  menu_view = bs_bar_tray_menu_view_new(tree, &constraints, NULL, NULL, NULL);
  g_assert_nonnull(menu_view);
  g_object_ref_sink(menu_view);

  scroller = test_menu_view_get_scroller(menu_view);
  g_assert_true(GTK_IS_SCROLLED_WINDOW(scroller));
  gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(scroller), &hpolicy, &vpolicy);
  g_assert_cmpint(gtk_scrolled_window_get_min_content_width(GTK_SCROLLED_WINDOW(scroller)), ==, 230);
  g_assert_cmpint(gtk_scrolled_window_get_max_content_width(GTK_SCROLLED_WINDOW(scroller)), ==, 510);
  g_assert_cmpint(gtk_scrolled_window_get_max_content_height(GTK_SCROLLED_WINDOW(scroller)), ==, 390);
  g_assert_true(gtk_scrolled_window_get_propagate_natural_width(GTK_SCROLLED_WINDOW(scroller)));
  g_assert_true(gtk_scrolled_window_get_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller)));
  g_assert_cmpint(hpolicy, ==, GTK_POLICY_NEVER);
  g_assert_cmpint(vpolicy, ==, GTK_POLICY_AUTOMATIC);

  g_object_unref(menu_view);
  bs_tray_menu_tree_free(tree);
}

static void
test_tray_menu_view_wraps_long_labels(void) {
  BsTrayMenuTree *tree = NULL;
  GtkWidget *menu_view = NULL;
  GtkWidget *label = NULL;

  if (!test_require_gtk()) {
    return;
  }

  tree = make_menu_tree_with_label("This is a very long tray menu label that should wrap instead of truncating");
  menu_view = bs_bar_tray_menu_view_new(tree, NULL, NULL, NULL, NULL);
  g_assert_nonnull(menu_view);
  g_object_ref_sink(menu_view);

  label = test_menu_view_get_row_label(menu_view, 0);
  g_assert_true(GTK_IS_LABEL(label));
  g_assert_true(gtk_label_get_wrap(GTK_LABEL(label)));
  g_assert_cmpint(gtk_label_get_wrap_mode(GTK_LABEL(label)), ==, PANGO_WRAP_WORD_CHAR);
  g_assert_false(gtk_label_get_single_line_mode(GTK_LABEL(label)));
  g_assert_cmpint(gtk_label_get_ellipsize(GTK_LABEL(label)), ==, PANGO_ELLIPSIZE_NONE);

  g_object_unref(menu_view);
  bs_tray_menu_tree_free(tree);
}

static void
test_tray_menu_view_submenu_navigation_rebuilds_content(void) {
  BsTrayMenuTree *tree = NULL;
  GtkWidget *menu_view = NULL;
  GtkWidget *header = NULL;
  GtkWidget *row_button = NULL;
  GtkWidget *label = NULL;

  if (!test_require_gtk()) {
    return;
  }

  tree = make_menu_tree_with_submenu("Submenu", "Nested item");
  menu_view = bs_bar_tray_menu_view_new(tree, NULL, NULL, NULL, NULL);
  g_assert_nonnull(menu_view);
  g_object_ref_sink(menu_view);

  header = test_menu_view_get_header(menu_view);
  g_assert_true(GTK_IS_WIDGET(header));
  g_assert_false(gtk_widget_get_visible(header));

  row_button = test_menu_view_get_row_button(menu_view, 0);
  g_assert_true(GTK_IS_BUTTON(row_button));
  g_signal_emit_by_name(row_button, "clicked");

  g_assert_true(gtk_widget_get_visible(header));
  label = test_menu_view_get_row_label(menu_view, 0);
  g_assert_true(GTK_IS_LABEL(label));
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, "Nested item");

  g_object_unref(menu_view);
  bs_tray_menu_tree_free(tree);
}

static void
test_tray_popup_external_close_clears_controller_state(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };

  test_tray_fixture_init(&fixture);
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");
  g_assert_cmpstr(fixture.controller->open_item_id, ==, "item-a");
  g_assert_true(button->menu_open);

  button->menu_open = FALSE;
  bs_bar_tray_controller_handle_item_menu_closed(fixture.controller, "item-a");
  g_assert_null(fixture.controller->open_item_id);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_toggle_same_item_closes_menu(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };

  test_tray_fixture_init(&fixture);
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");
  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");

  g_assert_null(fixture.controller->open_item_id);
  g_assert_false(button->menu_open);
  g_assert_cmpuint(button->close_count, ==, 1);
  g_assert_false(fixture.controller->pending_open);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_retargets_to_new_item(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button_a = NULL;
  TestTrayButton *button_b = NULL;
  const TestTraySnapshotSpec spec = {
    .include_item_a = TRUE,
    .include_item_b = TRUE,
    .menu_object_a = TRUE,
    .menu_object_b = TRUE,
    .menu_tree_a = TRUE,
    .menu_tree_b = TRUE,
  };

  test_tray_fixture_init(&fixture);
  button_a = test_tray_fixture_add_button(&fixture, "item-a");
  button_b = test_tray_fixture_add_button(&fixture, "item-b");
  test_tray_fixture_apply_snapshot(&fixture, &spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button_a, "item-a");
  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button_b, "item-b");

  g_assert_cmpstr(fixture.controller->open_item_id, ==, "item-b");
  g_assert_false(button_a->menu_open);
  g_assert_true(button_b->menu_open);
  g_assert_cmpuint(button_a->close_count, ==, 1);
  g_assert_cmpuint(button_b->present_count, ==, 1);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_item_removed_clears_open_state(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec open_spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };
  const TestTraySnapshotSpec removed_spec = {0};

  test_tray_fixture_init(&fixture);
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &open_spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");
  g_hash_table_remove(fixture.harness.buttons_by_item_id, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &removed_spec);
  bs_bar_tray_controller_sync_from_vm(fixture.controller);

  g_assert_null(fixture.controller->open_item_id);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_pending_refresh_reopens_when_tree_arrives(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec pending_spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
  };
  const TestTraySnapshotSpec ready_spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };

  test_tray_fixture_init(&fixture);
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &pending_spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");
  g_assert_true(fixture.controller->pending_open);
  g_assert_cmpstr(fixture.controller->pending_item_id, ==, "item-a");
  g_assert_cmpuint(fixture.harness.refresh_request_count, ==, 1);

  test_tray_fixture_apply_snapshot(&fixture, &ready_spec);
  bs_bar_tray_controller_sync_from_vm(fixture.controller);

  g_assert_cmpstr(fixture.controller->open_item_id, ==, "item-a");
  g_assert_false(fixture.controller->pending_open);
  g_assert_null(fixture.controller->pending_item_id);
  g_assert_true(button->menu_open);
  g_assert_cmpuint(button->present_count, ==, 1);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_pending_refresh_clears_when_item_disappears(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec pending_spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
  };
  const TestTraySnapshotSpec removed_spec = {0};

  test_tray_fixture_init(&fixture);
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &pending_spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");
  g_assert_true(fixture.controller->pending_open);

  g_hash_table_remove(fixture.harness.buttons_by_item_id, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &removed_spec);
  bs_bar_tray_controller_sync_from_vm(fixture.controller);

  g_assert_false(fixture.controller->pending_open);
  g_assert_null(fixture.controller->pending_item_id);
  g_assert_null(fixture.controller->open_item_id);

  test_tray_fixture_clear(&fixture);
}

static void
test_tray_popup_shell_reset_clears_runtime_state(void) {
  TestTrayFixture fixture = {0};
  TestTrayButton *button = NULL;
  const TestTraySnapshotSpec spec = {
    .include_item_a = TRUE,
    .menu_object_a = TRUE,
    .menu_tree_a = TRUE,
  };

  test_tray_fixture_init(&fixture);
  button = test_tray_fixture_add_button(&fixture, "item-a");
  test_tray_fixture_apply_snapshot(&fixture, &spec);

  bs_bar_tray_controller_handle_menu(fixture.controller, (GtkWidget *) button, "item-a");
  bs_bar_tray_controller_set_pending_open(fixture.controller, "item-a");
  bs_bar_tray_controller_handle_shell_reset(fixture.controller);

  g_assert_null(fixture.controller->open_item_id);
  g_assert_false(fixture.controller->pending_open);
  g_assert_null(fixture.controller->pending_item_id);
  g_assert_false(button->menu_open);
  g_assert_cmpuint(button->close_count, ==, 1);

  test_tray_fixture_clear(&fixture);
}

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  test_gtk_available = gtk_init_check();

  g_test_add_func("/tray_menu_ux/navigation_skips_noninteractive_rows",
                  test_tray_menu_navigation_skips_noninteractive_rows);
  g_test_add_func("/tray_menu_ux/tree_ready_rejects_separator_only",
                  test_tray_menu_tree_ready_rejects_separator_only);
  g_test_add_func("/tray_menu_ux/tree_ready_accepts_visible_action",
                  test_tray_menu_tree_ready_accepts_visible_action);
  g_test_add_func("/tray_menu_ux/constraints_follow_monitor_geometry",
                  test_tray_menu_constraints_follow_monitor_geometry);
  g_test_add_func("/tray_menu_ux/constraints_normalize_extreme_geometry",
                  test_tray_menu_constraints_normalize_extreme_geometry);
  g_test_add_func("/tray_menu_ux/view_applies_runtime_constraints",
                  test_tray_menu_view_applies_runtime_constraints);
  g_test_add_func("/tray_menu_ux/view_wraps_long_labels",
                  test_tray_menu_view_wraps_long_labels);
  g_test_add_func("/tray_menu_ux/view_submenu_navigation_rebuilds_content",
                  test_tray_menu_view_submenu_navigation_rebuilds_content);
  g_test_add_func("/tray_popup/passes_computed_size_constraints",
                  test_tray_popup_passes_computed_size_constraints);
  g_test_add_func("/tray_popup/uses_fallback_size_constraints",
                  test_tray_popup_uses_fallback_size_constraints);
  g_test_add_func("/tray_popup/external_close_clears_controller_state",
                  test_tray_popup_external_close_clears_controller_state);
  g_test_add_func("/tray_popup/toggle_same_item_closes_menu",
                  test_tray_popup_toggle_same_item_closes_menu);
  g_test_add_func("/tray_popup/retargets_to_new_item",
                  test_tray_popup_retargets_to_new_item);
  g_test_add_func("/tray_popup/item_removed_clears_open_state",
                  test_tray_popup_item_removed_clears_open_state);
  g_test_add_func("/tray_popup/pending_refresh_reopens_when_tree_arrives",
                  test_tray_popup_pending_refresh_reopens_when_tree_arrives);
  g_test_add_func("/tray_popup/pending_refresh_clears_when_item_disappears",
                  test_tray_popup_pending_refresh_clears_when_item_disappears);
  g_test_add_func("/tray_popup/shell_reset_clears_runtime_state",
                  test_tray_popup_shell_reset_clears_runtime_state);
  return g_test_run();
}
