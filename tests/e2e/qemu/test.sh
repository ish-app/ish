#!/bin/sh
# -no-pie because the test contains non-position-independent inline asm
gcc qemu-test.c -no-pie -o qemu-test
./qemu-test
