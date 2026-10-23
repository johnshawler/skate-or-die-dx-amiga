#!/usr/bin/env python3
"""Check cpu816 against the SingleStepTests/65816 suite (native mode only: the IIGS game code runs in native mode).
usage: sst_test.py <path to 65816/v1> [tests per opcode] [opcode hex ...]"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu816 as C
root = sys.argv[1]; per = int(sys.argv[2]) if len(sys.argv) > 2 else 300
only = {int(x, 16) for x in sys.argv[3:]}
C.io.enabled = 0
mem = C.mem; R = C.cpu
SKIP = {0x00: 'BRK', 0x02: 'COP', 0xDB: 'STP', 0xCB: 'WAI', 0x42: 'WDM'}
bad = {}; total = 0
for op in range(256):
    if only and op not in only: continue
    if op in SKIP: continue
    fn = f'{root}/{op:02x}.n.json'
    if not os.path.exists(fn): continue
    tests = json.load(open(fn))[:per]
    nbad = 0; first = None
    for t in tests:
        i, f = t['initial'], t['final']
        for a, v in i['ram']: mem[a] = v
        R.a, R.x, R.y, R.s, R.d, R.pc, R.db, R.pb, R.p = i['a'], i['x'], i['y'], i['s'], i['d'], i['pc'], i['dbr'], i['pbr'], i['p']
        C.run(1)
        got = (R.a, R.x, R.y, R.s, R.d, R.pc, R.db, R.pb, R.p)
        want = (f['a'], f['x'], f['y'], f['s'], f['d'], f['pc'], f['dbr'], f['pbr'], f['p'])
        ok = got == want
        ramdiff = []
        for a, v in f['ram']:
            if mem[a] != v: ok = False; ramdiff.append((a, mem[a], v))
        total += 1
        if not ok:
            nbad += 1
            if first is None: first = (t['name'], i, got, want, ramdiff[:3])
        for a, v in i['ram']: mem[a] = 0
        for a, v in f['ram']: mem[a] = 0
    if nbad: bad[op] = (nbad, len(tests), first)
print('tests run:', total, ' opcodes with mismatches:', len(bad))
for op, (n, m, first) in sorted(bad.items()):
    name, i, got, want, rd = first
    print('%02X: %d/%d fail; e.g. %s  p_in=%02x a_in=%04x x_in=%04x\n     got  A,X,Y,S,D,PC,DB,PB,P = %s\n     want                     = %s  ram %s' % (op, n, m, name, i['p'], i['a'], i['x'], [hex(v) for v in got], [hex(v) for v in want], rd))
