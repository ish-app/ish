#!/bin/bash
cd tests
file=busybox-1.26.2
curl -LO http://busybox.net/downloads/$file.tar.bz2
tar -xf $file.tar.bz2
cd $file
export CFLAGS=-m32 LDFLAGS=-m32
make defconfig
make
cp busybox ..
