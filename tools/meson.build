if get_option('kernel') == 'ish'
    # these tools are specific to x86_64 linux
    if host_machine.system() == 'linux' and host_machine.cpu() == 'x86_64'
        transplant_src = [
            'vdso-transplant.c',
            'ptutil.c',
        ]
        executable('ptraceomatic', ['ptraceomatic.c', 'undefined-flags.c', transplant_src], dependencies: [ish])
        configure_file(input: 'ptraceomatic-gdb.gdb', output: '@PLAINNAME@', copy: true)

        # tools for messing with vdsos
        # executable('vdso-dump', ['vdso-dump.c'], c_args: ['-m32'], link_args: ['-m32'])
        executable('vdso-transplant', ['vdso-transplant-main.c', transplant_src],
            include_directories: includes)
    endif

    unicorn = cc.find_library('unicorn', required: false)
    if unicorn.found()
        executable('unicornomatic', ['unicornomatic.c', 'undefined-flags.c'], dependencies: [ish, unicorn])
        configure_file(input: 'ptraceomatic-gdb.gdb', output: 'unicornomatic-gdb.gdb', copy: true)
    endif
endif

libarchive = dependency('libarchive', required: false)
if not libarchive.found()
    # homebrew
    libarchive_lib = cc.find_library('archive', dirs: ['/usr/local/opt/libarchive/lib'], required: false)
    if not libarchive_lib.found()
        libarchive_lib = cc.find_library('archive', dirs: ['/opt/homebrew/opt/libarchive/lib'], required: false)
    endif
    if libarchive_lib.found()
        libarchive = declare_dependency(dependencies: [libarchive_lib], include_directories: include_directories(['/usr/local/opt/libarchive/include']))
    endif
endif

if libarchive.found()
    fakefsify = executable('fakefsify', ['fakefsify.c', 'fakefs.c'], dependencies: [libarchive], link_with: [libfakefs], include_directories: [includes])
    custom_target('unfakefsify',
        build_by_default: true,
        command: ['ln', '-sf', 'fakefsify', '@OUTPUT@'],
        output: 'unfakefsify')
endif
