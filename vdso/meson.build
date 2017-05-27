# the vdso
vdso = shared_library('vdso', ['vdso.S', 'vdso.c', 'note.S'], c_args: ['-m32'],
    link_args: ['-m32', '-nostdlib', '-Wl,-T,../vdso/vdso.lds'],
    link_depends: ['vdso.lds'])
