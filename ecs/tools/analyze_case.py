#!/usr/bin/env python3
"""Summarise one harness run: analyze_case.py <run dir> [first_frame] -> JSON on stdout.
Event frames = frames with the SHR screen on (c029 bit 7) from first_frame on. pass counts come from the 65816 pass-hook counter (pc0)."""
import sys, os, csv, json, statistics as st
d = sys.argv[1]; first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
rows = list(csv.DictReader(open(os.path.join(d, 'out', 'frames.csv'))))
def pct(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(len(v) * p / 100))] if v else None
ev = [r for r in rows if int(r['frame']) >= first and int(r['c029'], 16) & 0x80]
passes = [int(r['pc0']) for r in ev]; chg = [int(r['shr_changed_bytes']) for r in ev]; doc = [int(r['doc_writes']) for r in ev]
# passes per second in 1-second (60 frame) windows, and the longest gap without a pass
win = [sum(passes[i:i + 60]) for i in range(0, len(passes) - 59, 60)]
gap = best = 0
for p in passes:
    gap = 0 if p else gap + 1; best = max(best, gap)
out = dict(frames=len(ev), emulated_seconds=round(len(ev) / 60, 1), passes=sum(passes), passes_per_s_mean=round(sum(passes) / max(1, len(ev)) * 60, 2),
           passes_per_s_min_window=min(win) if win else None, passes_per_s_p5_window=pct(win, 5), passes_per_s_max_window=max(win) if win else None,
           longest_frames_without_pass=best,
           changed_bytes_mean=round(st.mean(chg)) if chg else None, changed_bytes_p95=pct(chg, 95), changed_bytes_p99=pct(chg, 99), changed_bytes_max=max(chg) if chg else None,
           frames_over_8000_changed=sum(1 for c in chg if c > 8000), doc_writes_per_frame_mean=round(st.mean(doc), 2) if doc else None, doc_writes_max=max(doc) if doc else None)
w = os.path.join(d, 'out', 'watch.csv')
if os.path.exists(w):
    vals = []
    for l in open(w):
        p = l.strip().split(',')
        if int(p[0]) >= first and len(p) > 1: vals.append(p[1])
    if vals:
        if len(vals[0]) == 4: n = [int(v[0:2], 16) for v in vals]; out['watch_first_word_lowbyte'] = dict(mean=round(st.mean(n), 1), p95=pct(n, 95), max=max(n))
        else:
            n = [sum(1 for k in range(0, len(v), 4) if v[k:k + 4] != '0000') for v in vals]; out['active_sprite_slots'] = dict(mean=round(st.mean(n), 2), p95=pct(n, 95), max=max(n))
print(json.dumps(out))
