#!/usr/bin/env python3
"""Plan joystick input that skates the overworld skater (tools/overworld_model.py, checked pass-for-pass against the original code) from the
start position to an exit, and print it as harness "H" segments:  route_plan.py <ramp|hj|race|jam|joust>
The result is replayed on the real game in KEGS; the position read back from game memory is compared with the model by the caller."""
import sys, copy, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from overworld_model import Skater
# (exit code, waypoints): the walls of the map (overworld_model.py _x_plus/_x_minus/_y_plus/_y_minus) are why the paths bend:
# the start pocket in front of the shop door is open only downwards, and the sign blocks at y=$90 force the downhill routes around it.
EXITS = {'ramp': (1, [(0x4B, 0x74), (0x78, 0x76), (0x7A, 0x40), (0x7A, 0x1E), (0x98, 0x1E)]),
         'hj': (2, [(0x4B, 0x74), (0x8A, 0x8C), (0x98, 0x90)]),
         'race': (3, [(0x4B, 0x74), (0x16, 0xC0)]),
         'jam': (4, [(0x4B, 0x70), (0x74, 0x78), (0x74, 0xC0)]),
         'joust': (5, [(0x4B, 0x74), (0x20, 0x8A), (0x00, 0x8A)])}
def ctl(jx, jy):          # joystick -> control byte (active low: bit0 up, bit1 down, bit2 left, bit3 right)
    n = 15
    if jy < 0: n &= ~1
    if jy > 0: n &= ~2
    if jx < 0: n &= ~4
    if jx > 0: n &= ~8
    return n
def dist(s, tx, ty): return (s.x - tx) ** 2 * 4 + (s.y - ty) ** 2
def plan(name, maxpass=3000):
    code, wps = EXITS[name]
    s = Skater(); seq = []; wi = 0
    while len(seq) < maxpass:
        tx, ty = wps[wi]
        if wi + 1 < len(wps) and dist(s, tx, ty) < 64: wi += 1; tx, ty = wps[wi]
        best = None
        for jx, jy in ((0, -1), (-1, -1), (1, -1), (0, 0), (-1, 0), (1, 0), (0, 1), (-1, 1), (1, 1)):
            for m in (3, 8, 16, 28):
                t = copy.deepcopy(s); d = 1e18; bad = False
                for k in range(m + 20):
                    t.step(ctl(jx if k < m else 0, jy if k < m else -1)); d = min(d, dist(t, tx, ty))
                    if t.exit_code or t.in_shop: break
                if t.exit_code == code: d = -1
                if t.in_shop or (t.exit_code and t.exit_code != code): d += 10 ** 6
                if best is None or d < best[0]: best = (d, jx, jy, m)
        _, jx, jy, m = best
        for k in range(min(m, 6)):
            seq.append((jx, jy)); s.step(ctl(jx, jy))
            if s.exit_code: return seq, s
            if s.in_shop: return None, s
    return None, s
def segments(seq):
    out = []
    for j in seq:
        if out and out[-1][0] == j: out[-1][1] += 1
        else: out.append([j, 1])
    return [(j[0], j[1], n) for j, n in out]
if __name__ == '__main__':
    seq, s = plan(sys.argv[1])
    if seq is None: sys.exit('no route found (ended at x=%02x y=%02x exit=%d shop=%s)' % (s.x, s.y, s.exit_code, s.in_shop))
    for jx, jy, n in segments(seq): print('H %d %d 0 0 %d' % (jx, jy, n))
    print('# exit code %d after %d passes at x=%02x y=%02x' % (s.exit_code, len(seq), s.x, s.y), file=sys.stderr)
