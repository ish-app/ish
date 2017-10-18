project('ish', 'c',
    default_options: ['default_library=static', 'c_std=gnu11'])

log_on = get_option('log').split()
log_off = get_option('nolog').split()
foreach channel : log_on + log_off
    if log_on.contains(channel)
        add_global_arguments('-DDEBUG_' + channel + '=1', language: 'c')
    else
        add_global_arguments('-DDEBUG_' + channel + '=0', language: 'c')
    endif
endforeach

if get_option('no_crlf')
    add_global_arguments('-DNO_CRLF', language: 'c')
endif

includes = [include_directories('.')]

subdir('vdso') # ish depends on the vdso

cify = executable('tools/cify', ['tools/cify.c'], native: true)
cified_vdso = custom_target('cify-vdso',
    output: ['libvdso.so.c', 'libvdso.so.h'],
    depends: [vdso],
    command: [cify, 'vdso_data', vdso.full_path(), '@OUTPUT@'])

src = [
    'kernel/init.c',

    'kernel/calls.c',
    'kernel/user.c',
    'kernel/vdso.c',
    'kernel/process.c',
    'kernel/group.c',

    'kernel/fork.c',
    'kernel/exec.c',
    'kernel/exit.c',
    'kernel/time.c',
    'kernel/mm.c',
    'kernel/mmap.c',
    'kernel/uname.c',
    'kernel/tls.c',
    'kernel/getset.c',
    'kernel/signal.c',
    'kernel/sock.c',
    'kernel/sockaddr.c',

    'kernel/fs.c',
    'fs/stat.c',
    'fs/dir.c',
    'fs/generic.c',
    'fs/path.c',
    'fs/real.c',
    'fs/poll.c',
    'fs/adhoc.c',

    'fs/dev.c',
    'fs/tty.c',
    'fs/tty-real.c',

    'kernel/poll.c',

    'util/timer.c',

    'emu/memory.c',
    'emu/interp.c',
    'emu/modrm.c',
    'emu/debug.c',
    cified_vdso,
]

threads = dependency('threads')
librt = meson.get_compiler('c').find_library('rt', required: false)
softfloat = subproject('softfloat').get_variable('softfloat_dep')

libish = library('ish', src,
    include_directories: includes,
    dependencies: [librt, threads, softfloat])
ish = declare_dependency(
    link_with: libish,
    include_directories: includes)

# ptraceomatic et al
subdir('tools')

executable('ish', ['main.c'], dependencies: [ish, softfloat, threads])

gdb_scripts = ['ish-gdb.gdb']
foreach script : gdb_scripts
    custom_target(script,
        output: script, input: script,
        command: ['ln', '-sf', '@INPUT@', '@OUTPUT@'],
        build_by_default: true)
endforeach
