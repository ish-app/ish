#!/usr/bin/env -S python3 -u
import sys
import re
import os
line_re = re.compile(r'^  [A-Z]+\s+')
dep_re = [
	re.compile(r"\s+Prerequisite [`']([^']*)' is (older|newer) than target"),
	re.compile(r"\s+No need to remake target [`'][^']*'; using VPATH name `([^']*)'"),
	re.compile(r"\s+No need to remake target [`']([^']*)'."),
]
deps = set()
for line in sys.stdin:
	if line_re.match(line):
		sys.stdout.write(line)
	if line.endswith('\n'):
		line = line[:-1]
	for re in dep_re:
		m = re.match(line)
		if m:
			deps.add(m.group(1))
			break
deps = [os.path.realpath(os.path.join(sys.argv[3], dep)) for dep in sorted(deps)]
deps = [dep for dep in deps if os.path.exists(dep)]
with open(sys.argv[1], 'w') as f:
	print(f'{sys.argv[2]}: \\', file=f)
	for dep in deps:
		print(f'{dep} \\', file=f)
