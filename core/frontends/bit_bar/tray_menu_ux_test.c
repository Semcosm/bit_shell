#include "frontends/bit_bar/tray_menu_view.h"

#include "model/tray_menu.h"

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

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/tray_menu_ux/navigation_skips_noninteractive_rows",
                  test_tray_menu_navigation_skips_noninteractive_rows);
  g_test_add_func("/tray_menu_ux/tree_ready_rejects_separator_only",
                  test_tray_menu_tree_ready_rejects_separator_only);
  g_test_add_func("/tray_menu_ux/tree_ready_accepts_visible_action",
                  test_tray_menu_tree_ready_accepts_visible_action);
  return g_test_run();
}
