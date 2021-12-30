#!/bin/bash -e
output="$1"
srctree="$2"
objtree="$3"
depfile="$4"
export ARCH=ish

# https://stackoverflow.com/a/3572105/1455016
realpath() {
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

makeargs=()
if [[ -n "$HOSTCC" ]]; then
    makeargs+=("HOSTCC=$HOSTCC")
fi
if [[ -n "$CC" ]]; then
    makeargs+=("CC=$CC")
fi
makeargs+=("LLVM_IAS=1")

mkdir -p "$objtree"
export ISH_MESON_VARS="$(realpath "$objtree/meson_vars.mk")"
cat >"$ISH_MESON_VARS" <<END
export ISH_CFLAGS = $ISH_CFLAGS
export LIB_ISH_EMU = $LIB_ISH_EMU
END

defconfig=app_defconfig
if [[ "$srctree/arch/ish/configs/$defconfig" -nt "$objtree/.config" ]]; then
    make -C "$srctree" O="$(realpath "$objtree")" "${makeargs[@]}" "$defconfig"
fi

make -C "$objtree" "${makeargs[@]}" syncconfig

case "$(uname)" in
    Darwin) cpus=$(sysctl -n hw.ncpu) ;;
    Linux) cpus=$(nproc) ;;
    *) cpus=1 ;;
esac

make -C "$objtree" -j "$cpus" "${makeargs[@]}" --debug=v | tee "/tmp/log" | "$srctree/../makefilter.py" "$depfile" "$output"
cp "$objtree/vmlinux" "$output"
