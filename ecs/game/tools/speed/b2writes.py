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
hits=0; byb=collections.Counter(); pcs=collections.Counter(); last=0
while True:
    r=C.run(50_000_000)
    if r!=1 or C.hit_pc()!=hookpc: break
    hits+=1
    n=min(C.wlog_n(),cap)
    seg=wl[last:n]; last=n
    for pc,av in seg:
        a=int(av)>>8; b=a>>16
        byb[b]+=1
        if b==2: pcs[hex(int(pc))]+=1
    if hits>=passes: break
    L.apply_input(C.io,script(hits))
    hp=C.hit_pc(); C.hook(hp,False); C.run(1); C.hook(hp,True)
print('writes per bank',{hex(k):v for k,v in byb.items()})
print('bank 2 writers (pc: count)',pcs.most_common(12))
