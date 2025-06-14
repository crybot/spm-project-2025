# /meson.build

project('spm_project', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23', 'buildtype=release']
)

inc = include_directories('include')
subdir('src')
subdir('apps')
subdir('tools')
subdir('tests')
