#!/bin/bash -ex
# This script only works on my machine
archive_remote="wasabici:alpine-archive"

function sync_repo() {
    version="$1"
    path="$2"
    index_name_file="$3"
    rclone copy -v --transfers=32 "alpine:$version/$path" "$archive_remote/$path"
    date=$(date +%F)
    new_index_name="APKINDEX-$version-$date.tar.gz"
    rclone moveto "$archive_remote/$path/APKINDEX.tar.gz" "$archive_remote/$path/$new_index_name"
    echo "$new_index_name" > "$index_name_file"
}

function update_repo() {
    version="$1"
    path="$2"
    index_name_file="$3"
    old_index_name="$(cat "$index_name_file")"
    sync_repo "$version" "$path" "$index_name_file"
    new_index_name="$(cat "$index_name_file")"
    rclone cat "$archive_remote/$path/$new_index_name" | tar -xzOf - -O APKINDEX | format_index > /tmp/APKINDEX.new
    rclone cat "$archive_remote/$path/$old_index_name" | tar -xzOf - -O APKINDEX | format_index > /tmp/APKINDEX.old
    if diff -u /tmp/APKINDEX.new /tmp/APKINDEX.old; then
        echo "nothing new"
        echo "$old_index_name" > "$index_name_file"
    fi
}

function format_index() {
    awk '/^P:/ {name=substr($0,3)} /^V:/ {version=substr($0,3)} /^$/ {print name, version}' | sort
}

if [[ "$#" -lt 2 || "$#" -gt 3 ]]; then
    echo "usage: $0 version path [index.txt]" >&2
    exit 1
fi
if [[ -n "$3" ]]; then
    update_repo "$1" "$2" "$3"
else
    sync_repo "$1" "$2" /dev/null
fi
