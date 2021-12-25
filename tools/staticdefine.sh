#!/bin/bash -e
compile_commands=$1
input=$2
output=$3
dep=$4
flags=$(python3 - <<END
import json
with open('$compile_commands') as f:
    commands = json.load(f)
for command in commands:
    if command['file'].endswith('jit/jit.c'):
        break
command = command['command']
command = command.split()[:-9] + ['-MD', '-MQ', '$output', '-MF', '$dep']
print(' '.join(command))
END
)
$flags $input -include "$(dirname $0)/staticdefine.h" -S -o - | \
sed -ne 's:^[[:space:]]*\.ascii[[:space:]]*"\(.*\)".*:\1:;
         /^->/{s:->#\(.*\):/* \1 */:;
         s:^->\([^ ]*\) [\$$#]*\([^ ]*\) \(.*\):#define \1 \2 /* \3 */:;
         s:->::; p;}' > $output
# sed magic was copied from the linux kernel build system
