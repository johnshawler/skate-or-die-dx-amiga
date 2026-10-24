#!/usr/bin/env python3
"""Differential test of a native routine (native/engine.h) against the original machine code on real game states.
usage: nativetest.py <cfg> <mode> <overlay constants header> <routine pc hex> <native fn> [samples] [passes] [kind seed]
The oracle (tools/cpu816) runs the event with scripted input; every time the routine is entered the state (all banks, registers) is
recorded, the routine is run to its return, and the state after it is recorded. The native routine, compiled for the host, is run from
the same state and must leave the same registers (A X Y S, flags N V Z C) and memory (all banks, including the stack bytes)."""
import sys, os, json, ctypes, subprocess, hashlib
G = os.path.dirname(os.path.abspath(__file__)) + '/..'              # ecs/game
here = os.path.abspath(G + '/../../tools/t816')                        # the translator, the oracle and the runtime (not modified)
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, lockstep
cfgn, mode, hdr, pc, fn = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4], 16), sys.argv[5]
samples = int(sys.argv[6]) if len(sys.argv) > 6 else 40; passes = int(sys.argv[7]) if len(sys.argv) > 7 else 1500
kind = os.environ.get('T816_KIND', 'waggle'); seed = int(os.environ.get('T816_SEED', 1))
BANKS = [0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1]
cfg = json.load(open(f'{G}/cfg/{cfgn}.json'))
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
src = f'/tmp/ntg_{cfgn}_{fn}.c'; lib = f'/tmp/ntg_{cfgn}_{fn}.so'
open(src, 'w').write('#include "m816.h"\n#include "%s"\n#include "%s/native/%s"\nu32 dispatch_call(u16 a) { (void)a; return 0; }\nint t_run(void) { return %s(); }\n' % (os.path.abspath(hdr), G, os.environ.get('NAT_H', 'engine.h'), fn))
subprocess.check_call(['gcc', '-O1', '-shared', '-fPIC', f'-I{G}/runtime', f'-I{here}/runtime', f'-I{G}/native', '-o', lib, src, f'{here}/runtime/m816rt.c'])
rt = ctypes.CDLL(lib)
rt.rt_bank_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 65536); rt.rt_cpu.restype = ctypes.c_void_p; rt.rt_dmin_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 200)
class Cpu(ctypes.Structure): _fields_ = [('a', ctypes.c_uint16), ('x', ctypes.c_uint16), ('y', ctypes.c_uint16), ('s', ctypes.c_uint16), ('d', ctypes.c_uint16), ('db', ctypes.c_uint8), ('n', ctypes.c_uint8), ('v', ctypes.c_uint8), ('z', ctypes.c_uint8), ('c', ctypes.c_uint8), ('i', ctypes.c_uint8)]
cp = Cpu.from_address(rt.rt_cpu())
rt.rt_io_ptr.restype = ctypes.c_void_p
nio = C.IO.from_address(rt.rt_io_ptr())
rt.rt_init()
E.load_mode(mode); C.io.enabled = 1
script = lockstep.make_script(kind, seed)
for h in hooks: C.hook(h)
entry = 0x030000 | pc; C.hook(entry)
k = 0; got = 0; ok = 0; fallback = 0; bad = 0; stride = int(os.environ.get('T816_STRIDE', 7)); seen = 0
lockstep.apply_input(C.io, script(0))
def LE(b, o): return b[o] | b[o + 1] << 8
def snap(): return {b: bytes(C.mem[b << 16:(b << 16) + 65536]) for b in BANKS}
while k < passes and got < samples:
    r = C.run(50_000_000)
    p = (C.cpu.pb << 16) | C.cpu.pc
    if r != 1 or p == 0x077E08: break
    if p in hooks:
        k += 1; lockstep.apply_input(C.io, script(k))
        C.hook(p, False); C.run(1); C.hook(p, True); continue
    if p == entry:
        seen += 1
        if seen % stride:
            C.hook(p, False); C.run(1); C.hook(p, True); continue
        if os.environ.get('T816_MUTATE') and 'push' in fn:           # make the rectangle lists hit the edge cases (x = 22, 342, 24, 343, ...)
            import re, random
            K = {m[0]: int(m[1], 0) for m in re.findall(r'#define (N_\w+) (0x[0-9A-Fa-f]+)', open(hdr).read())}
            rnd = random.Random(seen)
            n = rnd.randint(0, 6); C.wr16(0x030000 | K['N_VCNT'], 2 * n)
            for i in range(n):
                ec = rnd.randint(1, 8); rows = rnd.randint(1, 5)
                x0 = rnd.choice([0x14, 0x16, 0x18, 0x1A, 0x150, 0x152, 0x154, 0x156, 0x158, 0x15A, rnd.randrange(0, 0x180, 2), rnd.randrange(0x0, 0x20, 2), 0x120])
                C.wr16(0x030000 | (K['N_VX'] + 2 * i), x0 | rnd.choice([0, 1])); C.wr16(0x030000 | (K['N_VW'] + 2 * i), ec)
                C.wr16(0x030000 | (K['N_VH'] + 2 * i), ec * rows); C.wr16(0x030000 | (K['N_VOFF'] + 2 * i), 0x1F4C + rnd.randrange(0, 160 * 150, 2))
        if os.environ.get('T816_MUTATE') and 'glyph' in fn:
            import re, random
            K = {m[0]: int(m[1], 0) for m in re.findall(r'#define (N_\w+) (0x[0-9A-Fa-f]+)', open(hdr).read())}
            rnd = random.Random(seen)
            C.wr16(0x030000 | K['N_GCOL'], rnd.choice([0, 1, 0x26, 0x27, 0x28, rnd.randrange(0, 0x28)])); C.wr16(0x030000 | K['N_GROW'], rnd.choice([0, 0x17, 0x18, 0x19, rnd.randrange(0, 0x19)]))
            C.cpu.a = rnd.choice([0, 1, 0x20, 0x41, rnd.randrange(0, 0x60)])
        if os.environ.get('T816_MUTATE') and 'joy' in fn:             # paddle timers of every length the Amiga build uses, and timers still running at entry
            import random
            rnd = random.Random(seen * 7 + 1)
            for q in range(4): C.io.pdl_len[q] = rnd.choice([0, 0, 50, 96, 60, 300, rnd.randrange(0, 130)])
            for q in range(2): C.io.pdl_left[q] = rnd.choice([0, 0, rnd.randrange(0, 130)])
            C.io.btn[0] = rnd.choice([0, 0x80])
        before = snap(); iob = (list(C.io.pdl_len), list(C.io.pdl_left), C.io.btn[0]); rg = (C.cpu.a, C.cpu.x, C.cpu.y, C.cpu.s, C.cpu.d, C.cpu.db, C.cpu.p)
        ret = (C.rd16(0x000000 | (C.cpu.s + 1)) + 1) & 0xFFFF
        C.hook(p, False); C.hook(0x030000 | ret)
        r2 = C.run(5_000_000)
        assert r2 == 1 and (C.cpu.pb << 16 | C.cpu.pc) == (0x030000 | ret), 'did not return'
        after = snap(); ra = (C.cpu.a, C.cpu.x, C.cpu.y, C.cpu.s, C.cpu.d, C.cpu.db, C.cpu.p)
        C.hook(0x030000 | ret, False); C.hook(p, True)
        # native from the same state
        for i, b in enumerate(BANKS): ctypes.memmove(rt.rt_bank_ptr(b), before[b], 65536)
        cp.a, cp.x, cp.y, cp.s, cp.d, cp.db = rg[0], rg[1], rg[2], rg[3], rg[4], rg[5]
        for q in range(4): nio.pdl_len[q] = iob[0][q]; nio.pdl_left[q] = iob[1][q]
        nio.btn[0] = iob[2]
        pp = rg[6]; cp.n, cp.v, cp.z, cp.c, cp.i = (pp >> 7) & 1, (pp >> 6) & 1, (pp >> 1) & 1, pp & 1, (pp >> 2) & 1
        got += 1
        if not rt.t_run():
            fallback += 1
            if fallback <= 2: print('fallback input: A=%04x X=%04x Y=%04x S=%04x D=%04x DB=%02x P=%02x' % rg, 'E2=%06x E6=%06x EC=%04x EE=%04x DD=%04x C035=%02x' % (LE(before[0], 0xE2) | before[0][0xE4] << 16, LE(before[0], 0xE6) | before[0][0xE8] << 16, LE(before[3], 0xEC), LE(before[3], 0xEE), LE(before[0], 0xDD), before[0xE0][0xC035]))
        else:
            ok_ = True; msgs = []
            cp.s = (cp.s + 2) & 0xFFFF                                   # the RTS the caller of the native would do
            nat = (cp.a, cp.x, cp.y, cp.s)
            if nat != ra[:4]: ok_ = False; msgs.append('regs native A=%04x X=%04x Y=%04x S=%04x oracle A=%04x X=%04x Y=%04x S=%04x' % (nat + ra[:4]))
            fl = ((cp.n << 7) | (cp.v << 6) | (cp.z << 1) | cp.c); ofl = ra[6] & 0xC3
            if fl != ofl: ok_ = False; msgs.append('flags native %02x oracle %02x (N=80 V=40 Z=02 C=01) (input p=%02x)' % (fl, ofl, rg[6]))
            for b in BANKS:
                tm = bytes(rt.rt_bank_ptr(b).contents)
                if tm != after[b]:
                    d = [j for j in range(65536) if tm[j] != after[b][j]]
                    ok_ = False; msgs.append('bank %02x: %d bytes differ, first %s (native/oracle)' % (b, len(d), [(hex(j), tm[j], after[b][j]) for j in d[:6]]))
            if 'joy' in fn and (list(nio.pdl_left) != list(C.io.pdl_left)):
                ok_ = False; msgs.append('timers native %s oracle %s' % (list(nio.pdl_left), list(C.io.pdl_left)))
            if ok_: ok += 1
            else:
                bad += 1
                if bad <= 3: print('MISMATCH at sample %d (input A=%04x X=%04x Y=%04x S=%04x):' % (got, rg[0], rg[1], rg[2], rg[3]), *msgs, sep='\n  ')
        C.run(1)    # step the oracle on (it is at the hook... we removed it): continue from the return address
        continue
    C.hook(p, False); C.run(1); C.hook(p, True)
print('%s at %04x: %d samples, %d identical, %d fell back to the translated routine, %d different' % (fn, pc, got, ok, fallback, bad))
