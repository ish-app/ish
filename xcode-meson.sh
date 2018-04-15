#!/bin/bash -x
mkdir -p $MESON_BUILD_DIR
cd $MESON_BUILD_DIR

config=$(meson introspect --buildoptions)
if [[ $? -ne 0 ]]; then
    export CC="env -u SDKROOT -u IPHONEOS_DEPLOYMENT_TARGET clang"
    crossfile=cross.txt
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
    meson $SRCROOT --cross-file $crossfile
fi

buildtype=debug
b_ndebug=false
if [[ $CONFIGURATION == Release ]]; then
    buildtype=debugoptimized
    b_ndebug=true
fi
log=$ISH_LOG
log_handler=nslog
for var in buildtype log b_ndebug log_handler; do
    old_value=$(jq -r ".[] | select(.name==\"$var\") | .value" <<< $config)
    new_value=${!var}
    if [[ $old_value != $new_value ]]; then
        meson configure -D$var=$new_value
    fi
done
