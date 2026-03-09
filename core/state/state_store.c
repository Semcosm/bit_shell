#include "state/state_store.h"

#include <glib.h>

#include "model/types.h"

struct _BsStateStore {
  BsSnapshot snapshot;
  BsStateStoreObserver observer;
  gpointer observer_user_data;
};

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

BsStateStore *
bs_state_store_new(void) {
  BsStateStore *store = g_new0(BsStateStore, 1);
  bs_snapshot_init(&store->snapshot);
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
bs_state_store_mark_topic_changed(BsStateStore *store, BsTopic topic) {
  g_return_if_fail(store != NULL);
  g_return_if_fail(topic >= 0 && topic < BS_TOPIC_COUNT);

  store->snapshot.generation += 1;
  store->snapshot.topic_generations[topic] += 1;

  if (store->observer != NULL) {
    store->observer(store, topic, store->observer_user_data);
  }
}
