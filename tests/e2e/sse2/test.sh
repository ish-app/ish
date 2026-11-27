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

echo minmaxdiv
gcc -msse2 minmaxdiv.c -o test_minmaxdiv -lm
./test_minmaxdiv

echo cmpps
gcc -msse2 cmpps.c -o test_cmpps -lm
./test_cmpps

echo cvt
gcc -msse2 cvt.c -o test_cvt -lm
./test_cvt

echo movmskps
gcc -msse2 movmskps.c -o test_movmskps -lm
./test_movmskps

echo movhlps
gcc -msse2 movhlps.c -o test_movhlps -lm
./test_movhlps
