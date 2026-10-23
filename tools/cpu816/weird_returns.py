import sys, os
sys.path.insert(0,'.')
import cpu816 as C, event_sim as E
mode=int(sys.argv[1]); mil=float(sys.argv[2])
E.load_mode(mode); C.prof_reset(); C.prof_enable(1)
tot=0
while tot<mil*1e6:
    r=C.run(1_000_000); tot+=1_000_000
    if r==2: print('fault'); break
pcs=C.prof_pc()
bad=set()
for k,n in C.prof_records(2):
    f=(k>>32)&0xFFFFFF; t=k&0xFFFFFF
    op=C.mem[f]
    if op==0x60:
        ok = (C.mem[t-3]==0x20 and pcs[t-3]>0) or (C.mem[t-4]==0x22 and pcs[t-4]>0) or (C.mem[t-3]==0xFC and pcs[t-3]>0)
        if not ok: bad.add((f,t))
    elif op==0x6B:
        if not (C.mem[t-4]==0x22 and pcs[t-4]>0): bad.add((f,t))
print('returns not following a JSR/JSL:', ['%06x->%06x'%b for b in sorted(bad)])
