#!/bin/sh
gcc -O2 -pthread cmpxchg8b.c -o cmpxchg8b
./cmpxchg8b
