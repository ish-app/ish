transplant_src = [
    'vdso-transplant.c',
    'ptutil.c',
    cified_vdso,
]
executable('ptraceomatic', ['ptraceomatic.c', transplant_src], dependencies: [ish])

# tools for messing with vdsos
executable('vdso-dump', ['vdso-dump.c'], c_args: ['-m32'], link_args: ['-m32'])
executable('vdso-transplant', ['vdso-transplant-main.c', transplant_src],
    include_directories: includes)

executable('sigframe', ['sigframe.S'], c_args: ['-m32'], link_args: ['-m32', '-nostdlib'])
executable('xsave', ['xsave.S'], c_args: ['-m32'], link_args: ['-m32', '-nostdlib'])
