project('gui_calendar', 'c')

wayland_client = dependency('wayland-client')
cairo          = dependency('cairo')
pango          = dependency('pango')
pangocairo     = dependency('pangocairo')
libical        = dependency('libical')

dep_scanner = dependency('wayland-scanner')
prog_scanner = find_program(dep_scanner
  .get_pkgconfig_variable('wayland_scanner'))

dep_wp = dependency('wayland-protocols', version: '>= 1.18')
dir_wp_base = dep_wp.get_pkgconfig_variable('pkgdatadir')

base_file = 'xdg-shell'
xml_path = '@0@/stable/@1@/@1@.xml'.format(dir_wp_base, base_file)

xdg_shell_header_target = custom_target(
  'xdg_shell_header_target',
  command: [ prog_scanner, 'client-header', '@INPUT@', '@OUTPUT@' ],
  input: xml_path,
  output: '@0@-client-protocol.h'.format(base_file),
)

xdg_shell_private_target = custom_target(
  'xdg_shell_private_target',
  command: [ prog_scanner, 'private-code', '@INPUT@', '@OUTPUT@' ],
  input: xml_path,
  output: '@0@-client-protocol.c'.format(base_file),
)

incdir = include_directories('include')
sources = [
  'util.c',
  'calendar.c',
  'editor.c',
  'pango.c',
  'subprocess.c',
  'colors.c',
  'calendar_layout.c',
  'libical_parse.c',
  'keyboard.c',
  'algo.c',
  'render.c',
  'application.c',
  'util/hashmap.c',
]
wayland_sources = [
  'backend/wayland.c',
  xdg_shell_header_target,
  xdg_shell_private_target,
]
dependencies = [
  wayland_client, cairo, pango, pangocairo, libical
]

add_global_arguments('-Wno-parentheses', language : 'c')

lib_gui_calendar_common = static_library(
  'gui-calendar-common',
  sources,
  include_directories: incdir,
  dependencies: dependencies
)

executable(
  'gui-calendar',
  'main.c',
  wayland_sources,
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common
)

executable(
  'gui-calendar-fbdev',
  'main.c',
  'backend/fbdev.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DBACKEND_FBDEV'
)

test_main = executable(
  'test-editor',
  'tests/test_main.c',
  'backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DBACKEND_DUMMY'
)
test('test-main', test_main)

executable(
  'fuzz-editor',
  'tests/fuzz_editor.c',
  'backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DBACKEND_DUMMY'
)

executable(
  'fuzz-parse-ics',
  'tests/fuzz_parse_ics.c',
  'backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DBACKEND_DUMMY'
)

executable(
  'fuzz-image',
  'main.c',
  'backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DBACKEND_DUMMY'
)
