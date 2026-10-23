import sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, analyze, emit, lockstep
cfgn, mode, passes, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
cfg = analyze.load_cfg_fix(json.load(open(f'{here}/cfg/{cfgn}.json')))
data = open(f'{here}/{cfg["file"]}', 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
em = emit.Emitter(a, cfg, 'x'); em.partition()
E.load_mode(mode); C.io.enabled = 1
hook_pc = 0x030000 | int(cfg['pass_hook'], 0)
C.prof_reset(); C.prof_enable(1); C.hook(hook_pc); C.hook(0x077E08)
script = lockstep.make_script(os.environ.get('T816_KIND', 'random'), seed); k = 0
while k <= passes:
    lockstep.apply_input(C.io, script(k))
    r = C.run(50_000_000)
    if r != 1 or C.hit_pc() != hook_pc: break
    C.hook(hook_pc, False); C.run(1); C.hook(hook_pc, True); k += 1
pc = C.prof_pc()
tot = 0; per = {}
for p, e in em.owner.items():
    n = pc[0x030000 | p]; tot += n; per[e] = per.get(e, 0) + n
print('passes', k, 'instructions executed in translated code:', tot, ' per pass: %.0f' % (tot / max(k, 1)))
for e, n in sorted(per.items(), key=lambda t: -t[1])[:14]:
    print('  f_%04X  %5.1f%%  (%d instructions in function)' % (e, 100.0 * n / tot, len(em.fsets[e])))
