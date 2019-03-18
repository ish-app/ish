#!/bin/bash -x
mkdir -p $MESON_BUILD_DIR
cd $MESON_BUILD_DIR

config=$(meson introspect --buildoptions)
if [[ $? -ne 0 ]]; then
    export CC="env -u SDKROOT -u IPHONEOS_DEPLOYMENT_TARGET clang"
    crossfile=cross.txt
    echo $ARCHS
    arch=${ARCHS%% *}
    meson_arch=$arch
    case "$arch" in
        arm64)
            meson_arch=aarch64
            ;;
    esac
    cat > $crossfile <<EOF
[binaries]
c = 'clang'
ar = 'ar'

[host_machine]
system = 'darwin'
cpu_family = '$meson_arch'
cpu = '$meson_arch'
endian = 'little'

[properties]
c_args = ['-arch', '$arch']
needs_exe_wrapper = true
EOF
    meson $SRCROOT --cross-file $crossfile || exit $?
    config=$(meson introspect --buildoptions)
fi

buildtype=debug
b_ndebug=false
if [[ $CONFIGURATION == Release ]]; then
    buildtype=debugoptimized
    b_ndebug=true
fi
b_sanitize=none
log=$ISH_LOG
log_handler=nslog
jit=true
for var in buildtype log b_ndebug b_sanitize log_handler jit; do
    old_value=$(python3 -c "import sys, json; v = next(x['value'] for x in json.load(sys.stdin) if x['name'] == '$var'); print(str(v).lower() if isinstance(v, bool) else v)" <<< $config)
    new_value=${!var}
    if [[ $old_value != $new_value ]]; then
        meson configure "-D$var=$new_value"
    fi
done
