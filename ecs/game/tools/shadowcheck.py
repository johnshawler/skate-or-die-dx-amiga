#!/usr/bin/env python3
"""Is the work picture (bank 1 $2000-$9CFF) equal to the displayed picture (bank E1) at the end of every main-loop pass?  shadowcheck.py <cfg> <mode> <passes> [kind seed]"""
import sys, os, json
G = os.path.dirname(os.path.abspath(__file__)) + '/..'
here = os.path.abspath(G + '/../../tools/t816')
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, lockstep
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
kind = os.environ.get('T816_KIND', 'waggle'); seed = int(os.environ.get('T816_SEED', 1))
cfg = json.load(open(f'{G}/cfg/{cfgn}.json'))
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
E.load_mode(mode); C.io.enabled = 1
script = lockstep.make_script(kind, seed)
for h in hooks: C.hook(h)
lockstep.apply_input(C.io, script(0))
k = 0; bad = 0; worst = 0
while k < passes:
    r = C.run(50_000_000)
    p = (C.cpu.pb << 16) | C.cpu.pc
    if r != 1 or p == 0x077E08: break
    if p in hooks:
        k += 1
        b1 = bytes(C.mem[0x10000 + 0x2000:0x10000 + 0x9D00]); e1 = bytes(C.mem[0xE10000 + 0x2000:0xE10000 + 0x9D00])
        if b1 != e1:
            d = sum(1 for x, y in zip(b1, e1) if x != y); bad += 1; worst = max(worst, d)
            if bad <= 3: print('pass %d: %d bytes differ, first at %04x' % (k, d, next(i for i, (x, y) in enumerate(zip(b1, e1)) if x != y) + 0x2000))
        lockstep.apply_input(C.io, script(k))
        C.hook(p, False); C.run(1); C.hook(p, True); continue
    C.hook(p, False); C.run(1); C.hook(p, True)
print('%s: %d passes, B1 != E1 at %d of them (worst %d bytes)' % (cfgn, k, bad, worst))
