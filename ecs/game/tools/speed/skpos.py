import sys, os, collections
sys.path.insert(0,'/home/user/sod-amiga/ecs/game/tools')
import numpy as np
import lockstep_ecs as L
C=L.C; E=L.E
mode=4; hookpc=0x031077
direction=sys.argv[1]   # right/left/up/down
C.io.enabled=1
for k in range(4): C.io.pdl_left[k]=0
E.load_mode(mode)
def script(k):
    fire = 1 if (k<150 and (k//40)%2) else 0
    if k<300: return (0,0,fire,0)
    d={'right':(1,0),'left':(-1,0),'up':(0,-1),'down':(0,1)}[direction]
    return (d[0],d[1],0,0)
C.hook(hookpc); C.hook(0x077E08)
L.apply_input(C.io,script(0))
snaps=[]; hits=0
while True:
    r=C.run(50_000_000)
    if r!=1 or C.hit_pc()!=hookpc: break
    hits+=1
    snaps.append(np.frombuffer(bytes(C.mem[0x030000:0x040000]),dtype='<u2').copy())
    if hits in (300,330,360,420,520): open('%s_%d.e1'%(sys.argv[2],hits),'wb').write(bytes(C.mem[0xE12000:0xE12000+32000])); open('%s_%d.pal'%(sys.argv[2],hits),'wb').write(bytes(C.mem[0xE19E00:0xE19E20]))   # bank 3 as 16-bit words (even offsets)
    if hits>=520: break
    L.apply_input(C.io,script(hits))
    hp=C.hit_pc(); C.hook(hp,False); C.run(1); C.hook(hp,True)
S=np.array(snaps[300:520]).astype(np.int32)    # passes 300..519
d=np.diff(S,axis=0)
# words that move by a small constant in a consistent direction for many passes
cand=[]
for w in range(S.shape[1]):
    col=d[:,w]
    nz=col[col!=0]
    if len(nz)>=40 and np.all(np.abs(nz)<=16):
        pos=(nz>0).mean()
        if pos>0.9 or pos<0.1: cand.append((w*2, len(nz), float(nz.mean()), int(S[0,w]), int(S[-1,w])))
cand.sort(key=lambda t:-t[1])
print('words that drift steadily (offset, #moves in 220 passes, mean step, first, last):')
for c in cand[:25]: print(hex(c[0]),c[1],round(c[2],2),c[3],c[4])
