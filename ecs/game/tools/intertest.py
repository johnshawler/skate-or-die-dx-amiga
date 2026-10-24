#!/usr/bin/env python3
"""INTER (the original's event/competition driver, bank 7 $6800-$7BFF) translated to C against the original code on the 65816 oracle.
The events themselves are faked: whenever INTER jumps to $031000 (run the loaded overlay) the test writes a result as the event would ($7E04.. scores) and enters INTER at its
event-exit vector ($7578). Compares, at every such stop and at the end, bank 7 $6800-$7FFF, the text page ($E0:0400-07FF), the keys read and the text page at every key wait.
usage: intertest.py <players 1-8> <practice 0/1> <compete_all 0/1> <first event 1-5> <seed> [keys...]"""
import sys, os, json, ctypes, subprocess, random
G = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..')
T = os.path.abspath(G + '/../../tools/t816'); X = os.path.abspath(G + '/../tools/cpu816x')
sys.path.insert(0, X); sys.path.insert(0, T)
import cpu816 as C, overworld_sim as O
np_, practice, compete_all, ev0, seed = [int(a) for a in sys.argv[1:6]]
RAW = O.RAW
W = '/tmp/intertest'; os.makedirs(W, exist_ok=True)
# ---- build the translated INTER for the host
subprocess.check_call([sys.executable, f'{G}/tools/gen_ecs.py', f'{G}/cfg/inter.json', W + '/inter.c'], stderr=subprocess.DEVNULL)
open(W + '/stubs.c', 'w').write(r'''
#include "m816.h"
void inter_prodos(void) { cpu.c = 0; }
void inter_scenes(void) { }
int test_nkeys = 0; unsigned char test_keys[64]; int test_ki = 0; int test_snaps = 0; unsigned char test_snap[64][1024]; int test_btn = 0;
void io_poll(void)
{
    if (test_snaps < 64) { memcpy(test_snap[test_snaps++], BE0 + 0x400, 1024); }
    if (test_ki < test_nkeys) { io.key = test_keys[test_ki++]; io.key_waiting = 1; io.anykey = 1; }
    else { io.key = 0x20; io.key_waiting = 1; io.anykey = 1; }
}
''')
inc = [f'-I{G}/runtime', f'-I{T}/runtime', f'-I{G}/native']
subprocess.check_call(['gcc', '-O1', '-fPIC', '-c', '-Dio_poll=io_poll_default'] + inc + ['-o', W + '/rt.o', f'{T}/runtime/m816rt.c'])
subprocess.check_call(['gcc', '-O1', '-fPIC', '-c'] + inc + ['-o', W + '/inter.o', W + '/inter.c'])
subprocess.check_call(['gcc', '-O1', '-fPIC', '-c'] + inc + ['-o', W + '/stubs.o', W + '/stubs.c'])
subprocess.check_call(['gcc', '-shared', '-o', W + '/lib.so', W + '/inter.o', W + '/stubs.o', W + '/rt.o'])
rt = ctypes.CDLL(W + '/lib.so')
rt.rt_bank_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 65536); rt.rt_cpu.restype = ctypes.c_void_p; rt.rt_io_ptr.restype = ctypes.c_void_p
class Cpu(ctypes.Structure): _fields_ = [('a', ctypes.c_uint16), ('x', ctypes.c_uint16), ('y', ctypes.c_uint16), ('s', ctypes.c_uint16), ('d', ctypes.c_uint16), ('db', ctypes.c_uint8), ('n', ctypes.c_uint8), ('v', ctypes.c_uint8), ('z', ctypes.c_uint8), ('c', ctypes.c_uint8), ('i', ctypes.c_uint8)]
cp = Cpu.from_address(rt.rt_cpu()); nio = C.IO.from_address(rt.rt_io_ptr())
BANKS = [0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1]
keys = [int(k, 0) for k in sys.argv[6:]] or [0x0D]
# ---- initial machine (what F39 leaves when it jumps to $076800)
C.load(0x076800, open(f'{RAW}/INTER', 'rb').read()); C.load(0x078000, open(f'{RAW}/DRIVER', 'rb').read()); C.load(0x077F00, open(f'{RAW}/HIGHS', 'rb').read())
for a in range(0x078000, 0x07803C, 3): C.rtl_at(a)
C.wr8(0x076F7C, 0x60)                                                       # the scene runner does nothing
rnd = random.Random(seed)
def w8(a, v): C.wr8(a, v)
def w16(a, v): C.wr16(a, v)
for k in range(0x1A): w8(0x077E00 + k, C.rd8(0x077C50 + k))             # INTER's boot copies this template to $7E00
for k in range(0x24): w8(0x0768A8 + k, 0)
for k in range(12): w8(0x0768E2 + k, 0)
for k in range(8): w8(0x0768D2 + k, 0); w8(0x0768DA + k, k)
w16(0x0768CC, np_); w16(0x0768D0, compete_all); w16(0x077E17, practice)
for k in range(np_):
    for j, ch in enumerate(('PLAYER%d' % (k + 1)).encode().ljust(15)): w8(0x076810 + 16 * k + j, ch if j < len(('PLAYER%d' % (k + 1))) else 0)
for k, col in enumerate([0x0F00, 0x00F0, 0x000F, 0x0FF0, 0x0F0F, 0x0FFF, 0x0888, 0x0F80]): w16(0x0768EE + 2 * k, col)
for k in range(8): w8(0x0768FE + k, k + 1)
def snapshot(): return {b: bytes(C.mem[(0x70000 if b == 7 else (b << 16)):(0x70000 if b == 7 else (b << 16)) + 65536]) for b in BANKS}
init = snapshot()
rt.rt_init()
for b in BANKS: ctypes.memmove(rt.rt_bank_ptr(b), init[b], 65536)             # rt_init clears some tables
nio.enabled = 1; C.io.enabled = 1; nio.key_waiting = 0; nio.key = 0; nio.anykey = 0; C.io.key_waiting = 0; C.io.key = 0; C.io.anykey = 0
def bank7(): return bytes(C.mem[0x76800:0x78000]), bytes(rt.rt_bank_ptr(7).contents[0x6800:0x8000])
def text(): return bytes(C.mem[0xE00400:0xE00800]), bytes(rt.rt_bank_ptr(0xE0).contents[0x400:0x800])
# ---- run both
def set_result(stop):
    r = random.Random(seed * 1000 + stop)
    sc = [r.randrange(0, 10**6) for _ in range(2)]
    bcd = lambda v: bytes(int(str(v).zfill(6)[i:i + 2], 16) for i in (4, 2, 0))   # 3 bytes little endian BCD
    res = {0x077E04: bcd(sc[0]), 0x077E0F: bcd(sc[1] // 3)}
    ab = 0
    return res, ab
log = []
rt_stop = 0
# oracle
C.cpu.pb = 7; C.cpu.pc = 0x6800; C.cpu.db = 3; C.cpu.d = 0; C.cpu.s = 0x1FF; C.cpu.p = 0; C.cpu.x = ev0; C.cpu.a = 0; C.cpu.y = 0
cp.x = ev0; cp.s = 0x1FF; cp.d = 0; cp.db = 3; cp.a = 0; cp.y = 0; cp.n = cp.v = cp.z = cp.c = cp.i = 0
for pc in (0x031000, 0x0775D3, 0x077769, 0x077886): C.hook(pc)
ki = 0; osnaps = []; stops = 0
rt.test_nkeys = ctypes.c_int.in_dll(rt, 'test_nkeys'); 
ctypes.c_int.in_dll(rt, 'test_nkeys').value = len(keys)
for i, k in enumerate(keys): ctypes.c_ubyte.in_dll(rt, 'test_keys') if False else None
arr = (ctypes.c_ubyte * 64).in_dll(rt, 'test_keys')
for i, k in enumerate(keys): arr[i] = k
o_stops = []
steps = 0
while True:
    r = C.run(2_000_000); p = (C.cpu.pb << 16) | C.cpu.pc; steps += 1
    if r == 2: print('oracle fault at %06x' % C.fault_pc()); break
    if p == 0x031000:
        o_stops.append((C.rd16(0x0768CE), bytes(C.mem[0x76800:0x78000]), bytes(C.mem[0xE00400:0xE00800])))
        ev = C.rd16(0x0768CE)
        if ev == 0 or ev >= 6 or len(o_stops) > 60: break
        res, ab = set_result(len(o_stops))
        for a, v in res.items():
            for j, b in enumerate(v): w8(a + j, b)
        w8(0x077E19, ab); w8(0x077E1A, 0)
        C.cpu.pb = 7; C.cpu.pc = 0x7578; C.cpu.s = 0x1FF; C.cpu.p = 0; C.cpu.db = 3; C.cpu.d = 0
        continue
    if p in (0x0775D3, 0x077769, 0x077886):
        osnaps.append(bytes(C.mem[0xE00400:0xE00800]))
        k = keys[ki] if ki < len(keys) else 0x20; ki += 1
        C.io.key = k; C.io.key_waiting = 1; C.io.anykey = 1
        C.hook(p, False); C.run(1); C.hook(p, True); continue
    if steps > 5000: print('oracle did not finish'); break
# translated
t_stops = []; tsn = 0
entry = 0x692D
rt.rt_run.argtypes = [ctypes.c_uint16]; rt.rt_run.restype = ctypes.c_int
while True:
    r = rt.rt_run(entry)
    got = ctypes.c_uint32.in_dll(rt, 'rt_err_got').value
    if r != 1 or got != 0x031000: print('translated stopped: rt_exit=%d target=%06x' % (r, got)); break
    b7 = bytes(rt.rt_bank_ptr(7).contents[0x6800:0x8000]); e0 = bytes(rt.rt_bank_ptr(0xE0).contents[0x400:0x800])
    ev = b7[0x68CE - 0x6800] | b7[0x68CF - 0x6800] << 8
    t_stops.append((ev, b7, e0))
    if ev == 0 or ev >= 6 or len(t_stops) > 60: break
    res, ab = set_result(len(t_stops))
    p7 = rt.rt_bank_ptr(7)
    for a, v in res.items():
        for j, b in enumerate(v): p7.contents[(a & 0xFFFF) + j] = b
    p7.contents[0x7E19] = ab; p7.contents[0x7E1A] = 0
    cp.s = 0x1FF; cp.d = 0; cp.db = 3
    entry = 0x7578
# compare
nsn = ctypes.c_int.in_dll(rt, 'test_snaps').value
tsnaps = [bytes(ctypes.string_at(ctypes.addressof(ctypes.c_ubyte.in_dll(rt, 'test_snap')) + 1024 * i, 1024)) for i in range(nsn)]
print('stops: oracle %d translated %d; key waits: oracle %d translated %d' % (len(o_stops), len(t_stops), len(osnaps), len(tsnaps)))
bad = 0
for i, (o, t) in enumerate(zip(o_stops, t_stops)):
    if o[0] != t[0]: print('stop %d: event %d vs %d' % (i, o[0], t[0])); bad += 1
    d = [j for j in range(len(o[1])) if o[1][j] != t[1][j]]
    if d: print('stop %d: bank 7 differs at %s' % (i, ['%04X(%02x/%02x)' % (0x6800 + j, o[1][j], t[1][j]) for j in d[:8]])); bad += 1
    if o[2] != t[2]: print('stop %d: text page differs' % i); bad += 1
for i, (o, t) in enumerate(zip(osnaps, tsnaps)):
    if o != t: print('key wait %d: text page differs (%d bytes)' % (i, sum(1 for a, b in zip(o, t) if a != b))); bad += 1
print('RESULT:', 'identical' if not bad and len(o_stops) == len(t_stops) and len(osnaps) == len(tsnaps) else 'DIFFERENT (%d)' % bad)
if os.environ.get('SHOW'):
    def page(b):
        rows = [0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8, 0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0]
        return '\n'.join(''.join(chr((c & 0x7F) if (c & 0x7F) >= 0x20 else 0x2E) if c else ' ' for c in b[r:r + 40]) for r in rows).rstrip()
    for i in range(min(len(osnaps), len(tsnaps))):
        if osnaps[i] != tsnaps[i]:
            print('--- key wait %d oracle' % i); print(page(osnaps[i])); print('--- translated'); print(page(tsnaps[i])); break
if os.environ.get('SHOWALL'):
    rows = [0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8, 0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0]
    def pg(b): return '\n'.join(''.join(chr((c & 0x7F) if (c & 0x7F) >= 0x20 else 0x2E) if c else ' ' for c in b[r:r + 40]) for r in rows).rstrip()
    for nm, sn in (('oracle', osnaps), ('translated', tsnaps)):
        for i, s in enumerate(sn): print('=== %s wait %d' % (nm, i)); print(pg(s))
