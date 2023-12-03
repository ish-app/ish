#!/bin/bash

# Try to figure out the user's PATH to pick up their installed utilities.
export PATH="$PATH:$(sudo -u "$USER" -i printenv PATH)"

mkdir -p "$MESON_BUILD_DIR"
cd "$MESON_BUILD_DIR"

config=$(meson introspect --buildoptions)
if [[ $? -ne 0 ]]; then
    export CC_FOR_BUILD="env -u SDKROOT -u IPHONEOS_DEPLOYMENT_TARGET xcrun clang"
    export CC="$CC_FOR_BUILD" # compatibility with meson < 0.54.0
    crossfile=cross.txt
    for arch in $ARCHS; do
        arch_args="'-arch', '$arch', $arch_args"
    done
    arch_args="${arch_args%%, }"
    meson_arch=${ARCHS%% *}
    case "$meson_arch" in
        arm64) meson_arch=aarch64 ;;
    esac
    cat | tee $crossfile <<-EOF
    [binaries]
    c = 'clang'
    ar = 'ar'

    [host_machine]
    system = 'darwin'
    cpu_family = '$meson_arch'
    cpu = '$meson_arch'
    endian = 'little'

    [built-in options]
    c_args = [$arch_args]
    
    [properties]
    needs_exe_wrapper = true
EOF
    (set -x; meson $SRCROOT --cross-file $crossfile) || exit $?
    config=$(meson introspect --buildoptions)
fi

buildtype=debug
b_ndebug=false
if [[ $CONFIGURATION == Release ]]; then
    buildtype=debugoptimized
fi
b_sanitize=none
if [[ -n "$ENABLE_ADDRESS_SANITIZER" ]]; then
    b_sanitize=address
fi
log=$ISH_LOG
log_handler=$ISH_LOGGER
kernel=ish
if [[ -n "$ISH_KERNEL" ]]; then
    kernel=$ISH_KERNEL
fi
kconfig=""
for var in buildtype log b_ndebug b_sanitize log_handler kernel kconfig; do
    old_value=$(python3 -c "import sys, json; v = next(x['value'] for x in json.load(sys.stdin) if x['name'] == '$var'); print(str(v).lower() if isinstance(v, bool) else ','.join(v) if isinstance(v, list) else v)" <<< $config)
    new_value=${!var}
    if [[ $old_value != $new_value ]]; then
        set -x; meson configure "-D$var=$new_value"
    fi
done
