#include "model/config.h"

#include <glib.h>
#include <string.h>

static char *
bs_strdup_or_null(const char *value) {
  return value != NULL ? g_strdup(value) : NULL;
}

const char *
bs_dock_display_mode_to_string(BsDockDisplayMode mode) {
  switch (mode) {
    case BS_DOCK_DISPLAY_MODE_IMMERSIVE: return "immersive";
    case BS_DOCK_DISPLAY_MODE_RESERVED: return "reserved";
    case BS_DOCK_DISPLAY_MODE_AUTOHIDE: return "autohide";
    default: return "unknown";
  }
}

bool
bs_dock_display_mode_from_string(const char *value, BsDockDisplayMode *mode_out) {
  if (g_strcmp0(value, "immersive") == 0) {
    if (mode_out != NULL) {
      *mode_out = BS_DOCK_DISPLAY_MODE_IMMERSIVE;
    }
    return true;
  }
  if (g_strcmp0(value, "reserved") == 0) {
    if (mode_out != NULL) {
      *mode_out = BS_DOCK_DISPLAY_MODE_RESERVED;
    }
    return true;
  }
  if (g_strcmp0(value, "autohide") == 0) {
    if (mode_out != NULL) {
      *mode_out = BS_DOCK_DISPLAY_MODE_AUTOHIDE;
    }
    return true;
  }
  return false;
}

void
bs_dock_config_init_defaults(BsDockConfig *config) {
  g_return_if_fail(config != NULL);

  memset(config, 0, sizeof(*config));
  config->icon_size_px = 56;
  config->magnification_enabled = true;
  config->magnification_scale = 1.80;
  config->hover_range_cap_units = 4;
  config->spacing_px = 0;
  config->bottom_margin_px = 14;
  config->show_running_indicator = true;
  config->animate_opening_apps = true;
  config->display_mode = BS_DOCK_DISPLAY_MODE_IMMERSIVE;
  config->center_on_primary_output = true;
}

void
bs_dock_config_normalize(BsDockConfig *config) {
  g_return_if_fail(config != NULL);

  config->icon_size_px = CLAMP(config->icon_size_px, 32, 128);
  config->magnification_scale = CLAMP(config->magnification_scale, 1.0, 3.0);
  config->hover_range_cap_units = CLAMP(config->hover_range_cap_units, 2, 12);
  config->spacing_px = MIN(config->spacing_px, 64);
  config->bottom_margin_px = MIN(config->bottom_margin_px, 128);

  if (!config->magnification_enabled) {
    config->magnification_scale = 1.0;
  }
}

void
bs_runtime_paths_init(BsRuntimePaths *paths) {
  g_return_if_fail(paths != NULL);
  memset(paths, 0, sizeof(*paths));
}

void
bs_runtime_paths_init_user_defaults(BsRuntimePaths *paths) {
  const char *runtime_dir = NULL;

  g_return_if_fail(paths != NULL);

  bs_runtime_paths_init(paths);
  runtime_dir = g_get_user_runtime_dir();

  paths->niri_socket_path = bs_strdup_or_null(g_getenv("NIRI_SOCKET"));
  paths->ipc_socket_path = runtime_dir != NULL
                             ? g_build_filename(runtime_dir, "bit_shell", "bit_shelld.sock", NULL)
                             : g_build_filename(g_get_tmp_dir(), "bit_shell", "bit_shelld.sock", NULL);
  paths->config_path = g_build_filename(g_get_user_config_dir(), "bit_shell", "config.toml", NULL);
  paths->state_path = g_build_filename(g_get_user_state_dir(), "bit_shell", "state.json", NULL);
  paths->applications_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
}

void
bs_runtime_paths_clear(BsRuntimePaths *paths) {
  if (paths == NULL) {
    return;
  }

  g_clear_pointer(&paths->niri_socket_path, g_free);
  g_clear_pointer(&paths->ipc_socket_path, g_free);
  g_clear_pointer(&paths->config_path, g_free);
  g_clear_pointer(&paths->state_path, g_free);
  g_clear_pointer(&paths->applications_dir, g_free);
}

void
bs_runtime_paths_copy(BsRuntimePaths *dst, const BsRuntimePaths *src) {
  g_return_if_fail(dst != NULL);
  g_return_if_fail(src != NULL);

  bs_runtime_paths_init(dst);
  dst->niri_socket_path = bs_strdup_or_null(src->niri_socket_path);
  dst->ipc_socket_path = bs_strdup_or_null(src->ipc_socket_path);
  dst->config_path = bs_strdup_or_null(src->config_path);
  dst->state_path = bs_strdup_or_null(src->state_path);
  dst->applications_dir = bs_strdup_or_null(src->applications_dir);
}

void
bs_shell_config_init_defaults(BsShellConfig *config) {
  g_return_if_fail(config != NULL);

  memset(config, 0, sizeof(*config));
  bs_runtime_paths_init_user_defaults(&config->paths);

  config->tray_watcher_name = g_strdup("org.kde.StatusNotifierWatcher");
  config->auto_reconnect_niri = true;

  config->bar.height_px = 32;
  config->bar.show_workspace_strip = true;
  config->bar.show_focused_title = true;
  config->bar.show_tray = true;
  config->bar.show_clock = true;

  bs_dock_config_init_defaults(&config->dock);

  config->launchpad.resident = true;
  config->launchpad.grid_icon_size_px = 64;
  config->launchpad.max_recent_apps = 12;
  config->launchpad.show_categories = true;
}

void
bs_shell_config_clear(BsShellConfig *config) {
  if (config == NULL) {
    return;
  }

  bs_runtime_paths_clear(&config->paths);
  g_clear_pointer(&config->tray_watcher_name, g_free);
  g_clear_pointer(&config->primary_output, g_free);
  memset(&config->bar, 0, sizeof(config->bar));
  memset(&config->dock, 0, sizeof(config->dock));
  memset(&config->launchpad, 0, sizeof(config->launchpad));
  config->auto_reconnect_niri = false;
}

void
bs_shell_config_copy(BsShellConfig *dst, const BsShellConfig *src) {
  g_return_if_fail(dst != NULL);
  g_return_if_fail(src != NULL);

  bs_shell_config_init_defaults(dst);
  bs_runtime_paths_clear(&dst->paths);
  bs_runtime_paths_copy(&dst->paths, &src->paths);
  g_clear_pointer(&dst->tray_watcher_name, g_free);
  g_clear_pointer(&dst->primary_output, g_free);
  dst->tray_watcher_name = bs_strdup_or_null(src->tray_watcher_name);
  dst->primary_output = bs_strdup_or_null(src->primary_output);
  dst->auto_reconnect_niri = src->auto_reconnect_niri;
  dst->bar = src->bar;
  dst->dock = src->dock;
  dst->launchpad = src->launchpad;
}
