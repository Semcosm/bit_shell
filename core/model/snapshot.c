#include "model/snapshot.h"

#include <glib.h>
#include <string.h>

static void
bs_hash_table_destroy_value(gpointer value) {
  g_free(value);
}

static GHashTable *
bs_hash_table_new_string_map(void) {
  return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bs_hash_table_destroy_value);
}

void
bs_snapshot_init(BsSnapshot *snapshot) {
  g_return_if_fail(snapshot != NULL);
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->windows = bs_hash_table_new_string_map();
  snapshot->workspaces = bs_hash_table_new_string_map();
  snapshot->outputs = bs_hash_table_new_string_map();
  snapshot->apps = bs_hash_table_new_string_map();
  snapshot->dock_items = bs_hash_table_new_string_map();
}

void
bs_snapshot_clear(BsSnapshot *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  g_clear_pointer(&snapshot->windows, g_hash_table_unref);
  g_clear_pointer(&snapshot->workspaces, g_hash_table_unref);
  g_clear_pointer(&snapshot->outputs, g_hash_table_unref);
  g_clear_pointer(&snapshot->apps, g_hash_table_unref);
  g_clear_pointer(&snapshot->dock_items, g_hash_table_unref);
  memset(snapshot->topic_generations, 0, sizeof(snapshot->topic_generations));
  snapshot->generation = 0;
}

uint64_t
bs_snapshot_topic_generation(const BsSnapshot *snapshot, BsTopic topic) {
  g_return_val_if_fail(snapshot != NULL, 0);
  g_return_val_if_fail(topic >= 0 && topic < BS_TOPIC_COUNT, 0);
  return snapshot->topic_generations[topic];
}
