# the vdso
# I can't use shared_library for this because then I have to use the same
# compiler and options as the rest of the project which prevents me from using
# sanitizers and afl
vdso_compiler = find_program('../gcc-docker/docker-multilib-gcc')
vdso = custom_target('vdso', input: ['vdso.S', 'vdso.c', 'vdso.lds'], output: 'libvdso.so',
    command: [vdso_compiler, '-o', '@OUTPUT@', '@INPUT0@', '@INPUT1@',
        '-m32', '-nostdlib', '-Wl,-T,@INPUT2@', '-Wl,--hash-style,sysv', '-shared', '-fPIC']
        + get_option('vdso_c_args').split())
