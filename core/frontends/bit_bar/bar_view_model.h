#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_BAR_VIEW_MODEL_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_BAR_VIEW_MODEL_H

#include <glib.h>
#include <stdbool.h>

#include "model/config.h"

typedef struct _BsBarViewModel BsBarViewModel;

typedef enum {
  BS_BAR_VM_PHASE_DISCONNECTED = 0,
  BS_BAR_VM_PHASE_WAITING_SNAPSHOT,
  BS_BAR_VM_PHASE_WAITING_SUBSCRIBE_ACK,
  BS_BAR_VM_PHASE_LIVE,
} BsBarVmPhase;

typedef enum {
  BS_BAR_VM_DIRTY_NONE = 0,
  BS_BAR_VM_DIRTY_LEFT = 1 << 0,
  BS_BAR_VM_DIRTY_CENTER = 1 << 1,
  BS_BAR_VM_DIRTY_RIGHT = 1 << 2,
  BS_BAR_VM_DIRTY_LAYOUT = 1 << 3,
  BS_BAR_VM_DIRTY_ALL = 0x0f,
} BsBarVmDirtyFlags;

typedef enum {
  BS_BAR_VM_WORKSPACE_STRIP_LOADING = 0,
  BS_BAR_VM_WORKSPACE_STRIP_READY_EMPTY,
  BS_BAR_VM_WORKSPACE_STRIP_READY_ITEMS,
} BsBarVmWorkspaceStripState;

typedef enum {
  BS_BAR_WORKSPACE_PRESENTATION_FULL = 0,
  BS_BAR_WORKSPACE_PRESENTATION_COMPACT,
  BS_BAR_WORKSPACE_PRESENTATION_MINIMAL,
} BsBarWorkspacePresentation;

typedef enum {
  BS_BAR_VM_CENTER_CONNECTING = 0,
  BS_BAR_VM_CENTER_SYNCING_WINDOWS,
  BS_BAR_VM_CENTER_READY_NO_FOCUSED_WINDOW,
  BS_BAR_VM_CENTER_READY_FOCUSED_WINDOW,
} BsBarVmCenterState;

typedef enum {
  BS_BAR_VM_TRAY_CONNECTING = 0,
  BS_BAR_VM_TRAY_READY_EMPTY,
  BS_BAR_VM_TRAY_READY_ITEMS,
} BsBarVmTrayState;

typedef enum {
  BS_BAR_TRAY_VISUAL_ACTIVE = 0,
  BS_BAR_TRAY_VISUAL_ATTENTION,
  BS_BAR_TRAY_VISUAL_PASSIVE,
} BsBarTrayVisualState;

typedef enum {
  BS_BAR_TRAY_PRIMARY_ACTIVATE = 0,
  BS_BAR_TRAY_PRIMARY_MENU,
  BS_BAR_TRAY_PRIMARY_NONE,
} BsBarTrayPrimaryAction;

typedef struct {
  char *id;
  char *name;
  char *output_name;
  bool focused;
  bool empty;
  int local_index;
  char *display_label;
  char *tooltip_label;
  BsBarWorkspacePresentation presentation;
  guint sort_rank;
} BsBarWorkspaceStripItem;

typedef struct {
  char *window_id;
  char *title;
  char *desktop_id;
  char *app_id;
  bool focused;
  guint64 focus_ts;
} BsBarWindowCandidate;

typedef struct {
  char *item_id;
  char *title;
  char *icon_name;
  char *attention_icon_name;
  char *status;
  bool item_is_menu;
  bool has_activate;
  bool has_context_menu;
  char *effective_icon_name;
  char *fallback_label;
  BsBarTrayVisualState visual_state;
  BsBarTrayPrimaryAction primary_action;
  bool show_by_default;
} BsBarTrayItemView;

typedef void (*BsBarVmChangedFn)(BsBarViewModel *vm,
                                 guint dirty_flags,
                                 gpointer user_data);

BsBarViewModel *bs_bar_view_model_new(void);
void bs_bar_view_model_free(BsBarViewModel *vm);

void bs_bar_view_model_reset_connection(BsBarViewModel *vm);
bool bs_bar_view_model_consume_json_line(BsBarViewModel *vm,
                                         const char *line,
                                         GError **error);

void bs_bar_view_model_set_changed_cb(BsBarViewModel *vm,
                                      BsBarVmChangedFn cb,
                                      gpointer user_data);

BsBarVmPhase bs_bar_view_model_phase(BsBarViewModel *vm);
const BsBarConfig *bs_bar_view_model_bar_config(BsBarViewModel *vm);
const char *bs_bar_view_model_focused_title(BsBarViewModel *vm);
const char *bs_bar_view_model_focused_app_name(BsBarViewModel *vm);
const char *bs_bar_view_model_focused_output_name(BsBarViewModel *vm);
const char *bs_bar_view_model_focused_workspace_id(BsBarViewModel *vm);
const char *bs_bar_view_model_focused_window_id(BsBarViewModel *vm);
bool bs_bar_view_model_show_tray(BsBarViewModel *vm);
BsBarVmWorkspaceStripState bs_bar_view_model_workspace_strip_state(BsBarViewModel *vm);
BsBarVmCenterState bs_bar_view_model_center_state(BsBarViewModel *vm);
BsBarVmTrayState bs_bar_view_model_tray_state(BsBarViewModel *vm);
bool bs_bar_view_model_can_open_window_list(BsBarViewModel *vm);
GPtrArray *bs_bar_view_model_workspace_items(BsBarViewModel *vm);
GPtrArray *bs_bar_view_model_window_candidates(BsBarViewModel *vm);
GPtrArray *bs_bar_view_model_tray_items(BsBarViewModel *vm);

char *bs_bar_view_model_build_snapshot_request(void);
char *bs_bar_view_model_build_subscribe_request(void);
bool bs_bar_view_model_needs_resnapshot(BsBarViewModel *vm);

#endif
