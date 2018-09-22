# [iSH](https://ish.app)

[![Build Status](https://travis-ci.org/tbodt/ish.svg?branch=master)](https://travis-ci.org/tbodt/ish)
[![goto counter](https://img.shields.io/github/search/tbodt/ish/goto.svg)](https://github.com/tbodt/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/tbodt/ish/fuck.svg)](https://github.com/tbodt/ish/search?q=fuck)

A project to get a Linux shell running on iOS, using usermode x86 emulation and syscall translation.

For the current status of the project, check the [Emulation project](https://github.com/tbodt/ish/projects/3) and the commit logs.

# Hacking

You'll need these things to build the project:

 - Python 3
 - Ninja
 - Meson (`pip install meson`)
 - Clang and LLD (on mac, `brew install llvm`, on linux, `sudo apt install clang lld` or `sudo pacman -S clang lld` or whatever)

To set up your environment, cd to the project and run `meson build` to create a build directory in `build`. Then cd to the build directory and run `ninja`.

To set up a self-contained Alpine linux filesystem, download the Alpine minirootfs tarball for i386 from the alpine website and run the `tools/fakefsify.py` script. Specify the minirootfs tarball as the first argument and the name of the output directory as the second argument. Then you can run things inside the Alpine filesystem with `./ish -f alpine/data /bin/login`, assuming the output directory is called `alpine`.

You can replace `ish` with `tools/ptraceomatic` to run the program in a real process and single step and compare the registers at each step. I use it for debugging. Requires 64-bit Linux 4.11 or later.

To compile the iOS app, just open the Xcode project and click run. There are scripts that should download and set up the alpine filesystem and create build directories for cross compilation and so on automatically.

# A note on the JIT

Possibly the most interesting thing I wrote as part of iSH is the JIT. It's not actually a JIT since it doesn't target machine code. Instead it generates an array of pointers to functions called gadgets, and each gadget ends with a tailcall to the next function; like the threaded code technique used by some Forth interpreters. The result is a speedup of roughly 3-5x compared to pure emulation.

Unfortunately, I made the decision to write nearly all of the gadgets in assembly language. This was probably a good decision with regards to performance (though I'll never know for sure), but a horrible decision with regards to readability, maintainability, and my sanity. The amount of bullshit I've had to put up with from the compiler/assembler/linker is insane. It's like there's a demon in there that makes sure my code is sufficiently deformed, and if not, makes up stupid reasons why it shouldn't compile. In order to stay sane while writing this code, I've had to ignore best practices in code structure and naming. You'll find macros and variables with such descriptive names as `ss` and `s` and `a`. Assembler macros nested beyond belief. And to top it off, there are almost no comments.

So a warning: Long-term exposure to this code may cause loss of sanity, nightmares about GAS macros and linker errors, or any number of other debilitating side effects. This code is known to the State of California to cause cancer, birth defects, and reproductive harm.
