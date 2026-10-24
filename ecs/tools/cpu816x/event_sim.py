#!/usr/bin/env python3
"""Load an event overlay (game modes 1-5) the way INTER's loader does and run it headless on the 65816 core.
usage: event_sim.py <mode> [instructions_millions] [out.png]   (exploratory: prints where it is spinning)"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu816 as C, overworld_sim as O, flow

RES = O.RES; RAW = O.RAW
TABLE = [l.split() for l in open(f'{O.ROOT}/docs/resource_table.txt') if l.strip()[:1].isdigit() and l.split()[1].startswith('/sod')]
MODES = {1: [0, 1, 2, 3, 4, 5, 6], 2: [0, 1, 2, 3, 4, 5, 7], 3: [8, 9, 10, 11, 12, 13, 14, 15], 4: [16, 17, 18, 19, 20, 21, 22],
         5: [23, 24, 25, 26, 27, 28, 29, 30], 6: [31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42]}

def load_mode(mode):
    C.load(0x076800, open(f'{RAW}/INTER', 'rb').read())
    C.load(0x078000, open(f'{RAW}/DRIVER', 'rb').read())
    for a in range(0x078000, 0x07803C, 3): C.rtl_at(a)
    C.rtl_at(0xE100A8)
    for idx in MODES[mode]:
        i, path, typ, arg, dest = TABLE[idx][:5]
        typ = int(typ); arg = int(arg, 16); bank, addr = [int(x, 16) for x in dest.split(':')]
        n = int(path.split('/f')[-1]) if '/f' in path else None
        if path == '/sod/highs': C.load(0x077F00, open(f'{RAW}/HIGHS', 'rb').read()); continue
        if path == '/sod/driver': continue
        d = open(f'{RES}/F{n}.bin', 'rb').read()
        base = (bank << 16) | addr
        if typ == 2:                                           # table of 7 arrays, each placed at a fixed stride
            cnt = struct.unpack('<H', d[:2])[0]
            for k in range(7): C.load(base + k * arg, d[2 + k * cnt * 2: 2 + (k + 1) * cnt * 2])
        elif typ == 1:                                         # u16 count (read into a variable by INTER), then count*2 bytes to the destination
            cnt = struct.unpack('<H', d[:2])[0]
            C.load(base, d[2:2 + cnt * 2])
        elif typ in (3, 5):                                    # 32-byte palette to the destination, the 16 KB of graphics after it to 04:D400 (3) / 05:0000 (5)
            C.load(base, d[:32]); C.load(0x04D400 if typ == 3 else 0x050000, d[32:32 + 0x4000])
        else:
            C.load(base, d)
    C.wr16(0x0768CE, mode)
    n = int(os.environ.get('T816_PLAYERS', 0))
    if n:                                                   # what INTER leaves for n players ($76991-$769EC, $7544): see also demo/src/evplat.h
        practice = int(os.environ.get('T816_PRACTICE', 0))
        C.wr16(0x0768CC, n)
        for k in range(n):
            for j, ch in enumerate(b'PLAYER%d' % (k + 1)): C.wr8(0x076810 + 16 * k + j, ch)
        for k, col in enumerate([0x0F00, 0x00F0, 0x000F, 0x0FF0, 0x0F0F, 0x0FFF, 0x0888, 0x0F80]): C.wr16(0x0768EE + 2 * k, col)
        C.wr16(0x077C2F, n); C.wr16(0x077E17, practice)
        for k in range(n + 1): C.wr8(0x077C1E + k, k)
        C.wr16(0x077E13, 0); C.wr16(0x077B8A, 0)
        computer = False
        if practice: C.wr16(0x077E00, 0x7B08); computer = True
        else:
            C.wr16(0x077E02, C.rd16(0x0768EE)); C.wr16(0x077E00, 0x6810)
            if mode in (4, 5):
                C.wr16(0x077B8A, 1)
                if 1 < n: C.wr16(0x077E0D, C.rd16(0x0768EE + 2)); C.wr16(0x077E0B, 0x6810 + 16)
                else: computer = True
        if computer: C.wr16(0x077E13, 0xFF); C.wr16(0x077E0B, 0x7AFF); C.wr16(0x077E0D, 0xF0); C.wr16(0x077E02, 0x0F00)
    C.cpu.pb = 3; C.cpu.pc = 0x1000; C.cpu.db = 0; C.cpu.d = 0; C.cpu.s = 0x1FF; C.cpu.p = 0x00

if __name__ == '__main__':
    mode = int(sys.argv[1]); mil = float(sys.argv[2]) if len(sys.argv) > 2 else 5
    out = sys.argv[3] if len(sys.argv) > 3 else None
    load_mode(mode)
    from collections import Counter
    pcs = Counter()
    total = 0
    while total < mil * 1e6:
        r = C.run(100000); total += 100000
        pcs[(C.cpu.pb << 16) | C.cpu.pc] += 1
        if r == 2: print('fault at %06x' % C.fault_pc()); break
    print('stopped; hottest PCs:', [('%06x' % a, n) for a, n in pcs.most_common(6)])
    if out: flow.shr_image(out, 2)
