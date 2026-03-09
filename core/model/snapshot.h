#ifndef BIT_SHELL_CORE_MODEL_SNAPSHOT_H
#define BIT_SHELL_CORE_MODEL_SNAPSHOT_H

#include <glib.h>
#include <stdint.h>

typedef enum {
  BS_TOPIC_SHELL = 0,
  BS_TOPIC_WINDOWS,
  BS_TOPIC_WORKSPACES,
  BS_TOPIC_DOCK,
  BS_TOPIC_TRAY,
  BS_TOPIC_SETTINGS,
} BsTopic;

typedef struct {
  uint64_t generation;
  GHashTable *windows;
  GHashTable *workspaces;
  GHashTable *outputs;
  GHashTable *apps;
  GHashTable *dock_items;
} BsSnapshot;

const char *bs_topic_to_string(BsTopic topic);
void bs_snapshot_init(BsSnapshot *snapshot);
void bs_snapshot_clear(BsSnapshot *snapshot);

#endif
