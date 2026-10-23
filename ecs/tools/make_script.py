#!/usr/bin/env python3
"""Cold-boot input script for the harness: make_script.py <event> [event_input_file] [event_start_frame]  -> stdout
Title: button at frame 3000.  Shop (frame 5000): cursor left+down to GO PRACTICE, fire.  Map (frame 5600): planned route to the event's exit.
Optional event phase: the lines of event_input_file (H segments) start at event_start_frame."""
import sys, os, subprocess
here = os.path.dirname(os.path.abspath(__file__))
ev = sys.argv[1]
out = ['# cold boot -> title -> shop -> GO PRACTICE -> overworld -> %s' % ev,
       'J 0 0 0 0 0', 'J 3000 0 0 1 0', 'J 3012 0 0 0 0', 'S 5000', 'H -1 1 0 0 85', 'H 0 0 1 0 12', 'H 0 0 0 0 1', 'S 5600']
route = subprocess.run([sys.executable, '-I', f'{here}/route_plan.py', ev], capture_output=True, text=True, check=True).stdout
out += route.strip().split('\n') + ['H 0 0 0 0 100000']
if len(sys.argv) > 3:
    out += ['S %s' % sys.argv[3]] + [l.rstrip() for l in open(sys.argv[2]) if l.strip() and not l.startswith('#')]
print('\n'.join(out))
