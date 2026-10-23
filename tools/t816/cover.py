"""Compare the statically analysed instruction set with what the original executed on the oracle (profile run)."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../cpu816'))
import analyze, cpu816 as C, event_sim as E
cfgp, mode, mil = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
cfg = json.load(open(cfgp))
analyze.load_cfg_fix(cfg)
data = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), cfg['file']), 'rb').read()
a = analyze.analyze(data, cfg['base'], cfg['bank'], cfg)
E.load_mode(mode); C.prof_reset(); C.prof_enable(1)
tot = 0
while tot < mil * 1e6:
    r = C.run(1_000_000); tot += 1_000_000
    if r == 2: print('fault'); break
pc = C.prof_pc()
ex = {x & 0xFFFF for x in range(0x030000, 0x040000) if pc[x]}
miss = sorted(x for x in ex if x not in a.insn and cfg['base'] <= x < cfg['base'] + len(data))
print('executed instruction addresses:', len(ex), ' statically found:', len(a.insn), ' executed but not found:', len(miss))
print(' '.join('%04X' % x for x in miss[:60]))
