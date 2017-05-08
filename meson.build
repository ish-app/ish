project('ish', 'c')
add_global_arguments('-std=gnu99', language: 'c')

includes = [include_directories('.')]

sys_src = [
    'sys/calls.c',
    'sys/exec/exec.c',
    'sys/exit.c',
    'sys/write.c',
]
emu_src = [
    'emu/memory.c',
    'emu/process.c',
    'emu/cpu.c',
    'emu/modrm.c',
    'emu/debug.c',
]

lib = library('ish', sys_src + emu_src)

executable('thingy', ['main.c'], link_with: [lib])

# testing program
executable('a.out', ['hello.c'], c_args: ['-m32'], link_args: ['-m32', '-nostdlib'])
