#!/bin/bash -ex
cd $SRCROOT

crossfile=cross-$(basename $BUILT_PRODUCTS_DIR).txt
cat > $crossfile <<EOF
[binaries]
c = 'clang'
ar = 'ar'

[host_machine]
system = 'ios'
cpu_family = '$ARCHS'
cpu = '$ARCHS'
endian = 'little'

[properties]
c_args = ['-arch', '$ARCHS']
needs_exe_wrapper = true
EOF

export CC="$SRCROOT/no-clang-env.sh clang"
if ! meson introspect --projectinfo; then
    meson $BUILT_PRODUCTS_DIR --cross-file $crossfile
fi
