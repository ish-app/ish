#!/bin/sh -x
# This script only works on my machine
function sync_repo() {
    version="$1"
    path="$2"
    rclone copy -v --transfers=32 "alpine:$version/$path" "b2:alpine-archive/$path"
    date=$(date +%F)
    rclone moveto "b2:alpine-archive/$path/APKINDEX.tar.gz" "b2:alpine-archive/$path/APKINDEX-$version-$date.tar.gz"
}
if [[ "$#" != 2 ]]; then
    echo "usage: $0 version path" >&2
    exit 1
fi
sync_repo "$@"
