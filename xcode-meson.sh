#!/bin/bash -ex
cd $SRCROOT

crossfile=cross-$(basename $TARGET_BUILD_DIR).txt
cat > $crossfile <<EOF
[binaries]
c = 'clang'
ar = 'ar'

[host_machine]
system = 'darwin'
cpu_family = '$ARCHS'
cpu = '$ARCHS'
endian = 'little'

[properties]
c_args = ['-arch', '$ARCHS']
needs_exe_wrapper = true
EOF

export CC="$SRCROOT/no-clang-env.sh clang"
if ! meson introspect --projectinfo; then
    meson $TARGET_BUILD_DIR --cross-file $crossfile
fi
