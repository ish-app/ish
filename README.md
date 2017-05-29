# ish

A user-mode x86 emulator for Linux.

Build system is meson. Run programs with `./thingy program`. Currently, there is no guarantee this will work for anything other than a hello world program written in assembler.

You can also replace `thingy` with `ptraceomatic` to run the program in a real process and single step and compare the registers at each step. I use it for debugging.

Ultimate goal: A Linux shell on iOS.
