#!/usr/bin/env python3
"""Find monotonic counters (progress/distance/time) in bank 3 while an event runs on the oracle: progvars.py <cfg> <mode> <passes> [step]"""
import sys, os, json
G = os.path.dirname(os.path.abspath(__file__)) + '/..'
here = os.path.abspath(G + '/../../tools/t816')
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, lockstep
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); step = int(sys.argv[4]) if len(sys.argv) > 4 else 50
kind = os.environ.get('T816_KIND', 'waggle')
cfg = json.load(open(f'{G}/cfg/{cfgn}.json'))
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
E.load_mode(mode); C.io.enabled = 1
script = lockstep.make_script(kind, 1)
for h in hooks: C.hook(h)
lockstep.apply_input(C.io, script(0))
snaps = []; k = 0
while k < passes:
    r = C.run(50_000_000); p = (C.cpu.pb << 16) | C.cpu.pc
    if r != 1 or p == 0x077E08: print('event ended at pass', k); break
    if p in hooks:
        k += 1
        if k % step == 0: snaps.append(bytes(C.mem[0x30000:0x40000]))
        lockstep.apply_input(C.io, script(k)); C.hook(p, False); C.run(1); C.hook(p, True); continue
    C.hook(p, False); C.run(1); C.hook(p, True)
import struct
n = len(snaps); print(n, 'snapshots')
cands = []
for off in range(0, 0x10000 - 1, 1):
    vals = [s[off] | s[off + 1] << 8 for s in snaps]
    if vals[0] != vals[-1] and all(b >= a for a, b in zip(vals, vals[1:])) and len(set(vals)) > n // 2: cands.append((off, vals[:3], vals[-3:]))
for c in cands[:40]: print('%04X' % c[0], c[1], '...', c[2])
