#!/bin/sh
# -no-pie because the test contains non-position-independent inline asm
# silence a few warnings that I can't be bothered to fix
gcc qemu-test.c -o qemu-test -msse2 -no-pie -Wno-attributes -Wno-format
./qemu-test
