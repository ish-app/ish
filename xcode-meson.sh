#!/bin/bash -ex
cd $SRCROOT

crossfile=cross-$(basename $TARGET_BUILD_DIR).txt
arch_args=
for arch in $ARCHS; do
    arch_args="'-arch', '$arch', $arch_args"
done
first_arch=${ARCHS%% *}
cat > $crossfile <<EOF
[binaries]
c = 'clang'
ar = 'ar'

[host_machine]
system = 'darwin'
cpu_family = '$first_arch'
cpu = '$first_arch'
endian = 'little'

[properties]
c_args = ['-arch', '$first_arch']
needs_exe_wrapper = true
EOF

if meson introspect --projectinfo $TARGET_BUILD_DIR; then
    exit
fi

export CC="$SRCROOT/no-clang-env.sh clang"
case $CONFIGURATION in
    Release)
        buildtype=release
        ;;
    *)
        buildtype=debug
        ;;
esac
meson $TARGET_BUILD_DIR -Dbuildtype=$buildtype --cross-file $crossfile
