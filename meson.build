project('ish', 'c')
add_global_arguments('-std=gnu99', language: 'c')
if meson.get_compiler('c').get_id() == 'clang'
    lto = '-flto=thin'
else
    lto = '-flto'
endif
add_global_link_arguments(lto, language: 'c')

includes = [include_directories('.')]

sys_src = [
    'sys/exec/exec.c',
]
emu_src = [
    'emu/memory.c',
    'emu/process.c',
    'emu/cpu.c',
    'emu/modrm.c',
]

lib = library('ish', sys_src + emu_src)

executable('thingy', ['main.c'], link_with: [lib])

# testing program
executable('a.out', ['hello.c'], c_args: ['-m32'], link_args: ['-m32', '-nostdlib'])
