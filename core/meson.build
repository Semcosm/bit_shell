git_commit = 'unknown'
git_commit_result = run_command('git', 'rev-parse', '--short=12', 'HEAD', check: false)
if git_commit_result.returncode() == 0
  git_commit = git_commit_result.stdout().strip()
endif

build_info_conf = configuration_data()
build_info_conf.set_quoted('BS_BUILD_VERSION', meson.project_version())
build_info_conf.set_quoted('BS_BUILD_COMMIT', git_commit)

configure_file(
  input: 'build_info.h.in',
  output: 'build_info.h',
  configuration: build_info_conf,
)

core_includes = include_directories('.')

bit_shelld_sources = files(
  'model/ipc.c',
  'model/config.c',
  'model/snapshot.c',
  'shelld/main.c',
  'shelld/app.c',
  'shelld/config_watcher.c',
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
  'model/config.c',
  'frontends/common/ipc_client.c',
  'frontends/bit_dock/main.c',
  'frontends/bit_dock/dock_layout.c',
  'frontends/bit_dock/dock_app.c',
)

executable(
  'bit_dock',
  bit_dock_sources,
  include_directories: core_includes,
  dependencies: [glib_dep, gio_dep, gio_unix_dep, json_glib_dep, gtk4_dep, gtk4_layer_shell_dep, math_dep],
  install: false,
)

bit_bar_sources = files(
  'model/config.c',
  'model/ipc.c',
  'frontends/common/ipc_client.c',
  'frontends/bit_bar/main.c',
  'frontends/bit_bar/bar_app.c',
  'frontends/bit_bar/bar_view_model.c',
  'frontends/bit_bar/clock_widget.c',
  'frontends/bit_bar/tray_icon_resolver.c',
  'frontends/bit_bar/tray_icon_resolver.h',
  'frontends/bit_bar/tray_menu_bridge.c',
  'frontends/bit_bar/tray_menu_bridge.h',
  'frontends/bit_bar/tray_strip.c',
  'frontends/bit_bar/tray_strip.h',
  'frontends/bit_bar/tray_item_button.c',
  'frontends/bit_bar/tray_item_button.h',
)

executable(
  'bit_bar',
  bit_bar_sources,
  include_directories: core_includes,
  dependencies: [glib_dep, gio_dep, gio_unix_dep, json_glib_dep, gtk4_dep, gtk4_layer_shell_dep],
  install: false,
)

bit_bar_tray_icon_resolver_test = executable(
  'bit_bar_tray_icon_resolver_test',
  [
    'frontends/bit_bar/tray_icon_resolver_test.c',
    'frontends/bit_bar/tray_icon_resolver.c',
  ],
  include_directories: core_includes,
  dependencies: [glib_dep, gtk4_dep],
  install: false,
)

test('bit_bar_tray_icon_resolver', bit_bar_tray_icon_resolver_test)
