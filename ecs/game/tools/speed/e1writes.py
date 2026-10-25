import sys, os, collections
sys.path.insert(0,'/home/user/sod-amiga/ecs/game/tools')
import numpy as np
import lockstep_ecs as L
C=L.C; E=L.E
mode=int(sys.argv[1]); hookpc=0x030000|int(sys.argv[2],0); passes=int(sys.argv[3]); kind=sys.argv[4]
C.io.enabled=1
for k in range(4): C.io.pdl_left[k]=0
E.load_mode(mode)
script=L.make_script(kind,3)
C.hook(hookpc); C.hook(0x077E08)
cap=60_000_000
wl=np.zeros((cap,2),dtype=np.uint32)
C.wlog_start(wl.ctypes.data, cap)
L.apply_input(C.io,script(0))
hits=0; last=0; byrange=collections.Counter(); pcs=collections.Counter()
while True:
    r=C.run(50_000_000)
    if r!=1 or C.hit_pc()!=hookpc: break
    hits+=1
    n=min(C.wlog_n(),cap)
    for pc,av in wl[last:n]:
        a=int(av)>>8; b=a>>16; o=a&0xFFFF
        if b in (1,0xE1) and 0x2000<=o<0x9D00:
            win = o>=0x3400
            pc=int(pc)
            # the original bgfill's writes: bank 1 from the bgfill routine
            byrange[(hex(b),'window' if win else 'hud')]+=1
            pcs[(hex(b),'window' if win else 'hud',hex(pc))]+=1
    last=n
    if hits>=passes: break
    L.apply_input(C.io,script(hits))
    hp=C.hit_pc(); C.hook(hp,False); C.run(1); C.hook(hp,True)
print(dict(byrange))
for k,v in pcs.most_common(14): print(k,v)
