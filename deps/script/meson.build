libarchive = dependency('libarchive', method: 'pkg-config', required: false)
if not libarchive.found()
    # try homebrew
    libarchive_lib = cc.find_library('libarchive', dirs: ['/usr/local/opt/libarchive/lib'], required: false)
    if libarchive_lib.found()
        libarchive = declare_dependency(dependencies: [libarchive_lib], include_directories: include_directories(['/usr/local/opt/libarchive/include']))
    endif
endif
if not libarchive.found()
    warning('no system libarchive found, building with cmake')
    cmake = import('cmake')
    libarchive = cmake.subproject('libarchive').dependency('archive_static')
endif

