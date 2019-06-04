#!/bin/sh
echo Hello, sh!

apk update > verbose.txt
apk add python2 python3 >> verbose.txt
python test_python2.py
python3 test_python3.py

apk add gcc libc-dev >> verbose.txt
gcc test_c.c -o ./hello_c
./hello_c
