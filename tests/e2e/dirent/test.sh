#!/bin/sh
gcc dirent.c -o ./dirent

mkdir t t/sub
touch t/file
ln -s file t/link
mkfifo t/fifo
./dirent t

./dirent /proc self cpuinfo
