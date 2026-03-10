#include "state/state_store.h"

#include <glib.h>
#include <string.h>

struct _BsStateStore {
  BsSnapshot snapshot;
  BsTopicSet pending_topics;
  guint update_depth;
  BsStateStoreObserver observer;
  gpointer observer_user_data;
};

static BsWindow *bs_window_dup(const BsWindow *window);
static BsWorkspace *bs_workspace_dup(const BsWorkspace *workspace);
static BsOutput *bs_output_dup(const BsOutput *output);
static void bs_state_store_rebuild_workspace_emptiness(BsStateStore *store);
static void bs_state_store_refresh_shell_state(BsStateStore *store);
static void bs_state_store_commit_pending(BsStateStore *store);

void
bs_window_clear(BsWindow *window) {
  if (window == NULL) {
    return;
  }
  g_free(window->id);
  g_free(window->title);
  g_free(window->app_id);
  g_free(window->desktop_id);
  g_free(window->workspace_id);
  g_free(window->output_name);
}

void
bs_workspace_clear(BsWorkspace *workspace) {
  if (workspace == NULL) {
    return;
  }
  g_free(workspace->id);
  g_free(workspace->name);
  g_free(workspace->output_name);
}

void
bs_output_clear(BsOutput *output) {
  if (output == NULL) {
    return;
  }
  g_free(output->name);
}

void
bs_shell_state_clear(BsShellState *shell_state) {
  if (shell_state == NULL) {
    return;
  }

  g_free(shell_state->degraded_reason);
  g_free(shell_state->focused_output_name);
  g_free(shell_state->focused_workspace_id);
  g_free(shell_state->focused_window_id);
  g_free(shell_state->focused_window_title);
}

void
bs_app_state_clear(BsAppState *app_state) {
  if (app_state == NULL) {
    return;
  }
  g_free(app_state->desktop_id);
  g_free(app_state->app_id);
  g_free(app_state->name);
  g_free(app_state->icon_name);
}

void
bs_dock_item_clear(BsDockItem *dock_item) {
  if (dock_item == NULL) {
    return;
  }
  g_free(dock_item->app_key);
  if (dock_item->window_ids != NULL) {
    g_ptr_array_unref(dock_item->window_ids);
  }
}

static BsWindow *
bs_window_dup(const BsWindow *window) {
  BsWindow *copy = NULL;

  if (window == NULL) {
    return NULL;
  }

  copy = g_new0(BsWindow, 1);
  copy->id = g_strdup(window->id);
  copy->title = g_strdup(window->title);
  copy->app_id = g_strdup(window->app_id);
  copy->desktop_id = g_strdup(window->desktop_id);
  copy->workspace_id = g_strdup(window->workspace_id);
  copy->output_name = g_strdup(window->output_name);
  copy->focused = window->focused;
  copy->floating = window->floating;
  copy->fullscreen = window->fullscreen;
  copy->focus_ts = window->focus_ts;
  return copy;
}

static BsWorkspace *
bs_workspace_dup(const BsWorkspace *workspace) {
  BsWorkspace *copy = NULL;

  if (workspace == NULL) {
    return NULL;
  }

  copy = g_new0(BsWorkspace, 1);
  copy->id = g_strdup(workspace->id);
  copy->name = g_strdup(workspace->name);
  copy->output_name = g_strdup(workspace->output_name);
  copy->focused = workspace->focused;
  copy->empty = workspace->empty;
  copy->local_index = workspace->local_index;
  return copy;
}

static BsOutput *
bs_output_dup(const BsOutput *output) {
  BsOutput *copy = NULL;

  if (output == NULL) {
    return NULL;
  }

  copy = g_new0(BsOutput, 1);
  copy->name = g_strdup(output->name);
  copy->width = output->width;
  copy->height = output->height;
  copy->scale = output->scale;
  copy->focused = output->focused;
  return copy;
}

static void
bs_state_store_rebuild_workspace_emptiness(BsStateStore *store) {
  GHashTableIter workspace_iter;
  GHashTableIter window_iter;
  gpointer workspace_value = NULL;
  gpointer window_value = NULL;

  g_return_if_fail(store != NULL);

  g_hash_table_iter_init(&workspace_iter, store->snapshot.workspaces);
  while (g_hash_table_iter_next(&workspace_iter, NULL, &workspace_value)) {
    BsWorkspace *workspace = workspace_value;
    workspace->empty = true;
  }

  g_hash_table_iter_init(&window_iter, store->snapshot.windows);
  while (g_hash_table_iter_next(&window_iter, NULL, &window_value)) {
    BsWindow *window = window_value;
    BsWorkspace *workspace = NULL;

    if (window->workspace_id == NULL) {
      continue;
    }

    workspace = g_hash_table_lookup(store->snapshot.workspaces, window->workspace_id);
    if (workspace != NULL) {
      workspace->empty = false;
    }
  }
}

static void
bs_state_store_refresh_shell_state(BsStateStore *store) {
  BsShellState *shell = NULL;
  GHashTableIter iter;
  gpointer value = NULL;
  const char *focused_workspace_id = NULL;
  const char *focused_output_name = NULL;
  const char *focused_window_id = NULL;
  const char *focused_window_title = NULL;

  g_return_if_fail(store != NULL);

  shell = &store->snapshot.shell;

  g_hash_table_iter_init(&iter, store->snapshot.workspaces);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    BsWorkspace *workspace = value;

    if (workspace->focused) {
      focused_workspace_id = workspace->id;
      focused_output_name = workspace->output_name;
      break;
    }
  }

  g_hash_table_iter_init(&iter, store->snapshot.windows);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    BsWindow *window = value;

    if (window->focused) {
      focused_window_id = window->id;
      focused_window_title = window->title;
      if (focused_output_name == NULL) {
        focused_output_name = window->output_name;
      }
      if (focused_workspace_id == NULL) {
        focused_workspace_id = window->workspace_id;
      }
      break;
    }
  }

  if (g_strcmp0(shell->focused_workspace_id, focused_workspace_id) != 0) {
    g_free(shell->focused_workspace_id);
    shell->focused_workspace_id = g_strdup(focused_workspace_id);
  }
  if (g_strcmp0(shell->focused_output_name, focused_output_name) != 0) {
    g_free(shell->focused_output_name);
    shell->focused_output_name = g_strdup(focused_output_name);
  }
  if (g_strcmp0(shell->focused_window_id, focused_window_id) != 0) {
    g_free(shell->focused_window_id);
    shell->focused_window_id = g_strdup(focused_window_id);
  }
  if (g_strcmp0(shell->focused_window_title, focused_window_title) != 0) {
    g_free(shell->focused_window_title);
    shell->focused_window_title = g_strdup(focused_window_title);
  }

  g_hash_table_iter_init(&iter, store->snapshot.outputs);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    BsOutput *output = value;
    output->focused = focused_output_name != NULL && g_strcmp0(output->name, focused_output_name) == 0;
  }
}

BsStateStore *
bs_state_store_new(void) {
  BsStateStore *store = g_new0(BsStateStore, 1);
  bs_snapshot_init(&store->snapshot);
  bs_topic_set_clear(&store->pending_topics);
  return store;
}

void
bs_state_store_free(BsStateStore *store) {
  if (store == NULL) {
    return;
  }
  bs_snapshot_clear(&store->snapshot);
  g_free(store);
}

void
bs_state_store_set_observer(BsStateStore *store,
                            BsStateStoreObserver observer,
                            gpointer user_data) {
  g_return_if_fail(store != NULL);
  store->observer = observer;
  store->observer_user_data = user_data;
}

BsSnapshot *
bs_state_store_snapshot(BsStateStore *store) {
  g_return_val_if_fail(store != NULL, NULL);
  return &store->snapshot;
}

uint64_t
bs_state_store_generation(const BsStateStore *store) {
  g_return_val_if_fail(store != NULL, 0);
  return store->snapshot.generation;
}

uint64_t
bs_state_store_topic_generation(const BsStateStore *store, BsTopic topic) {
  g_return_val_if_fail(store != NULL, 0);
  return bs_snapshot_topic_generation(&store->snapshot, topic);
}

void
bs_state_store_begin_update(BsStateStore *store) {
  g_return_if_fail(store != NULL);
  store->update_depth += 1;
}

static void
bs_state_store_commit_pending(BsStateStore *store) {
  bool has_any = false;

  g_return_if_fail(store != NULL);

  for (int topic = 0; topic < BS_TOPIC_COUNT; topic++) {
    if (bs_topic_set_contains(&store->pending_topics, (BsTopic) topic)) {
      has_any = true;
      store->snapshot.topic_generations[topic] += 1;
    }
  }

  if (!has_any) {
    return;
  }

  store->snapshot.generation += 1;
  for (int topic = 0; topic < BS_TOPIC_COUNT; topic++) {
    if (!bs_topic_set_contains(&store->pending_topics, (BsTopic) topic)) {
      continue;
    }
    if (store->observer != NULL) {
      store->observer(store, (BsTopic) topic, store->observer_user_data);
    }
  }

  bs_topic_set_clear(&store->pending_topics);
}

void
bs_state_store_finish_update(BsStateStore *store) {
  g_return_if_fail(store != NULL);
  g_return_if_fail(store->update_depth > 0);

  store->update_depth -= 1;
  if (store->update_depth == 0) {
    bs_state_store_commit_pending(store);
  }
}

void
bs_state_store_mark_topic_changed(BsStateStore *store, BsTopic topic) {
  g_return_if_fail(store != NULL);
  g_return_if_fail(topic >= 0 && topic < BS_TOPIC_COUNT);

  bs_topic_set_add(&store->pending_topics, topic);
  if (store->update_depth == 0) {
    bs_state_store_commit_pending(store);
  }
}

void
bs_state_store_set_shell_connection_state(BsStateStore *store,
                                          bool niri_connected,
                                          const char *degraded_reason) {
  BsShellState *shell = NULL;

  g_return_if_fail(store != NULL);

  bool changed = false;

  shell = &store->snapshot.shell;
  if (shell->niri_connected != niri_connected) {
    shell->niri_connected = niri_connected;
    changed = true;
  }
  if (g_strcmp0(shell->degraded_reason, degraded_reason) != 0) {
    g_free(shell->degraded_reason);
    shell->degraded_reason = g_strdup(degraded_reason);
    changed = true;
  }

  if (changed) {
    bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
  }
}

void
bs_state_store_replace_outputs(BsStateStore *store, GPtrArray *outputs) {
  g_return_if_fail(store != NULL);

  g_hash_table_remove_all(store->snapshot.outputs);
  if (outputs != NULL) {
    for (guint i = 0; i < outputs->len; i++) {
      BsOutput *output = g_ptr_array_index(outputs, i);
      if (output == NULL || output->name == NULL) {
        continue;
      }
      g_hash_table_replace(store->snapshot.outputs,
                           g_strdup(output->name),
                           bs_output_dup(output));
    }
  }

  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_replace_workspaces(BsStateStore *store, GPtrArray *workspaces) {
  g_return_if_fail(store != NULL);

  g_hash_table_remove_all(store->snapshot.workspaces);
  if (workspaces != NULL) {
    for (guint i = 0; i < workspaces->len; i++) {
      BsWorkspace *workspace = g_ptr_array_index(workspaces, i);
      if (workspace == NULL || workspace->id == NULL) {
        continue;
      }
      g_hash_table_replace(store->snapshot.workspaces,
                           g_strdup(workspace->id),
                           bs_workspace_dup(workspace));
    }
  }

  bs_state_store_rebuild_workspace_emptiness(store);
  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_replace_windows(BsStateStore *store, GPtrArray *windows) {
  g_return_if_fail(store != NULL);

  g_hash_table_remove_all(store->snapshot.windows);
  if (windows != NULL) {
    for (guint i = 0; i < windows->len; i++) {
      BsWindow *window = g_ptr_array_index(windows, i);
      if (window == NULL || window->id == NULL) {
        continue;
      }
      g_hash_table_replace(store->snapshot.windows,
                           g_strdup(window->id),
                           bs_window_dup(window));
    }
  }

  bs_state_store_rebuild_workspace_emptiness(store);
  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WINDOWS);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_upsert_window(BsStateStore *store, const BsWindow *window) {
  g_return_if_fail(store != NULL);
  g_return_if_fail(window != NULL);
  g_return_if_fail(window->id != NULL);

  if (window->focused) {
    GHashTableIter iter;
    gpointer value = NULL;

    g_hash_table_iter_init(&iter, store->snapshot.windows);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
      BsWindow *existing = value;
      existing->focused = false;
    }
  }

  g_hash_table_replace(store->snapshot.windows,
                       g_strdup(window->id),
                       bs_window_dup(window));
  bs_state_store_rebuild_workspace_emptiness(store);
  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WINDOWS);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_remove_window(BsStateStore *store, const char *window_id) {
  g_return_if_fail(store != NULL);
  g_return_if_fail(window_id != NULL);

  if (!g_hash_table_remove(store->snapshot.windows, window_id)) {
    return;
  }

  bs_state_store_rebuild_workspace_emptiness(store);
  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WINDOWS);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_set_workspace_activated(BsStateStore *store,
                                       const char *workspace_id,
                                       bool focused) {
  GHashTableIter iter;
  gpointer value = NULL;

  g_return_if_fail(store != NULL);
  g_return_if_fail(workspace_id != NULL);

  g_hash_table_iter_init(&iter, store->snapshot.workspaces);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    BsWorkspace *workspace = value;
    if (g_strcmp0(workspace->id, workspace_id) == 0) {
      workspace->focused = focused;
    } else if (focused) {
      workspace->focused = false;
    }
  }

  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_set_workspace_active_window(BsStateStore *store,
                                           const char *workspace_id,
                                           const char *window_id) {
  (void) workspace_id;
  (void) window_id;
  g_return_if_fail(store != NULL);
  bs_state_store_rebuild_workspace_emptiness(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WORKSPACES);
}

void
bs_state_store_set_window_focus(BsStateStore *store, const char *window_id) {
  GHashTableIter iter;
  gpointer value = NULL;

  g_return_if_fail(store != NULL);

  g_hash_table_iter_init(&iter, store->snapshot.windows);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    BsWindow *window = value;
    window->focused = window_id != NULL && g_strcmp0(window->id, window_id) == 0;
  }

  bs_state_store_refresh_shell_state(store);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WINDOWS);
  bs_state_store_mark_topic_changed(store, BS_TOPIC_SHELL);
}

void
bs_state_store_set_window_focus_timestamp(BsStateStore *store,
                                          const char *window_id,
                                          bool has_value,
                                          uint64_t focus_ts) {
  BsWindow *window = NULL;

  g_return_if_fail(store != NULL);
  g_return_if_fail(window_id != NULL);

  window = g_hash_table_lookup(store->snapshot.windows, window_id);
  if (window == NULL) {
    return;
  }

  window->focus_ts = has_value ? focus_ts : 0;
  bs_state_store_mark_topic_changed(store, BS_TOPIC_WINDOWS);
}
