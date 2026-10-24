#!/usr/bin/env python3
"""Which 256-byte pages of the IIGS memory does an event read or write while it runs (instruction fetches excluded: the Amiga build runs translated C)?
usage: workingset.py <event cfg: ramp|ev8|ev15|ev21|ev28> <mode> <passes>  (T816_PLAYERS=1 for the two-skater events)"""
import sys, os, json, ctypes
here = os.path.dirname(os.path.abspath(__file__)); t816 = f'{here}/../../../tools/t816'
sys.path.insert(0, here)
import cpu816 as C, event_sim as E
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
cfg = json.load(open(f'{t816}/cfg/{cfgn}.json'))
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
C._lib.pages_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 65536)
import random
PDL = {-1: 0, 0: 60, 1: 300}
def apply(jx, jy, fire):
    C.io.pdl_len[0] = PDL[jx]; C.io.pdl_len[1] = PDL[jy]; C.io.btn[0] = 0x80 if fire else 0
rng = random.Random(1)
seeds = int(os.environ.get('SEEDS', 4)); runs = 0
for seed in range(1, seeds + 1):
  for kind in ('waggle', 'random'):
    rng = random.Random(seed)
    E.load_mode(mode); C.io.enabled = 1
    for h in hooks: C.hook(h)
    C.hook(0x077E08)
    k = 0; jx = 1; hold = 0
    while k < passes:
        r = C.run(50_000_000); p = (C.cpu.pb << 16) | C.cpu.pc
        if r != 1 or p == 0x077E08: break
        if p in hooks:
            k += 1
            if hold <= 0:
                if kind == 'waggle': jx = -jx; jy = rng.choice([0, 0, 0, 0, -1, 1]); hold = rng.choice([1, 1, 2, 3, 6]); fire = rng.random() < 0.3
                else: jx = rng.choice([-1, 0, 0, 1]); jy = rng.choice([-1, 0, 0, 1]); hold = int(rng.expovariate(1 / 12.0)) + 1; fire = rng.random() < 0.25
                apply(jx, jy, fire)
            hold -= 1
        C.hook(p, False); C.run(1); C.hook(p, True)
    runs += 1
rd = C._lib.pages_ptr(0).contents; wr = C._lib.pages_ptr(1).contents
layout = {}
for b in range(256):
    pg = [i for i in range(256) if rd[b * 256 + i] or wr[b * 256 + i]]
    if pg: layout['%02X' % b] = dict(pages=pg, read=[i for i in pg if rd[b * 256 + i]], written=[i for i in pg if wr[b * 256 + i]])
tot = sum(len(v['pages']) for v in layout.values())
print(cfgn, 'mode', mode, runs, 'runs:', {b: len(v['pages']) for b, v in layout.items()}, 'total pages', tot, '= %d KB' % (tot // 4))
json.dump(layout, open(os.environ.get('OUT', f'/tmp/layout_{cfgn}.json'), 'w'))
