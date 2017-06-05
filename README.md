# ish

A user-mode x86 emulator for Linux.

Build system is meson. Run programs with `./thingy program`. Currently just barely enough stuff is implemented to initialize the C library. If you're not sure what to run, there are some test programs in (you guessed it) `tests`.

You can also replace `thingy` with `ptraceomatic` to run the program in a real process and single step and compare the registers at each step. I use it for debugging. Very likely will not work on anything that isn't Linux 4.11 x86_64.

Ultimate goal: A Linux shell on iOS.
