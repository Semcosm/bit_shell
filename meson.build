project(
  'bit_shell',
  'c',
  version: '0.1.0',
  meson_version: '>= 1.2.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
  ],
)

glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')

subdir('core')
