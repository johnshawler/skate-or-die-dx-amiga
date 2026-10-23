#!/usr/bin/env python3
"""Print the statically analysed instructions of an overlay in an address range: adis.py <cfg name> <lo hex> <hi hex>"""
import sys, json, os
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import analyze, emit
cfg = analyze.load_cfg_fix(json.load(open(f'{here}/cfg/{sys.argv[1]}.json')))
data = open(f'{here}/{cfg["file"]}', 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
em = emit.Emitter(a, cfg, 'x')
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
for pc, i in sorted(a.insn.items()):
    if lo <= pc < hi: print('%04X %s' % (pc, em.disasm(i)))
