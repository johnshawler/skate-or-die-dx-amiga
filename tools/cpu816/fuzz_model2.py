#!/usr/bin/env python3
"""Directed fuzz: put the original skater at random map positions/headings/speeds, then compare the model pass for pass.
Covers every collision edge and map exit, not just what random driving from the start reaches."""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import overworld_sim as O, cpu816 as C
from overworld_model import Skater, s16
COMBOS = [0xFF, 0xFE, 0xFD, 0xFB, 0xF7, 0xFA, 0xF6, 0xF9, 0xF5]
def trial(seed, passes=260):
    rnd = random.Random(seed)
    O.setup()
    x = rnd.randint(0, 0x98); y = rnd.randint(0x15, 0xC0); h = rnd.randint(-32, 32); spd = rnd.randint(1, 5)
    prefix = [0xFF] * 14 + [0xFD] * 30 + [0xEF] * 3 + [0xFF] * 12
    plan = []
    while len(plan) < passes: plan += [rnd.choice(COMBOS)] * rnd.randint(3, 40)
    seq = prefix + plan
    st = {'inj': False, 'model': None, 'k0': None}
    def pre(k):
        if not st['inj'] and k > 20 and C.rd16(0x034BD5) == 0 and C.rd16(0x03003C) != 0:
            r16 = lambda a: C.wr16(0x030000 + a, 0)
            C.wr16(0x03003C, x); C.wr16(0x03003E, y); C.wr16(0x030040, h & 0xFFFF); C.wr16(0x030042, spd)
            for a in (0x44, 0x4A, 0x4C, 0xB8, 0xBE, 0xBC, 0x6C): C.wr16(0x030000 + a, 0)
            m = Skater(); m.x, m.y, m.h, m.spd = x, y, h, spd
            st['model'] = m; st['inj'] = True; st['k0'] = k
    log = O.run_frames(lambda f: seq[f] if f < len(seq) else 0xFF, len(seq), pre=pre)
    m = st['model']
    if m is None: return 0, None, 'never reached state 0'
    n = 0
    for k, s in enumerate(log):
        if s.get('exit') or k <= st['k0']: continue
        m.step(seq[k - 1])
        if s['state'] != 0:
            ok = (s['state'] == 1) == m.in_shop and (s['state'] == 8) == (m.exit_code != 0)   # the original consumes $6C at once and enters state 8
            return n, (None if ok else (k, ('state', s['state'], 'exit', s['exitc']), ('shop', m.in_shop, 'exit', m.exit_code))), ('shop' if m.in_shop else 'exit %d' % m.exit_code)
        ref = (s['x'], s['y'], s['h'] if s['h'] < 0x8000 else s['h'] - 0x10000, s['spd'], s['sub'], s16(s['turn']), s16(s['tc']), s16(s['b8']), s16(s['fx']), s16(s['fy']))
        mine = (m.x, m.y, m.h, m.spd, m.sub, m.turn, m.tc, m.b8, m.fx, m.fy)
        n += 1
        if ref != mine: return n, (k, ref, mine, seq[k - 1]), 'mismatch'
    return n, None, 'ran to the end'
if __name__ == '__main__':
    import collections
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    tot = 0; ends = collections.Counter(); bad = 0
    for seed in range(N):
        n, b, how = trial(seed); tot += n; ends[how] += 1
        if b: bad += 1; print('seed', seed, 'MISMATCH', b)
    print('trials %d, passes compared %d, mismatches %d' % (N, tot, bad)); print('ended by:', dict(ends))
