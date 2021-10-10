#!/bin/sh
gcc aio_rw.c -o ./aio_rw
./aio_rw
rm test.txt