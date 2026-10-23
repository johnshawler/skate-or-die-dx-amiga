#!/usr/bin/env python3
"""Profile an event overlay: hot addresses, self-modified code, I/O accesses, indirect branches.
usage: profile_event.py <mode> <millions of instructions>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu816 as C, event_sim as E
from collections import Counter
mode = int(sys.argv[1]); mil = float(sys.argv[2])
E.load_mode(mode)
C.set_code_range(0x031000, 0x03F000)
C.prof_reset(); C.prof_enable(1)
tot = 0
while tot < mil * 1e6:
    r = C.run(1_000_000); tot += 1_000_000
    if r == 2: print('fault', hex(C.fault_pc())); break
pc = C.prof_pc()
hot = sorted(((pc[a], a) for a in range(len(pc)) if pc[a]), reverse=True)
print('distinct instruction addresses executed:', len(hot), ' total executed:', sum(h[0] for h in hot))
print('hottest:', ' '.join('%06x:%d' % (a, n) for n, a in hot[:25]))
print('code writes (writer, target):', [('%06x' % (k >> 32), '%06x' % (k & 0xFFFFFF), n) for k, n in C.prof_records(0)][:40])
io = C.prof_records(1)
print('i/o (pc, addr, rw, count):', [('%06x' % (k >> 32), '%06x' % (k & 0x7FFFFFFF), 'W' if k & (1 << 31) else 'R', n) for k, n in io][:60])
ib = C.prof_records(2)
nonrts = [(k >> 32, k & 0xFFFFFFFF, n) for k, n in ib]
print('indirect branches (from -> to):', len(nonrts))
for f, t, n in sorted(nonrts)[:80]: print('   %06x -> %06x  x%d' % (f, t, n))
print('icount', C.icount(), 'cycles', C.cycles())
