#!/bin/sh
if [ ! -e /dev/null ]; then
    mknod /dev/null c 1 3 # shell uses this internally
fi

echo builtin echo
/bin/echo real echo

# some background stuff
sleep 1000 &
bg=$!
echo started sleep in background
kill $bg
echo killed it
