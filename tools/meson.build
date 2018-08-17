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

unicorn = meson.get_compiler('c').find_library('unicorn', required: false)
if unicorn.found()
    executable('unicornomatic', ['unicornomatic.c', 'undefined-flags.c'], dependencies: [ish, unicorn])
    configure_file(input: 'ptraceomatic-gdb.gdb', output: 'unicornomatic-gdb.gdb', copy: true)
endif
