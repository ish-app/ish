# [iSH](https://ish.app)

[![Build Status](https://travis-ci.org/tbodt/ish.svg?branch=master)](https://travis-ci.org/tbodt/ish)
[![goto counter](https://img.shields.io/github/search/tbodt/ish/goto.svg)](https://github.com/tbodt/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/tbodt/ish/fuck.svg)](https://github.com/tbodt/ish/search?q=fuck)

<p align="center">
<a href="https://ish.app">
<img src="https://ish.app/assets/github-readme.png">
</a>
</p>

A project to get a Linux shell running on iOS, using usermode x86 emulation and syscall translation.

For the current status of the project, check the issues tab, and the commit logs.

You can [join the Testflight beta](https://testflight.apple.com/join/97i7KM8O) now. There's also a [Discord server](https://discord.gg/SndDh5y).

# Hacking

You'll need these things to build the project:

 - Python 3
 - Ninja
 - Yarn (only when building for iOS)
 - Meson (`pip install meson`)
 - Clang and LLD (on mac, `brew install llvm`, on linux, `sudo apt install clang lld` or `sudo pacman -S clang lld` or whatever)

To set up your environment, cd to the project and run `meson build` to create a build directory in `build`. Then cd to the build directory and run `ninja`.

To set up a self-contained Alpine linux filesystem, download the Alpine minirootfs tarball for i386 from the [Alpine website](https://alpinelinux.org/downloads/) and run the `tools/fakefsify.py` script. Specify the minirootfs tarball as the first argument and the name of the output directory as the second argument. Then you can run things inside the Alpine filesystem with `./ish -f alpine /bin/login`, assuming the output directory is called `alpine`.

You can replace `ish` with `tools/ptraceomatic` to run the program in a real process and single step and compare the registers at each step. I use it for debugging. Requires 64-bit Linux 4.11 or later.

To compile the iOS app, just open the Xcode project and click run. There are scripts that should download and set up the alpine filesystem and create build directories for cross compilation and so on automatically.

## Further setup guide

To enable local development there are a few more steps that needs to be done.

- Go to the project settings in Xcode find the "iSH" target
- Under "General" change the bundle identifier to a specific identifier for you
- Under "Capabilities" change the name of the "App Group" and remove the old app group

- Go to the "iSHFileProvider" target
- Under "General" use the same bundle identifier you created before and add `.FileProvider` to it
- Under "Capabilities" use the same name of the "App Group" as for the "iSH" target

- Go to the file `app/AppDelegate.m`
- Change the string in the function `manager containerURLForSecurityApplicationGroupIdentifier:` to your App Group name that you entered in the step before.

Congratulations! You should now have the app running!

# A note on the JIT

Possibly the most interesting thing I wrote as part of iSH is the JIT. It's not actually a JIT since it doesn't target machine code. Instead it generates an array of pointers to functions called gadgets, and each gadget ends with a tailcall to the next function; like the threaded code technique used by some Forth interpreters. The result is a speedup of roughly 3-5x compared to pure emulation.

Unfortunately, I made the decision to write nearly all of the gadgets in assembly language. This was probably a good decision with regards to performance (though I'll never know for sure), but a horrible decision with regards to readability, maintainability, and my sanity. The amount of bullshit I've had to put up with from the compiler/assembler/linker is insane. It's like there's a demon in there that makes sure my code is sufficiently deformed, and if not, makes up stupid reasons why it shouldn't compile. In order to stay sane while writing this code, I've had to ignore best practices in code structure and naming. You'll find macros and variables with such descriptive names as `ss` and `s` and `a`. Assembler macros nested beyond belief. And to top it off, there are almost no comments.

So a warning: Long-term exposure to this code may cause loss of sanity, nightmares about GAS macros and linker errors, or any number of other debilitating side effects. This code is known to the State of California to cause cancer, birth defects, and reproductive harm.
