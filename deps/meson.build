if get_option('kernel') == 'linux'
    build_linux = custom_target('linux',
        command: [
            find_program('linux-build.sh'),
            '@OUTPUT0@',
            '@SOURCE_ROOT@/deps/linux',
            '@OUTPUT1@',
            '@DEPFILE@',

            # hack to find the right directories because meson is bad
             '-I'+meson.project_source_root(), # ISH_CFLAGS
            meson.project_build_root() + '/libish_emu.a', # LIB_ISH_EMU
        ],
        depend_files: ['makefilter.py'],
        depends: [libish_emu],
        console: true,
        depfile: 'linux.d',
        output: ['liblinux.a', 'linux'])

    # Create empty include directories so the liblinux declaration doesn't throw File Not Found
    run_command('mkdir', [
        '-p',
        meson.current_build_dir()+'/linux/include/generated/uapi',
        meson.current_build_dir()+'/linux/arch/ish/include/generated/uapi'])

    linux_headers = declare_dependency(
        include_directories: [include_directories(
            'linux/arch/ish/include',
            'linux/arch/ish/include/generated',
            'linux/arch/ish/include/ishemu',
            'linux/include',
            'linux/arch/ish/include/uapi',
            'linux/arch/ish/include/generated/uapi',
            'linux/include/uapi',
            'linux/include/generated/uapi',
        )] + includes,
        compile_args: [
            # '-nostdlibinc',
            '-include', 'linux/compiler_attributes.h',
            '-include', 'linux/kconfig.h',
            '-D__KERNEL__', '-U__weak',
            # Warnings triggered by kernel headers. Some of these
            # warn of UB, but I feel all right with silencing them
            # because if clang does something to break Linux
            # there'll be hell to pay.
            '-Wno-unused-parameter',
            '-Wno-sign-compare',
            '-Wno-implicit-fallthrough',
            '-Wno-pointer-sign',
            '-Wno-null-pointer-arithmetic',
        ],
        link_with: [build_linux[0]],
        sources: [build_linux[1]],
    )

    if host_machine.system() == 'darwin'
        liblinux_link_args = [
            '-sectalign', '__DATA', '__percpu_first', '1000',
            '-sectalign', '__DATA', '__tracepoints', '20',
        ]
    elif host_machine.system() == 'linux'
        liblinux_link_args = [
            '-Wl,-T', meson.current_build_dir()+'/linux/arch/ish/kernel/vmlinux.lds'
        ]
    endif
    liblinux = declare_dependency(
        link_with: [libish_emu],
        link_whole: [build_linux[0]],
        sources: [build_linux[1]],
        link_args: liblinux_link_args,
    )
endif
