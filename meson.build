project('ish', 'c',
    default_options: ['default_library=static', 'c_std=gnu99'])

if get_option('debug_log')
    add_global_arguments('-DDEBUG_LOG=1', language: 'c')
endif

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
    'sys/exec.c',
    'sys/vdso.c',

    'sys/exit.c',
    'sys/time.c',
    'sys/mm.c',
    'sys/mmap.c',
    'sys/uname.c',
    'sys/tls.c',
    'sys/getset.c',
    'sys/signal.c',

    'sys/fs.c',
    'sys/fs/stat.c',
    'sys/fs/dir.c',
    'sys/fs/generic.c',
    'sys/fs/path.c',
    'sys/fs/pathname.c',
    'sys/fs/real.c',
]
emu_src = [
    'emu/memory.c',
    'emu/process.c',
    'emu/interp.c',
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
