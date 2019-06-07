#!/bin/sh
echo Hello, sh!

python test_python2.py
python3 test_python3.py

gcc test_c.c -o ./hello_c
./hello_c
