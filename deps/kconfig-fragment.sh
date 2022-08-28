#!/bin/sh
output="$1"
shift
: > "$output"
for cfg in $@; do
    echo "$cfg" >> "$output"
done
