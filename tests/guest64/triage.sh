#!/bin/bash
# triage.sh <label> <ish binary> <root to copy> : run each Python OS-level test module alone, one line per module
cd ~/src/ish64-roots
label=$1; bin=$2; src=$3
out=tests/triage-$label.txt; : > $out
for m in ${MODULES:-test_os test_posix test_subprocess test_signal test_epoll test_poll test_pty test_fcntl test_glob test_resource test_shutil test_mmap test_selectors test_select test_socket test_eventfd}; do
    W=$PWD/triage-$label-$m; cp -a $src $W
    timeout -s KILL 420 $bin -f $W /bin/sh -c "export PATH=/usr/bin:/bin HOME=/root; cd /tmp; python3 -m test --timeout 300 -v $m 2>&1" > tests/triage-$label-$m.log 2>&1
    rc=$?
    res=$(grep -E "^Total tests:" tests/triage-$label-$m.log | tail -1)
    verdict=$(grep -E "^Result:" tests/triage-$label-$m.log | tail -1)
    [ $rc = 137 ] && verdict="Result: KILLED after 420 s"
    [ -z "$verdict" ] && verdict="Result: iSH exited rc=$rc without a summary"
    echo "$m | $verdict | $res" >> $out
    rm -rf "$W"
done
echo done >> $out
