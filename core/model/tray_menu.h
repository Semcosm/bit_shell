#ifndef BIT_SHELL_CORE_MODEL_TRAY_MENU_H
#define BIT_SHELL_CORE_MODEL_TRAY_MENU_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BS_TRAY_MENU_ITEM_NORMAL = 0,
  BS_TRAY_MENU_ITEM_SEPARATOR,
  BS_TRAY_MENU_ITEM_CHECK,
  BS_TRAY_MENU_ITEM_RADIO,
  BS_TRAY_MENU_ITEM_SUBMENU,
} BsTrayMenuItemKind;

typedef struct _BsTrayMenuNode {
  gint32 id;
  BsTrayMenuItemKind kind;
  char *label;
  char *icon_name;
  bool visible;
  bool enabled;
  bool checked;
  bool is_radio;
  GPtrArray *children;
} BsTrayMenuNode;

typedef struct {
  char *item_id;
  guint32 revision;
  BsTrayMenuNode *root;
} BsTrayMenuTree;

BsTrayMenuNode *bs_tray_menu_node_dup(const BsTrayMenuNode *node);
void bs_tray_menu_node_free(BsTrayMenuNode *node);
bool bs_tray_menu_node_equals(const BsTrayMenuNode *lhs, const BsTrayMenuNode *rhs);

BsTrayMenuTree *bs_tray_menu_tree_dup(const BsTrayMenuTree *tree);
void bs_tray_menu_tree_free(BsTrayMenuTree *tree);
bool bs_tray_menu_tree_equals(const BsTrayMenuTree *lhs, const BsTrayMenuTree *rhs);

#endif
