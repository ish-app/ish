# ish

A project to get a Linux shell running on iOS. Currently, a usermode x86 emulator for Linux.

Roadmap:

 - [x] Hello world in assembler
 - [x] Hello world with statically linked C library
 - [x] Hello world with dynamically linked C library
 - [ ] Busybox shell
 - [ ] Port the thing to Darwin/iOS, release on the app store
 - [ ] QEMU cpu test program
 - [ ] Busybox wget

Build system is meson. Run programs with `./thingy program`. If you're not sure what to run, there are some test programs in (you guessed it) `tests`. Run `ninja busybox` to download and build busybox.

You can also replace `thingy` with `ptraceomatic` to run the program in a real process and single step and compare the registers at each step. I use it for debugging. Requires 64-bit Linux 4.11 or later.
