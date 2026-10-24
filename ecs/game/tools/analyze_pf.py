#!/usr/bin/env python3
"""Sampling profile: analyze_pf.py <mode> <dir with pf_<mode>?.bin> [top N]. Needs build/ov<mode>.elf of the same build (symbols)."""
import sys, glob, os, struct, subprocess, bisect, collections
mode, d = int(sys.argv[1]), sys.argv[2]; top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
elf = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'ov%d.elf' % mode) if not os.environ.get('ELFDIR') else os.path.join(os.environ['ELFDIR'], 'ov%d.elf' % mode)
nm = subprocess.run(['m68k-linux-gnu-nm', '-n', '--defined-only', elf], capture_output=True, text=True).stdout
syms = sorted((int(l.split()[0], 16), l.split()[2]) for l in nm.splitlines() if len(l.split()) == 3 and l.split()[1] in 'tTdDrRbB')
addr = [a for a, n in syms]
h = [0] * 16384
for f in sorted(glob.glob(os.path.join(d, 'pf_%d*.bin' % mode))):
    b = open(f, 'rb').read(); v = struct.unpack('>16384I', b[:65536])
    h = [x + y for x, y in zip(h, v)]
tot = sum(h); by = collections.Counter()
for i, c in enumerate(h):
    if c: by[syms[max(bisect.bisect_right(addr, i * 16 + 8) - 1, 0)][1]] += c
print('samples %d' % tot)
for n, c in by.most_common(top): print('  %-24s %5.1f%%' % (n, 100.0 * c / tot))
