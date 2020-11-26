project('smuc', 'c', default_options : [ 'c_std=c11', 'default_library=static' ])

em = host_machine.system() == 'emscripten'

# Compiler setup
cc = meson.get_compiler('c')
add_project_arguments('-Wno-parentheses', language : 'c')
add_project_arguments('-D_XOPEN_SOURCE=700', language : 'c')

if em
  glesv2 = []
  add_project_link_arguments([ '-s', 'FORCE_FILESYSTEM=1' ], language : 'c')
  cmake = import('cmake')
  opt_var = cmake.subproject_options()
  opt_var.add_cmake_defines({
    'CMAKE_INSTALL_PREFIX' : '/usr',
    'CMAKE_DISABLE_FIND_PACKAGE_ICU' : true,
    'CMAKE_DISABLE_FIND_PACKAGE_BDB' : true,
    'WITH_CXX_BINDINGS' : false,
    # 'USE_BUILTIN_TZDATA' : true,
    'LIBICAL_BUILD_TESTING' : false,
    'ICAL_BUILD_DOCS' : false,
    'STATIC_ONLY' : true,
    'ICAL_GLIB' : false,
    'ENABLE_GTK_DOC' : false,
  })
  libical_proj = cmake.subproject('libical', options : opt_var)
  libical = libical_proj.dependency('ical')
else
  glesv2 = dependency('glesv2')
  libical = dependency('libical')
endif

# Dependencies
m = cc.find_library('m')

ds_proj = subproject('ds')
ds_vec = ds_proj.get_variable('ds_vec_dep')
ds_hashmap = ds_proj.get_variable('ds_hashmap_dep')
ds_tree = ds_proj.get_variable('ds_tree_dep')
ds_matrix = ds_proj.get_variable('ds_matrix_dep')

libtouch_proj = subproject('libtouch')
libtouch = libtouch_proj.get_variable('libtouch_dep')

mgu_proj = subproject('mgu')
mgu_gl = mgu_proj.get_variable('mgu_gl_dep')
mgu_sr = mgu_proj.get_variable('mgu_sr_dep')
mgu_win = mgu_proj.get_variable('mgu_win_dep')

# gperf lookup table code generation
prog_gperf = find_program('gperf')
gperf_tables = {}
if prog_gperf.found()
  foreach gp : [ 'src/common/colors.gperf' ]
    basename = gp.split('.')[-2].split('/')[-1]
    output = basename + '.c'
    message('making target:', output)
    target = custom_target(output,
      input : gp,
      output : output,
      command : [ prog_gperf, '@INPUT@', '--output-file=@OUTPUT@' ],
      build_by_default: true
    )
    gperf_tables += { output : target }
  endforeach
endif

# Source files
incdir = include_directories('include')

lib_core = static_library(
  'core',
  'src/core/core.c',
  include_directories: incdir
)

src_common = [
  gperf_tables.get('colors.c'),
  'src/common/calendar.c',
  'src/common/datetime.c',
  'src/common/libical_iface.c',
  'src/common/subprocess.c',
  'src/common/util.c',
  'src/common/algo/perm.c',
  'src/common/algo/calendar_layout.c',
  'src/common/algo/todo_schedule.c',
  'src/common/algo/heapsort.c',
  'src/common/props.c',
  'src/common/loop.c',
]
dep_common = [ libical ]
lib_common = static_library(
  'common',
  src_common,
  include_directories: incdir,
  dependencies: [ dep_common, ds_vec ],
  link_with: [ lib_core ]
)

src_uexpr = [
  'src/uexpr/uexpr.c'
]
lib_uexpr = static_library(
  'uexpr',
  src_uexpr,
  include_directories: incdir,
  dependencies: [ ds_vec, ds_hashmap ],
  link_with: [ lib_core ]
)
executable(
  'uexpr',
  'src/uexpr/main.c',
  dependencies : [ ds_vec ],
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
  'src/gui-calendar/views.c',
]
dep_gui_calendar = [
  glesv2, ds_matrix, mgu_gl, mgu_win, mgu_sr, m, ds_tree, libtouch
]
lib_gui_calendar = static_library(
  'gui-calendar',
  src_gui_calendar,
  include_directories: incdir,
  dependencies: [ dep_gui_calendar, ds_vec, ds_hashmap, ds_tree ],
  link_with: [ lib_core, lib_common, lib_uexpr ]
)
executable(
  'smuc',
  'src/gui-calendar/main.c',
  include_directories: incdir,
  dependencies: dep_gui_calendar,
  link_with: lib_gui_calendar,
)

executable(
  'gen',
  'src/utils/gen.c',
  include_directories: incdir,
  dependencies: [ libical, dep_gui_calendar ],
  link_with: lib_gui_calendar
)

# test_main = executable(
#   'test-editor',
#   'src/tests/test_main.c',
#   'src/gui-calendar/backend/dummy.c',
#   include_directories: incdir,
#   dependencies: dep_gui_calendar,
#   link_with: lib_gui_calendar,
# )
# test('test-main', test_main)

# executable(
#   'fuzz-editor',
#   'src/tests/fuzz_editor.c',
#   'src/gui-calendar/backend/dummy.c',
#   include_directories: incdir,
#   dependencies: dep_gui_calendar,
#   link_with: lib_gui_calendar,
#   c_args : '-DCONFIG_BACKEND_DUMMY'
# )

# executable(
#   'fuzz-parse-ics',
#   'src/tests/fuzz_parse_ics.c',
#   'src/gui-calendar/backend/dummy.c',
#   include_directories: incdir,
#   dependencies: dep_gui_calendar,
#   link_with: lib_gui_calendar,
#   c_args : '-DCONFIG_BACKEND_DUMMY'
# )

# executable(
#   'fuzz-image',
#   'src/gui-calendar/main.c',
#   'src/gui-calendar/backend/dummy.c',
#   include_directories: incdir,
#   dependencies: dep_gui_calendar,
#   link_with: lib_gui_calendar,
# )

prog_markdown = find_program('md2html', required: false)
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
