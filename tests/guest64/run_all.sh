#!/bin/bash
# Full regression for the 64-bit guest. Usage: tests/run_all.sh [x86|arm]
# Uses fresh copies of the roots, so nothing here changes alpine64-clean/-dev.
cd ${ROOTS:-~/src/ish64-roots}
which=${1:-x86}
if [ $which = arm ]; then ISHBIN="qemu-aarch64 $HOME/src/ish64/build64arm/ish"; else ISHBIN="${ISHBIN:-$HOME/src/ish64/build64r/ish}"; fi
D=$PWD/alpine64-dev
fails=0
ok() { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fails=$((fails+1)); }
# 1. shell checks on a fresh copy of the clean root
W=$PWD/regress-clean-$$; cp -a alpine64-clean $W
r=$(ISH="$ISHBIN" ROOT=$W ./run_checks.sh | tail -1); [ "$r" = "64-bit checks: 15 passed, 0 failed" ] && ok "shell checks ($r)" || bad "shell checks ($r)"
# 2. programs whose output must equal the native run of the same binary
for t in torture_O0 torture_O2 simd nan notify; do
    n=tests/$t.native; [ -f $n ] || n=tests/native_${t#torture_}.txt
    b=${t}; [ -f tests/$t.bin ] || b=$t
    timeout -s KILL 900 $ISHBIN -f $D /root/$b > tests/$t.$which.out 2>&1
    cmp -s $n tests/$t.$which.out && ok "$t identical to native" || bad "$t differs from native"
done
# 3. programs that print OK/FAIL lines
for t in sys as64 ptrace smc; do
    timeout -s KILL 300 $ISHBIN -f $D /root/$t > tests/$t.$which.out 2>&1
    n=$(grep -c "OK$" tests/$t.$which.out); f=$(grep -Evc "OK$|refused \(ish limit" tests/$t.$which.out)
    [ $f = 0 ] && [ $n -gt 5 ] && ok "$t ($n checks)" || bad "$t ($f not OK of $((n+f)))"
done
r=$(timeout -s KILL 120 $ISHBIN -f $D /root/sig 2>&1); [ "$r" = "signals delivered: yes, long double equal: 1, double equal: 1" ] && ok "signal handler keeps FP state" || bad "sig: $r"
# 4. Python
P=$PWD/regress-py-$$; cp -a alpine64-python $P
r=$(timeout -s KILL 300 $ISHBIN -f $P /bin/sh -c "$(cat py_only.sh)" 2>&1 | md5sum | cut -c1-8)
[ "$r" = "$(cat tests/py_only.md5 2>/dev/null)" ] && ok "python script" || bad "python script (md5 $r)"
# 5. system behaviors that Python's own OS tests exposed
n=$(timeout -s KILL 120 $ISHBIN -f $P /usr/bin/python3 - < tests/py/os_misc.py 2>&1 | grep -c "True$")
[ "$n" = 30 ] && ok "OS behaviors ($n checks)" || bad "OS behaviors ($n of 30)"
echo "$which: $fails failed"
echo "temporary roots left behind (delete them): $W $P"
