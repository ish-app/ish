#!/bin/bash
# 64-bit guest smoke checks: each line is "command ==> expected last line of output"
I="${ISH:-$HOME/src/ish64/build64i/ish} -f ${ROOT:-$HOME/src/ish64-roots/alpine64-clean}"
pass=0; fail=0
while IFS= read -r line; do
    t=${line%% ==> *}; want=${line##* ==> }
    got=$(timeout -s KILL 60 $I /bin/sh -c "$t" 2>&1 | tail -1)
    if [ "$got" = "$want" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $t => $got (want $want)"; fi
done <<'EOF'
uname -m ==> x86_64
echo a; echo b ==> b
uname -s; echo after ==> after
echo piped | tr a-z A-Z ==> PIPED
ls /bin | wc -l ==> 82
sleep 1; echo slept ==> slept
x=$(echo sub); echo "got $x" ==> got sub
echo hi > /tmp/f; cat /tmp/f; rm /tmp/f ==> hi
trap "echo trapped" USR1; kill -USR1 $$; echo after-trap ==> after-trap
awk "BEGIN{print 2^40, 3.14159*2}" ==> 1099511627776 6.28318
sha256sum /bin/busybox | cut -c1-16 ==> 672ec674aa77a969
seq 1 2000 | sort -rn | head -1 ==> 2000
i=0; while [ $i -lt 20 ]; do i=$((i+1)); true & done; wait; echo $i ==> 20
dd if=/dev/zero bs=1024 count=2048 2>/dev/null | wc -c ==> 2097152
echo 3 > /tmp/n; (read n < /tmp/n; echo $((n*2))) ==> 6
EOF
echo "64-bit checks: $pass passed, $fail failed"
