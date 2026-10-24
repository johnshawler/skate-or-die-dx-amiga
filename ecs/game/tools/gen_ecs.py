#!/usr/bin/env python3
"""usage: gen_ecs.py cfg.json out.c  -- like tools/t816/gen.py but with ecs/game/tools/emit_ecs.py (direct accesses for indexed addresses of a known bank)"""
import sys, os, json
here = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../tools/t816')
sys.path.insert(0, here); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze, emit_ecs as emit
cfgp, outp = sys.argv[1], sys.argv[2]
cfg = analyze.load_cfg_fix(json.load(open(cfgp)))
data = open(os.path.join(here, cfg['file']), 'rb').read()
a = analyze.Analysis(data, cfg.get('base', 0x1000), cfg.get('bank', 3), cfg)
a.explore_function(cfg['entry'], analyze.St(cfg.get('m', 0), cfg.get('x', 0), 0, cfg.get('db', 0), cfg.get('dk', 0), (), None))
for e in cfg.get('extra_entries', []):
    a.explore_function(e, analyze.St(0, 0, 0, cfg.get('extra_db', 3), 0, (), None))
for w in a.warn: print('warning:', w, file=sys.stderr)
em = emit.Emitter(a, cfg, os.path.basename(cfg['file']))
src = em.emit()
pfx = [x.split('=', 1)[1] for x in sys.argv[3:] if x.startswith('--prefix=')]
if pfx:       # several code images in one overlay: give each its own symbols
    import re
    src = re.sub(r'\b(f_[0-9A-F]{4}|dispatch_call)\b', lambda m: pfx[0] + m.group(1), src)
open(outp, 'w').write(src)
print('%d instructions, %d functions, %d lines of C' % (len(a.insn), len(em.entries), src.count('\n')), file=sys.stderr)
