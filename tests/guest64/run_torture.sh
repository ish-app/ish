#!/bin/bash
# compile tests/torture.c inside the guest, run it natively and in ish, compare
cd ~/src/ish64-roots
I="${ISH:-$HOME/src/ish64/build64r/ish} -f $PWD/alpine64-dev"
P='export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin; cd /root;'
$I /bin/sh -c 'cat > /root/torture.c' < tests/torture.c
for O in ${OPTS:-O0 O2}; do
    timeout -s KILL 500 $I /bin/sh -c "$P gcc -$O -static -o torture_$O torture.c -lm -lpthread" 2>&1 | tail -3
    $I /bin/sh -c "cat /root/torture_$O" > tests/torture_$O; chmod +x tests/torture_$O
    /usr/bin/time -f "native -$O: %e s" tests/torture_$O > tests/native_$O.txt
    /usr/bin/time -f "interp -$O: %e s" timeout -s KILL 300 $I /root/torture_$O > tests/interp_$O.txt
    diff tests/native_$O.txt tests/interp_$O.txt && echo "$O: IDENTICAL"
done
