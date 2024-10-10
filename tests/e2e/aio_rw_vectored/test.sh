#!/bin/sh
gcc aio_rw_vectored.c -o ./aio_rw_vectored
./aio_rw_vectored
rm test.txt
