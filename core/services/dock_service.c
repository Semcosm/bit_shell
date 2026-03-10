#include "services/dock_service.h"

#include <glib.h>

struct _BsDockService {
  BsStateStore *store;
  BsAppRegistry *app_registry;
};

static void bs_dock_service_free_dock_item_ptr(gpointer data);
static BsDockItem *bs_dock_service_ensure_item(GHashTable *items_by_key,
                                               const char *app_key,
                                               const BsAppState *app_state,
                                               const BsWindow *window);
static void bs_dock_service_update_item_from_window(BsDockItem *dock_item, const BsWindow *window);
static const BsAppState *bs_dock_service_lookup_app(BsSnapshot *snapshot, const char *app_key);

static void
bs_dock_service_free_dock_item_ptr(gpointer data) {
  BsDockItem *dock_item = data;

  if (dock_item == NULL) {
    return;
  }

  bs_dock_item_clear(dock_item);
  g_free(dock_item);
}

BsDockService *
bs_dock_service_new(BsStateStore *store, BsAppRegistry *app_registry) {
  BsDockService *service = g_new0(BsDockService, 1);
  service->store = store;
  service->app_registry = app_registry;
  return service;
}

void
bs_dock_service_free(BsDockService *service) {
  g_free(service);
}

bool
bs_dock_service_rebuild(BsDockService *service, GError **error) {
  g_autoptr(GHashTable) items_by_key = NULL;
  g_autoptr(GPtrArray) dock_items = NULL;
  BsSnapshot *snapshot = NULL;
  GHashTableIter iter;
  gpointer value = NULL;

  g_return_val_if_fail(service != NULL, false);
  (void) error;

  snapshot = bs_state_store_snapshot(service->store);
  if (snapshot == NULL) {
    return false;
  }

  items_by_key = g_hash_table_new_full(g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       (GDestroyNotify) bs_dock_service_free_dock_item_ptr);

  if (snapshot->pinned_app_ids != NULL) {
    for (guint i = 0; i < snapshot->pinned_app_ids->len; i++) {
      const char *desktop_id = g_ptr_array_index(snapshot->pinned_app_ids, i);
      const BsAppState *app_state = bs_dock_service_lookup_app(snapshot, desktop_id);
      BsDockItem *dock_item = NULL;

      if (desktop_id == NULL || *desktop_id == '\0') {
        continue;
      }

      dock_item = bs_dock_service_ensure_item(items_by_key, desktop_id, app_state, NULL);
      dock_item->pinned = true;
      dock_item->pinned_index = (int) i;
    }
  }

  g_hash_table_iter_init(&iter, snapshot->windows);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    const BsWindow *window = value;
    g_autofree char *app_key = NULL;
    const BsAppState *app_state = NULL;
    BsDockItem *dock_item = NULL;

    app_key = bs_app_registry_resolve_window_app_key(service->app_registry, window);
    if (app_key == NULL || *app_key == '\0') {
      continue;
    }

    app_state = bs_dock_service_lookup_app(snapshot, app_key);
    dock_item = bs_dock_service_ensure_item(items_by_key, app_key, app_state, window);
    bs_dock_service_update_item_from_window(dock_item, window);
  }

  dock_items = g_ptr_array_new_with_free_func((GDestroyNotify) bs_dock_service_free_dock_item_ptr);
  g_hash_table_iter_init(&iter, items_by_key);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    g_ptr_array_add(dock_items, value);
    g_hash_table_iter_steal(&iter);
  }

  bs_state_store_replace_dock_items(service->store, dock_items);
  return true;
}

static BsDockItem *
bs_dock_service_ensure_item(GHashTable *items_by_key,
                            const char *app_key,
                            const BsAppState *app_state,
                            const BsWindow *window) {
  BsDockItem *dock_item = NULL;

  g_return_val_if_fail(items_by_key != NULL, NULL);
  g_return_val_if_fail(app_key != NULL, NULL);

  dock_item = g_hash_table_lookup(items_by_key, app_key);
  if (dock_item != NULL) {
    return dock_item;
  }

  dock_item = g_new0(BsDockItem, 1);
  dock_item->app_key = g_strdup(app_key);
  dock_item->desktop_id = app_state != NULL ? g_strdup(app_state->desktop_id) : NULL;
  dock_item->name = app_state != NULL ? g_strdup(app_state->name) : NULL;
  dock_item->icon_name = app_state != NULL ? g_strdup(app_state->icon_name) : NULL;
  dock_item->window_ids = g_ptr_array_new_with_free_func(g_free);
  dock_item->pinned_index = -1;

  if (dock_item->name == NULL && window != NULL) {
    dock_item->name = g_strdup(window->title != NULL ? window->title : window->app_id);
  }

  g_hash_table_insert(items_by_key, g_strdup(app_key), dock_item);
  return dock_item;
}

static void
bs_dock_service_update_item_from_window(BsDockItem *dock_item, const BsWindow *window) {
  g_return_if_fail(dock_item != NULL);
  g_return_if_fail(window != NULL);

  if (window->id != NULL) {
    g_ptr_array_add(dock_item->window_ids, g_strdup(window->id));
  }
  dock_item->running = true;
  dock_item->focused = dock_item->focused || window->focused;
  if (window->focus_ts > dock_item->last_focus_ts) {
    dock_item->last_focus_ts = window->focus_ts;
  }

  if (dock_item->name == NULL || *dock_item->name == '\0') {
    g_free(dock_item->name);
    dock_item->name = g_strdup(window->title != NULL ? window->title : window->app_id);
  }
}

static const BsAppState *
bs_dock_service_lookup_app(BsSnapshot *snapshot, const char *app_key) {
  g_return_val_if_fail(snapshot != NULL, NULL);
  g_return_val_if_fail(app_key != NULL, NULL);

  return g_hash_table_lookup(snapshot->apps, app_key);
}
