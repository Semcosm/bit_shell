#include "model/ipc.h"

#include <glib.h>
#include <string.h>

typedef struct {
  const char *name;
  int value;
} BsNameMap;

static const BsNameMap bs_topic_names[] = {
  {"shell", BS_TOPIC_SHELL},
  {"windows", BS_TOPIC_WINDOWS},
  {"workspaces", BS_TOPIC_WORKSPACES},
  {"dock", BS_TOPIC_DOCK},
  {"tray", BS_TOPIC_TRAY},
  {"tray_menu", BS_TOPIC_TRAY_MENU},
  {"settings", BS_TOPIC_SETTINGS},
};

static const BsNameMap bs_command_names[] = {
  {"subscribe", BS_COMMAND_SUBSCRIBE},
  {"snapshot", BS_COMMAND_SNAPSHOT},
  {"launch_app", BS_COMMAND_LAUNCH_APP},
  {"activate_app", BS_COMMAND_ACTIVATE_APP},
  {"focus_next_app_window", BS_COMMAND_FOCUS_NEXT_APP_WINDOW},
  {"focus_prev_app_window", BS_COMMAND_FOCUS_PREV_APP_WINDOW},
  {"focus_window", BS_COMMAND_FOCUS_WINDOW},
  {"switch_workspace", BS_COMMAND_SWITCH_WORKSPACE},
  {"toggle_launchpad", BS_COMMAND_TOGGLE_LAUNCHPAD},
  {"reload_settings", BS_COMMAND_RELOAD_SETTINGS},
  {"pin_app", BS_COMMAND_PIN_APP},
  {"unpin_app", BS_COMMAND_UNPIN_APP},
  {"tray_activate", BS_COMMAND_TRAY_ACTIVATE},
  {"tray_context_menu", BS_COMMAND_TRAY_CONTEXT_MENU},
  {"tray_menu_activate", BS_COMMAND_TRAY_MENU_ACTIVATE},
};

static const char *
bs_name_map_lookup_string(const BsNameMap *map, size_t len, int value) {
  for (size_t i = 0; i < len; i++) {
    if (map[i].value == value) {
      return map[i].name;
    }
  }
  return NULL;
}

static bool
bs_name_map_lookup_value(const BsNameMap *map, size_t len, const char *name, int *value_out) {
  if (name == NULL || *name == '\0') {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    if (g_strcmp0(map[i].name, name) == 0) {
      if (value_out != NULL) {
        *value_out = map[i].value;
      }
      return true;
    }
  }
  return false;
}

const char *
bs_topic_to_string(BsTopic topic) {
  const char *name = bs_name_map_lookup_string(bs_topic_names,
                                               G_N_ELEMENTS(bs_topic_names),
                                               topic);
  return name != NULL ? name : "unknown";
}

bool
bs_topic_from_string(const char *value, BsTopic *topic_out) {
  int parsed = 0;
  if (!bs_name_map_lookup_value(bs_topic_names,
                                G_N_ELEMENTS(bs_topic_names),
                                value,
                                &parsed)) {
    return false;
  }

  if (topic_out != NULL) {
    *topic_out = (BsTopic) parsed;
  }
  return true;
}

const char *
bs_command_to_string(BsCommand command) {
  const char *name = bs_name_map_lookup_string(bs_command_names,
                                               G_N_ELEMENTS(bs_command_names),
                                               command);
  return name != NULL ? name : "invalid";
}

bool
bs_command_from_string(const char *value, BsCommand *command_out) {
  int parsed = 0;
  if (!bs_name_map_lookup_value(bs_command_names,
                                G_N_ELEMENTS(bs_command_names),
                                value,
                                &parsed)) {
    return false;
  }

  if (command_out != NULL) {
    *command_out = (BsCommand) parsed;
  }
  return true;
}

void
bs_topic_set_clear(BsTopicSet *set) {
  if (set == NULL) {
    return;
  }

  memset(set->values, 0, sizeof(set->values));
}

void
bs_topic_set_add(BsTopicSet *set, BsTopic topic) {
  if (set == NULL) {
    return;
  }

  if (topic < 0 || topic >= BS_TOPIC_COUNT) {
    return;
  }

  set->values[topic] = true;
}

bool
bs_topic_set_contains(const BsTopicSet *set, BsTopic topic) {
  if (set == NULL) {
    return false;
  }

  if (topic < 0 || topic >= BS_TOPIC_COUNT) {
    return false;
  }

  return set->values[topic];
}

size_t
bs_topic_set_count(const BsTopicSet *set) {
  size_t count = 0;

  if (set == NULL) {
    return 0;
  }

  for (size_t i = 0; i < BS_TOPIC_COUNT; i++) {
    if (set->values[i]) {
      count += 1;
    }
  }

  return count;
}

char *
bs_topic_set_to_json(const BsTopicSet *set) {
  GString *json = g_string_new("[");
  bool first = true;

  if (set != NULL) {
    for (int topic = 0; topic < BS_TOPIC_COUNT; topic++) {
      if (!set->values[topic]) {
        continue;
      }

      if (!first) {
        g_string_append(json, ",");
      }
      g_string_append_printf(json, "\"%s\"", bs_topic_to_string((BsTopic) topic));
      first = false;
    }
  }

  g_string_append(json, "]");
  return g_string_free(json, false);
}

void
bs_command_request_init(BsCommandRequest *request) {
  g_return_if_fail(request != NULL);
  memset(request, 0, sizeof(*request));
  request->command = BS_COMMAND_INVALID;
  request->menu_item_id = 0;
  request->x = BS_IPC_COORD_UNSET;
  request->y = BS_IPC_COORD_UNSET;
  bs_topic_set_clear(&request->topics);
}

void
bs_command_request_clear(BsCommandRequest *request) {
  if (request == NULL) {
    return;
  }

  g_clear_pointer(&request->desktop_id, g_free);
  g_clear_pointer(&request->app_key, g_free);
  g_clear_pointer(&request->window_id, g_free);
  g_clear_pointer(&request->workspace_id, g_free);
  g_clear_pointer(&request->item_id, g_free);
  request->command = BS_COMMAND_INVALID;
  request->menu_item_id = 0;
  request->x = BS_IPC_COORD_UNSET;
  request->y = BS_IPC_COORD_UNSET;
  bs_topic_set_clear(&request->topics);
}
