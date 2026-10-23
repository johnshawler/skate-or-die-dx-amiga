#!/usr/bin/env python3
"""Event-phase input for the harness (H segments, one segment = n joystick polls = n game passes):
gen_stress.py <event> <kind: waggle|random> <seed> [polls]
Every event starts with fire pulses (control choice / start prompts); then the stress input: waggle = fast left/right alternation, fire pulses of
random length, now and then up/down (running/jumping); random = random directions and fire with exponential holds."""
import sys, random
ev, kind, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]); polls = int(sys.argv[4]) if len(sys.argv) > 4 else 20000
rng = random.Random(seed); out = []
def hold(jx, jy, b0, n): out.append('H %d %d %d 0 %d' % (jx, jy, b0, n))
intro = {'ramp': 2, 'hj': 2, 'race': 3, 'jam': 3, 'joust': 4}[ev]
for _ in range(intro): hold(0, 0, 0, 40); hold(0, 0, 1, 6)
hold(0, 0, 0, 60)
left = polls; jx = 1; fire = 0
while left > 0:
    if kind == 'waggle':
        jx = -jx; jy = rng.choice([0, 0, 0, 0, -1, 1]); n = rng.choice([1, 1, 2, 3, 6])
        fire = rng.random() < 0.3
    else:
        jx = rng.choice([-1, 0, 0, 1]); jy = rng.choice([-1, 0, 0, 1]); n = int(rng.expovariate(1 / 12.0)) + 1
        fire = rng.random() < 0.25
    hold(jx, jy, 1 if fire else 0, n); left -= n
print('\n'.join(out))
