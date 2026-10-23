#!/usr/bin/env python3
"""Which constant input moves an event past its start screen? usage: probe_inputs.py <cfg> <mode> <passes> <var hex addresses...>
Runs the oracle with each single input held (and the first 30 passes idle) and prints the instruction coverage and the given 16-bit
variables (bank 3) at the end."""
import sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, lockstep, analyze
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); vars_ = [int(x, 16) for x in sys.argv[4:]]
cfg = analyze.load_cfg_fix(json.load(open(f'{here}/cfg/{cfgn}.json')))
data = open(f'{here}/{cfg["file"]}', 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg); st = set(a.insn)
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
for name, inp in (('idle', (0, 0, 0)), ('fire', (0, 0, 1)), ('left', (-1, 0, 0)), ('right', (1, 0, 0)), ('up', (0, -1, 0)), ('down', (0, 1, 0)), ('fire+down', (0, 1, 1)), ('fire+left', (-1, 0, 1))):
    E.load_mode(mode); C.io.enabled = 1
    C.prof_reset(); C.prof_enable(1)
    for h in hooks: C.hook(h)
    C.hook(0x077E08)
    k = 0; lockstep.apply_input(C.io, (0, 0, 0, 0))
    while k < passes:
        r = C.run(50_000_000); p = (C.cpu.pb << 16) | C.cpu.pc
        if r != 1 or p == 0x077E08: break
        if p in hooks:
            k += 1
            jx, jy, f = inp if k > 30 else (0, 0, 0)
            lockstep.apply_input(C.io, (jx, jy, f, 0))
        C.hook(p, False); C.run(1); C.hook(p, True)
    pc = C.prof_pc(); ex = {x & 0xFFFF for x in range(0x030000, 0x040000) if pc[x]}
    print('%-10s covered %d  passes %d  vars %s' % (name, len(ex & st), k, ' '.join('%04x' % C.rd16(0x030000 | v) for v in vars_)), flush=True)
