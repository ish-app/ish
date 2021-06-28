#!/bin/bash -e
output="$1"
srctree="$2"
objtree="$3"
depfile="$4"
export ISH_CFLAGS="$5"
export LIB_ISH_EMU="$6"
export ARCH=ish

makeargs=()
if [[ -n "$LINUX_HOSTCC" ]]; then
    makeargs+="HOSTCC=$LINUX_HOSTCC"
fi

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

make -C "$objtree" -j "$(nproc)" "${makeargs[@]}" --debug=v | tee "/tmp/log" | "$srctree/../makefilter.py" "$depfile" "$output"
cp "$objtree/vmlinux" "$output"
