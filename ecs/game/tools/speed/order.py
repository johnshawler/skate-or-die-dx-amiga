import sys, os, collections
sys.path.insert(0,'/home/user/sod-amiga/ecs/game/tools')
import numpy as np
import lockstep_ecs as L
C=L.C; E=L.E
mode=int(sys.argv[1]); hookpc=0x030000|int(sys.argv[2],0); bgpc=0x030000|int(sys.argv[3],0); passes=int(sys.argv[4]); kind=sys.argv[5]
C.io.enabled=1
for k in range(4): C.io.pdl_left[k]=0
E.load_mode(mode)
script=L.make_script(kind,3)
C.hook(hookpc); C.hook(bgpc); C.hook(0x077E08)
cap=60_000_000
wl=np.zeros((cap,2),dtype=np.uint32)
C.wlog_start(wl.ctypes.data, cap)
L.apply_input(C.io,script(0))
hits=0; last=0; seq=[]
def step_off():
    hp=C.hit_pc(); C.hook(hp,False); C.run(1); C.hook(hp,True)
b2_after_bg=0; b2_before_bg=0; passes_seen=0; bgn=None
while True:
    r=C.run(50_000_000)
    if r!=1: break
    pc=C.hit_pc()
    n=min(C.wlog_n(),cap)
    if pc==bgpc:
        bgn=n
        b2=sum(1 for pc2,av in wl[last:n] if (int(av)>>24)==2)   # b2 writes since the pass start, before bgfill
        b2_before_bg+=b2
    elif pc==hookpc:
        hits+=1
        if bgn is not None:
            b2a=sum(1 for pc2,av in wl[bgn:n] if (int(av)>>24)==2)
            b2_after_bg+=b2a
        last=n; bgn=None
        if hits>=passes: break
        L.apply_input(C.io,script(hits))
    else: break
    step_off()
print('bank-2 writes before the bgfill call (per pass)', b2_before_bg/ max(hits,1), ' after it, before the next pass hook', b2_after_bg/max(hits,1))
