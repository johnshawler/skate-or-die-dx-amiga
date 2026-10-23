import sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, analyze, lockstep
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); seeds = range(int(sys.argv[4]), int(sys.argv[5]))
cfg = analyze.load_cfg_fix(json.load(open(f'{here}/cfg/{cfgn}.json')))
data = open(f'{here}/{cfg["file"]}', 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
st = set(a.insn); union = set()
hook_pc = 0x030000 | int(cfg['pass_hook'], 0); hooks = [hook_pc] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
for seed in seeds:
    E.load_mode(mode); C.io.enabled = 1
    C.prof_reset(); C.prof_enable(1); [C.hook(h) for h in hooks]; C.hook(0x077E08)
    script = lockstep.make_script(os.environ.get('T816_KIND', 'random'), seed); k = 0
    while k <= passes:
        lockstep.apply_input(C.io, script(k))
        r = C.run(50_000_000)
        if r != 1 or C.hit_pc() not in hooks: break
        hp = C.hit_pc(); C.hook(hp, False); C.run(1); C.hook(hp, True); k += 1
    pc = C.prof_pc()
    ex = {x & 0xFFFF for x in range(0x030000, 0x040000) if pc[x]}
    union |= (ex & st)
    print('seed %d: %d passes, executed %d, union so far %d of %d (%.1f%%)' % (seed, k, len(ex & st), len(union), len(st), 100.0 * len(union) / len(st)), flush=True)
miss = sorted(st - union)
print('never executed:', len(miss))
# group into runs
runs = []; 
for x in miss:
    if runs and x - runs[-1][1] <= 3: runs[-1][1] = x
    else: runs.append([x, x])
print(' '.join('%04X-%04X' % (s, e) if s != e else '%04X' % s for s, e in runs[:80]))
