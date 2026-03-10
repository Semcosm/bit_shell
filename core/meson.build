core_includes = include_directories('.')

bit_shelld_sources = files(
  'model/ipc.c',
  'model/config.c',
  'model/snapshot.c',
  'shelld/main.c',
  'shelld/app.c',
  'shelld/command_router.c',
  'shelld/ipc_server.c',
  'niri/niri_backend.c',
  'state/state_store.c',
  'services/app_registry.c',
  'services/workspace_service.c',
  'services/dock_service.c',
  'services/launcher_service.c',
  'services/tray_service.c',
  'services/settings_service.c',
)

executable(
  'bit_shelld',
  bit_shelld_sources,
  include_directories: core_includes,
  dependencies: [glib_dep, gio_dep, gio_unix_dep, json_glib_dep],
  install: false,
)

bit_dock_sources = files(
  'frontends/bit_dock/main.c',
  'frontends/bit_dock/dock_app.c',
)

executable(
  'bit_dock',
  bit_dock_sources,
  include_directories: core_includes,
  dependencies: [glib_dep, gio_dep, gio_unix_dep, json_glib_dep, gtk4_dep, gtk4_layer_shell_dep],
  install: false,
)
