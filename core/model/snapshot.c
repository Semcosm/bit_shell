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

static void
bs_json_append_quoted(GString *json, const char *value) {
  char *escaped = g_strescape(value != NULL ? value : "", NULL);
  g_string_append_printf(json, "\"%s\"", escaped != NULL ? escaped : "");
  g_free(escaped);
}

static void
bs_json_append_object_id_array(GString *json, GHashTable *table, const char *field_name) {
  GHashTableIter iter;
  gpointer key = NULL;
  bool first = true;

  g_string_append(json, "[");
  if (table != NULL) {
    g_hash_table_iter_init(&iter, table);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
      if (!first) {
        g_string_append(json, ",");
      }
      g_string_append(json, "{");
      g_string_append_printf(json, "\"%s\":", field_name);
      bs_json_append_quoted(json, (const char *) key);
      g_string_append(json, "}");
      first = false;
    }
  }
  g_string_append(json, "]");
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
      g_string_append(json, "{\"focused_app_key\":null,\"launchpad_visible\":false}");
      break;
    case BS_TOPIC_WINDOWS:
      g_string_append(json, "{\"windows\":");
      bs_json_append_object_id_array(json, snapshot->windows, "id");
      g_string_append(json, "}");
      break;
    case BS_TOPIC_WORKSPACES:
      g_string_append(json, "{\"workspaces\":");
      bs_json_append_object_id_array(json, snapshot->workspaces, "id");
      g_string_append(json, ",\"outputs\":");
      bs_json_append_object_id_array(json, snapshot->outputs, "name");
      g_string_append(json, "}");
      break;
    case BS_TOPIC_DOCK:
      g_string_append(json, "{\"items\":");
      bs_json_append_object_id_array(json, snapshot->dock_items, "app_key");
      g_string_append(json, "}");
      break;
    case BS_TOPIC_TRAY:
      g_string_append(json, "{\"items\":[]}");
      break;
    case BS_TOPIC_SETTINGS:
      g_string_append(json, "{\"config_loaded\":true}");
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
