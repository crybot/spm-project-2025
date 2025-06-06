project('spm_project', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23']
)

inc = include_directories('include')

executable(
  'spm_project',
  'src/main.cpp',
  'src/utils.cpp',
  'src/record.cpp',
  'src/record_loader.cpp',
  include_directories : inc,
  install : false,
  cpp_args : ['-O3' ]
)

# Meson automatically generates compile_commands.json in the build directory (e.g. for LSP servers)
