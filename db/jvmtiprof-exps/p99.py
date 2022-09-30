#!/usr/bin/env python3
import sys
import re
r = []
for line in sys.stdin:
    x = re.match(r'(?:[^:]+:)?\s*<p99th>([\d\.]+)</p99th>', line)
    r.append(float(x.group(1)))
print(r)
