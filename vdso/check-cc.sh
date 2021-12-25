#!/bin/sh
cc=$1
test_c=$(mktemp)
cat > $test_c <<END
#if !defined(__i386__) && !defined(__ELF__)
#error "__i386__ or __ELF__ is not defined"
#endif
END
cmd="$cc -target i386-linux -fuse-ld=lld -shared -nostdlib -x c $test_c -o /dev/null"
echo $ $cmd
$cmd
status=$?
exit $status
