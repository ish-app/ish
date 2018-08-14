# The VDSO gets inserted into the address space of emulated processes, so it
# needs to be compiled as an i386 ELF shared library. This requires a cross
# compiler and linker on most of the platforms that matter (Mac, 64-bit Linux,
# Raspberry Pi). Clang and LLD are easy to install on all of these platforms,
# so that's currently the only supported option (though if you have some other
# compatible toolchain you can edit this file to point to it instead.)

clang = find_program('/usr/local/opt/llvm/bin/clang', 'clang')
check_cc = find_program('check-cc.sh')
result = run_command(check_cc, clang)
if result.returncode() != 0
    message('\n' + result.stdout() + result.stderr())
    if build_machine.system() == 'darwin'
        install_msg = 'Install LLVM through Homebrew (brew install llvm)'
    elif build_machine.system() == 'linux'
        install_msg = 'Install Clang and LLD using your package manager (e.g. sudo apt install clang lld)'
    endif
    error('Could not find usable VDSO compiler. ' + install_msg)
endif
vdso_compiler = [clang, '-target', 'i386-linux', '-fuse-ld=lld']

vdso = custom_target('vdso', input: ['vdso.S', 'vdso.c', 'vdso.lds'], output: 'libvdso.so.elf',
    command: vdso_compiler + ['-o', '@OUTPUT@', '@INPUT0@', '@INPUT1@',
        '-nostdlib', '-Wl,-T,@INPUT2@', '-Wl,--hash-style,sysv', '-shared', '-fPIC']
        + get_option('vdso_c_args').split())
