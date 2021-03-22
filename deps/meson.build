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

    liblinux = declare_dependency(
        link_with: [libish_emu],
        link_whole: [build_linux[0]],
        link_args: [
            '-sectalign', '__DATA', '__percpu_first', '1000',
            '-sectalign', '__DATA', '__tracepoints', '20',
        ],
        compile_args: ['-include', 'linux/compiler_attributes.h', '-U__weak'],
        include_directories: include_directories(
            'linux/arch/ish/include',
            'linux/arch/ish/include/generated',
            'linux/include',
            'linux/arch/ish/include/uapi',
            'linux/arch/ish/include/generated/uapi',
            'linux/include/uapi',
            'linux/include/generated/uapi',
        ))
endif
