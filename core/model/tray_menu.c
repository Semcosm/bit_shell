#include "model/tray_menu.h"

#include <string.h>

BsTrayMenuNode *
bs_tray_menu_node_dup(const BsTrayMenuNode *node) {
  BsTrayMenuNode *copy = NULL;

  if (node == NULL) {
    return NULL;
  }

  copy = g_new0(BsTrayMenuNode, 1);
  copy->id = node->id;
  copy->kind = node->kind;
  copy->label = g_strdup(node->label);
  copy->icon_name = g_strdup(node->icon_name);
  copy->visible = node->visible;
  copy->enabled = node->enabled;
  copy->checked = node->checked;
  copy->is_radio = node->is_radio;
  copy->children = g_ptr_array_new_with_free_func((GDestroyNotify) bs_tray_menu_node_free);
  if (node->children != NULL) {
    for (guint i = 0; i < node->children->len; i++) {
      const BsTrayMenuNode *child = g_ptr_array_index(node->children, i);
      g_ptr_array_add(copy->children, bs_tray_menu_node_dup(child));
    }
  }
  return copy;
}

void
bs_tray_menu_node_free(BsTrayMenuNode *node) {
  if (node == NULL) {
    return;
  }

  g_free(node->label);
  g_free(node->icon_name);
  g_clear_pointer(&node->children, g_ptr_array_unref);
  g_free(node);
}

bool
bs_tray_menu_node_equals(const BsTrayMenuNode *lhs, const BsTrayMenuNode *rhs) {
  if (lhs == NULL && rhs == NULL) {
    return true;
  }
  if (lhs == NULL || rhs == NULL) {
    return false;
  }
  if (lhs->id != rhs->id
      || lhs->kind != rhs->kind
      || lhs->visible != rhs->visible
      || lhs->enabled != rhs->enabled
      || lhs->checked != rhs->checked
      || lhs->is_radio != rhs->is_radio
      || g_strcmp0(lhs->label, rhs->label) != 0
      || g_strcmp0(lhs->icon_name, rhs->icon_name) != 0) {
    return false;
  }

  if (lhs->children == NULL && rhs->children == NULL) {
    return true;
  }
  if (lhs->children == NULL || rhs->children == NULL) {
    return false;
  }
  if (lhs->children->len != rhs->children->len) {
    return false;
  }

  for (guint i = 0; i < lhs->children->len; i++) {
    if (!bs_tray_menu_node_equals(g_ptr_array_index(lhs->children, i),
                                  g_ptr_array_index(rhs->children, i))) {
      return false;
    }
  }
  return true;
}

BsTrayMenuTree *
bs_tray_menu_tree_dup(const BsTrayMenuTree *tree) {
  BsTrayMenuTree *copy = NULL;

  if (tree == NULL) {
    return NULL;
  }

  copy = g_new0(BsTrayMenuTree, 1);
  copy->item_id = g_strdup(tree->item_id);
  copy->revision = tree->revision;
  copy->root = bs_tray_menu_node_dup(tree->root);
  return copy;
}

void
bs_tray_menu_tree_free(BsTrayMenuTree *tree) {
  if (tree == NULL) {
    return;
  }

  g_free(tree->item_id);
  bs_tray_menu_node_free(tree->root);
  g_free(tree);
}

bool
bs_tray_menu_tree_equals(const BsTrayMenuTree *lhs, const BsTrayMenuTree *rhs) {
  if (lhs == NULL && rhs == NULL) {
    return true;
  }
  if (lhs == NULL || rhs == NULL) {
    return false;
  }

  return g_strcmp0(lhs->item_id, rhs->item_id) == 0
         && lhs->revision == rhs->revision
         && bs_tray_menu_node_equals(lhs->root, rhs->root);
}
