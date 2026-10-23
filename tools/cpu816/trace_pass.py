import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu816 as C, overworld_sim as O
def advance_to_loop_top():
    while True:
        r = C.run(5_000_000)
        if r == 1 and C.hit_pc() == O.LOOP_TOP: return
        if r != 1: raise SystemExit('stopped r=%d pc=%02x:%04x' % (r, C.cpu.pb, C.cpu.pc))
def one_pass(ctl, record=True):
    C.wr16(0x033062, ctl); C.wr16(0x033060, 0x00FF)
    pcs = []
    # leave the hook address: run one instruction first
    C.run(1)
    while True:
        pc = (C.cpu.pb << 16) | C.cpu.pc
        if pc == O.LOOP_TOP: break
        pcs.append(pc)
        r = C.run(1)
        if r == 2: raise SystemExit('fault')
    return pcs
if __name__ == '__main__':
    O.setup(); advance_to_loop_top()
    one_pass(0xFF)         # first pass after init (state 0 -> 2)
    for ctl, name in ((0xFF, 'none'), (0xF7, 'right')):
        for k in range(2):
            pcs = one_pass(ctl)
            s = O.snapshot()
            print('ctl %s pass %d: %d instr, state %d cx %d cy %d sx0 %d sy0 %d fr0 %d' % (name, k, len(pcs), s['state'], s['cx'], s['cy'], s['sx'][0], s['sy'][0], s['fr'][0]))
    # which routines were entered in the last pass (JSR targets seen as first pcs of subroutines is hard; list unique 4K-page ranges)
    import collections
    print('distinct addresses executed in last pass:', len(set(pcs)))
    rng = sorted(set(p & 0xFFFF for p in pcs))
    # compress ranges
    out = []; s0 = rng[0]; p0 = s0
    for a in rng[1:]:
        if a - p0 > 8: out.append((s0, p0)); s0 = a
        p0 = a
    out.append((s0, p0)); print(' '.join('%04X-%04X' % r for r in out))
