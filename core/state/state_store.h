#ifndef BIT_SHELL_CORE_STATE_STATE_STORE_H
#define BIT_SHELL_CORE_STATE_STATE_STORE_H

#include <glib.h>
#include <stdbool.h>

#include "model/ipc.h"
#include "model/snapshot.h"
#include "model/types.h"

typedef struct _BsStateStore BsStateStore;
typedef void (*BsStateStoreObserver)(BsStateStore *store, BsTopic topic, gpointer user_data);
typedef void (*BsStateStoreDerivedUpdater)(BsStateStore *store, gpointer user_data);

BsStateStore *bs_state_store_new(void);
void bs_state_store_free(BsStateStore *store);

void bs_state_store_set_observer(BsStateStore *store,
                                 BsStateStoreObserver observer,
                                 gpointer user_data);
void bs_state_store_set_derived_updater(BsStateStore *store,
                                        BsStateStoreDerivedUpdater updater,
                                        gpointer user_data);

BsSnapshot *bs_state_store_snapshot(BsStateStore *store);
uint64_t bs_state_store_generation(const BsStateStore *store);
uint64_t bs_state_store_topic_generation(const BsStateStore *store, BsTopic topic);
const BsWindow *bs_state_store_lookup_window(BsStateStore *store, const char *window_id);
const BsDockItem *bs_state_store_lookup_dock_item(BsStateStore *store, const char *app_key);
GPtrArray *bs_state_store_list_app_windows(BsStateStore *store, const char *app_key);

void bs_state_store_begin_update(BsStateStore *store);
void bs_state_store_finish_update(BsStateStore *store);

void bs_state_store_mark_topic_changed(BsStateStore *store, BsTopic topic);

void bs_state_store_set_shell_connection_state(BsStateStore *store,
                                               bool niri_connected,
                                               const char *degraded_reason);
void bs_state_store_replace_outputs(BsStateStore *store, GPtrArray *outputs);
void bs_state_store_replace_workspaces(BsStateStore *store, GPtrArray *workspaces);
void bs_state_store_replace_windows(BsStateStore *store, GPtrArray *windows);
void bs_state_store_upsert_window(BsStateStore *store, const BsWindow *window);
void bs_state_store_remove_window(BsStateStore *store, const char *window_id);
void bs_state_store_set_workspace_activated(BsStateStore *store,
                                            const char *workspace_id,
                                            bool focused);
void bs_state_store_set_workspace_active_window(BsStateStore *store,
                                                const char *workspace_id,
                                                const char *window_id);
void bs_state_store_set_window_focus(BsStateStore *store, const char *window_id);
void bs_state_store_set_window_focus_timestamp(BsStateStore *store,
                                               const char *window_id,
                                               bool has_value,
                                               uint64_t focus_ts);
void bs_state_store_replace_apps(BsStateStore *store, GPtrArray *apps);
void bs_state_store_replace_dock_items(BsStateStore *store, GPtrArray *dock_items);
void bs_state_store_replace_pinned_app_ids(BsStateStore *store, GPtrArray *pinned_app_ids);

#endif
