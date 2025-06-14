# src/meson.build

inc_dirs = include_directories('../include')
extsort_lib = static_library('extsort_lib',
  'utils.cpp',
  'record.cpp',
  'record_loader.cpp',
  'memory_arena.cpp',
  include_directories : inc_dirs,
  install : false,
  cpp_args : ['-O3']
)

extsort_dep = declare_dependency(
  include_directories : inc_dirs,
  link_with : extsort_lib
)
