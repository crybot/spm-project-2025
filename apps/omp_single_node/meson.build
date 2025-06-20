# apps/ff_omp_single_node/meson.build

executable('sorter_omp', 'main.cpp',
    dependencies : [
        extsort_dep, # Our internal library dependency,
        dependency('openmp')
    ]
)
