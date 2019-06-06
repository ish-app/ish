#!/bin/sh
apk update > verbose.txt
apk add gcc libc-dev >> verbose.txt
gcc test_fpu.c -o ./test_fpu
./test_fpu
