#!/usr/bin/env python3
"""Do shorter paddle timer lengths (0 / 50 / 96 reads for left-up / centre / right-down instead of 0 / 60 / 300) give the games the same decisions?
Runs an event on the 65816 oracle twice with the same stress input and compares the memory of all banks pass by pass (crc every 100 passes).
usage: pdlcheck.py <cfg> <mode> <passes>"""
import sys, os, json, random, zlib
here = os.path.dirname(os.path.abspath(__file__)); t816 = f'{here}/../../../tools/t816'
sys.path.insert(0, here)
import cpu816 as C, event_sim as E
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
cfg = json.load(open(f'{t816}/cfg/{cfgn}.json'))
hooks = [0x030000 | int(cfg['pass_hook'], 0)]
def run(pdl):
    rng = random.Random(7); E.load_mode(mode); C.io.enabled = 1
    for h in hooks: C.hook(h)
    C.hook(0x077E08)
    k = 0; jx = 1; hold = 0; crcs = []
    while k < passes:
        r = C.run(50_000_000); p = (C.cpu.pb << 16) | C.cpu.pc
        if r != 1 or p == 0x077E08: break
        if p in hooks:
            k += 1
            if hold <= 0:
                jx = -jx; jy = rng.choice([0, 0, 0, 0, -1, 1]); hold = rng.choice([1, 1, 2, 3, 6]); fire = rng.random() < 0.3
                C.io.pdl_len[0] = pdl[jx]; C.io.pdl_len[1] = pdl[jy]; C.io.btn[0] = 0x80 if fire else 0
            hold -= 1
            if k % 100 == 0:
                crcs.append(zlib.crc32(b''.join(bytes(C.mem[b << 16:(b << 16) + 65536]) for b in (0, 1, 2, 3, 4, 5, 7, 0xE1))))
        C.hook(p, False); C.run(1); C.hook(p, True)
    return k, crcs
a = run({-1: 0, 0: 60, 1: 300}); b = run({-1: 0, 0: 50, 1: 96})
print(cfgn, 'passes', a[0], b[0], 'identical memory at every 100th pass:', a[1] == b[1], 'checked', len(a[1]))
