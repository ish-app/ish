# these tools are linux-specific
if build_machine.system() == 'linux'
    transplant_src = [
        'vdso-transplant.c',
        'ptutil.c',
        cified_vdso,
    ]
    executable('ptraceomatic', ['ptraceomatic.c', 'undefined-flags.c', transplant_src], dependencies: [ish, softfloat, threads])
    configure_file(input: 'ptraceomatic-gdb.gdb', output: '@PLAINNAME@', configuration: configuration_data())

    # tools for messing with vdsos
    executable('vdso-dump', ['vdso-dump.c'], c_args: ['-m32'], link_args: ['-m32'])
    executable('vdso-transplant', ['vdso-transplant-main.c', transplant_src],
        include_directories: includes)
endif

executable('xattrify_fs', ['xattrify_fs.c'], include_directories: includes)
