#!/usr/bin/env python3
"""Run the 68020 chunky-to-planar row kernels (src/c2p.h) in Unicorn on random pictures and compare with a plain reference.  usage: c2ptest.py [rounds] [-DC2P_PAIR]"""
import sys, os, subprocess, struct, random
from unicorn import *
from unicorn.m68k_const import *
G = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..'); T = os.path.abspath(G + '/../../tools/t816')
rounds = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 200
extra = [a for a in sys.argv[1:] if a.startswith('-D')]
W = '/tmp/c2ptest'; os.makedirs(W, exist_ok=True)
subprocess.check_call(['m68k-linux-gnu-gcc', '-m68020', '-O2', '-ffixed-a6', '-fomit-frame-pointer', '-ffreestanding', '-fno-builtin', '-fno-pic', '-nostdlib', '-DT816_AMIGA'] + extra +
    [f'-I{G}/runtime', f'-I{T}/runtime', f'-I{G}/src', '-c', f'{G}/tools/c2ptest_drv.c', '-o', W + '/d.o'])
subprocess.check_call(['m68k-linux-gnu-ld', '-Ttext=0', '-o', W + '/d.elf', W + '/d.o'], stderr=subprocess.DEVNULL)
subprocess.check_call(['m68k-linux-gnu-objcopy', '-O', 'binary', W + '/d.elf', W + '/d.bin'])
nm = subprocess.run(['m68k-linux-gnu-nm', W + '/d.elf'], capture_output=True, text=True).stdout
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
code = open(W + '/d.bin', 'rb').read()
mu = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); mu.ctl_set_cpu_model(UC_CPU_M68K_M68020)
mu.mem_map(0, 0x100000); mu.mem_write(0, code); mu.mem_map(0x1000000, 0x400000)
TAB, SRC, DST, SH, STK = 0x1000000, 0x1100000, 0x1110000, 0x1120000, 0x11F0000
def call(fn, *args):
    for i, a in enumerate(reversed(args)): mu.mem_write(STK - 4 * (i + 1), struct.pack('>I', a & 0xFFFFFFFF))
    sp = STK - 4 * (len(args) + 1); mu.mem_write(sp, struct.pack('>I', 0xFFFFFFF0)); mu.reg_write(UC_M68K_REG_A7, sp)
    mu.emu_start(sym[fn], 0xFFFFFFF0)
call('tst_build', TAB)
big = '-DC2P_BIGLUT' in extra
def ref_group(q): return true_group(q)
# the table itself against first principles: pixel x of the group is nibble x (high nibble first); plane p bit = bit p of the nibble, plane 0 is the top byte
def true_group(q):
    pix = []
    for b in q: pix += [b >> 4, b & 15]
    out = []
    for p in range(4): out.append(sum(((pix[x] >> p) & 1) << (7 - x) for x in range(8)))
    return out
rnd = random.Random(5); bad = 0
for r in range(rounds):
    g0 = rnd.randrange(0, 20); n = rnd.randrange(1, 40 - g0 + 1); force = rnd.choice([0, 1])
    src = bytes(rnd.randrange(256) if rnd.random() < .7 else 0 for _ in range(160))
    shadow = bytearray(src)
    for x in range(40):
        if rnd.random() < .5: shadow[4 * x + rnd.randrange(4)] ^= rnd.randrange(1, 256)
    dst = bytearray(rnd.randrange(256) for _ in range(160))
    for x in range(40):                                  # what the planes hold: the conversion of the shadow copy (that is what the shadow means)
        for p, bv in enumerate(ref_group(list(shadow[4 * x:4 * x + 4]))): dst[p * 40 + x] = bv
    mu.mem_write(SRC, src); mu.mem_write(SH, bytes(shadow)); mu.mem_write(DST, bytes(dst))
    call('tst_row', TAB, SRC + 4 * g0, DST + g0, SH + 4 * g0, g0, n, force)
    d2 = bytes(mu.mem_read(DST, 160)); s2 = bytes(mu.mem_read(SH, 160))
    exp_d = bytearray(dst); exp_s = bytearray(shadow)
    for x in range(g0, g0 + n):
        q = list(src[4 * x:4 * x + 4])
        if force or bytes(q) != bytes(shadow[4 * x:4 * x + 4]):
            exp_s[4 * x:4 * x + 4] = bytes(q)
            for p, bv in enumerate(ref_group(q)): exp_d[p * 40 + x] = bv
    # the picture buffer layout used here: plane p of the row at DST + 40 p
    if d2 != bytes(exp_d) or s2 != bytes(exp_s):
        bad += 1
        if bad <= 3: print('MISMATCH round %d g0=%d n=%d force=%d' % (r, g0, n, force), [i for i in range(160) if d2[i] != exp_d[i]][:8])
print('%d rounds, %d wrong' % (rounds, bad))
