# ish

[![goto counter](https://img.shields.io/github/search/tbodt/ish/goto.svg)](https://github.com/tbodt/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/tbodt/ish/fuck.svg)](https://github.com/tbodt/ish/search?q=fuck)

A project to get a Linux shell running on iOS. Currently, a usermode x86 emulator for Linux.

Roadmap:

 - [x] Hello world in assembler
 - [x] Hello world with statically linked C library
 - [x] Hello world with dynamically linked C library
 - [x] Busybox shell
 - [x] Busybox vi (VIM MASTER RACE)
 - [x] Busybox login
 - [ ] Busybox getty
 - [x] Busybox wget
 - [x] Alpine package manager
 - [x] Port the thing to Darwin/iOS
 - [ ] QEMU cpu test program

# Hacking

You'll need these things to build the project:

 - Python 3
 - Ninja
 - Meson (`pip install meson`)
 - Clang and LLD (on mac, `brew install llvm`, on linux, `sudo apt install clang lld` or `sudo pacman -S clang lld` or whatever)

To set up your environment, cd to the project and run `meson build`. Then run `ninja` in the build directory to build.

Run programs with `./ish program`. If you're not sure what to run, there are some test programs in (you guessed it) `tests`. Run `ninja busybox` to download and build busybox.

To set up a self-contained Alpine linux filesystem, download the Alpine minirootfs tarball for i386 from the alpine website and run the `tools/fakefsify.py` script. Specify the minirootfs tarball as the first argument and the name of the output directory as the second argument. Then you can run things inside the Alpine filesystem with `./ish -f alpine/data /bin/login`, assuming the output directory is called `alpine`.

You can replace `ish` with `tools/ptraceomatic` to run the program in a real process and single step and compare the registers at each step. I use it for debugging. Requires 64-bit Linux 4.11 or later.

To compile the iOS app, just open the Xcode project and click run. There are scripts that should download and set up the alpine filesystem and create build directories for cross compilation and so on automatically.
