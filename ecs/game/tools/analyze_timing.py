#!/usr/bin/env python3
"""Per-pass work times of an event from the TIMING build: analyze_timing.py <mode> <pass rate per second> <dir with tm_<mode>?.bin>
Each sample is the time between the end of one pacing wait and the start of the next (the game code, the picture conversion and the input), in 90 us units."""
import sys, glob, struct, os
import numpy as np
mode, hz, d = int(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
vals = []; c2 = []
for f in sorted(glob.glob(os.path.join(d, 'tm_%d*.bin' % mode))):
    b = open(f, 'rb').read(); u = struct.unpack('>%dH' % (len(b) // 2), b); (vals.extend(u), c2.extend([0] * len(u))) if os.environ.get('SINGLE') else (vals.extend(u[0::2]), c2.extend(u[1::2]))
a = np.array(vals, dtype=np.float64) * 64 * 1.4098 / 1000.0     # ms
slot = 1000.0 / hz
print('samples %d  slot %.1f ms (%.1f passes/s)' % (len(a), slot, hz))
print('work per pass ms: median %.1f  mean %.1f  p95 %.1f  p99 %.1f  max %.1f' % (np.median(a), a.mean(), np.percentile(a, 95), np.percentile(a, 99), a.max()))
print('passes over the slot: %d (%.2f%%)   over 2 slots: %d' % ((a > slot).sum(), 100.0 * (a > slot).mean(), (a > 2 * slot).sum()))
c = np.array(c2, dtype=np.float64) * 64 * 1.4098 / 1000.0
print('of which picture conversion ms: median %.1f  mean %.1f  p95 %.1f  max %.1f   (game code and the rest: median %.1f)' % (np.median(c), c.mean(), np.percentile(c, 95), c.max(), np.median(a - c)))
