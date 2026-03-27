#ifndef BIT_SHELL_CORE_MODEL_TYPES_H
#define BIT_SHELL_CORE_MODEL_TYPES_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool niri_connected;
  bool outputs_ready;
  bool workspaces_ready;
  bool windows_ready;
  bool bootstrap_used_fallback;
  char *degraded_reason;
  char *focused_output_name;
  char *focused_workspace_id;
  char *focused_window_id;
  char *focused_window_title;
} BsShellState;

typedef struct {
  char *id;
  char *title;
  char *app_id;
  char *desktop_id;
  char *workspace_id;
  char *output_name;
  bool focused;
  bool floating;
  bool fullscreen;
  uint64_t focus_ts;
} BsWindow;

typedef struct {
  char *id;
  char *name;
  char *output_name;
  bool focused;
  bool empty;
  int local_index;
} BsWorkspace;

typedef struct {
  char *name;
  int width;
  int height;
  double scale;
  bool focused;
} BsOutput;

typedef struct {
  char *desktop_id;
  char *app_id;
  char *name;
  char *icon_name;
  bool pinned;
  int recent_score;
  int launch_count;
} BsAppState;

typedef struct {
  char *app_key;
  char *desktop_id;
  char *name;
  char *icon_name;
  GPtrArray *window_ids;
  bool pinned;
  bool running;
  bool focused;
  int pinned_index;
  int running_order;
} BsDockItem;

typedef enum {
  BS_TRAY_ITEM_STATUS_PASSIVE = 0,
  BS_TRAY_ITEM_STATUS_ACTIVE,
  BS_TRAY_ITEM_STATUS_ATTENTION,
} BsTrayItemStatus;

typedef struct {
  char *item_id;
  char *bus_name;
  char *object_path;
  char *menu_object_path;
  char *id;
  char *title;
  char *icon_name;
  char *attention_icon_name;
  BsTrayItemStatus status;
  bool item_is_menu;
  bool has_activate;
  bool has_context_menu;
  guint64 presentation_seq;
} BsTrayItem;

void bs_shell_state_clear(BsShellState *shell_state);
void bs_window_clear(BsWindow *window);
void bs_workspace_clear(BsWorkspace *workspace);
void bs_output_clear(BsOutput *output);
void bs_app_state_clear(BsAppState *app_state);
void bs_dock_item_clear(BsDockItem *dock_item);
void bs_tray_item_clear(BsTrayItem *tray_item);

#endif
