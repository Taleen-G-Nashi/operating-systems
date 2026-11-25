#!/usr/bin/env python3
"""Independent reference implementation (regex / split based, no shared code
with prod_cons.c). Prints the same format as ./prod_cons."""
import re, sys
from collections import Counter

data = open(sys.argv[1], 'rb').read()
letters = Counter()
for b in data:
    if 65 <= b <= 90: letters[chr(b)] += 1
    elif 97 <= b <= 122: letters[chr(b - 32)] += 1
words = Counter(len(w) for w in re.findall(rb'[A-Za-z]+', data))
parts = data.split(b'\n')
lines = []
for p in parts[:-1]:                       # each piece was terminated by LF
    lines.append(len(p) - 1 if p.endswith(b'\r') else len(p))
if parts[-1]:                              # unterminated final line (CR is content)
    lines.append(len(parts[-1]))
lines = Counter(lines)

out = ["== Letter counts =="]
out += [f"{chr(65+i)}: {letters[chr(65+i)]}" for i in range(26)]
for title, unit, h in (("Word lengths", "words", words), ("Line lengths", "lines", lines)):
    out += [f"== {title} ==", f"total {unit}: {sum(h.values())}", "length frequency"]
    out += [f"{k} {h[k]}" for k in sorted(h)]
print("\n".join(out))
