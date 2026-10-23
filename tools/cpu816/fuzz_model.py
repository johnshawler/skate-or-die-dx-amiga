#!/usr/bin/env python3
"""Fuzz tools/overworld_model.py against the original F39 code on the 65816 core, pass for pass."""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import overworld_sim as O, cpu816 as C
from overworld_model import Skater, s16

def sgn(v): return s16(v)
EXITS = []
COMBOS = [0xFF, 0xFE, 0xFD, 0xFB, 0xF7, 0xFA, 0xF6, 0xF9, 0xF5]
def run(seed, passes=700):
    rnd = random.Random(seed)
    O.setup()
    prefix = [0xFF] * 14 + [0xFD] * 30 + [0xEF] * 3 + [0xFF] * 12      # reach state 0 via GO COMPETE
    plan = []
    while len(plan) < passes:
        plan += [rnd.choice(COMBOS)] * rnd.randint(3, 45)
    seq = prefix + plan
    log = O.run_frames(lambda f: seq[f] if f < len(seq) else 0xFF, len(seq))
    sk = Skater(); compared = 0; first_bad = None
    seen_cursor = False; aligned = False; prev = None
    for k, s in enumerate(log):
        if s.get('exit'): break
        if s['state'] == 2: seen_cursor = True
        if seen_cursor and s['state'] == 0:
            ref = (s['x'], s['y'], sgn(s['h']), s['spd'], s['sub'], sgn(s['turn']), sgn(s['tc']), sgn(s['b8']), sgn(s['fx']), sgn(s['fy']))
            if not aligned:
                sk = Skater(); (sk.x, sk.y, sk.h, sk.spd, sk.sub, sk.turn, sk.tc, sk.b8, sk.fx, sk.fy) = ref; aligned = True
            elif prev is not None and prev['state'] == 0:
                sk.step(seq[k - 1])
                mine = (sk.x, sk.y, sk.h, sk.spd, sk.sub, sk.turn, sk.tc, sk.b8, sk.fx, sk.fy)
                compared += 1
                if ref != mine:
                    first_bad = (k, ref, mine, seq[k - 1]); break
        elif aligned and s['state'] != 0:
            sk.step(seq[k - 1]) if prev is not None and prev['state'] == 0 else None
            ok = (s['state'] == 1) == sk.in_shop and (s['exitc'] != 0) == (sk.exit_code != 0)
            if not ok: first_bad = (k, ('state', s['state'], 'exit', s['exitc']), ('shop', sk.in_shop, 'exit', sk.exit_code), seq[k - 1])
            else: EXITS.append(('shop' if sk.in_shop else 'exit %d' % sk.exit_code))
            break
        prev = s
    return compared, first_bad, (log[-1] if log else None)
if __name__ == '__main__':
    tot = 0
    for seed in range(int(sys.argv[1]) if len(sys.argv) > 1 else 6):
        c, bad, last = run(seed)
        tot += c
        print('seed %d: %d passes compared, %s' % (seed, c, 'ALL MATCH' if bad is None else 'MISMATCH at snapshot %d: original %s model %s ctl %02x' % bad))
    print('total passes compared:', tot)
    print('how runs ended (model agrees with original):', EXITS)
