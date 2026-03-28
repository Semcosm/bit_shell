#include "model/snapshot.h"

#include <glib.h>
#include <string.h>

static void bs_hash_table_destroy_window(gpointer value);
static void bs_hash_table_destroy_workspace(gpointer value);
static void bs_hash_table_destroy_output(gpointer value);
static void bs_hash_table_destroy_app_state(gpointer value);
static void bs_hash_table_destroy_dock_item(gpointer value);
static void bs_hash_table_destroy_tray_item(gpointer value);
static void bs_hash_table_destroy_tray_menu(gpointer value);
static GHashTable *bs_hash_table_new_full_map(GDestroyNotify value_destroy);
static void bs_json_append_quoted(GString *json, const char *value);
static void bs_json_append_nullable_string(GString *json, const char *value);
static void bs_json_append_string_array(GString *json, GPtrArray *values);
static GPtrArray *bs_hash_table_values_to_ptr_array(GHashTable *table);
static gint bs_compare_string_ids(const char *lhs, const char *rhs);
static gint bs_compare_string_ptr(gconstpointer lhs, gconstpointer rhs);
static gint bs_compare_workspace(gconstpointer lhs, gconstpointer rhs);
static gint bs_compare_output(gconstpointer lhs, gconstpointer rhs);
static gint bs_compare_window(gconstpointer lhs, gconstpointer rhs);
static gint bs_compare_dock_item(gconstpointer lhs, gconstpointer rhs);
static gint bs_compare_tray_item(gconstpointer lhs, gconstpointer rhs);
static gint bs_compare_tray_menu(gconstpointer lhs, gconstpointer rhs);
static const char *bs_tray_item_status_to_string(BsTrayItemStatus status);
static const char *bs_tray_menu_item_kind_to_string(BsTrayMenuItemKind kind);
static void bs_json_append_tray_pixmaps(GString *json, GPtrArray *pixmaps);
static void bs_json_append_tray_menu_node(GString *json, const BsTrayMenuNode *node);
static void bs_json_append_tray_menu_payload(GString *json, const BsSnapshot *snapshot);

static void
bs_hash_table_destroy_window(gpointer value) {
  BsWindow *window = value;
  if (window == NULL) {
    return;
  }
  bs_window_clear(window);
  g_free(window);
}

static void
bs_hash_table_destroy_workspace(gpointer value) {
  BsWorkspace *workspace = value;
  if (workspace == NULL) {
    return;
  }
  bs_workspace_clear(workspace);
  g_free(workspace);
}

static void
bs_hash_table_destroy_output(gpointer value) {
  BsOutput *output = value;
  if (output == NULL) {
    return;
  }
  bs_output_clear(output);
  g_free(output);
}

static void
bs_hash_table_destroy_app_state(gpointer value) {
  BsAppState *app_state = value;
  if (app_state == NULL) {
    return;
  }
  bs_app_state_clear(app_state);
  g_free(app_state);
}

static void
bs_hash_table_destroy_dock_item(gpointer value) {
  BsDockItem *dock_item = value;
  if (dock_item == NULL) {
    return;
  }
  bs_dock_item_clear(dock_item);
  g_free(dock_item);
}

static void
bs_hash_table_destroy_tray_item(gpointer value) {
  BsTrayItem *tray_item = value;
  if (tray_item == NULL) {
    return;
  }
  bs_tray_item_clear(tray_item);
  g_free(tray_item);
}

static void
bs_hash_table_destroy_tray_menu(gpointer value) {
  BsTrayMenuTree *tree = value;

  if (tree == NULL) {
    return;
  }

  bs_tray_menu_tree_free(tree);
}

static GHashTable *
bs_hash_table_new_full_map(GDestroyNotify value_destroy) {
  return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, value_destroy);
}

static void
bs_json_append_quoted(GString *json, const char *value) {
  g_autofree char *valid = NULL;
  const char *cursor = NULL;

  g_return_if_fail(json != NULL);

  valid = g_utf8_make_valid(value != NULL ? value : "", -1);
  g_string_append_c(json, '"');
  for (cursor = valid; *cursor != '\0'; cursor++) {
    switch (*cursor) {
      case '"':
        g_string_append(json, "\\\"");
        break;
      case '\\':
        g_string_append(json, "\\\\");
        break;
      case '\b':
        g_string_append(json, "\\b");
        break;
      case '\f':
        g_string_append(json, "\\f");
        break;
      case '\n':
        g_string_append(json, "\\n");
        break;
      case '\r':
        g_string_append(json, "\\r");
        break;
      case '\t':
        g_string_append(json, "\\t");
        break;
      default:
        if ((guchar) *cursor < 0x20) {
          g_string_append_printf(json, "\\u%04x", (guint) (guchar) *cursor);
        } else {
          g_string_append_c(json, *cursor);
        }
        break;
    }
  }
  g_string_append_c(json, '"');
}

static void
bs_json_append_nullable_string(GString *json, const char *value) {
  if (value == NULL) {
    g_string_append(json, "null");
    return;
  }
  bs_json_append_quoted(json, value);
}

static void
bs_json_append_string_array(GString *json, GPtrArray *values) {
  g_string_append(json, "[");
  if (values != NULL) {
    for (guint i = 0; i < values->len; i++) {
      const char *value = g_ptr_array_index(values, i);
      if (i > 0) {
        g_string_append(json, ",");
      }
      bs_json_append_nullable_string(json, value);
    }
  }
  g_string_append(json, "]");
}

static GPtrArray *
bs_hash_table_values_to_ptr_array(GHashTable *table) {
  GPtrArray *values = g_ptr_array_new();
  GHashTableIter iter;
  gpointer value = NULL;

  if (table == NULL) {
    return values;
  }

  g_hash_table_iter_init(&iter, table);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    g_ptr_array_add(values, value);
  }

  return values;
}

static gint
bs_compare_string_ids(const char *lhs, const char *rhs) {
  guint64 lhs_num = 0;
  guint64 rhs_num = 0;
  bool lhs_numeric = false;
  bool rhs_numeric = false;
  char *lhs_end = NULL;
  char *rhs_end = NULL;

  if (lhs == NULL && rhs == NULL) {
    return 0;
  }
  if (lhs == NULL) {
    return -1;
  }
  if (rhs == NULL) {
    return 1;
  }

  lhs_num = g_ascii_strtoull(lhs, &lhs_end, 10);
  rhs_num = g_ascii_strtoull(rhs, &rhs_end, 10);
  lhs_numeric = lhs_end != NULL && *lhs_end == '\0';
  rhs_numeric = rhs_end != NULL && *rhs_end == '\0';

  if (lhs_numeric && rhs_numeric) {
    if (lhs_num < rhs_num) {
      return -1;
    }
    if (lhs_num > rhs_num) {
      return 1;
    }
    return 0;
  }

  return g_strcmp0(lhs, rhs);
}

static gint
bs_compare_string_ptr(gconstpointer lhs, gconstpointer rhs) {
  const char *a = *(const char * const *) lhs;
  const char *b = *(const char * const *) rhs;
  return bs_compare_string_ids(a, b);
}

static gint
bs_compare_workspace(gconstpointer lhs, gconstpointer rhs) {
  const BsWorkspace *a = *(BsWorkspace * const *) lhs;
  const BsWorkspace *b = *(BsWorkspace * const *) rhs;
  gint cmp = 0;

  cmp = g_strcmp0(a->output_name, b->output_name);
  if (cmp != 0) {
    return cmp;
  }
  if (a->local_index != b->local_index) {
    return a->local_index < b->local_index ? -1 : 1;
  }
  return bs_compare_string_ids(a->id, b->id);
}

static gint
bs_compare_output(gconstpointer lhs, gconstpointer rhs) {
  const BsOutput *a = *(BsOutput * const *) lhs;
  const BsOutput *b = *(BsOutput * const *) rhs;
  return g_strcmp0(a->name, b->name);
}

static gint
bs_compare_window(gconstpointer lhs, gconstpointer rhs) {
  const BsWindow *a = *(BsWindow * const *) lhs;
  const BsWindow *b = *(BsWindow * const *) rhs;

  if (a->focused != b->focused) {
    return a->focused ? -1 : 1;
  }
  if (a->focus_ts != b->focus_ts) {
    return a->focus_ts > b->focus_ts ? -1 : 1;
  }
  return bs_compare_string_ids(a->id, b->id);
}

static gint
bs_compare_dock_item(gconstpointer lhs, gconstpointer rhs) {
  const BsDockItem *a = *(BsDockItem * const *) lhs;
  const BsDockItem *b = *(BsDockItem * const *) rhs;

  if (a->pinned != b->pinned) {
    return a->pinned ? -1 : 1;
  }
  if (a->pinned && b->pinned && a->pinned_index != b->pinned_index) {
    return a->pinned_index < b->pinned_index ? -1 : 1;
  }
  if (a->running != b->running) {
    return a->running ? -1 : 1;
  }
  if (a->running && b->running && a->running_order != b->running_order) {
    return a->running_order < b->running_order ? -1 : 1;
  }
  return g_strcmp0(a->app_key, b->app_key);
}

static gint
bs_compare_tray_item(gconstpointer lhs, gconstpointer rhs) {
  const BsTrayItem *a = *(BsTrayItem * const *) lhs;
  const BsTrayItem *b = *(BsTrayItem * const *) rhs;

  if (a->presentation_seq != b->presentation_seq) {
    return a->presentation_seq < b->presentation_seq ? -1 : 1;
  }
  return g_strcmp0(a->item_id, b->item_id);
}

static gint
bs_compare_tray_menu(gconstpointer lhs, gconstpointer rhs) {
  const BsTrayMenuTree *a = *(BsTrayMenuTree * const *) lhs;
  const BsTrayMenuTree *b = *(BsTrayMenuTree * const *) rhs;

  return g_strcmp0(a->item_id, b->item_id);
}

static const char *
bs_tray_item_status_to_string(BsTrayItemStatus status) {
  switch (status) {
    case BS_TRAY_ITEM_STATUS_ACTIVE:
      return "active";
    case BS_TRAY_ITEM_STATUS_ATTENTION:
      return "attention";
    case BS_TRAY_ITEM_STATUS_PASSIVE:
    default:
      return "passive";
  }
}

static const char *
bs_tray_menu_item_kind_to_string(BsTrayMenuItemKind kind) {
  switch (kind) {
    case BS_TRAY_MENU_ITEM_SEPARATOR:
      return "separator";
    case BS_TRAY_MENU_ITEM_CHECK:
      return "check";
    case BS_TRAY_MENU_ITEM_RADIO:
      return "radio";
    case BS_TRAY_MENU_ITEM_SUBMENU:
      return "submenu";
    case BS_TRAY_MENU_ITEM_NORMAL:
    default:
      return "normal";
  }
}

static void
bs_json_append_tray_pixmaps(GString *json, GPtrArray *pixmaps) {
  g_string_append(json, "[");
  if (pixmaps != NULL) {
    for (guint i = 0; i < pixmaps->len; i++) {
      const BsTrayPixmap *pixmap = g_ptr_array_index(pixmaps, i);
      gsize data_len = 0;
      const guint8 *data = NULL;
      g_autofree char *bytes_b64 = NULL;

      if (pixmap == NULL) {
        continue;
      }
      if (i > 0) {
        g_string_append(json, ",");
      }

      data = pixmap->argb32 != NULL ? g_bytes_get_data(pixmap->argb32, &data_len) : NULL;
      bytes_b64 = data != NULL && data_len > 0 ? g_base64_encode(data, data_len) : g_strdup("");
      g_string_append_printf(json,
                             "{\"width\":%d,\"height\":%d,\"bytes_b64\":\"%s\"}",
                             pixmap->width,
                             pixmap->height,
                             bytes_b64 != NULL ? bytes_b64 : "");
    }
  }
  g_string_append(json, "]");
}

static void
bs_json_append_tray_menu_node(GString *json, const BsTrayMenuNode *node) {
  g_return_if_fail(json != NULL);

  if (node == NULL) {
    g_string_append(json, "null");
    return;
  }

  g_string_append_printf(json,
                         "{\"id\":%d,\"kind\":\"%s\",\"label\":",
                         node->id,
                         bs_tray_menu_item_kind_to_string(node->kind));
  bs_json_append_nullable_string(json, node->label);
  g_string_append(json, ",\"icon_name\":");
  bs_json_append_nullable_string(json, node->icon_name);
  g_string_append_printf(json,
                         ",\"visible\":%s,\"enabled\":%s,\"checked\":%s,\"is_radio\":%s,\"children\":[",
                         node->visible ? "true" : "false",
                         node->enabled ? "true" : "false",
                         node->checked ? "true" : "false",
                         node->is_radio ? "true" : "false");
  if (node->children != NULL) {
    for (guint i = 0; i < node->children->len; i++) {
      if (i > 0) {
        g_string_append(json, ",");
      }
      bs_json_append_tray_menu_node(json, g_ptr_array_index(node->children, i));
    }
  }
  g_string_append(json, "]}");
}

static void
bs_json_append_shell_payload(GString *json, const BsSnapshot *snapshot) {
  g_string_append(json, "{");
  g_string_append_printf(json,
                         "\"niri_connected\":%s,",
                         snapshot->shell.niri_connected ? "true" : "false");
  g_string_append_printf(json,
                         "\"outputs_ready\":%s,",
                         snapshot->shell.outputs_ready ? "true" : "false");
  g_string_append_printf(json,
                         "\"workspaces_ready\":%s,",
                         snapshot->shell.workspaces_ready ? "true" : "false");
  g_string_append_printf(json,
                         "\"windows_ready\":%s,",
                         snapshot->shell.windows_ready ? "true" : "false");
  g_string_append_printf(json,
                         "\"bootstrap_used_fallback\":%s,",
                         snapshot->shell.bootstrap_used_fallback ? "true" : "false");
  g_string_append(json, "\"degraded_reason\":");
  bs_json_append_nullable_string(json, snapshot->shell.degraded_reason);
  g_string_append(json, ",\"focused_output_name\":");
  bs_json_append_nullable_string(json, snapshot->shell.focused_output_name);
  g_string_append(json, ",\"focused_workspace_id\":");
  bs_json_append_nullable_string(json, snapshot->shell.focused_workspace_id);
  g_string_append(json, ",\"focused_window_id\":");
  bs_json_append_nullable_string(json, snapshot->shell.focused_window_id);
  g_string_append(json, ",\"focused_window_title\":");
  bs_json_append_nullable_string(json, snapshot->shell.focused_window_title);
  g_string_append(json, "}");
}

static void
bs_json_append_windows_payload(GString *json, const BsSnapshot *snapshot) {
  g_autoptr(GPtrArray) windows = bs_hash_table_values_to_ptr_array(snapshot->windows);

  g_ptr_array_sort(windows, bs_compare_window);
  g_string_append(json, "{\"windows\":[");
  for (guint i = 0; i < windows->len; i++) {
    const BsWindow *window = g_ptr_array_index(windows, i);
    if (i > 0) {
      g_string_append(json, ",");
    }
    g_string_append(json, "{");
    g_string_append(json, "\"id\":");
    bs_json_append_nullable_string(json, window->id);
    g_string_append(json, ",\"title\":");
    bs_json_append_nullable_string(json, window->title);
    g_string_append(json, ",\"app_id\":");
    bs_json_append_nullable_string(json, window->app_id);
    g_string_append(json, ",\"desktop_id\":");
    bs_json_append_nullable_string(json, window->desktop_id);
    g_string_append(json, ",\"workspace_id\":");
    bs_json_append_nullable_string(json, window->workspace_id);
    g_string_append(json, ",\"output_name\":");
    bs_json_append_nullable_string(json, window->output_name);
    g_string_append_printf(json,
                           ",\"focused\":%s,\"floating\":%s,\"fullscreen\":%s,\"focus_ts\":%" G_GUINT64_FORMAT,
                           window->focused ? "true" : "false",
                           window->floating ? "true" : "false",
                           window->fullscreen ? "true" : "false",
                           window->focus_ts);
    g_string_append(json, "}");
  }
  g_string_append(json, "]}");
}

static void
bs_json_append_workspaces_payload(GString *json, const BsSnapshot *snapshot) {
  g_autoptr(GPtrArray) outputs = bs_hash_table_values_to_ptr_array(snapshot->outputs);
  g_autoptr(GPtrArray) workspaces = bs_hash_table_values_to_ptr_array(snapshot->workspaces);

  g_ptr_array_sort(outputs, bs_compare_output);
  g_ptr_array_sort(workspaces, bs_compare_workspace);

  g_string_append(json, "{\"outputs\":[");
  for (guint i = 0; i < outputs->len; i++) {
    const BsOutput *output = g_ptr_array_index(outputs, i);
    if (i > 0) {
      g_string_append(json, ",");
    }
    g_string_append(json, "{");
    g_string_append(json, "\"name\":");
    bs_json_append_nullable_string(json, output->name);
    g_string_append_printf(json,
                           ",\"width\":%d,\"height\":%d,\"scale\":%.3f,\"focused\":%s",
                           output->width,
                           output->height,
                           output->scale,
                           output->focused ? "true" : "false");
    g_string_append(json, "}");
  }
  g_string_append(json, "],\"workspaces\":[");
  for (guint i = 0; i < workspaces->len; i++) {
    const BsWorkspace *workspace = g_ptr_array_index(workspaces, i);
    if (i > 0) {
      g_string_append(json, ",");
    }
    g_string_append(json, "{");
    g_string_append(json, "\"id\":");
    bs_json_append_nullable_string(json, workspace->id);
    g_string_append(json, ",\"name\":");
    bs_json_append_nullable_string(json, workspace->name);
    g_string_append(json, ",\"output_name\":");
    bs_json_append_nullable_string(json, workspace->output_name);
    g_string_append_printf(json,
                           ",\"focused\":%s,\"empty\":%s,\"local_index\":%d",
                           workspace->focused ? "true" : "false",
                           workspace->empty ? "true" : "false",
                           workspace->local_index);
    g_string_append(json, "}");
  }
  g_string_append(json, "]}");
}

static void
bs_json_append_dock_payload(GString *json, const BsSnapshot *snapshot) {
  g_autoptr(GPtrArray) dock_items = bs_hash_table_values_to_ptr_array(snapshot->dock_items);

  g_ptr_array_sort(dock_items, bs_compare_dock_item);
  g_string_append(json, "{\"items\":[");
  for (guint i = 0; i < dock_items->len; i++) {
    const BsDockItem *dock_item = g_ptr_array_index(dock_items, i);
    g_autoptr(GPtrArray) window_ids = g_ptr_array_new_with_free_func(g_free);

    if (i > 0) {
      g_string_append(json, ",");
    }

    if (dock_item->window_ids != NULL) {
      for (guint window_index = 0; window_index < dock_item->window_ids->len; window_index++) {
        const char *window_id = g_ptr_array_index(dock_item->window_ids, window_index);
        g_ptr_array_add(window_ids, g_strdup(window_id));
      }
    }
    g_ptr_array_sort(window_ids, bs_compare_string_ptr);

    g_string_append(json, "{");
    g_string_append(json, "\"app_key\":");
    bs_json_append_nullable_string(json, dock_item->app_key);
    g_string_append(json, ",\"desktop_id\":");
    bs_json_append_nullable_string(json, dock_item->desktop_id);
    g_string_append(json, ",\"name\":");
    bs_json_append_nullable_string(json, dock_item->name);
    g_string_append(json, ",\"icon_name\":");
    bs_json_append_nullable_string(json, dock_item->icon_name);
    g_string_append_printf(json,
                           ",\"pinned\":%s,\"running\":%s,\"focused\":%s,\"pinned_index\":%d,\"window_ids\":",
                           dock_item->pinned ? "true" : "false",
                           dock_item->running ? "true" : "false",
                           dock_item->focused ? "true" : "false",
                           dock_item->pinned_index);
    bs_json_append_string_array(json, window_ids);
    g_string_append(json, "}");
  }
  g_string_append(json, "]}");
}

static void
bs_json_append_settings_payload(GString *json, const BsSnapshot *snapshot) {
  g_string_append(json, "{\"config_loaded\":true,\"pinned_apps\":");
  bs_json_append_string_array(json, snapshot->pinned_app_ids);
  g_string_append_printf(json,
                         ",\"bar\":{\"height_px\":%u,\"show_workspace_strip\":%s,\"show_focused_title\":%s,\"show_tray\":%s,\"show_clock\":%s}",
                         snapshot->bar_config.height_px,
                         snapshot->bar_config.show_workspace_strip ? "true" : "false",
                         snapshot->bar_config.show_focused_title ? "true" : "false",
                         snapshot->bar_config.show_tray ? "true" : "false",
                         snapshot->bar_config.show_clock ? "true" : "false");
  g_string_append_printf(json,
                         ",\"dock\":{\"icon_size_px\":%u,\"magnification_enabled\":%s,\"magnification_scale\":%.3f,\"hover_range_cap_units\":%u,\"spacing_px\":%u,\"bottom_margin_px\":%u,\"show_running_indicator\":%s,\"animate_opening_apps\":%s,\"display_mode\":\"%s\",\"center_on_primary_output\":%s}}",
                         snapshot->dock_config.icon_size_px,
                         snapshot->dock_config.magnification_enabled ? "true" : "false",
                         snapshot->dock_config.magnification_scale,
                         snapshot->dock_config.hover_range_cap_units,
                         snapshot->dock_config.spacing_px,
                         snapshot->dock_config.bottom_margin_px,
                         snapshot->dock_config.show_running_indicator ? "true" : "false",
                         snapshot->dock_config.animate_opening_apps ? "true" : "false",
                         bs_dock_display_mode_to_string(snapshot->dock_config.display_mode),
                         snapshot->dock_config.center_on_primary_output ? "true" : "false");
}

static void
bs_json_append_tray_payload(GString *json, const BsSnapshot *snapshot) {
  g_autoptr(GPtrArray) tray_items = bs_hash_table_values_to_ptr_array(snapshot->tray_items);

  g_ptr_array_sort(tray_items, bs_compare_tray_item);
  g_string_append(json, "{\"items\":[");
  for (guint i = 0; i < tray_items->len; i++) {
    const BsTrayItem *tray_item = g_ptr_array_index(tray_items, i);

    if (i > 0) {
      g_string_append(json, ",");
    }

    g_string_append(json, "{");
    g_string_append(json, "\"item_id\":");
    bs_json_append_nullable_string(json, tray_item->item_id);
    g_string_append(json, ",\"id\":");
    bs_json_append_nullable_string(json, tray_item->id);
    g_string_append(json, ",\"title\":");
    bs_json_append_nullable_string(json, tray_item->title);
    g_string_append(json, ",\"status\":");
    bs_json_append_nullable_string(json, bs_tray_item_status_to_string(tray_item->status));
    g_string_append(json, ",\"icon_name\":");
    bs_json_append_nullable_string(json, tray_item->icon_name);
    g_string_append(json, ",\"icon_pixmaps\":");
    bs_json_append_tray_pixmaps(json, tray_item->icon_pixmaps);
    g_string_append(json, ",\"attention_icon_name\":");
    bs_json_append_nullable_string(json, tray_item->attention_icon_name);
    g_string_append(json, ",\"attention_icon_pixmaps\":");
    bs_json_append_tray_pixmaps(json, tray_item->attention_icon_pixmaps);
    g_string_append(json, ",\"menu_object_path\":");
    bs_json_append_nullable_string(json, tray_item->menu_object_path);
    g_string_append_printf(json,
                           ",\"item_is_menu\":%s,\"has_activate\":%s,\"has_context_menu\":%s,\"presentation_seq\":%" G_GUINT64_FORMAT,
                           tray_item->item_is_menu ? "true" : "false",
                           tray_item->has_activate ? "true" : "false",
                           tray_item->has_context_menu ? "true" : "false",
                           tray_item->presentation_seq);
    g_string_append(json, "}");
  }
  g_string_append(json, "]}");
}

static void
bs_json_append_tray_menu_payload(GString *json, const BsSnapshot *snapshot) {
  g_autoptr(GPtrArray) tray_menus = bs_hash_table_values_to_ptr_array(snapshot->tray_menus);

  g_ptr_array_sort(tray_menus, bs_compare_tray_menu);
  g_string_append(json, "{\"items\":[");
  for (guint i = 0; i < tray_menus->len; i++) {
    const BsTrayMenuTree *tree = g_ptr_array_index(tray_menus, i);

    if (i > 0) {
      g_string_append(json, ",");
    }
    g_string_append(json, "{\"item_id\":");
    bs_json_append_nullable_string(json, tree->item_id);
    g_string_append_printf(json, ",\"revision\":%u,\"root\":", tree->revision);
    bs_json_append_tray_menu_node(json, tree->root);
    g_string_append(json, "}");
  }
  g_string_append(json, "]}");
}

void
bs_snapshot_init(BsSnapshot *snapshot) {
  g_return_if_fail(snapshot != NULL);
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->bar_config.height_px = 32;
  snapshot->bar_config.show_workspace_strip = true;
  snapshot->bar_config.show_focused_title = true;
  snapshot->bar_config.show_tray = true;
  snapshot->bar_config.show_clock = true;
  bs_dock_config_init_defaults(&snapshot->dock_config);
  snapshot->windows = bs_hash_table_new_full_map(bs_hash_table_destroy_window);
  snapshot->workspaces = bs_hash_table_new_full_map(bs_hash_table_destroy_workspace);
  snapshot->outputs = bs_hash_table_new_full_map(bs_hash_table_destroy_output);
  snapshot->apps = bs_hash_table_new_full_map(bs_hash_table_destroy_app_state);
  snapshot->dock_items = bs_hash_table_new_full_map(bs_hash_table_destroy_dock_item);
  snapshot->tray_items = bs_hash_table_new_full_map(bs_hash_table_destroy_tray_item);
  snapshot->tray_menus = bs_hash_table_new_full_map(bs_hash_table_destroy_tray_menu);
  snapshot->pinned_app_ids = g_ptr_array_new_with_free_func(g_free);
}

void
bs_snapshot_clear(BsSnapshot *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  bs_shell_state_clear(&snapshot->shell);
  g_clear_pointer(&snapshot->windows, g_hash_table_unref);
  g_clear_pointer(&snapshot->workspaces, g_hash_table_unref);
  g_clear_pointer(&snapshot->outputs, g_hash_table_unref);
  g_clear_pointer(&snapshot->apps, g_hash_table_unref);
  g_clear_pointer(&snapshot->dock_items, g_hash_table_unref);
  g_clear_pointer(&snapshot->tray_items, g_hash_table_unref);
  g_clear_pointer(&snapshot->tray_menus, g_hash_table_unref);
  g_clear_pointer(&snapshot->pinned_app_ids, g_ptr_array_unref);
  memset(snapshot->topic_generations, 0, sizeof(snapshot->topic_generations));
  snapshot->generation = 0;
}

uint64_t
bs_snapshot_topic_generation(const BsSnapshot *snapshot, BsTopic topic) {
  g_return_val_if_fail(snapshot != NULL, 0);
  g_return_val_if_fail(topic >= 0 && topic < BS_TOPIC_COUNT, 0);
  return snapshot->topic_generations[topic];
}

char *
bs_snapshot_serialize_topic_versions_json(const BsSnapshot *snapshot) {
  GString *json = g_string_new("{");

  g_return_val_if_fail(snapshot != NULL, g_strdup("{}"));

  for (int topic = 0; topic < BS_TOPIC_COUNT; topic++) {
    if (topic > 0) {
      g_string_append(json, ",");
    }
    g_string_append_printf(json,
                           "\"%s\":%" G_GUINT64_FORMAT,
                           bs_topic_to_string((BsTopic) topic),
                           snapshot->topic_generations[topic]);
  }

  g_string_append(json, "}");
  return g_string_free(json, false);
}

char *
bs_snapshot_serialize_topic_payload_json(const BsSnapshot *snapshot, BsTopic topic) {
  GString *json = g_string_new(NULL);

  g_return_val_if_fail(snapshot != NULL, g_strdup("{}"));

  switch (topic) {
    case BS_TOPIC_SHELL:
      bs_json_append_shell_payload(json, snapshot);
      break;
    case BS_TOPIC_WINDOWS:
      bs_json_append_windows_payload(json, snapshot);
      break;
    case BS_TOPIC_WORKSPACES:
      bs_json_append_workspaces_payload(json, snapshot);
      break;
    case BS_TOPIC_DOCK:
      bs_json_append_dock_payload(json, snapshot);
      break;
    case BS_TOPIC_TRAY:
      bs_json_append_tray_payload(json, snapshot);
      break;
    case BS_TOPIC_SETTINGS:
      bs_json_append_settings_payload(json, snapshot);
      break;
    case BS_TOPIC_TRAY_MENU:
      bs_json_append_tray_menu_payload(json, snapshot);
      break;
    default:
      g_string_append(json, "{}");
      break;
  }

  return g_string_free(json, false);
}

char *
bs_snapshot_serialize_json(const BsSnapshot *snapshot) {
  GString *json = g_string_new(NULL);
  char *topic_versions_json = NULL;

  g_return_val_if_fail(snapshot != NULL, g_strdup("{\"ok\":false,\"kind\":\"error\"}"));

  topic_versions_json = bs_snapshot_serialize_topic_versions_json(snapshot);
  g_string_append_printf(json,
                         "{\"ok\":true,\"kind\":\"snapshot\",\"generation\":%" G_GUINT64_FORMAT ",\"topic_versions\":%s,\"state\":{",
                         snapshot->generation,
                         topic_versions_json);
  g_free(topic_versions_json);

  for (int topic = 0; topic < BS_TOPIC_COUNT; topic++) {
    char *payload_json = NULL;
    if (topic > 0) {
      g_string_append(json, ",");
    }
    payload_json = bs_snapshot_serialize_topic_payload_json(snapshot, (BsTopic) topic);
    g_string_append_printf(json, "\"%s\":%s", bs_topic_to_string((BsTopic) topic), payload_json);
    g_free(payload_json);
  }

  g_string_append(json, "}}");
  return g_string_free(json, false);
}
