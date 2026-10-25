#!/usr/bin/env python3
"""The "SKATING TO ..." screens (src/ow.c, sk_* tables): every line must fit the 40-column text page (they start in column 1) and the table must leave the rows below for the footer."""
import re, sys
s = open(sys.argv[1] if len(sys.argv) > 1 else 'src/ow.c').read()
t = s[s.index('static const char *const sk_title'):s.index('static void sk_text')]
bad = 0
for name, body in re.findall(r'(sk_\w+)\[\] = \{(.*?)\};', t, re.S):
    items = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    mx = max(len(x) for x in items); last = 2 + len(items) - 1
    print('%-9s %2d lines, longest %2d, last row %2d' % (name, len(items), mx, last))
    if mx > 39 or last > 20: bad = 1
print('LINT', 'FAIL' if bad else 'ok'); sys.exit(bad)
