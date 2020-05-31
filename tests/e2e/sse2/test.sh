#!/bin/sh
gcc -msse2 movaps.c -o test_movaps
echo movaps
./test_movaps

echo movss
gcc -msse2 movss.c -o test_movss
./test_movss

echo xorps
gcc -msse2 xorps.c -o test_xorps
./test_xorps

echo psllq
gcc -msse2 psllq.c -o test_psllq
./test_psllq

echo psrlq
gcc -msse2 psrlq.c -o test_psrlq
./test_psrlq

echo paddq
gcc -msse2 paddq.c -o test_paddq
./test_paddq
