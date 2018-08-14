# the vdso
# it must be compiled with clang and lld, because they are capable of generating elf i386 binaries on all platforms
# I can't use shared_library for this because then I have to use the same
# compiler and options as the rest of the project which prevents me from using
# sanitizers and afl
vdso_compiler = find_program('clang')
vdso = custom_target('vdso', input: ['vdso.S', 'vdso.c', 'vdso.lds'], output: 'libvdso.so.elf',
    command: [vdso_compiler, '-target', 'i386-linux', '-fuse-ld=lld',
        '-o', '@OUTPUT@', '@INPUT0@', '@INPUT1@',
        '-nostdlib', '-Wl,-T,@INPUT2@', '-Wl,--hash-style,sysv', '-shared', '-fPIC']
        + get_option('vdso_c_args').split())
