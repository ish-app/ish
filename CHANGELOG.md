# Changelog

## Build 48
### Fixed
- Files opened from the Files app having random numbers as names, preventing them from being syntax highlighted in some apps and opening at all in other apps

## Build 47
### Fixed
- Failure to pass arguments to #! scripts
- Performance regression

## Build 46
### New
- Beta quality read/write Files app support
- File locking
- Forking and exiting is now 10x faster
- Probably more that I missed
### Fixed
- Error when running a program with more than 256 arguments
- `RLIM_INFINITY` on `RLIMIT_NOFILE` being interpreted as -1
- Zero bytes on a terminal being interpreted as newlines
- Crash when running dmesg and the log buffer is too big
- Various problems with pty semantics, to get zpty working better
- lld showing incorrect output
- Very serious problems caused by renaming directories
- Crash when dealing with a path such as /../..
- A memory corruption issue was addressed with improved memory handling.

## Build 45
### New
- pseudoterminals (in other words, you can now ssh into your phone)
- 1337 h4x0r colorscheme
- Command-K clears the screen
- bit testing instructions implemented
### Fixed
- DNS configuration not working for IPv6 servers
- [invalid utf8]
- Crash when attempting to use negative file descriptors
- Various other memory leaks and segfaults

## Build 44
### Fixed
- Ring buffer sometimes segfaulting when it overflowed
- zsh sometimes hanging in sigsuspend

## Build 42
### New
- zsh
- Background jobs (Ctrl-Z, bg, fg, etc)
- ~10% performance improvement from correctly tracking the last JIT block
- Option in settings to change launch command (also general settings reorganization)
- top runs (though it's useless since CPU usage is always displayed as 0%)
### Fixed
- Rounding error when adding 18446744073709551616 and 1.5
- Crash when trying to run top
- Selection being immediately copied to the clipboard if Speak Selection is enabled in iOS settings
- Make a test network connection on startup, to hopefully fix network permission popup not appearing on devices sold in China
- Crash when running a script with a #! line pointing to a nonexistent file
- ^C not appearing when you press Ctrl-C and it kills the program
- ^C not flushing the input buffer
- Arrow keys not working in some programs
- Emacs and Ruby summoning nasal demons after receiving a signal

## Build 40
### Fixed
- SSH not working
- Paste button being the wrong color in dark mode
- gem install eventmachine failing due to missing system calls

## Build 39
### New
- irssi
- rubygems
- fish
- traceroute (thanks @wallisch)
- mtr
- rcl and rcr are implemented, leaving gcc with no more known reasons to crash
- Enough of /proc for ps and killall to work
- epoll (you can now install python packages with dependencies!)
- Paste button (with bad icon, oh well)
### Fixed
- phpinfo() crashing the app
- tar hanging after finishing extraction
- Rare crash when starting a process due to RNG failure

## Build 37
### Fixed
- Keyboard buttons being the wrong color in dark theme

## Build 36
### New
- Emacs
- /dev/random and /dev/urandom (thanks @lunixbochs)
- Perl
- dialog
- ping (thanks @wallisch)
- Auto lock control setting
### Fixed
- I accidentally installed shadow and coreutils and everything broke. Instead of reinstalling and losing my filesystem I fixed everything.
- Arrow keys being invisible to VoiceOver, as well as a couple other VoiceOver tweaks
- Better hide keyboard button
- Stepper for font size selection
- resolv.conf not getting truncated when overwritten
- SIGWINCH with no handler installed interrupting blocking syscalls
- GCJ crashing the app (it still fails for many other reasons, oh well)

## Build 35
### New
- Server sockets
- SSH client works (apk add openssh-client)
### Fixed
- `out of memory`/`short read` error from tar, due to fork setting brk to 0
- Signals not being blocked while the signal handler is running
- Orphaned processes not being reparented to init
- Git doing strange things when it gets a signal
- /dev/tty not existing (if you're not installing from scratch, you have to run mknod /dev/tty c 5 0 to get it)
- Copy button on iOS not existing
- Segfault when trying to run a binary that requires glibc
- irb exiting with an EINVAL due to stdin not reporting that it is open in read/write mode
- ffmpeg reporting "Unknown encoder: 'copy'" on iOS due to 8-bit string compares not working

## Build 34
### New
- Enough stubs and instructions to get numpy to import successfully.
- DARK MODE
### Fixed
- Massive VM leak, if you allocated 100M with mmap only the first 16k would ever get freed. This fixes MemoryError when installing stuff with pip.
- Terminal is now correctly sized instead of overlapping with the keyboard initially.
- You can now select text without making the keyboard go away.

## Build 33
### New
- Database changed from GDBM to SQLite, it's now much more reliable. You will need to reinstall the app, otherwise you'll get an error on launch.
### Fixed
- Segfault while doing large download with pip, due to returning NULL from successful mremap. ffmpeg also had this problem.
- File provider now shows files like it's supposed to.
