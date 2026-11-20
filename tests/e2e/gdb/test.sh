#!/bin/sh
gcc -g -O0 test_gdb.c -o ./test_gdb
gdb -q -batch \
    -ex 'break square' -ex run -ex 'print x' \
    -ex continue -ex 'print x' \
    -ex delete -ex continue \
    ./test_gdb 2>&1 | grep -E '^Breakpoint 1,|^\$|^total=|^Program|exited' | sed 's/ (process [0-9]*)//'
