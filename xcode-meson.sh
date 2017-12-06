#!/bin/bash -ex
crossfile=$SRCROOT/cross-$(basename $TARGET_BUILD_DIR).txt
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

cd $TARGET_BUILD_DIR
if ! meson introspect --projectinfo; then
    export CC="$SRCROOT/no-clang-env.sh clang"
    meson $SRCROOT --cross-file $crossfile
fi

buildtype=debug
if [[ $CONFIGURATION == Release ]]; then
    buildtype=release
fi
log=$ISH_LOG
config=$(meson introspect --buildoptions)
for var in buildtype log; do
    old_value=$(jq -r ".[] | select(.name==\"$var\") | .value" <<< $config)
    new_value=${!var}
    if [[ $old_value != $new_value ]]; then
        meson configure -D$var=$new_value
    fi
done
