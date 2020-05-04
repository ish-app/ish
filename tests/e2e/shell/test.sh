#!/bin/sh
mknod /dev/null c 1 3 # shell uses this internally

echo builtin echo
/bin/echo real echo

# some background stuff
sleep 1000 &
bg=$!
echo started sleep in background
kill $bg
echo killed it
