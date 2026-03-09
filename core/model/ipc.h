#ifndef BIT_SHELL_CORE_MODEL_IPC_H
#define BIT_SHELL_CORE_MODEL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BS_TOPIC_COUNT 6
#define BS_IPC_COORD_UNSET INT32_MIN

typedef enum {
  BS_TOPIC_SHELL = 0,
  BS_TOPIC_WINDOWS,
  BS_TOPIC_WORKSPACES,
  BS_TOPIC_DOCK,
  BS_TOPIC_TRAY,
  BS_TOPIC_SETTINGS,
} BsTopic;

typedef enum {
  BS_COMMAND_INVALID = 0,
  BS_COMMAND_SUBSCRIBE,
  BS_COMMAND_SNAPSHOT,
  BS_COMMAND_LAUNCH_APP,
  BS_COMMAND_ACTIVATE_APP,
  BS_COMMAND_FOCUS_WINDOW,
  BS_COMMAND_SWITCH_WORKSPACE,
  BS_COMMAND_TOGGLE_LAUNCHPAD,
  BS_COMMAND_PIN_APP,
  BS_COMMAND_UNPIN_APP,
  BS_COMMAND_TRAY_ACTIVATE,
  BS_COMMAND_TRAY_CONTEXT_MENU,
} BsCommand;

typedef struct {
  bool values[BS_TOPIC_COUNT];
} BsTopicSet;

typedef struct {
  BsCommand command;
  BsTopicSet topics;
  char *desktop_id;
  char *app_key;
  char *window_id;
  char *workspace_id;
  char *item_id;
  int32_t x;
  int32_t y;
} BsCommandRequest;

const char *bs_topic_to_string(BsTopic topic);
bool bs_topic_from_string(const char *value, BsTopic *topic_out);

const char *bs_command_to_string(BsCommand command);
bool bs_command_from_string(const char *value, BsCommand *command_out);

void bs_topic_set_clear(BsTopicSet *set);
void bs_topic_set_add(BsTopicSet *set, BsTopic topic);
bool bs_topic_set_contains(const BsTopicSet *set, BsTopic topic);
size_t bs_topic_set_count(const BsTopicSet *set);
char *bs_topic_set_to_json(const BsTopicSet *set);

void bs_command_request_init(BsCommandRequest *request);
void bs_command_request_clear(BsCommandRequest *request);

#endif
