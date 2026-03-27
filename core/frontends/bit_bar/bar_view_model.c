#include "frontends/bit_bar/bar_view_model.h"

#include <json-glib/json-glib.h>

#include "model/ipc.h"

typedef struct {
  bool niri_connected;
  bool outputs_ready;
  bool workspaces_ready;
  bool windows_ready;
  bool bootstrap_used_fallback;
  char *degraded_reason;
  char *focused_output_name;
  char *focused_workspace_id;
  char *focused_window_id;
  char *focused_window_title;
} BsBarVmShellState;

typedef struct {
  char *id;
  char *title;
  char *app_id;
  char *desktop_id;
  char *workspace_id;
  char *output_name;
  bool focused;
  guint64 focus_ts;
} BsBarVmWindow;

typedef struct {
  char *name;
  bool focused;
} BsBarVmOutput;

typedef struct {
  char *id;
  char *name;
  char *output_name;
  bool focused;
  bool empty;
  int local_index;
} BsBarVmWorkspace;

typedef struct {
  char *item_id;
  char *title;
  char *icon_name;
  char *attention_icon_name;
  char *status;
  char *menu_object_path;
  guint64 presentation_seq;
  bool item_is_menu;
  bool has_activate;
  bool has_context_menu;
} BsBarVmTrayItem;

struct _BsBarViewModel {
  BsBarVmPhase phase;
  guint64 generation;
  guint64 topic_versions[BS_TOPIC_COUNT];
  guint64 snapshot_topic_versions[BS_TOPIC_COUNT];
  BsBarConfig bar_config;
  BsBarVmShellState shell;
  GHashTable *windows_by_id;
  GHashTable *outputs_by_name;
  GHashTable *workspaces_by_id;
  GHashTable *tray_items_by_id;
  GPtrArray *workspace_strip_items;
  GPtrArray *window_candidates;
  GPtrArray *tray_items;
  BsBarVmWorkspaceStripState workspace_strip_state;
  BsBarVmCenterState center_state;
  BsBarVmTrayState tray_state;
  char *focused_title;
  char *focused_app_name;
  bool subscribed;
  bool needs_resnapshot;
  BsBarVmChangedFn changed_cb;
  gpointer changed_user_data;
};

static void bs_bar_vm_shell_state_clear(BsBarVmShellState *shell);
static void bs_bar_vm_window_free(gpointer data);
static void bs_bar_vm_output_free(gpointer data);
static void bs_bar_vm_workspace_free(gpointer data);
static void bs_bar_vm_tray_item_free(gpointer data);
static void bs_bar_workspace_strip_item_free(gpointer data);
static void bs_bar_window_candidate_free(gpointer data);
static void bs_bar_tray_item_view_free(gpointer data);
static void bs_bar_view_model_emit_changed(BsBarViewModel *vm, guint dirty_flags);
static void bs_bar_view_model_clear_caches(BsBarViewModel *vm);
static void bs_bar_view_model_apply_bar_config_defaults(BsBarConfig *config);
static const char *bs_bar_vm_json_string_member(JsonObject *object, const char *member_name);
static bool bs_bar_vm_json_bool_member(JsonObject *object, const char *member_name, bool fallback);
static int bs_bar_vm_json_int_member(JsonObject *object, const char *member_name, int fallback);
static guint64 bs_bar_vm_json_uint64_member(JsonObject *object, const char *member_name, guint64 fallback);
static void bs_bar_view_model_parse_shell(BsBarViewModel *vm, JsonObject *object);
static void bs_bar_view_model_parse_windows(BsBarViewModel *vm, JsonObject *object);
static void bs_bar_view_model_parse_workspaces(BsBarViewModel *vm, JsonObject *object);
static void bs_bar_view_model_parse_tray(BsBarViewModel *vm, JsonObject *object);
static bool bs_bar_view_model_parse_settings(BsBarViewModel *vm, JsonObject *object);
static void bs_bar_view_model_parse_topic_versions(BsBarViewModel *vm,
                                                   JsonObject *object,
                                                   guint64 *versions_out);
static void bs_bar_view_model_copy_topic_versions(guint64 *dst, const guint64 *src);
static bool bs_bar_view_model_topic_versions_equal(const guint64 *lhs, const guint64 *rhs);
static bool bs_bar_view_model_transport_live(BsBarViewModel *vm);
static const char *bs_bar_vm_workspace_full_label(const BsBarVmWorkspace *workspace);
static char *bs_bar_vm_workspace_compact_label(const BsBarVmWorkspace *workspace);
static BsBarWorkspacePresentation bs_bar_vm_choose_workspace_presentation(guint workspace_count,
                                                                          const BsBarVmWorkspace *workspace);
static BsBarTrayVisualState bs_bar_vm_tray_visual_state(const BsBarVmTrayItem *item);
static BsBarTrayPrimaryAction bs_bar_vm_tray_primary_action(const BsBarVmTrayItem *item);
static char *bs_bar_vm_tray_effective_icon_name(const BsBarVmTrayItem *item);
static char *bs_bar_vm_tray_fallback_label(const BsBarVmTrayItem *item);
static void bs_bar_view_model_rebuild_workspace_strip(BsBarViewModel *vm);
static void bs_bar_view_model_rebuild_center_state(BsBarViewModel *vm);
static void bs_bar_view_model_rebuild_tray_items(BsBarViewModel *vm);
static gint bs_bar_vm_compare_workspace_ptr(gconstpointer lhs, gconstpointer rhs);
static gint bs_bar_vm_compare_window_candidate_ptr(gconstpointer lhs, gconstpointer rhs);
static gint bs_bar_vm_compare_tray_item_ptr(gconstpointer lhs, gconstpointer rhs);
static void bs_bar_view_model_rebuild_all(BsBarViewModel *vm);

static void
bs_bar_vm_shell_state_clear(BsBarVmShellState *shell) {
  if (shell == NULL) {
    return;
  }

  g_clear_pointer(&shell->focused_output_name, g_free);
  g_clear_pointer(&shell->focused_workspace_id, g_free);
  g_clear_pointer(&shell->focused_window_id, g_free);
  g_clear_pointer(&shell->focused_window_title, g_free);
  g_clear_pointer(&shell->degraded_reason, g_free);
  shell->niri_connected = false;
  shell->outputs_ready = false;
  shell->workspaces_ready = false;
  shell->windows_ready = false;
  shell->bootstrap_used_fallback = false;
}

static void
bs_bar_vm_window_free(gpointer data) {
  BsBarVmWindow *window = data;

  if (window == NULL) {
    return;
  }

  g_free(window->id);
  g_free(window->title);
  g_free(window->app_id);
  g_free(window->desktop_id);
  g_free(window->workspace_id);
  g_free(window->output_name);
  g_free(window);
}

static void
bs_bar_vm_output_free(gpointer data) {
  BsBarVmOutput *output = data;

  if (output == NULL) {
    return;
  }

  g_free(output->name);
  g_free(output);
}

static void
bs_bar_vm_workspace_free(gpointer data) {
  BsBarVmWorkspace *workspace = data;

  if (workspace == NULL) {
    return;
  }

  g_free(workspace->id);
  g_free(workspace->name);
  g_free(workspace->output_name);
  g_free(workspace);
}

static void
bs_bar_vm_tray_item_free(gpointer data) {
  BsBarVmTrayItem *item = data;

  if (item == NULL) {
    return;
  }

  g_free(item->item_id);
  g_free(item->title);
  g_free(item->icon_name);
  g_free(item->attention_icon_name);
  g_free(item->status);
  g_free(item->menu_object_path);
  g_free(item);
}

static void
bs_bar_workspace_strip_item_free(gpointer data) {
  BsBarWorkspaceStripItem *item = data;

  if (item == NULL) {
    return;
  }

  g_free(item->id);
  g_free(item->name);
  g_free(item->output_name);
  g_free(item->display_label);
  g_free(item->tooltip_label);
  g_free(item);
}

static void
bs_bar_window_candidate_free(gpointer data) {
  BsBarWindowCandidate *candidate = data;

  if (candidate == NULL) {
    return;
  }

  g_free(candidate->window_id);
  g_free(candidate->title);
  g_free(candidate->desktop_id);
  g_free(candidate->app_id);
  g_free(candidate);
}

static void
bs_bar_tray_item_view_free(gpointer data) {
  BsBarTrayItemView *item = data;

  if (item == NULL) {
    return;
  }

  g_free(item->item_id);
  g_free(item->title);
  g_free(item->icon_name);
  g_free(item->attention_icon_name);
  g_free(item->status);
  g_free(item->menu_object_path);
  g_free(item->effective_icon_name);
  g_free(item->fallback_label);
  g_free(item);
}

static void
bs_bar_view_model_emit_changed(BsBarViewModel *vm, guint dirty_flags) {
  g_return_if_fail(vm != NULL);

  if (dirty_flags == BS_BAR_VM_DIRTY_NONE) {
    return;
  }
  if (vm->changed_cb != NULL) {
    vm->changed_cb(vm, dirty_flags, vm->changed_user_data);
  }
}

static void
bs_bar_view_model_clear_caches(BsBarViewModel *vm) {
  g_return_if_fail(vm != NULL);

  bs_bar_vm_shell_state_clear(&vm->shell);
  g_hash_table_remove_all(vm->windows_by_id);
  g_hash_table_remove_all(vm->outputs_by_name);
  g_hash_table_remove_all(vm->workspaces_by_id);
  g_hash_table_remove_all(vm->tray_items_by_id);
  g_ptr_array_set_size(vm->workspace_strip_items, 0);
  g_ptr_array_set_size(vm->window_candidates, 0);
  g_ptr_array_set_size(vm->tray_items, 0);
  g_clear_pointer(&vm->focused_title, g_free);
  g_clear_pointer(&vm->focused_app_name, g_free);
}

static void
bs_bar_view_model_apply_bar_config_defaults(BsBarConfig *config) {
  BsShellConfig shell_defaults = {0};

  g_return_if_fail(config != NULL);

  bs_shell_config_init_defaults(&shell_defaults);
  *config = shell_defaults.bar;
  bs_shell_config_clear(&shell_defaults);
}

static const char *
bs_bar_vm_json_string_member(JsonObject *object, const char *member_name) {
  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return NULL;
  }

  return json_object_get_string_member(object, member_name);
}

static bool
bs_bar_vm_json_bool_member(JsonObject *object, const char *member_name, bool fallback) {
  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return fallback;
  }

  return json_object_get_boolean_member(object, member_name);
}

static int
bs_bar_vm_json_int_member(JsonObject *object, const char *member_name, int fallback) {
  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return fallback;
  }

  return json_object_get_int_member(object, member_name);
}

static guint64
bs_bar_vm_json_uint64_member(JsonObject *object, const char *member_name, guint64 fallback) {
  JsonNode *node = NULL;
  GValue value = G_VALUE_INIT;

  if (object == NULL || member_name == NULL || !json_object_has_member(object, member_name)) {
    return fallback;
  }

  node = json_object_get_member(object, member_name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node)) {
    return fallback;
  }

  json_node_get_value(node, &value);
  if (G_VALUE_HOLDS_INT64(&value)
      || G_VALUE_HOLDS_UINT64(&value)
      || G_VALUE_HOLDS_INT(&value)
      || G_VALUE_HOLDS_UINT(&value)) {
    guint64 parsed = (guint64) json_node_get_int(node);

    g_value_unset(&value);
    return parsed;
  }
  if (G_VALUE_HOLDS_STRING(&value)) {
    const char *string_value = json_node_get_string(node);

    g_value_unset(&value);
    if (string_value == NULL || *string_value == '\0') {
      return fallback;
    }
    return g_ascii_strtoull(string_value, NULL, 10);
  }

  g_value_unset(&value);
  return fallback;
}

static void
bs_bar_view_model_parse_shell(BsBarViewModel *vm, JsonObject *object) {
  g_return_if_fail(vm != NULL);

  bs_bar_vm_shell_state_clear(&vm->shell);
  if (object == NULL) {
    return;
  }

  vm->shell.niri_connected = bs_bar_vm_json_bool_member(object, "niri_connected", false);
  vm->shell.outputs_ready = bs_bar_vm_json_bool_member(object, "outputs_ready", false);
  vm->shell.workspaces_ready = bs_bar_vm_json_bool_member(object, "workspaces_ready", false);
  vm->shell.windows_ready = bs_bar_vm_json_bool_member(object, "windows_ready", false);
  vm->shell.bootstrap_used_fallback = bs_bar_vm_json_bool_member(object,
                                                                 "bootstrap_used_fallback",
                                                                 false);
  vm->shell.degraded_reason = g_strdup(bs_bar_vm_json_string_member(object, "degraded_reason"));
  vm->shell.focused_output_name = g_strdup(bs_bar_vm_json_string_member(object, "focused_output_name"));
  vm->shell.focused_workspace_id = g_strdup(bs_bar_vm_json_string_member(object, "focused_workspace_id"));
  vm->shell.focused_window_id = g_strdup(bs_bar_vm_json_string_member(object, "focused_window_id"));
  vm->shell.focused_window_title = g_strdup(bs_bar_vm_json_string_member(object, "focused_window_title"));
}

static void
bs_bar_view_model_parse_windows(BsBarViewModel *vm, JsonObject *object) {
  JsonArray *windows = NULL;

  g_return_if_fail(vm != NULL);

  g_hash_table_remove_all(vm->windows_by_id);
  if (object == NULL || !json_object_has_member(object, "windows")) {
    return;
  }

  windows = json_object_get_array_member(object, "windows");
  for (guint i = 0; i < json_array_get_length(windows); i++) {
    JsonObject *item = json_array_get_object_element(windows, i);
    BsBarVmWindow *window = NULL;
    const char *id = NULL;

    if (item == NULL) {
      continue;
    }

    id = bs_bar_vm_json_string_member(item, "id");
    if (id == NULL || *id == '\0') {
      continue;
    }

    window = g_new0(BsBarVmWindow, 1);
    window->id = g_strdup(id);
    window->title = g_strdup(bs_bar_vm_json_string_member(item, "title"));
    window->app_id = g_strdup(bs_bar_vm_json_string_member(item, "app_id"));
    window->desktop_id = g_strdup(bs_bar_vm_json_string_member(item, "desktop_id"));
    window->workspace_id = g_strdup(bs_bar_vm_json_string_member(item, "workspace_id"));
    window->output_name = g_strdup(bs_bar_vm_json_string_member(item, "output_name"));
    window->focused = bs_bar_vm_json_bool_member(item, "focused", false);
    window->focus_ts = bs_bar_vm_json_uint64_member(item, "focus_ts", 0);

    g_hash_table_replace(vm->windows_by_id, g_strdup(window->id), window);
  }
}

static void
bs_bar_view_model_parse_workspaces(BsBarViewModel *vm, JsonObject *object) {
  JsonArray *outputs = NULL;
  JsonArray *workspaces = NULL;

  g_return_if_fail(vm != NULL);

  g_hash_table_remove_all(vm->outputs_by_name);
  g_hash_table_remove_all(vm->workspaces_by_id);
  if (object == NULL) {
    return;
  }

  if (json_object_has_member(object, "outputs")) {
    outputs = json_object_get_array_member(object, "outputs");
    for (guint i = 0; i < json_array_get_length(outputs); i++) {
      JsonObject *item = json_array_get_object_element(outputs, i);
      BsBarVmOutput *output = NULL;
      const char *name = NULL;

      if (item == NULL) {
        continue;
      }

      name = bs_bar_vm_json_string_member(item, "name");
      if (name == NULL || *name == '\0') {
        continue;
      }

      output = g_new0(BsBarVmOutput, 1);
      output->name = g_strdup(name);
      output->focused = bs_bar_vm_json_bool_member(item, "focused", false);
      g_hash_table_replace(vm->outputs_by_name, g_strdup(output->name), output);
    }
  }

  if (!json_object_has_member(object, "workspaces")) {
    return;
  }

  workspaces = json_object_get_array_member(object, "workspaces");
  for (guint i = 0; i < json_array_get_length(workspaces); i++) {
    JsonObject *item = json_array_get_object_element(workspaces, i);
    BsBarVmWorkspace *workspace = NULL;
    const char *id = NULL;

    if (item == NULL) {
      continue;
    }

    id = bs_bar_vm_json_string_member(item, "id");
    if (id == NULL || *id == '\0') {
      continue;
    }

    workspace = g_new0(BsBarVmWorkspace, 1);
    workspace->id = g_strdup(id);
    workspace->name = g_strdup(bs_bar_vm_json_string_member(item, "name"));
    workspace->output_name = g_strdup(bs_bar_vm_json_string_member(item, "output_name"));
    workspace->focused = bs_bar_vm_json_bool_member(item, "focused", false);
    workspace->empty = bs_bar_vm_json_bool_member(item, "empty", false);
    workspace->local_index = bs_bar_vm_json_int_member(item, "local_index", 0);
    g_hash_table_replace(vm->workspaces_by_id, g_strdup(workspace->id), workspace);
  }
}

static void
bs_bar_view_model_parse_tray(BsBarViewModel *vm, JsonObject *object) {
  JsonArray *items = NULL;

  g_return_if_fail(vm != NULL);

  g_hash_table_remove_all(vm->tray_items_by_id);
  if (object == NULL || !json_object_has_member(object, "items")) {
    return;
  }

  items = json_object_get_array_member(object, "items");
  for (guint i = 0; i < json_array_get_length(items); i++) {
    JsonObject *item = json_array_get_object_element(items, i);
    BsBarVmTrayItem *tray_item = NULL;
    const char *item_id = NULL;

    if (item == NULL) {
      continue;
    }

    item_id = bs_bar_vm_json_string_member(item, "item_id");
    if (item_id == NULL || *item_id == '\0') {
      continue;
    }

    tray_item = g_new0(BsBarVmTrayItem, 1);
    tray_item->item_id = g_strdup(item_id);
    tray_item->title = g_strdup(bs_bar_vm_json_string_member(item, "title"));
    tray_item->icon_name = g_strdup(bs_bar_vm_json_string_member(item, "icon_name"));
    tray_item->attention_icon_name = g_strdup(bs_bar_vm_json_string_member(item,
                                                                           "attention_icon_name"));
    tray_item->status = g_strdup(bs_bar_vm_json_string_member(item, "status"));
    tray_item->menu_object_path = g_strdup(bs_bar_vm_json_string_member(item, "menu_object_path"));
    tray_item->presentation_seq = bs_bar_vm_json_uint64_member(item, "presentation_seq", 0);
    tray_item->item_is_menu = bs_bar_vm_json_bool_member(item, "item_is_menu", false);
    tray_item->has_activate = bs_bar_vm_json_bool_member(item, "has_activate", false);
    tray_item->has_context_menu = bs_bar_vm_json_bool_member(item, "has_context_menu", false);
    g_hash_table_replace(vm->tray_items_by_id, g_strdup(tray_item->item_id), tray_item);
  }
}

static bool
bs_bar_view_model_parse_settings(BsBarViewModel *vm, JsonObject *object) {
  JsonObject *bar = NULL;
  BsBarConfig next = {0};

  g_return_val_if_fail(vm != NULL, false);

  if (object == NULL || !json_object_has_member(object, "bar")) {
    return false;
  }

  bar = json_object_get_object_member(object, "bar");
  if (bar == NULL) {
    return false;
  }

  next = vm->bar_config;
  next.height_px = (uint32_t) bs_bar_vm_json_int_member(bar, "height_px", (int) next.height_px);
  next.show_workspace_strip = bs_bar_vm_json_bool_member(bar,
                                                         "show_workspace_strip",
                                                         next.show_workspace_strip);
  next.show_focused_title = bs_bar_vm_json_bool_member(bar,
                                                       "show_focused_title",
                                                       next.show_focused_title);
  next.show_tray = bs_bar_vm_json_bool_member(bar, "show_tray", next.show_tray);
  next.show_clock = bs_bar_vm_json_bool_member(bar, "show_clock", next.show_clock);

  if (memcmp(&vm->bar_config, &next, sizeof(next)) == 0) {
    return false;
  }

  vm->bar_config = next;
  return true;
}

static void
bs_bar_view_model_parse_topic_versions(BsBarViewModel *vm,
                                       JsonObject *object,
                                       guint64 *versions_out) {
  JsonObjectIter iter;
  const char *name = NULL;
  JsonNode *value = NULL;

  g_return_if_fail(vm != NULL);
  g_return_if_fail(versions_out != NULL);

  memset(versions_out, 0, sizeof(guint64) * BS_TOPIC_COUNT);
  if (object == NULL) {
    return;
  }

  json_object_iter_init(&iter, object);
  while (json_object_iter_next(&iter, &name, &value)) {
    BsTopic topic = BS_TOPIC_SHELL;

    if (name == NULL || value == NULL || !JSON_NODE_HOLDS_VALUE(value)) {
      continue;
    }
    if (!bs_topic_from_string(name, &topic)) {
      continue;
    }

    versions_out[topic] = (guint64) json_node_get_int(value);
  }
}

static void
bs_bar_view_model_copy_topic_versions(guint64 *dst, const guint64 *src) {
  memcpy(dst, src, sizeof(guint64) * BS_TOPIC_COUNT);
}

static bool
bs_bar_view_model_topic_versions_equal(const guint64 *lhs, const guint64 *rhs) {
  return memcmp(lhs, rhs, sizeof(guint64) * BS_TOPIC_COUNT) == 0;
}

static bool
bs_bar_view_model_transport_live(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, false);
  return vm->phase == BS_BAR_VM_PHASE_LIVE && vm->shell.niri_connected;
}

static const char *
bs_bar_vm_workspace_full_label(const BsBarVmWorkspace *workspace) {
  g_return_val_if_fail(workspace != NULL, NULL);

  if (workspace->name != NULL && *workspace->name != '\0') {
    return workspace->name;
  }
  return workspace->id;
}

static char *
bs_bar_vm_workspace_compact_label(const BsBarVmWorkspace *workspace) {
  g_return_val_if_fail(workspace != NULL, NULL);

  if (workspace->local_index > 0) {
    return g_strdup_printf("%d", workspace->local_index);
  }
  if (workspace->name != NULL && *workspace->name != '\0') {
    gunichar first = g_utf8_get_char_validated(workspace->name, -1);

    if (first != (gunichar) -1 && first != (gunichar) -2 && first != 0) {
      char buffer[8] = {0};
      gint len = g_unichar_to_utf8(g_unichar_toupper(first), buffer);

      if (len > 0) {
        return g_strdup(buffer);
      }
    }
  }
  if (workspace->id != NULL && *workspace->id != '\0') {
    return g_strdup(workspace->id);
  }
  return g_strdup("?");
}

static BsBarWorkspacePresentation
bs_bar_vm_choose_workspace_presentation(guint workspace_count, const BsBarVmWorkspace *workspace) {
  g_return_val_if_fail(workspace != NULL, BS_BAR_WORKSPACE_PRESENTATION_FULL);

  if (workspace_count <= 4) {
    return BS_BAR_WORKSPACE_PRESENTATION_FULL;
  }
  if (workspace_count <= 7) {
    if (workspace->focused) {
      return BS_BAR_WORKSPACE_PRESENTATION_FULL;
    }
    return BS_BAR_WORKSPACE_PRESENTATION_COMPACT;
  }

  if (workspace->focused) {
    return BS_BAR_WORKSPACE_PRESENTATION_FULL;
  }
  if (!workspace->empty) {
    return BS_BAR_WORKSPACE_PRESENTATION_COMPACT;
  }
  return BS_BAR_WORKSPACE_PRESENTATION_MINIMAL;
}

static BsBarTrayVisualState
bs_bar_vm_tray_visual_state(const BsBarVmTrayItem *item) {
  g_return_val_if_fail(item != NULL, BS_BAR_TRAY_VISUAL_PASSIVE);

  if (item->status != NULL && g_strcmp0(item->status, "attention") == 0) {
    return BS_BAR_TRAY_VISUAL_ATTENTION;
  }
  if (item->status != NULL && g_strcmp0(item->status, "active") == 0) {
    return BS_BAR_TRAY_VISUAL_ACTIVE;
  }
  return BS_BAR_TRAY_VISUAL_PASSIVE;
}

static BsBarTrayPrimaryAction
bs_bar_vm_tray_primary_action(const BsBarVmTrayItem *item) {
  g_return_val_if_fail(item != NULL, BS_BAR_TRAY_PRIMARY_NONE);

  if (item->has_activate) {
    return BS_BAR_TRAY_PRIMARY_ACTIVATE;
  }
  if (item->has_context_menu) {
    return BS_BAR_TRAY_PRIMARY_MENU;
  }
  return BS_BAR_TRAY_PRIMARY_NONE;
}

static char *
bs_bar_vm_tray_effective_icon_name(const BsBarVmTrayItem *item) {
  g_return_val_if_fail(item != NULL, NULL);

  if (item->status != NULL && g_strcmp0(item->status, "attention") == 0
      && item->attention_icon_name != NULL && *item->attention_icon_name != '\0') {
    return g_strdup(item->attention_icon_name);
  }
  if (item->icon_name != NULL && *item->icon_name != '\0') {
    return g_strdup(item->icon_name);
  }
  return NULL;
}

static char *
bs_bar_vm_tray_fallback_label(const BsBarVmTrayItem *item) {
  const char *source = NULL;

  g_return_val_if_fail(item != NULL, NULL);

  source = item->title != NULL && *item->title != '\0' ? item->title : item->item_id;
  if (source != NULL && *source != '\0') {
    gunichar first = g_utf8_get_char_validated(source, -1);

    if (first != (gunichar) -1 && first != (gunichar) -2 && first != 0) {
      char buffer[8] = {0};
      gint len = g_unichar_to_utf8(g_unichar_toupper(first), buffer);

      if (len > 0) {
        return g_strdup(buffer);
      }
    }
  }
  return g_strdup("?");
}

static gint
bs_bar_vm_compare_workspace_ptr(gconstpointer lhs, gconstpointer rhs) {
  const BsBarWorkspaceStripItem *a = *(BsBarWorkspaceStripItem * const *) lhs;
  const BsBarWorkspaceStripItem *b = *(BsBarWorkspaceStripItem * const *) rhs;

  if (a->local_index != b->local_index) {
    return a->local_index < b->local_index ? -1 : 1;
  }
  return g_strcmp0(a->id, b->id);
}

static gint
bs_bar_vm_compare_window_candidate_ptr(gconstpointer lhs, gconstpointer rhs) {
  const BsBarWindowCandidate *a = *(BsBarWindowCandidate * const *) lhs;
  const BsBarWindowCandidate *b = *(BsBarWindowCandidate * const *) rhs;

  if (a->focused != b->focused) {
    return a->focused ? -1 : 1;
  }
  if (a->focus_ts != b->focus_ts) {
    return a->focus_ts > b->focus_ts ? -1 : 1;
  }
  return g_strcmp0(a->title, b->title);
}

static gint
bs_bar_vm_compare_tray_item_ptr(gconstpointer lhs, gconstpointer rhs) {
  const BsBarTrayItemView *a = *(BsBarTrayItemView * const *) lhs;
  const BsBarTrayItemView *b = *(BsBarTrayItemView * const *) rhs;

  if (a->presentation_seq != b->presentation_seq) {
    return a->presentation_seq < b->presentation_seq ? -1 : 1;
  }
  return g_strcmp0(a->item_id, b->item_id);
}

static void
bs_bar_view_model_rebuild_workspace_strip(BsBarViewModel *vm) {
  GHashTableIter iter;
  gpointer value = NULL;
  guint workspace_count = 0;

  g_return_if_fail(vm != NULL);

  workspace_count = (guint) g_hash_table_size(vm->workspaces_by_id);
  g_ptr_array_set_size(vm->workspace_strip_items, 0);
  g_hash_table_iter_init(&iter, vm->workspaces_by_id);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    const BsBarVmWorkspace *workspace = value;
    BsBarWorkspaceStripItem *item = NULL;

    if (workspace == NULL) {
      continue;
    }
    if (vm->shell.focused_output_name != NULL
        && workspace->output_name != NULL
        && g_strcmp0(workspace->output_name, vm->shell.focused_output_name) != 0) {
      continue;
    }

    item = g_new0(BsBarWorkspaceStripItem, 1);
    item->id = g_strdup(workspace->id);
    item->name = g_strdup(workspace->name);
    item->output_name = g_strdup(workspace->output_name);
    item->focused = workspace->focused;
    item->empty = workspace->empty;
    item->local_index = workspace->local_index;
    item->tooltip_label = g_strdup(bs_bar_vm_workspace_full_label(workspace));
    item->presentation = bs_bar_vm_choose_workspace_presentation(workspace_count, workspace);
    item->sort_rank = workspace->focused ? 0U : (workspace->empty ? 2U : 1U);
    if (item->presentation == BS_BAR_WORKSPACE_PRESENTATION_FULL) {
      item->display_label = g_strdup(item->tooltip_label);
    } else if (item->presentation == BS_BAR_WORKSPACE_PRESENTATION_COMPACT) {
      item->display_label = bs_bar_vm_workspace_compact_label(workspace);
    } else {
      item->display_label = g_strdup("");
    }
    g_ptr_array_add(vm->workspace_strip_items, item);
  }

  g_ptr_array_sort(vm->workspace_strip_items, bs_bar_vm_compare_workspace_ptr);
  if (!bs_bar_view_model_transport_live(vm) || !vm->shell.workspaces_ready) {
    vm->workspace_strip_state = BS_BAR_VM_WORKSPACE_STRIP_LOADING;
  } else if (vm->workspace_strip_items->len == 0) {
    vm->workspace_strip_state = BS_BAR_VM_WORKSPACE_STRIP_READY_EMPTY;
  } else {
    vm->workspace_strip_state = BS_BAR_VM_WORKSPACE_STRIP_READY_ITEMS;
  }
}

static void
bs_bar_view_model_rebuild_center_state(BsBarViewModel *vm) {
  GHashTableIter iter;
  gpointer value = NULL;
  const BsBarVmWindow *focused_window = NULL;
  bool has_focused_window = false;

  g_return_if_fail(vm != NULL);

  g_ptr_array_set_size(vm->window_candidates, 0);
  g_clear_pointer(&vm->focused_title, g_free);
  g_clear_pointer(&vm->focused_app_name, g_free);

  g_hash_table_iter_init(&iter, vm->windows_by_id);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    const BsBarVmWindow *window = value;
    BsBarWindowCandidate *candidate = NULL;

    if (window == NULL) {
      continue;
    }
    if (vm->shell.focused_workspace_id != NULL && *vm->shell.focused_workspace_id != '\0') {
      if (g_strcmp0(window->workspace_id, vm->shell.focused_workspace_id) != 0) {
        continue;
      }
    } else if (vm->shell.focused_output_name != NULL && *vm->shell.focused_output_name != '\0') {
      if (g_strcmp0(window->output_name, vm->shell.focused_output_name) != 0) {
        continue;
      }
    }

    candidate = g_new0(BsBarWindowCandidate, 1);
    candidate->window_id = g_strdup(window->id);
    candidate->title = g_strdup(window->title);
    candidate->desktop_id = g_strdup(window->desktop_id);
    candidate->app_id = g_strdup(window->app_id);
    candidate->focused = window->focused;
    candidate->focus_ts = window->focus_ts;
    g_ptr_array_add(vm->window_candidates, candidate);

    if (window->focused
        || (vm->shell.focused_window_id != NULL
            && g_strcmp0(vm->shell.focused_window_id, window->id) == 0)) {
      focused_window = window;
    }
  }

  g_ptr_array_sort(vm->window_candidates, bs_bar_vm_compare_window_candidate_ptr);
  if (vm->shell.focused_window_id != NULL && *vm->shell.focused_window_id != '\0') {
    has_focused_window = true;
  }
  if (vm->shell.focused_window_title != NULL && *vm->shell.focused_window_title != '\0') {
    has_focused_window = true;
    vm->focused_title = g_strdup(vm->shell.focused_window_title);
  } else if (focused_window != NULL && focused_window->title != NULL && *focused_window->title != '\0') {
    has_focused_window = true;
    vm->focused_title = g_strdup(focused_window->title);
  } else if (focused_window != NULL && focused_window->desktop_id != NULL && *focused_window->desktop_id != '\0') {
    has_focused_window = true;
    vm->focused_title = g_strdup(focused_window->desktop_id);
  } else if (focused_window != NULL && focused_window->app_id != NULL && *focused_window->app_id != '\0') {
    has_focused_window = true;
    vm->focused_title = g_strdup(focused_window->app_id);
  } else {
    vm->focused_title = g_strdup("No focused window");
  }

  if (focused_window != NULL && focused_window->desktop_id != NULL && *focused_window->desktop_id != '\0') {
    vm->focused_app_name = g_strdup(focused_window->desktop_id);
  } else if (focused_window != NULL && focused_window->app_id != NULL && *focused_window->app_id != '\0') {
    vm->focused_app_name = g_strdup(focused_window->app_id);
  }

  if (!bs_bar_view_model_transport_live(vm)) {
    vm->center_state = BS_BAR_VM_CENTER_CONNECTING;
  } else if (!vm->shell.windows_ready) {
    vm->center_state = BS_BAR_VM_CENTER_SYNCING_WINDOWS;
  } else if (has_focused_window) {
    vm->center_state = BS_BAR_VM_CENTER_READY_FOCUSED_WINDOW;
  } else {
    vm->center_state = BS_BAR_VM_CENTER_READY_NO_FOCUSED_WINDOW;
  }
}

static void
bs_bar_view_model_rebuild_tray_items(BsBarViewModel *vm) {
  GHashTableIter iter;
  gpointer value = NULL;

  g_return_if_fail(vm != NULL);

  g_ptr_array_set_size(vm->tray_items, 0);
  g_hash_table_iter_init(&iter, vm->tray_items_by_id);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    const BsBarVmTrayItem *item = value;
    BsBarTrayItemView *view = NULL;

    if (item == NULL) {
      continue;
    }

    view = g_new0(BsBarTrayItemView, 1);
    view->item_id = g_strdup(item->item_id);
    view->title = g_strdup(item->title);
    view->icon_name = g_strdup(item->icon_name);
    view->attention_icon_name = g_strdup(item->attention_icon_name);
    view->status = g_strdup(item->status);
    view->menu_object_path = g_strdup(item->menu_object_path);
    view->presentation_seq = item->presentation_seq;
    view->item_is_menu = item->item_is_menu;
    view->has_activate = item->has_activate;
    view->has_context_menu = item->has_context_menu;
    view->effective_icon_name = bs_bar_vm_tray_effective_icon_name(item);
    view->fallback_label = bs_bar_vm_tray_fallback_label(item);
    view->visual_state = bs_bar_vm_tray_visual_state(item);
    view->primary_action = bs_bar_vm_tray_primary_action(item);
    view->show_by_default = true;
    g_ptr_array_add(vm->tray_items, view);
  }

  g_ptr_array_sort(vm->tray_items, bs_bar_vm_compare_tray_item_ptr);
  if (!bs_bar_view_model_transport_live(vm)) {
    vm->tray_state = BS_BAR_VM_TRAY_CONNECTING;
  } else if (vm->tray_items->len == 0) {
    vm->tray_state = BS_BAR_VM_TRAY_READY_EMPTY;
  } else {
    vm->tray_state = BS_BAR_VM_TRAY_READY_ITEMS;
  }
}

static void
bs_bar_view_model_rebuild_all(BsBarViewModel *vm) {
  g_return_if_fail(vm != NULL);

  bs_bar_view_model_rebuild_workspace_strip(vm);
  bs_bar_view_model_rebuild_center_state(vm);
  bs_bar_view_model_rebuild_tray_items(vm);
}

BsBarViewModel *
bs_bar_view_model_new(void) {
  BsBarViewModel *vm = g_new0(BsBarViewModel, 1);

  bs_bar_view_model_apply_bar_config_defaults(&vm->bar_config);
  vm->windows_by_id = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bs_bar_vm_window_free);
  vm->outputs_by_name = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bs_bar_vm_output_free);
  vm->workspaces_by_id = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bs_bar_vm_workspace_free);
  vm->tray_items_by_id = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bs_bar_vm_tray_item_free);
  vm->workspace_strip_items = g_ptr_array_new_with_free_func(bs_bar_workspace_strip_item_free);
  vm->window_candidates = g_ptr_array_new_with_free_func(bs_bar_window_candidate_free);
  vm->tray_items = g_ptr_array_new_with_free_func(bs_bar_tray_item_view_free);
  vm->phase = BS_BAR_VM_PHASE_DISCONNECTED;
  vm->workspace_strip_state = BS_BAR_VM_WORKSPACE_STRIP_LOADING;
  vm->center_state = BS_BAR_VM_CENTER_CONNECTING;
  vm->tray_state = BS_BAR_VM_TRAY_CONNECTING;
  vm->focused_title = g_strdup("Connecting...");
  return vm;
}

void
bs_bar_view_model_free(BsBarViewModel *vm) {
  if (vm == NULL) {
    return;
  }

  bs_bar_vm_shell_state_clear(&vm->shell);
  g_clear_pointer(&vm->windows_by_id, g_hash_table_unref);
  g_clear_pointer(&vm->outputs_by_name, g_hash_table_unref);
  g_clear_pointer(&vm->workspaces_by_id, g_hash_table_unref);
  g_clear_pointer(&vm->tray_items_by_id, g_hash_table_unref);
  g_clear_pointer(&vm->workspace_strip_items, g_ptr_array_unref);
  g_clear_pointer(&vm->window_candidates, g_ptr_array_unref);
  g_clear_pointer(&vm->tray_items, g_ptr_array_unref);
  g_clear_pointer(&vm->focused_title, g_free);
  g_clear_pointer(&vm->focused_app_name, g_free);
  g_free(vm);
}

void
bs_bar_view_model_reset_connection(BsBarViewModel *vm) {
  g_return_if_fail(vm != NULL);

  bs_bar_view_model_clear_caches(vm);
  memset(vm->topic_versions, 0, sizeof(vm->topic_versions));
  memset(vm->snapshot_topic_versions, 0, sizeof(vm->snapshot_topic_versions));
  vm->generation = 0;
  vm->phase = BS_BAR_VM_PHASE_WAITING_SNAPSHOT;
  vm->subscribed = false;
  vm->needs_resnapshot = false;
  vm->workspace_strip_state = BS_BAR_VM_WORKSPACE_STRIP_LOADING;
  vm->center_state = BS_BAR_VM_CENTER_CONNECTING;
  vm->tray_state = BS_BAR_VM_TRAY_CONNECTING;
  vm->focused_title = g_strdup("Connecting...");
  bs_bar_view_model_emit_changed(vm, BS_BAR_VM_DIRTY_ALL);
}

bool
bs_bar_view_model_consume_json_line(BsBarViewModel *vm,
                                    const char *line,
                                    GError **error) {
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *root = NULL;
  JsonObject *root_object = NULL;
  const char *kind = NULL;
  guint dirty_flags = BS_BAR_VM_DIRTY_NONE;

  g_return_val_if_fail(vm != NULL, false);
  g_return_val_if_fail(line != NULL, false);

  parser = json_parser_new();
  if (!json_parser_load_from_data(parser, line, -1, error)) {
    return false;
  }

  root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
    return true;
  }

  root_object = json_node_get_object(root);
  kind = bs_bar_vm_json_string_member(root_object, "kind");

  if (g_strcmp0(kind, "snapshot") == 0) {
    JsonObject *state = json_object_get_object_member(root_object, "state");

    if (json_object_has_member(root_object, "generation")) {
      vm->generation = (guint64) json_object_get_int_member(root_object, "generation");
    }
    if (json_object_has_member(root_object, "topic_versions")) {
      bs_bar_view_model_parse_topic_versions(vm,
                                             json_object_get_object_member(root_object, "topic_versions"),
                                             vm->snapshot_topic_versions);
      bs_bar_view_model_copy_topic_versions(vm->topic_versions, vm->snapshot_topic_versions);
    }

    if (state != NULL) {
      bs_bar_view_model_parse_shell(vm,
                                    json_object_has_member(state, "shell")
                                      ? json_object_get_object_member(state, "shell")
                                      : NULL);
      bs_bar_view_model_parse_windows(vm,
                                      json_object_has_member(state, "windows")
                                        ? json_object_get_object_member(state, "windows")
                                        : NULL);
      bs_bar_view_model_parse_workspaces(vm,
                                         json_object_has_member(state, "workspaces")
                                           ? json_object_get_object_member(state, "workspaces")
                                           : NULL);
      bs_bar_view_model_parse_tray(vm,
                                   json_object_has_member(state, "tray")
                                     ? json_object_get_object_member(state, "tray")
                                     : NULL);
      if (bs_bar_view_model_parse_settings(vm,
                                           json_object_has_member(state, "settings")
                                             ? json_object_get_object_member(state, "settings")
                                             : NULL)) {
        dirty_flags |= BS_BAR_VM_DIRTY_LAYOUT;
      }
    }

    bs_bar_view_model_rebuild_all(vm);
    vm->needs_resnapshot = false;
    vm->phase = vm->subscribed ? BS_BAR_VM_PHASE_LIVE : BS_BAR_VM_PHASE_WAITING_SUBSCRIBE_ACK;
    dirty_flags |= BS_BAR_VM_DIRTY_LEFT | BS_BAR_VM_DIRTY_CENTER | BS_BAR_VM_DIRTY_RIGHT;
    bs_bar_view_model_emit_changed(vm, dirty_flags);
    return true;
  }

  if (g_strcmp0(kind, "subscribed") == 0) {
    guint64 subscribed_versions[BS_TOPIC_COUNT] = {0};
    BsBarVmPhase previous_phase = vm->phase;

    vm->subscribed = true;
    if (json_object_has_member(root_object, "topic_versions")) {
      bs_bar_view_model_parse_topic_versions(vm,
                                             json_object_get_object_member(root_object, "topic_versions"),
                                             subscribed_versions);
      if (!bs_bar_view_model_topic_versions_equal(subscribed_versions, vm->snapshot_topic_versions)) {
        vm->needs_resnapshot = true;
        vm->phase = BS_BAR_VM_PHASE_WAITING_SNAPSHOT;
      } else {
        vm->needs_resnapshot = false;
        vm->phase = BS_BAR_VM_PHASE_LIVE;
      }
      bs_bar_view_model_copy_topic_versions(vm->topic_versions, subscribed_versions);
    } else {
      vm->phase = BS_BAR_VM_PHASE_LIVE;
    }

    if (vm->phase != previous_phase) {
      bs_bar_view_model_rebuild_workspace_strip(vm);
      bs_bar_view_model_rebuild_center_state(vm);
      bs_bar_view_model_rebuild_tray_items(vm);
      bs_bar_view_model_emit_changed(vm,
                                     BS_BAR_VM_DIRTY_LEFT | BS_BAR_VM_DIRTY_CENTER
                                       | BS_BAR_VM_DIRTY_RIGHT);
    }

    return true;
  }

  if (g_strcmp0(kind, "event") == 0) {
    const char *topic = bs_bar_vm_json_string_member(root_object, "topic");
    JsonObject *payload = json_object_has_member(root_object, "payload")
                            ? json_object_get_object_member(root_object, "payload")
                            : NULL;

    if (topic == NULL) {
      return true;
    }

    if (json_object_has_member(root_object, "generation")) {
      vm->generation = (guint64) json_object_get_int_member(root_object, "generation");
    }
    if (json_object_has_member(root_object, "version")) {
      BsTopic parsed_topic = BS_TOPIC_SHELL;
      if (bs_topic_from_string(topic, &parsed_topic)) {
        vm->topic_versions[parsed_topic] = (guint64) json_object_get_int_member(root_object, "version");
      }
    }

    if (g_strcmp0(topic, "shell") == 0) {
      bs_bar_view_model_parse_shell(vm, payload);
      bs_bar_view_model_rebuild_workspace_strip(vm);
      bs_bar_view_model_rebuild_center_state(vm);
      bs_bar_view_model_rebuild_tray_items(vm);
      dirty_flags |= BS_BAR_VM_DIRTY_LEFT | BS_BAR_VM_DIRTY_CENTER | BS_BAR_VM_DIRTY_RIGHT;
    } else if (g_strcmp0(topic, "windows") == 0) {
      bs_bar_view_model_parse_windows(vm, payload);
      bs_bar_view_model_rebuild_center_state(vm);
      dirty_flags |= BS_BAR_VM_DIRTY_CENTER;
    } else if (g_strcmp0(topic, "workspaces") == 0) {
      bs_bar_view_model_parse_workspaces(vm, payload);
      bs_bar_view_model_rebuild_workspace_strip(vm);
      dirty_flags |= BS_BAR_VM_DIRTY_LEFT;
    } else if (g_strcmp0(topic, "tray") == 0) {
      bs_bar_view_model_parse_tray(vm, payload);
      bs_bar_view_model_rebuild_tray_items(vm);
      dirty_flags |= BS_BAR_VM_DIRTY_RIGHT;
    } else if (g_strcmp0(topic, "settings") == 0) {
      if (bs_bar_view_model_parse_settings(vm, payload)) {
        dirty_flags |= BS_BAR_VM_DIRTY_LAYOUT | BS_BAR_VM_DIRTY_LEFT
                       | BS_BAR_VM_DIRTY_CENTER | BS_BAR_VM_DIRTY_RIGHT;
      }
    }

    bs_bar_view_model_emit_changed(vm, dirty_flags);
    return true;
  }

  return true;
}

void
bs_bar_view_model_set_changed_cb(BsBarViewModel *vm,
                                 BsBarVmChangedFn cb,
                                 gpointer user_data) {
  g_return_if_fail(vm != NULL);

  vm->changed_cb = cb;
  vm->changed_user_data = user_data;
}

BsBarVmPhase
bs_bar_view_model_phase(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, BS_BAR_VM_PHASE_DISCONNECTED);
  return vm->phase;
}

const BsBarConfig *
bs_bar_view_model_bar_config(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return &vm->bar_config;
}

const char *
bs_bar_view_model_focused_title(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->focused_title;
}

const char *
bs_bar_view_model_focused_app_name(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->focused_app_name;
}

const char *
bs_bar_view_model_focused_output_name(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->shell.focused_output_name;
}

const char *
bs_bar_view_model_focused_workspace_id(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->shell.focused_workspace_id;
}

const char *
bs_bar_view_model_focused_window_id(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->shell.focused_window_id;
}

bool
bs_bar_view_model_show_tray(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, false);
  return vm->bar_config.show_tray;
}

BsBarVmWorkspaceStripState
bs_bar_view_model_workspace_strip_state(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, BS_BAR_VM_WORKSPACE_STRIP_LOADING);
  return vm->workspace_strip_state;
}

BsBarVmCenterState
bs_bar_view_model_center_state(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, BS_BAR_VM_CENTER_CONNECTING);
  return vm->center_state;
}

BsBarVmTrayState
bs_bar_view_model_tray_state(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, BS_BAR_VM_TRAY_CONNECTING);
  return vm->tray_state;
}

bool
bs_bar_view_model_can_open_window_list(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, false);
  return vm->shell.windows_ready && vm->window_candidates != NULL && vm->window_candidates->len > 0;
}

GPtrArray *
bs_bar_view_model_workspace_items(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->workspace_strip_items;
}

GPtrArray *
bs_bar_view_model_window_candidates(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->window_candidates;
}

GPtrArray *
bs_bar_view_model_tray_items(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, NULL);
  return vm->tray_items;
}

char *
bs_bar_view_model_build_snapshot_request(void) {
  return g_strdup("{\"op\":\"snapshot\"}");
}

char *
bs_bar_view_model_build_subscribe_request(void) {
  return g_strdup("{\"op\":\"subscribe\",\"topics\":[\"shell\",\"windows\",\"workspaces\",\"tray\",\"settings\"]}");
}

bool
bs_bar_view_model_needs_resnapshot(BsBarViewModel *vm) {
  g_return_val_if_fail(vm != NULL, false);
  return vm->needs_resnapshot;
}
