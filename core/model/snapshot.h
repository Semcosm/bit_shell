#ifndef BIT_SHELL_CORE_MODEL_SNAPSHOT_H
#define BIT_SHELL_CORE_MODEL_SNAPSHOT_H

#include <glib.h>
#include <stdint.h>

#include "model/config.h"
#include "model/ipc.h"
#include "model/types.h"

typedef struct {
  uint64_t generation;
  uint64_t topic_generations[BS_TOPIC_COUNT];
  BsShellState shell;
  GHashTable *windows;
  GHashTable *workspaces;
  GHashTable *outputs;
  GHashTable *apps;
  GHashTable *dock_items;
  GHashTable *tray_items;
  GPtrArray *pinned_app_ids;
  BsBarConfig bar_config;
  BsDockConfig dock_config;
} BsSnapshot;

void bs_snapshot_init(BsSnapshot *snapshot);
void bs_snapshot_clear(BsSnapshot *snapshot);
uint64_t bs_snapshot_topic_generation(const BsSnapshot *snapshot, BsTopic topic);
char *bs_snapshot_serialize_topic_versions_json(const BsSnapshot *snapshot);
char *bs_snapshot_serialize_topic_payload_json(const BsSnapshot *snapshot, BsTopic topic);
char *bs_snapshot_serialize_json(const BsSnapshot *snapshot);

#endif
