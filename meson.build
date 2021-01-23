project('smuc', 'c', default_options : [
  'c_std=c11',
  'default_library=static',
  'werror=true',
])

# Compiler setup
cc = meson.get_compiler('c')
add_project_arguments('-Wno-parentheses', language : 'c')
add_project_arguments('-D_XOPEN_SOURCE=700', language : 'c')

libical = dependency('libical', required: false)
if not libical.found()
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
endif

# Dependencies
m = cc.find_library('m')
glesv2 = dependency('glesv2')

pu_proj = subproject('platform_utils')
pu_main_dep = pu_proj.get_variable('pu_main_dep')
pu_event_loop_dep = pu_proj.get_variable('pu_event_loop_dep')

ds_proj = subproject('ds')
ds_vec = ds_proj.get_variable('ds_vec_dep')
ds_hashmap = ds_proj.get_variable('ds_hashmap_dep')
ds_tree = ds_proj.get_variable('ds_tree_dep')
ds_matrix = ds_proj.get_variable('ds_matrix_dep')
ds_iter = ds_proj.get_variable('ds_iter_dep')

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
  glesv2, ds_matrix, mgu_gl, mgu_win, mgu_sr, m, ds_tree, ds_iter, libtouch
]
lib_gui_calendar = static_library(
  'gui-calendar',
  src_gui_calendar,
  include_directories: incdir,
  dependencies: [ dep_gui_calendar, ds_vec, ds_hashmap, ds_tree,
    pu_event_loop_dep ],
  link_with: [ lib_core, lib_common, lib_uexpr ]
)
executable(
  'smuc',
  'src/gui-calendar/main.c',
  include_directories: incdir,
  dependencies: [ dep_gui_calendar, pu_main_dep ],
  link_with: lib_gui_calendar,
)

executable(
  'gen',
  'src/utils/gen.c',
  include_directories: incdir,
  dependencies: [ libical, dep_gui_calendar, pu_main_dep ],
  link_with: lib_gui_calendar
)

# Tests
prog_valgrind = find_program('valgrind', required: false)

if prog_valgrind.found() and meson.can_run_host_binaries()
  foreach path : [ 'src/test/test_main.c' ]
    exe = executable(
      'exe-' + path.underscorify(),
      path,
      include_directories: incdir,
      dependencies: dep_gui_calendar,
      link_with: lib_gui_calendar,
    )
    test(
      'test-' + path.underscorify(),
      prog_valgrind,
      args: [ '--leak-check=full', '--error-exitcode=1', exe ]
    )
  endforeach
endif

test(
  'test-lint',
  files('scripts/lint.sh'),
  workdir: meson.project_source_root(),
)

prog_markdown = find_program('md2html', required: false)
if prog_markdown.found()
  foreach md : [
    'README.md',
    'Documentation/interface.md',
    'Documentation/bugs.md',
  ]
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
