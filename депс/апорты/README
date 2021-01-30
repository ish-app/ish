#!/bin/bash -ex
# This script only works on my machine
cd "$(dirname $0)"

function sync_repo() {
    version="$1"
    path="$2"
    rclone copy -v --transfers=32 "alpine:$version/$path" "b2:alpine-archive/$path"
    date=$(date +%F)
    new_index_name="APKINDEX-$version-$date.tar.gz"
    rclone moveto "b2:alpine-archive/$path/APKINDEX.tar.gz" "b2:alpine-archive/$path/$new_index_name"
    echo "$new_index_name" > "$path/index.txt"
}

function update_repo() {
    version="$1"
    path="$2"
    old_index_name="$(cat "$path/index.txt")"
    sync_repo "$version" "$path"
    new_index_name="$(cat "$path/index.txt")"
    rclone cat "b2:alpine-archive/$path/$new_index_name" | tar -xzOf - -O APKINDEX | format_index > /tmp/APKINDEX.new
    rclone cat "b2:alpine-archive/$path/$old_index_name" | tar -xzOf - -O APKINDEX | format_index > /tmp/APKINDEX.old
    diff -u /tmp/APKINDEX.new /tmp/APKINDEX.old
    if cmp -s /tmp/APKINDEX.new /tmp/APKINDEX.old; then
        echo "nothing new"
        echo "$old_index_name" > "$path/index.txt"
    fi
}

function format_index() {
    awk '/^P:/ {name=substr($0,3)} /^V:/ {version=substr($0,3)} /^$/ {print name, version}' | sort
}

if [[ "$#" != 2 ]]; then
    echo "usage: $0 version path" >&2
    exit 1
fi
update_repo "$@"
