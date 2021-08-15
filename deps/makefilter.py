#!/usr/bin/env python3
import sys
import re
import os
line_re = re.compile(r'^  [A-Z]+\s+')
dep_re = re.compile(r'\s+Prerequisite `([^\']*)\' is (older|newer) than target')
deps = set()
for line in sys.stdin:
	if line_re.match(line):
		sys.stdout.write(line)
	if line.endswith('\n'):
		line = line[:-1]
	m = dep_re.match(line)
	if m:
		deps.add(m.group(1))
deps = [os.path.realpath(dep) for dep in sorted(deps)]
deps = [dep for dep in deps if os.path.exists(dep)]
with open(sys.argv[1], 'w') as f:
	print(f'{sys.argv[2]}: \\', file=f)
	for dep in deps:
		print(f'{dep} \\', file=f)
