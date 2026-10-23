#!/usr/bin/env python3
"""Replay each case of a matrix dir from a cold boot and capture screens in its two hotspot windows: capture_hotspots.sh <matrix dir> <hotspots.jsonl> <out dir>
(env as run_hx.sh; the gen/script used for the case is read from <matrix dir>/<case>/script.txt)"""
import sys, os, json, subprocess
m, hf, out = sys.argv[1:4]
PH = dict(ramp='031076', hj='03107D', race='031082', jam='031077', joust='0310A6')
for l in open(hf):
    h = json.loads(l); case = h['case']; ev = case.split('-')[1]
    for tag, key in (('slow', 'lowest_pass_window'), ('busy', 'busiest_changed_window')):
        w = h.get(key)
        if not w: continue
        s = w['start_frame']; d = os.path.join(out, case, tag); os.makedirs(d, exist_ok=True)
        env = dict(os.environ, HX_PAD='0,70,255', HX_PCHOOK=PH[ev])
        subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)), 'run_hx.sh'), d, os.path.join(m, case, 'script.txt'), str(s + 130), '30', str(s)], env=env, check=True)
        print(case, tag, s)
