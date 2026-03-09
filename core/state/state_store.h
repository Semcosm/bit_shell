#ifndef BIT_SHELL_CORE_STATE_STATE_STORE_H
#define BIT_SHELL_CORE_STATE_STATE_STORE_H

#include <glib.h>
#include <stdbool.h>

#include "model/snapshot.h"

typedef struct _BsStateStore BsStateStore;
typedef void (*BsStateStoreObserver)(BsStateStore *store, BsTopic topic, gpointer user_data);

BsStateStore *bs_state_store_new(void);
void bs_state_store_free(BsStateStore *store);

void bs_state_store_set_observer(BsStateStore *store,
                                 BsStateStoreObserver observer,
                                 gpointer user_data);

BsSnapshot *bs_state_store_snapshot(BsStateStore *store);
uint64_t bs_state_store_generation(const BsStateStore *store);

void bs_state_store_mark_topic_changed(BsStateStore *store, BsTopic topic);

#endif
