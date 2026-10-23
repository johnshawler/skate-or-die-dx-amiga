import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, '..')
import cpu816 as C, event_sim as E, dis816
from collections import Counter
mode = int(sys.argv[1]); mil = float(sys.argv[2])
fn = {1: 7, 2: 8, 3: 15, 4: 21, 5: 28}[mode]
data = open(f'{E.RES}/F{fn}.bin', 'rb').read()
E.load_mode(mode)
C.set_code_range(0x031000, 0x031000 + len(data))
C.prof_reset(); C.prof_enable(1)
tot = 0
while tot < mil * 1e6:
    r = C.run(1_000_000); tot += 1_000_000
    if r == 2: print('fault', hex(C.fault_pc())); break
pc = C.prof_pc()
executed = [a & 0xFFFF for a in range(0x030000, 0x040000) if pc[a]]
print('overlay F%d: %d bytes, %d distinct instruction addresses executed' % (fn, len(data), len(executed)))
# bytes belonging to executed instructions (decode with the flow disassembler at those addresses, using the loaded image, m/x unknown -> use recorded?)
ib = C.prof_records(2)
tgts = sorted(set(k & 0xFFFF for k, n in ib if (k >> 16) & 0xFF == 3 or True))
out = dis816.dis(data, 0x1000, [0x1000] + tgts, 0, 0)
code = set()
for a, (o, sz, mm, xx, txt, raw) in out.items():
    for i in range(1 + sz): code.add(a + i)
print('flow-disassembled instructions: %d, code bytes: %d  (range %04x-%04x)' % (len(out), len(code), min(out), max(out)))
w = C.prof_records(0)
hit = Counter()
for k, n in w:
    wa = k & 0xFFFF; wr = (k >> 32) & 0xFFFF
    if wa in code: hit[(wr, wa)] += n
print('writes into instruction bytes:', len(hit))
for (wr, wa), n in sorted(hit.items()):
    # find owning instruction
    own = max(a for a in out if a <= wa)
    print('  writer %04x -> %04x (inside insn %04x: %s)  x%d' % (wr, wa, own, out[own][4], n))
