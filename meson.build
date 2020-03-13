project('smuc', 'c', default_options : [ 'c_std=c11' ])

cc = meson.get_compiler('c')

wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
cairo          = dependency('cairo')
pango          = dependency('pango')
pangocairo     = dependency('pangocairo')
libical        = dependency('libical')
m = cc.find_library('m', required : false)

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
  'src/util.c',
  'src/calendar.c',
  'src/datetime.c',
  'src/editor.c',
  'src/editor_parser.c',
  'src/pango.c',
  'src/subprocess.c',
  'src/colors.c',
  'src/libical_parse.c',
  'src/keyboard.c',
  'src/algo/perm.c',
  'src/algo/calendar_layout.c',
  'src/algo/todo_schedule.c',
  'src/render.c',
  'src/application.c',
  'src/util/hashmap.c',
  'src/views.c',
]
wayland_sources = [
  'src/backend/wayland.c',
  xdg_shell_header_target,
  xdg_shell_private_target,
]
dependencies = [
  wayland_client, wayland_cursor, cairo, pango, pangocairo, libical, m
]

add_global_arguments('-Wno-parentheses', language : 'c')
add_global_arguments('-D_XOPEN_SOURCE=700', language : 'c')

lib_gui_calendar_common = static_library(
  'gui-calendar-common',
  sources,
  include_directories: incdir,
  dependencies: dependencies
)

executable(
  'smuc',
  'src/main.c',
  wayland_sources,
  'src/backend/fbdev.c',
  'src/backend/svg.c',
  'src/backend/image.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : [
    '-DCONFIG_BACKEND_WAYLAND',
    '-DCONFIG_BACKEND_FBDEV',
    '-DCONFIG_BACKEND_SVG',
    '-DCONFIG_BACKEND_IMAGE'
  ]
)

test_main = executable(
  'test-editor',
  'src/tests/test_main.c',
  'src/backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)
test('test-main', test_main)

executable(
  'fuzz-editor',
  'src/tests/fuzz_editor.c',
  'src/backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)

executable(
  'fuzz-parse-ics',
  'src/tests/fuzz_parse_ics.c',
  'src/backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)

executable(
  'fuzz-image',
  'src/main.c',
  'src/backend/dummy.c',
  include_directories: incdir,
  dependencies: dependencies,
  link_with: lib_gui_calendar_common,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)
