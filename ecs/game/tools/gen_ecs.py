#!/usr/bin/env python3
"""usage: gen_ecs.py cfg.json out.c  -- like tools/t816/gen.py but with ecs/game/tools/emit_ecs.py (direct accesses for indexed addresses of a known bank)"""
import sys, os, json
here = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../tools/t816')
sys.path.insert(0, here); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze, emit_ecs as emit
cfgp, outp = sys.argv[1], sys.argv[2]
cfg = analyze.load_cfg_fix(json.load(open(cfgp)))
data = open(os.path.join(here, cfg['file']), 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
for w in a.warn: print('warning:', w, file=sys.stderr)
em = emit.Emitter(a, cfg, os.path.basename(cfg['file']))
src = em.emit()
open(outp, 'w').write(src)
print('%d instructions, %d functions, %d lines of C' % (len(a.insn), len(em.entries), src.count('\n')), file=sys.stderr)
