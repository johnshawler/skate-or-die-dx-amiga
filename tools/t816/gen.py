#!/usr/bin/env python3
"""usage: gen.py cfg/<event>.json out.c  -- translate one overlay to C"""
import sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import analyze, emit
cfgp, outp = sys.argv[1], sys.argv[2]
cfg = analyze.load_cfg_fix(json.load(open(cfgp)))
data = open(os.path.join(here, cfg['file']), 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
for w in a.warn: print('warning:', w, file=sys.stderr)
em = emit.Emitter(a, cfg, os.path.basename(cfg['file']))
src = em.emit()
pfx = [x.split('=', 1)[1] for x in sys.argv[3:] if x.startswith('--prefix=')]
if pfx:       # several events in one executable: give each its own symbols
    import re
    src = re.sub(r'\b(f_[0-9A-F]{4}|dispatch_call)\b', lambda m: pfx[0] + m.group(1), src)
open(outp, 'w').write(src)
print('%d instructions, %d functions, %d lines of C' % (len(a.insn), len(em.entries), src.count('\n')), file=sys.stderr)
