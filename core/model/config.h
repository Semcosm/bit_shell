#ifndef BIT_SHELL_CORE_MODEL_CONFIG_H
#define BIT_SHELL_CORE_MODEL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BS_DOCK_DISPLAY_MODE_IMMERSIVE = 0,
  BS_DOCK_DISPLAY_MODE_RESERVED,
  BS_DOCK_DISPLAY_MODE_AUTOHIDE,
} BsDockDisplayMode;

typedef struct {
  char *niri_socket_path;
  char *ipc_socket_path;
  char *config_path;
  char *state_path;
  char *applications_dir;
} BsRuntimePaths;

typedef struct {
  uint32_t height_px;
  bool show_workspace_strip;
  bool show_focused_title;
  bool show_tray;
  bool show_clock;
} BsBarConfig;

typedef struct {
  uint32_t icon_size_px;
  uint32_t spacing_px;
  BsDockDisplayMode display_mode;
  bool enable_magnification;
  bool center_on_primary_output;
} BsDockConfig;

typedef struct {
  bool resident;
  uint32_t grid_icon_size_px;
  uint32_t max_recent_apps;
  bool show_categories;
} BsLaunchpadConfig;

typedef struct {
  BsRuntimePaths paths;
  char *tray_watcher_name;
  char *primary_output;
  bool auto_reconnect_niri;
  BsBarConfig bar;
  BsDockConfig dock;
  BsLaunchpadConfig launchpad;
} BsShellConfig;

const char *bs_dock_display_mode_to_string(BsDockDisplayMode mode);
bool bs_dock_display_mode_from_string(const char *value, BsDockDisplayMode *mode_out);

void bs_runtime_paths_init(BsRuntimePaths *paths);
void bs_runtime_paths_init_user_defaults(BsRuntimePaths *paths);
void bs_runtime_paths_clear(BsRuntimePaths *paths);
void bs_runtime_paths_copy(BsRuntimePaths *dst, const BsRuntimePaths *src);

void bs_shell_config_init_defaults(BsShellConfig *config);
void bs_shell_config_clear(BsShellConfig *config);
void bs_shell_config_copy(BsShellConfig *dst, const BsShellConfig *src);

#endif
