project('ish', 'c', default_options: ['default_library=static'])
add_global_arguments('-std=gnu99', language: 'c')

includes = [include_directories('.')]

subdir('vdso') # ish depends on the vdso

cify = executable('tools/cify', ['tools/cify.c'])
cified_vdso = custom_target('cify-vdso',
    output: ['libvdso.so.c', 'libvdso.so.h'],
    depends: [vdso],
    command: [cify, 'vdso_data', vdso.full_path(), '@OUTPUT@'])

sys_src = [
    'sys/calls.c',
    'sys/user.c',
    'sys/exec/exec.c',

    'sys/exit.c',
    'sys/mm.c',
    'sys/mmap.c',
    'sys/uname.c',
    'sys/tls.c',
    'sys/fs.c',

    'fs/fs.c',
    'fs/path.c',
    'fs/real.c',
]
emu_src = [
    'emu/memory.c',
    'emu/process.c',
    'emu/cpu.c',
    'emu/modrm.c',
    'emu/debug.c',
    cified_vdso,
]

ish = declare_dependency(
    link_with: library('ish', sys_src + emu_src + ['setup.c'], include_directories: includes),
    include_directories: includes)

# testing programs
subdir('tests')
# ptraceomatic et al
subdir('tools')

executable('ish', ['main.c'], dependencies: [ish])
