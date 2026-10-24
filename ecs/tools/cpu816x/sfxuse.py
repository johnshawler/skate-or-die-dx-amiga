#!/usr/bin/env python3
"""Which sound effects (calls of the sound driver's 'play effect' entry $07:8003, A = effect number) does an event ask for?
usage: sfxuse.py <cfg> <mode> <passes>   (env SEEDS, T816_PLAYERS)"""
import sys, os, json, random
here = os.path.dirname(os.path.abspath(__file__)); t816 = f'{here}/../../../tools/t816'
sys.path.insert(0, here)
import cpu816 as C, event_sim as E
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
cfg = json.load(open(f'{t816}/cfg/{cfgn}.json'))
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
PDL = {-1: 0, 0: 60, 1: 300}
seen = {}
for seed in range(1, int(os.environ.get('SEEDS', 6)) + 1):
  for kind in ('waggle', 'random'):
    rng = random.Random(seed)
    E.load_mode(mode); C.io.enabled = 1
    for h in hooks: C.hook(h)
    C.hook(0x077E08); C.hook(0x078003)
    k = 0; jx = 1; hold = 0
    while k < passes:
        r = C.run(50_000_000); p = (C.cpu.pb << 16) | C.cpu.pc
        if r != 1 or p == 0x077E08: break
        if p == 0x078003: seen[C.cpu.a & 255] = seen.get(C.cpu.a & 255, 0) + 1
        if p in hooks:
            k += 1
            if hold <= 0:
                if kind == 'waggle': jx = -jx; jy = rng.choice([0, 0, 0, 0, -1, 1]); hold = rng.choice([1, 1, 2, 3, 6]); fire = rng.random() < 0.3
                else: jx = rng.choice([-1, 0, 0, 1]); jy = rng.choice([-1, 0, 0, 1]); hold = int(rng.expovariate(1 / 12.0)) + 1; fire = rng.random() < 0.25
                C.io.pdl_len[0] = PDL[jx]; C.io.pdl_len[1] = PDL[jy]; C.io.btn[0] = 0x80 if fire else 0
            hold -= 1
        C.hook(p, False); C.run(1); C.hook(p, True)
print(cfgn, 'mode', mode, 'effect ids (count):', {('%02X' % k): v for k, v in sorted(seen.items())})
