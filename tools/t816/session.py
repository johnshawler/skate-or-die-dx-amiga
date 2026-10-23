#!/usr/bin/env python3
"""Oracle-only session: run the original event with scripted input, report which statically analysed instructions were executed and
which values the state variable took, and optionally save screenshots.   usage: session.py <cfg> <mode> <passes> <seed> [shot_every]"""
import sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, analyze, lockstep, flow
cfgn, mode, passes, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
shot = int(sys.argv[5]) if len(sys.argv) > 5 else 0
cfg = analyze.load_cfg_fix(json.load(open(f'{here}/cfg/{cfgn}.json')))
data = open(f'{here}/{cfg["file"]}', 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
E.load_mode(mode); C.io.enabled = 1
hook_pc = 0x030000 | int(cfg['pass_hook'], 0); hooks = [hook_pc] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
C.prof_reset(); C.prof_enable(1); [C.hook(h) for h in hooks]
script = lockstep.make_script(os.environ.get('T816_KIND', 'random'), seed)
vars_ = {}
statev = int(cfg.get('state_var', '0x42'), 0)
k = 0
while k <= passes:
    lockstep.apply_input(C.io, script(k))
    r = C.run(50_000_000)
    if r != 1: print('stopped', r, hex(C.cpu.pb << 16 | C.cpu.pc)); break
    v = C.rd16(0x030000 + statev); vars_[v] = vars_.get(v, 0) + 1
    if shot and k % shot == 0:
        flow.shr_image(f'/tmp/t816_shot_{k:05d}.png', 1)
    hp = C.hit_pc(); C.hook(hp, False); C.run(1); C.hook(hp, True)
    k += 1
pc = C.prof_pc()
ex = {x & 0xFFFF for x in range(0x030000, 0x040000) if pc[x]}
st = set(a.insn)
print('passes run: %d; static instructions %d, executed %d (%.1f%%)' % (k, len(st), len(ex & st), 100.0 * len(ex & st) / len(st)))
print('state variable $%X values seen:' % statev, dict(sorted(vars_.items())))
