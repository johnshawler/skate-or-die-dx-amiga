#!/usr/bin/env python3
"""Rank the demanding stretches of the runs in a matrix dir: hotspots.py <matrix dir> -> JSON lines.
Per case: the 120-frame (2 s) window with the fewest game passes while the picture is on (the source CPU is busiest or the game is loading),
excluding stretches with no pass at all for more than 60 frames (loading/transition, listed separately), and the window with the most changed picture bytes."""
import sys, os, csv, json
m = sys.argv[1]; res = []
for case in sorted(os.listdir(m)):
    f = os.path.join(m, case, 'out', 'frames.csv')
    if not os.path.exists(f): continue
    rows = [r for r in csv.DictReader(open(f)) if int(r['frame']) >= 8700]
    fr = [int(r['frame']) for r in rows]; ps = [int(r['pc0']) for r in rows]; ch = [int(r['shr_changed_bytes']) for r in rows]; shr = [int(r['c029'], 16) & 0x80 != 0 for r in rows]
    W = 120; best_low = None; best_chg = None
    for i in range(0, len(rows) - W):
        if not all(shr[i:i + W]): continue
        p = sum(ps[i:i + W]); c = sum(ch[i:i + W])
        if best_low is None or p < best_low[0]:
            gap = max((len(list(g)) for k, g in __import__('itertools').groupby(ps[i:i + W], key=lambda v: v == 0) if k), default=0)
            if gap <= 60: best_low = (p, fr[i], gap)
        if best_chg is None or c > best_chg[0]: best_chg = (c, fr[i])
    loads = []; i = 0
    while i < len(rows):
        if not shr[i]:
            j = i
            while j < len(rows) and not shr[j]: j += 1
            if j - i > 30: loads.append((fr[i], j - i))
            i = j
        else: i += 1
    res.append(dict(case=case, lowest_pass_window=dict(start_frame=best_low[1], passes_in_2s=best_low[0], max_gap_frames=best_low[2]) if best_low else None,
                    busiest_changed_window=dict(start_frame=best_chg[1], changed_bytes_in_2s=best_chg[0]) if best_chg else None, picture_off_stretches=loads[:12]))
    print(json.dumps(res[-1]))
