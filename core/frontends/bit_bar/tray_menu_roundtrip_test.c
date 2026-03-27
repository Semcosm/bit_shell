#include "frontends/bit_bar/bar_view_model.h"

#include <json-glib/json-glib.h>

#include "model/snapshot.h"
#include "model/tray_menu.h"

static BsTrayMenuNode *
make_node(gint32 id,
          BsTrayMenuItemKind kind,
          const char *label,
          gboolean enabled,
          gboolean checked) {
  BsTrayMenuNode *node = g_new0(BsTrayMenuNode, 1);

  node->id = id;
  node->kind = kind;
  node->label = g_strdup(label);
  node->visible = true;
  node->enabled = enabled;
  node->checked = checked;
  node->children = g_ptr_array_new_with_free_func((GDestroyNotify) bs_tray_menu_node_free);
  return node;
}

static void
test_tray_menu_roundtrip_snapshot_to_vm(void) {
  BsSnapshot snapshot = {0};
  BsBarViewModel *vm = NULL;
  BsTrayMenuTree *tree = NULL;
  BsTrayMenuNode *root = NULL;
  BsTrayMenuNode *submenu = NULL;
  const BsTrayMenuTree *parsed = NULL;
  g_autofree char *json = NULL;
  g_autoptr(GError) error = NULL;

  bs_snapshot_init(&snapshot);

  root = make_node(0, BS_TRAY_MENU_ITEM_SUBMENU, NULL, true, false);
  g_ptr_array_add(root->children,
                  make_node(1, BS_TRAY_MENU_ITEM_NORMAL, "Open", true, false));
  g_ptr_array_add(root->children,
                  make_node(2, BS_TRAY_MENU_ITEM_CHECK, "Enabled", true, true));
  submenu = make_node(3, BS_TRAY_MENU_ITEM_SUBMENU, "More", true, false);
  g_ptr_array_add(submenu->children,
                  make_node(4, BS_TRAY_MENU_ITEM_RADIO, "Choice A", true, false));
  g_ptr_array_add(submenu->children,
                  make_node(5, BS_TRAY_MENU_ITEM_RADIO, "Choice B", true, true));
  g_ptr_array_add(root->children, submenu);

  tree = g_new0(BsTrayMenuTree, 1);
  tree->item_id = g_strdup("item-1");
  tree->revision = 7;
  tree->root = root;
  g_hash_table_replace(snapshot.tray_menus, g_strdup(tree->item_id), tree);

  json = bs_snapshot_serialize_json(&snapshot);
  vm = bs_bar_view_model_new();
  g_assert_true(bs_bar_view_model_consume_json_line(vm, json, &error));
  g_assert_no_error(error);

  parsed = bs_bar_view_model_lookup_tray_menu(vm, "item-1");
  g_assert_nonnull(parsed);
  g_assert_cmpuint(parsed->revision, ==, 7);
  g_assert_nonnull(parsed->root);
  g_assert_nonnull(parsed->root->children);
  g_assert_cmpuint(parsed->root->children->len, ==, 3);
  g_assert_cmpint(((BsTrayMenuNode *) g_ptr_array_index(parsed->root->children, 1))->kind,
                  ==,
                  BS_TRAY_MENU_ITEM_CHECK);
  g_assert_true(((BsTrayMenuNode *) g_ptr_array_index(parsed->root->children, 1))->checked);
  g_assert_cmpint(((BsTrayMenuNode *) g_ptr_array_index(parsed->root->children, 2))->kind,
                  ==,
                  BS_TRAY_MENU_ITEM_SUBMENU);
  g_assert_cmpuint(((BsTrayMenuNode *) g_ptr_array_index(parsed->root->children, 2))->children->len,
                   ==,
                   2);

  bs_bar_view_model_free(vm);
  bs_snapshot_clear(&snapshot);
}

static void
test_tray_menu_subscribe_request_includes_topic(void) {
  g_autofree char *request = bs_bar_view_model_build_subscribe_request();

  g_assert_nonnull(strstr(request, "\"tray_menu\""));
}

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/tray_menu/roundtrip_snapshot_to_vm",
                  test_tray_menu_roundtrip_snapshot_to_vm);
  g_test_add_func("/tray_menu/subscribe_request_includes_topic",
                  test_tray_menu_subscribe_request_includes_topic);
  return g_test_run();
}
