#!/bin/bash
# guest_cc.sh name [gcc flags]: compile tests/name.c inside the dev guest and print the output of running it
cd ~/src/ish64-roots
I="${ISH:-$HOME/src/ish64/build64r/ish} -f $PWD/alpine64-dev"
n=$1; shift
$I /bin/sh -c "cat > /root/$n.c" < tests/$n.c
timeout -s KILL 300 $I /bin/sh -c "export PATH=/usr/bin:/bin; cd /root; gcc ${*:--O2} -static -o $n $n.c -lm -lpthread" 2>&1 | head -20
$I /bin/sh -c "cat /root/$n" > tests/$n.bin; chmod +x tests/$n.bin
timeout -s KILL ${LIMIT:-120} $I /root/$n
