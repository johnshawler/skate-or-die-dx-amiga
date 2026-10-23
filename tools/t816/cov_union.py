#!/usr/bin/env python3
"""Union instruction coverage of random/waggle sessions on the oracle: cov_union.py <cfg> <mode> <passes> <seeds> (env T816_PLAYERS etc. as for lockstep.py)"""
import sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, lockstep, analyze
cfgn, mode, passes, seeds = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
cfg = analyze.load_cfg_fix(json.load(open(f'{here}/cfg/{cfgn}.json')))
data = open(f'{here}/{cfg["file"]}', 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg); st = set(a.insn)
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
union = set()
for kind in ('waggle', 'random'):
    for seed in range(1, seeds + 1):
        script = lockstep.make_script(kind, seed)
        E.load_mode(mode); C.io.enabled = 1
        C.prof_reset(); C.prof_enable(1)
        for h in hooks: C.hook(h)
        C.hook(0x077E08)
        k = 0; lockstep.apply_input(C.io, script(0))
        while k < passes:
            r = C.run(50_000_000); p = (C.cpu.pb << 16) | C.cpu.pc
            if r != 1 or p == 0x077E08: break
            if p in hooks:
                k += 1; lockstep.apply_input(C.io, script(k))
            C.hook(p, False); C.run(1); C.hook(p, True)
        pc = C.prof_pc(); ex = {x & 0xFFFF for x in range(0x030000, 0x040000) if pc[x]}
        union |= ex & st
        print('%s seed %d: passes %d, union covered %d of %d (%.1f%%)' % (kind, seed, k, len(union), len(st), 100 * len(union) / len(st)), flush=True)
