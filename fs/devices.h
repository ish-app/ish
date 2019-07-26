#ifndef FS_DEVICES_H
#define FS_DEVICES_H

// losely based on devices.txt from linux

// --- memory devices ---
#define MEM_MAJOR 1
// /dev/null
#define DEV_NULL_MINOR 3
// /dev/zero
#define DEV_ZERO_MINOR 5
// /dev/full
#define DEV_FULL_MINOR 7
// /dev/random
#define DEV_RANDOM_MINOR 8
// /dev/urandom
#define DEV_URANDOM_MINOR 9

// --- tty devices ---
// /dev/ttyX where X is minor
#define TTY_CONSOLE_MAJOR 4

// --- alternate tty devices ---
#define TTY_ALTERNATE_MAJOR 5
// /dev/tty
#define DEV_TTY_MINOR 0
// /dev/console
#define DEV_CONSOLE_MINOR 1
// /dev/ptmx
#define DEV_PTMX_MINOR 2

// --- pseudo tty devices ---
#define TTY_PSEUDO_MASTER_MAJOR 128
#define TTY_PSEUDO_SLAVE_MAJOR 136

// --- dynamic devices ---
#define DYN_DEV_MAJOR 240

// /dev/clipboard
#define DEV_CLIPBOARD_MINOR 0

#endif
