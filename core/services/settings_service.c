#include "services/settings_service.h"
#include "build_info.h"

#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

struct _BsSettingsService {
  BsStateStore *store;
  char *config_path;
  char *state_path;
  BsShellConfig shell_config;
  bool dirty_state;
};

static char *bs_settings_service_strip_toml_comment(const char *line);
static bool bs_settings_service_parse_bool_value(const char *value, bool *out_value);
static bool bs_settings_service_parse_uint32_value(const char *value,
                                                   uint32_t min_value,
                                                   uint32_t max_value,
                                                   uint32_t *out_value);
static bool bs_settings_service_parse_double_value(const char *value,
                                                   double min_value,
                                                   double max_value,
                                                   double *out_value);
static char *bs_settings_service_parse_string_value(const char *value);
static bool bs_settings_service_parse_config(BsSettingsService *service,
                                            const char *contents,
                                            BsShellConfig *config_out,
                                            GError **error);

static bool
bs_settings_service_ensure_parent_dir(const char *path, GError **error) {
  g_autofree char *parent = NULL;

  if (path == NULL || *path == '\0') {
    return true;
  }

  parent = g_path_get_dirname(path);
  if (parent == NULL || g_strcmp0(parent, ".") == 0) {
    return true;
  }

  if (g_mkdir_with_parents(parent, 0700) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "failed to create parent directory for %s",
                path);
    return false;
  }

  return true;
}

static char *
bs_settings_service_strip_toml_comment(const char *line) {
  GString *out = NULL;
  bool in_quotes = false;

  g_return_val_if_fail(line != NULL, g_strdup(""));

  out = g_string_new(NULL);
  for (const char *cursor = line; *cursor != '\0'; cursor++) {
    if (*cursor == '"' && (cursor == line || cursor[-1] != '\\')) {
      in_quotes = !in_quotes;
    } else if (*cursor == '#' && !in_quotes) {
      break;
    }

    g_string_append_c(out, *cursor);
  }

  return g_string_free(out, false);
}

static bool
bs_settings_service_parse_bool_value(const char *value, bool *out_value) {
  g_return_val_if_fail(value != NULL, false);
  g_return_val_if_fail(out_value != NULL, false);

  if (g_strcmp0(value, "true") == 0) {
    *out_value = true;
    return true;
  }
  if (g_strcmp0(value, "false") == 0) {
    *out_value = false;
    return true;
  }

  return false;
}

static bool
bs_settings_service_parse_uint32_value(const char *value,
                                       uint32_t min_value,
                                       uint32_t max_value,
                                       uint32_t *out_value) {
  guint64 parsed = 0;
  char *end = NULL;

  g_return_val_if_fail(value != NULL, false);
  g_return_val_if_fail(out_value != NULL, false);

  errno = 0;
  parsed = g_ascii_strtoull(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < min_value || parsed > max_value) {
    return false;
  }

  *out_value = (uint32_t) parsed;
  return true;
}

static bool
bs_settings_service_parse_double_value(const char *value,
                                       double min_value,
                                       double max_value,
                                       double *out_value) {
  double parsed = 0.0;
  char *end = NULL;

  g_return_val_if_fail(value != NULL, false);
  g_return_val_if_fail(out_value != NULL, false);

  errno = 0;
  parsed = g_ascii_strtod(value, &end);
  if (errno != 0 || end == value || *end != '\0' || parsed < min_value || parsed > max_value) {
    return false;
  }

  *out_value = parsed;
  return true;
}

static char *
bs_settings_service_parse_string_value(const char *value) {
  g_autofree char *raw = NULL;

  g_return_val_if_fail(value != NULL, NULL);

  if (value[0] == '"') {
    gsize len = strlen(value);
    if (len < 2 || value[len - 1] != '"') {
      return NULL;
    }

    raw = g_strndup(value + 1, len - 2);
    return g_strcompress(raw);
  }

  return g_strdup(value);
}

static bool
bs_settings_service_parse_config(BsSettingsService *service,
                                 const char *contents,
                                 BsShellConfig *config_out,
                                 GError **error) {
  g_auto(GStrv) lines = NULL;
  g_autofree char *current_section = NULL;
  BsShellConfig parsed;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(config_out != NULL, false);

  bs_shell_config_init_defaults(&parsed);
  bs_runtime_paths_clear(&parsed.paths);
  bs_runtime_paths_copy(&parsed.paths, &service->shell_config.paths);

  if (contents == NULL || *contents == '\0') {
    *config_out = parsed;
    return true;
  }

  lines = g_strsplit(contents, "\n", -1);
  for (guint line_index = 0; lines[line_index] != NULL; line_index++) {
    g_autofree char *without_comment = NULL;
    char *line = NULL;
    char *separator = NULL;
    char *key = NULL;
    char *value = NULL;

    without_comment = bs_settings_service_strip_toml_comment(lines[line_index]);
    line = g_strstrip(without_comment);
    if (*line == '\0') {
      continue;
    }

    if (line[0] == '[') {
      gsize len = strlen(line);
      if (len < 3 || line[len - 1] != ']') {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "config.toml invalid section header at line %u",
                    line_index + 1);
        goto fail;
      }

      g_free(current_section);
      current_section = g_strndup(line + 1, len - 2);
      g_strstrip(current_section);
      continue;
    }

    separator = strchr(line, '=');
    if (separator == NULL) {
      g_set_error(error,
                  G_IO_ERROR,
                  G_IO_ERROR_INVALID_DATA,
                  "config.toml invalid assignment at line %u",
                  line_index + 1);
      goto fail;
    }

    *separator = '\0';
    key = g_strstrip(line);
    value = g_strstrip(separator + 1);
    if (current_section == NULL || *key == '\0' || *value == '\0') {
      g_set_error(error,
                  G_IO_ERROR,
                  G_IO_ERROR_INVALID_DATA,
                  "config.toml invalid key/value at line %u",
                  line_index + 1);
      goto fail;
    }

    if (g_strcmp0(current_section, "shell") == 0) {
      if (g_strcmp0(key, "auto_reconnect_niri") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.auto_reconnect_niri)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid shell.auto_reconnect_niri at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "tray_watcher_name") == 0) {
        g_autofree char *parsed_value = bs_settings_service_parse_string_value(value);
        if (parsed_value == NULL) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid shell.tray_watcher_name at line %u", line_index + 1);
          goto fail;
        }
        g_free(parsed.tray_watcher_name);
        parsed.tray_watcher_name = g_steal_pointer(&parsed_value);
      } else if (g_strcmp0(key, "primary_output") == 0) {
        g_autofree char *parsed_value = bs_settings_service_parse_string_value(value);
        if (parsed_value == NULL) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid shell.primary_output at line %u", line_index + 1);
          goto fail;
        }
        g_free(parsed.primary_output);
        parsed.primary_output = g_steal_pointer(&parsed_value);
      }
      continue;
    }

    if (g_strcmp0(current_section, "bar") == 0) {
      if (g_strcmp0(key, "height_px") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 16, 128, &parsed.bar.height_px)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid bar.height_px at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "show_workspace_strip") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.bar.show_workspace_strip)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid bar.show_workspace_strip at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "show_focused_title") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.bar.show_focused_title)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid bar.show_focused_title at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "show_tray") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.bar.show_tray)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid bar.show_tray at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "show_clock") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.bar.show_clock)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid bar.show_clock at line %u", line_index + 1);
          goto fail;
        }
      }
      continue;
    }

    if (g_strcmp0(current_section, "dock") == 0) {
      if (g_strcmp0(key, "icon_size_px") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 0, G_MAXUINT32, &parsed.dock.icon_size_px)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.icon_size_px at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "magnification_enabled") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.dock.magnification_enabled)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.magnification_enabled at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "magnification_scale") == 0) {
        if (!bs_settings_service_parse_double_value(value, -G_MAXDOUBLE, G_MAXDOUBLE, &parsed.dock.magnification_scale)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.magnification_scale at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "hover_range_cap_units") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 0, G_MAXUINT32, &parsed.dock.hover_range_cap_units)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.hover_range_cap_units at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "spacing_px") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 0, G_MAXUINT32, &parsed.dock.spacing_px)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.spacing_px at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "bottom_margin_px") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 0, G_MAXUINT32, &parsed.dock.bottom_margin_px)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.bottom_margin_px at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "show_running_indicator") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.dock.show_running_indicator)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.show_running_indicator at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "animate_opening_apps") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.dock.animate_opening_apps)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.animate_opening_apps at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "display_mode") == 0) {
        g_autofree char *parsed_value = bs_settings_service_parse_string_value(value);
        if (parsed_value == NULL || !bs_dock_display_mode_from_string(parsed_value, &parsed.dock.display_mode)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.display_mode at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "center_on_primary_output") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.dock.center_on_primary_output)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid dock.center_on_primary_output at line %u", line_index + 1);
          goto fail;
        }
      }
      continue;
    }

    if (g_strcmp0(current_section, "launchpad") == 0) {
      if (g_strcmp0(key, "resident") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.launchpad.resident)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid launchpad.resident at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "grid_icon_size_px") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 32, 256, &parsed.launchpad.grid_icon_size_px)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid launchpad.grid_icon_size_px at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "max_recent_apps") == 0) {
        if (!bs_settings_service_parse_uint32_value(value, 1, 64, &parsed.launchpad.max_recent_apps)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid launchpad.max_recent_apps at line %u", line_index + 1);
          goto fail;
        }
      } else if (g_strcmp0(key, "show_categories") == 0) {
        if (!bs_settings_service_parse_bool_value(value, &parsed.launchpad.show_categories)) {
          g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid launchpad.show_categories at line %u", line_index + 1);
          goto fail;
        }
      }
    }
  }

  bs_dock_config_normalize(&parsed.dock);
  *config_out = parsed;
  return true;

fail:
  bs_shell_config_clear(&parsed);
  return false;
}

static GPtrArray *
bs_settings_service_dup_pinned_apps(BsSettingsService *service) {
  BsSnapshot *snapshot = NULL;
  GPtrArray *copy = NULL;

  g_return_val_if_fail(service != NULL, NULL);

  snapshot = bs_state_store_snapshot(service->store);
  copy = g_ptr_array_new_with_free_func(g_free);
  if (snapshot == NULL || snapshot->pinned_app_ids == NULL) {
    return copy;
  }

  for (guint i = 0; i < snapshot->pinned_app_ids->len; i++) {
    const char *desktop_id = g_ptr_array_index(snapshot->pinned_app_ids, i);
    if (desktop_id != NULL && *desktop_id != '\0') {
      g_ptr_array_add(copy, g_strdup(desktop_id));
    }
  }

  return copy;
}

static void
bs_settings_service_append_string_array_json(GString *content, GPtrArray *values) {
  g_return_if_fail(content != NULL);

  g_string_append(content, "[");
  if (values != NULL) {
    for (guint i = 0; i < values->len; i++) {
      char *escaped = NULL;
      const char *value = g_ptr_array_index(values, i);

      if (i > 0) {
        g_string_append(content, ",");
      }

      escaped = g_strescape(value != NULL ? value : "", NULL);
      g_string_append_printf(content, "\"%s\"", escaped != NULL ? escaped : "");
      g_free(escaped);
    }
  }
  g_string_append(content, "]");
}

static bool
bs_settings_service_parse_state(BsSettingsService *service,
                                const BsDockConfig *dock_config,
                                const char *contents,
                                GError **error) {
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GPtrArray) pinned_app_ids = NULL;
  g_autoptr(GHashTable) seen = NULL;
  JsonNode *root = NULL;
  JsonObject *root_object = NULL;
  JsonArray *pinned_apps = NULL;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(dock_config != NULL, false);

  if (contents == NULL || *contents == '\0') {
    bs_state_store_begin_update(service->store);
    bs_state_store_replace_pinned_app_ids(service->store, NULL);
    bs_state_store_replace_dock_config(service->store, dock_config);
    bs_state_store_finish_update(service->store);
    return true;
  }

  parser = json_parser_new();
  if (!json_parser_load_from_data(parser, contents, -1, error)) {
    return false;
  }

  root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "state.json root is not an object");
    return false;
  }

  root_object = json_node_get_object(root);
  pinned_app_ids = g_ptr_array_new_with_free_func(g_free);
  seen = g_hash_table_new(g_str_hash, g_str_equal);

  if (json_object_has_member(root_object, "pinned_apps")) {
    JsonNode *pinned_node = json_object_get_member(root_object, "pinned_apps");

    if (!JSON_NODE_HOLDS_ARRAY(pinned_node)) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "state.json pinned_apps is not an array");
      return false;
    }

    pinned_apps = json_node_get_array(pinned_node);
    for (guint i = 0; i < json_array_get_length(pinned_apps); i++) {
      const char *desktop_id = json_array_get_string_element(pinned_apps, i);

      if (desktop_id == NULL || *desktop_id == '\0') {
        continue;
      }
      if (g_hash_table_contains(seen, desktop_id)) {
        continue;
      }

      g_hash_table_add(seen, (gpointer) desktop_id);
      g_ptr_array_add(pinned_app_ids, g_strdup(desktop_id));
    }
  }

  bs_state_store_begin_update(service->store);
  bs_state_store_replace_pinned_app_ids(service->store, pinned_app_ids);
  bs_state_store_replace_dock_config(service->store, dock_config);
  bs_state_store_finish_update(service->store);
  return true;
}

static char *
bs_settings_service_build_config_stub(const BsShellConfig *config) {
  GString *content = g_string_new("# bit_shell config.toml\n"
                                  "# generated by core\n"
                                  "# version: " BS_BUILD_VERSION "\n"
                                  "# commit: " BS_BUILD_COMMIT "\n\n");

  g_string_append(content, "[shell]\n");
  g_string_append_printf(content, "auto_reconnect_niri = %s\n", config->auto_reconnect_niri ? "true" : "false");
  g_string_append_printf(content, "tray_watcher_name = \"%s\"\n", config->tray_watcher_name != NULL ? config->tray_watcher_name : "");
  g_string_append_printf(content, "primary_output = \"%s\"\n\n", config->primary_output != NULL ? config->primary_output : "");

  g_string_append(content, "[bar]\n");
  g_string_append_printf(content, "height_px = %u\n", config->bar.height_px);
  g_string_append_printf(content, "show_workspace_strip = %s\n", config->bar.show_workspace_strip ? "true" : "false");
  g_string_append_printf(content, "show_focused_title = %s\n", config->bar.show_focused_title ? "true" : "false");
  g_string_append_printf(content, "show_tray = %s\n", config->bar.show_tray ? "true" : "false");
  g_string_append_printf(content, "show_clock = %s\n\n", config->bar.show_clock ? "true" : "false");

  g_string_append(content, "[dock]\n");
  g_string_append_printf(content, "icon_size_px = %u\n", config->dock.icon_size_px);
  g_string_append_printf(content, "magnification_enabled = %s\n", config->dock.magnification_enabled ? "true" : "false");
  g_string_append_printf(content, "magnification_scale = %.2f\n", config->dock.magnification_scale);
  g_string_append_printf(content, "hover_range_cap_units = %u\n", config->dock.hover_range_cap_units);
  g_string_append_printf(content, "spacing_px = %u\n", config->dock.spacing_px);
  g_string_append_printf(content, "bottom_margin_px = %u\n", config->dock.bottom_margin_px);
  g_string_append_printf(content, "show_running_indicator = %s\n", config->dock.show_running_indicator ? "true" : "false");
  g_string_append_printf(content, "animate_opening_apps = %s\n", config->dock.animate_opening_apps ? "true" : "false");
  g_string_append_printf(content, "display_mode = \"%s\"\n", bs_dock_display_mode_to_string(config->dock.display_mode));
  g_string_append_printf(content, "center_on_primary_output = %s\n\n", config->dock.center_on_primary_output ? "true" : "false");

  g_string_append(content, "[launchpad]\n");
  g_string_append_printf(content, "resident = %s\n", config->launchpad.resident ? "true" : "false");
  g_string_append_printf(content, "grid_icon_size_px = %u\n", config->launchpad.grid_icon_size_px);
  g_string_append_printf(content, "max_recent_apps = %u\n", config->launchpad.max_recent_apps);
  g_string_append_printf(content, "show_categories = %s\n", config->launchpad.show_categories ? "true" : "false");

  return g_string_free(content, false);
}

static char *
bs_settings_service_build_state_stub(BsSettingsService *service) {
  g_autoptr(GPtrArray) pinned_app_ids = NULL;
  GString *content = g_string_new(NULL);

  pinned_app_ids = bs_settings_service_dup_pinned_apps(service);
  g_string_append_printf(content,
                         "{\n  \"generation\": %" G_GUINT64_FORMAT ",\n  \"settings_dirty\": %s,\n  \"pinned_apps\": ",
                         bs_state_store_generation(service->store),
                         service->dirty_state ? "true" : "false");
  bs_settings_service_append_string_array_json(content, pinned_app_ids);
  g_string_append(content, ",\n  \"recent_apps\": [],\n  \"favorites\": [],\n  \"recent_workspaces\": []\n}\n");
  return g_string_free(content, false);
}

static bool
bs_settings_service_ensure_stub_file(const char *path,
                                     const char *contents,
                                     GError **error) {
  if (path == NULL || *path == '\0') {
    return true;
  }

  if (!bs_settings_service_ensure_parent_dir(path, error)) {
    return false;
  }

  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    return true;
  }

  return g_file_set_contents(path, contents, -1, error);
}

BsSettingsService *
bs_settings_service_new(BsStateStore *store, const BsSettingsServiceConfig *config) {
  BsSettingsService *service = g_new0(BsSettingsService, 1);
  service->store = store;
  bs_shell_config_init_defaults(&service->shell_config);
  if (config != NULL && config->config_path != NULL) {
    service->config_path = g_strdup(config->config_path);
    g_free(service->shell_config.paths.config_path);
    service->shell_config.paths.config_path = g_strdup(config->config_path);
  }
  if (config != NULL && config->state_path != NULL) {
    service->state_path = g_strdup(config->state_path);
    g_free(service->shell_config.paths.state_path);
    service->shell_config.paths.state_path = g_strdup(config->state_path);
  }
  return service;
}

void
bs_settings_service_free(BsSettingsService *service) {
  if (service == NULL) {
    return;
  }

  g_free(service->config_path);
  g_free(service->state_path);
  bs_shell_config_clear(&service->shell_config);
  g_free(service);
}

bool
bs_settings_service_load(BsSettingsService *service, GError **error) {
  g_autofree char *config_stub = NULL;
  g_autofree char *state_stub = NULL;
  g_autofree char *config_contents = NULL;
  g_autofree char *state_contents = NULL;
  BsShellConfig parsed_config;

  g_return_val_if_fail(service != NULL, false);

  config_stub = bs_settings_service_build_config_stub(&service->shell_config);
  state_stub = bs_settings_service_build_state_stub(service);

  if (!bs_settings_service_ensure_stub_file(service->config_path, config_stub, error)) {
    return false;
  }
  if (!bs_settings_service_ensure_stub_file(service->state_path, state_stub, error)) {
    return false;
  }

  if (service->config_path != NULL) {
    (void) g_file_get_contents(service->config_path, &config_contents, NULL, NULL);
  }
  if (service->state_path != NULL) {
    (void) g_file_get_contents(service->state_path, &state_contents, NULL, NULL);
  }

  if (!bs_settings_service_parse_config(service, config_contents, &parsed_config, error)) {
    return false;
  }

  if (!bs_settings_service_parse_state(service, &parsed_config.dock, state_contents, error)) {
    bs_shell_config_clear(&parsed_config);
    return false;
  }

  bs_shell_config_clear(&service->shell_config);
  service->shell_config = parsed_config;
  return true;
}

bool
bs_settings_service_flush(BsSettingsService *service, GError **error) {
  g_autofree char *state_stub = NULL;

  g_return_val_if_fail(service != NULL, false);

  state_stub = bs_settings_service_build_state_stub(service);
  if (service->state_path != NULL) {
    if (!bs_settings_service_ensure_parent_dir(service->state_path, error)) {
      return false;
    }
    if (!g_file_set_contents(service->state_path, state_stub, -1, error)) {
      return false;
    }
  }

  service->dirty_state = false;
  return true;
}

const BsShellConfig *
bs_settings_service_shell_config(const BsSettingsService *service) {
  g_return_val_if_fail(service != NULL, NULL);
  return &service->shell_config;
}

void
bs_settings_service_mark_state_dirty(BsSettingsService *service) {
  g_return_if_fail(service != NULL);
  service->dirty_state = true;
}
