#!/usr/bin/env python3
"""The high-score update of F39's start-up ($1B5A: colours, $1414 new scores against the table, $1495 insert, $1538 shift) on the original overlay running on the 65816 oracle
against src/menu.h (menu_hs_colours / menu_hs_update), on random players, results and tables.   usage: hstest.py [trials] [seed]"""
import sys, os, random, ctypes, subprocess
G = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..'); X = os.path.abspath(G + '/../tools/cpu816x')
sys.path.insert(0, X)
import cpu816 as C, overworld_sim as O, flow
W = '/tmp/hstest'; os.makedirs(W, exist_ok=True)
open(W + '/drv.c', 'w').write(r'''
#include "menu.h"
static Menu M;
void t_set(const unsigned char *names, int count, const unsigned char *ord, const unsigned char *res, const unsigned char *rord, const unsigned char *scores)
{
    int i;
    for (i = 0; i < 128; i++) ((unsigned char *)M.names)[i] = names[i];
    M.count = (unsigned short)count;
    for (i = 0; i < 8; i++) M.ord[i] = ord[i];
    for (i = 0; i < 36; i++) M.res_score[i] = res[i];
    for (i = 0; i < 12; i++) M.res_ord[i] = rord[i];
    for (i = 0; i < 192; i++) M.scores[i] = scores[i];
    M.f70 = 0;
}
void t_run(void) { menu_hs_colours(&M); menu_hs_update(&M); }
const unsigned char *t_scores(void) { return M.scores; }
int t_f70(void) { return M.f70; }
''')
subprocess.check_call(['gcc', '-O1', '-shared', '-fPIC', '-I' + G + '/src', '-o', W + '/drv.so', W + '/drv.c'])
lib = ctypes.CDLL(W + '/drv.so'); lib.t_scores.restype = ctypes.POINTER(ctypes.c_ubyte)
trials = int(sys.argv[1]) if len(sys.argv) > 1 else 200; seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
rnd = random.Random(seed)
flow.setup(6)
r = C.run(8_000_000); assert r == 1 and C.hit_pc() == O.LOOP_TOP
C.rtl_at(0xE100A8); C.rts_at(0x031562)       # the save of the table is a GS/OS call: not run
C.load(0x033F00, bytes([0x20, 0x5A, 0x1B, 0x4C, 0x03, 0x3F])); C.hook(0x033F03)
bad = 0; ins = 0
def bcd3(v): return bytes(int(str(v).zfill(6)[i:i + 2], 16) for i in (4, 2, 0))
for t in range(trials):
    count = rnd.randint(0, 8)
    names = bytes(b for k in range(8) for b in (bytes(rnd.choice(b'ABCDEFGHIJ') for _ in range(rnd.randint(1, 9))) + b'\0' * 16)[:16])
    perm = list(range(8)); rnd.shuffle(perm); ordb = bytes(perm)
    res = b''.join(bcd3(rnd.choice([0, rnd.randrange(0, 1000000), rnd.randrange(0, 5000)])) for _ in range(12))
    rord = bytes(rnd.randrange(0, 8) for _ in range(12))
    sc = bytearray()
    for g in range(4):
        vals = sorted((rnd.choice([0, rnd.randrange(0, 1000000), rnd.randrange(0, 5000)]) for _ in range(3)), reverse=True)
        for v in vals: sc += (bytes(rnd.choice(b'ABCDEF') for _ in range(9)) + b'\0' + bcd3(v) + bytes([0, 0, rnd.choice([0x33, 0x44, 0])]))
    sc = bytes(sc)
    C.load(0x076810, names); C.wr16(0x0768CC, count); C.load(0x0768DA, ordb); C.load(0x0768A8, res); C.load(0x0768E2, rord); C.load(0x077F00, sc)
    C.wr16(0x030070, 0); C.wr16(0x034BD5, 5)
    C.cpu.pb = 3; C.cpu.pc = 0x3F00; C.cpu.db = 3; C.cpu.d = 0; C.cpu.s = 0x1FF; C.cpu.p = 0
    r = C.run(2_000_000)
    if r != 1 or C.hit_pc() != 0x033F03: print('oracle did not return', r, hex(C.hit_pc()), 'pc', hex(C.cpu.pb), hex(C.cpu.pc), 'count', count, ordb.hex(), rord.hex()); sys.exit(1)
    o_sc = bytes(C.mem[0x077F00:0x077F00 + 192]); o_f70 = C.rd16(0x030070)
    lib.t_set(names, count, ordb, res, rord, sc); lib.t_run()
    m_sc = bytes(lib.t_scores()[:192]); m_f70 = lib.t_f70()
    ins += o_f70
    if o_sc != m_sc or (o_f70 != 0) != (m_f70 != 0):
        bad += 1
        if bad < 4:
            d = [i for i in range(192) if o_sc[i] != m_sc[i]]; print('trial', t, 'differs at', d[:10], 'f70', o_f70, m_f70)
print('trials', trials, 'with new high scores', ins, 'differences', bad)
print('RESULT:', 'identical' if not bad else 'DIFFERENT')
