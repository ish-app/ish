project('ish', 'c',
    default_options: ['default_library=static', 'c_std=gnu11', 'jit=true', 'warning_level=2'])

log_on = get_option('log').split()
log_off = get_option('nolog').split()
foreach channel : log_on + log_off
    if log_on.contains(channel)
        add_project_arguments('-DDEBUG_' + channel + '=1', language: 'c')
    else
        add_project_arguments('-DDEBUG_' + channel + '=0', language: 'c')
    endif
endforeach
add_project_arguments('-DLOG_HANDLER_' + get_option('log_handler').to_upper() + '=1', language: 'c')

if get_option('no_crlf')
    add_project_arguments('-DNO_CRLF', language: 'c')
endif

if get_option('jit')
    add_project_arguments('-DJIT=1', language: 'c')
endif

add_project_arguments('-Wno-switch', language: 'c')

includes = [include_directories('.')]

cc = meson.get_compiler('c')
threads = dependency('threads')
librt = cc.find_library('rt', required: false)
libm = cc.find_library('m', required: false)

subdir('vdso') # ish depends on the vdso

offsets = custom_target('offsets',
    output: 'cpu-offsets.h', input: 'jit/offsets.c', depfile: 'cpu-offsets.h.d',
    command: [find_program('tools/staticdefine.sh'), '@OUTDIR@/compile_commands.json', '@INPUT@', '@OUTPUT@', '@DEPFILE@'])

src = [
    'kernel/init.c',
    'kernel/errno.c',

    'kernel/calls.c',
    'kernel/user.c',
    'kernel/vdso.c', vdso,
    'kernel/task.c',
    'kernel/group.c',
    'kernel/log.c',

    'kernel/fork.c',
    'kernel/exec.c',
    'kernel/exit.c',
    'kernel/time.c',
    'kernel/mmap.c',
    'kernel/uname.c',
    'kernel/tls.c',
    'kernel/futex.c',
    'kernel/getset.c',
    'kernel/signal.c',
    'kernel/resource.c',
    'kernel/random.c',
    'kernel/misc.c',
    'kernel/eventfd.c',
    'kernel/ipc.c',

    'kernel/fs.c',
    'kernel/fs_info.c',
    'fs/mount.c',
    'fs/fd.c',
    'fs/inode.c',
    'fs/stat.c',
    'fs/dir.c',
    'fs/generic.c',
    'fs/path.c',
    'fs/real.c',
    'fs/fake.c',
    'fs/fake-rebuild.c',
    'fs/fake-migrate.c',

    'fs/proc.c',
    'fs/proc/entry.c',
    'fs/proc/root.c',
    'fs/proc/pid.c',

    'fs/dyndev.c',

    'fs/adhoc.c',
    'fs/sock.c',
    'fs/pipe.c',
    'fs/sockrestart.c',
    'fs/lock.c',

    'fs/dev.c',
    'fs/mem.c',
    'fs/tty.c',
    'fs/tty-real.c',
    'fs/pty.c',

    'fs/poll.c',
    'kernel/poll.c',
    'kernel/epoll.c',

    'util/timer.c',
    'util/sync.c',
    'util/fifo.c',

    'emu/memory.c',
    'emu/tlb.c',
    'emu/fpu.c',
    'emu/sse.c',
    'emu/float80.c',

    'platform/' + host_machine.system() + '.c',
]
if get_option('jit')
    gadgets = 'jit/gadgets-' + host_machine.cpu_family()
    src += [
        'jit/jit.c',
        'jit/gen.c',
        'jit/helpers.c',
        gadgets+'/entry.S',
        gadgets+'/memory.S',
        gadgets+'/control.S',
        gadgets+'/math.S',
        gadgets+'/bits.S',
        gadgets+'/string.S',
        gadgets+'/misc.S',
        offsets,
    ]
else
    src += [
        'emu/interp.c',
    ]
endif

sqlite3 = cc.find_library('sqlite3')

dependencies = [librt, libm, threads, sqlite3]

libish = library('ish', src,
    include_directories: includes)
ish = declare_dependency(
    link_with: libish,
    dependencies: dependencies,
    include_directories: includes)

# ptraceomatic et al
subdir('tools')

if not meson.is_cross_build()
    executable('ish', ['main.c'], dependencies: ish)
endif

gdb_scripts = ['ish-gdb.gdb']
foreach script : gdb_scripts
    custom_target(script,
        output: script, input: script,
        command: ['ln', '-sf', '@INPUT@', '@OUTPUT@'],
        build_by_default: true)
endforeach

if not meson.is_cross_build()
    # test for floating point library
    float80_test = executable('float80_test', ['emu/float80.c', 'emu/float80-test.c'], dependencies: [libm])
    test('float80', float80_test, should_fail: true)
endif

e2e_test = find_program('tests/e2e/e2e.bash')
test('e2e', e2e_test, args: ['-y'], timeout: 120)
