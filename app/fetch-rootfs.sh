#!/bin/sh
# Download the root file system archive the app unpacks on first launch.
# Files under app/rootfs-overlay/ (not in git; for private builds) are added
# to it, owned by root: app/rootfs-overlay/root/NOTES.md becomes /root/NOTES.md.
set -e
out="$BUILT_PRODUCTS_DIR/$CONTENTS_FOLDER_PATH/root.tar.gz"
curl -fL "https://$ROOTFS_URL" -o "$out"
overlay="$SRCROOT/app/rootfs-overlay"
if [ -d "$overlay" ] && [ -n "$(find "$overlay" -type f | head -1)" ]; then
    tmp="$(mktemp -d)"
    gunzip -c "$out" > "$tmp/root.tar"
    # files only: the directories they go in are in the archive already
    (cd "$overlay" && find . -type f | sed 's|^\./||' > "$tmp/list")
    tar -rf "$tmp/root.tar" --uid 0 --gid 0 -C "$overlay" -T "$tmp/list"
    gzip -c "$tmp/root.tar" > "$out"
    echo "added to the root file system:"; cat "$tmp/list"
fi
