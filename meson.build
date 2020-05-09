project('smuc', 'c', default_options : [ 'c_std=c11' ])

# Compiler setup
cc = meson.get_compiler('c')
add_global_arguments('-Wno-parentheses', language : 'c')
add_global_arguments('-D_XOPEN_SOURCE=700', language : 'c')

# Dependencies
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

# Source files
incdir = include_directories('include')

lib_core = static_library(
  'core',
  'src/core/core.c',
  include_directories: incdir
)

src_container = [
  'src/container/hashmap.c',
  'src/container/vec.c'
]
lib_container = static_library(
  'container',
  src_container,
  include_directories: incdir,
  link_with: lib_core
)

src_common = [
  'src/common/colors.c',
  'src/common/calendar.c',
  'src/common/datetime.c',
  'src/common/libical_parse.c',
  'src/common/subprocess.c',
  'src/common/util.c',
  'src/common/algo/perm.c',
  'src/common/algo/calendar_layout.c',
  'src/common/algo/todo_schedule.c',
]
dep_common = [ libical ]
lib_common = static_library(
  'common',
  src_common,
  include_directories: incdir,
  dependencies: dep_common,
  link_with: [ lib_core, lib_container ]
)

src_uexpr = [
  'src/uexpr/uexpr.c'
]
lib_uexpr = static_library(
  'uexpr',
  src_uexpr,
  include_directories: incdir,
  link_with: [ lib_core, lib_container ]
)
executable(
  'uexpr',
  'src/uexpr/main.c',
  include_directories: incdir,
  link_with: lib_uexpr
)

src_gui_calendar = [
  'src/gui-calendar/application.c',
  'src/gui-calendar/uexpr_cal.c',
  'src/gui-calendar/editor.c',
  'src/gui-calendar/editor_parser.c',
  'src/gui-calendar/keyboard.c',
  'src/gui-calendar/render.c',
  'src/gui-calendar/pango.c',
  'src/gui-calendar/views.c',
]
dep_gui_calendar = [
  wayland_client, wayland_cursor, cairo, pango, pangocairo, m
]
wayland_sources = [
  'src/gui-calendar/backend/wayland.c',
  xdg_shell_header_target,
  xdg_shell_private_target,
]
lib_gui_calendar = static_library(
  'gui-calendar',
  src_gui_calendar,
  wayland_sources,
  'src/gui-calendar/backend/fbdev.c',
  'src/gui-calendar/backend/svg.c',
  'src/gui-calendar/backend/image.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: [ lib_core, lib_common, lib_container, lib_uexpr ]
)
executable(
  'smuc',
  'src/gui-calendar/main.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar,
  c_args : [
    '-DCONFIG_BACKEND_WAYLAND',
    '-DCONFIG_BACKEND_FBDEV',
    '-DCONFIG_BACKEND_SVG',
    '-DCONFIG_BACKEND_IMAGE'
  ]
)

executable(
  'gen',
  'src/utils/gen.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar
)

test_main = executable(
  'test-editor',
  'src/tests/test_main.c',
  'src/gui-calendar/backend/dummy.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)
test('test-main', test_main)

executable(
  'fuzz-editor',
  'src/tests/fuzz_editor.c',
  'src/gui-calendar/backend/dummy.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)

executable(
  'fuzz-parse-ics',
  'src/tests/fuzz_parse_ics.c',
  'src/gui-calendar/backend/dummy.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)

executable(
  'fuzz-image',
  'src/gui-calendar/main.c',
  'src/gui-calendar/backend/dummy.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar,
  c_args : '-DCONFIG_BACKEND_DUMMY'
)

prog_markdown = find_program('md2html')
if prog_markdown.found()
  foreach md : [ 'Documentation/interface.md', 'Documentation/bugs.md' ]
    basename = md.split('.')[-2].split('/')[-1]
    output = basename + '.html'
    message('making target:', output)
    custom_target(output,
      input : md,
      output : output,
      command : [ prog_markdown, '-f', '-o', '@OUTPUT@', '@INPUT@' ],
      build_by_default: true
    )
  endforeach
endif
