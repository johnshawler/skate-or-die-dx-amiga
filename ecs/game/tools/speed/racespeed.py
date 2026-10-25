import sys, os, collections
sys.path.insert(0,'/home/user/sod-amiga/ecs/game/tools')
import lockstep_ecs as L
C=L.C; E=L.E
mode=int(sys.argv[1]); passes=int(sys.argv[2]); kind=sys.argv[3]; hookpc=int(sys.argv[4],0)|0x030000; yv=int(sys.argv[5],0)
fire_at=int(sys.argv[6]) if len(sys.argv)>6 else -1
C.io.enabled=1
for k in range(4): C.io.pdl_left[k]=0
E.load_mode(mode)
if kind.startswith('const:'):
    jx,jy=[int(x) for x in kind[6:].split(',')]
    script=lambda k:(jx,jy,0,0)
elif kind.startswith('alt:'):
    N=int(kind[4:]); script=lambda k:((1 if (k//N)%2 else -1),0,0,0)
elif kind.startswith('startthen:'):
    M=int(kind[10:]); script=lambda k:(0,0,(1 if (k<M and (k//40)%2) else 0),0)
elif kind.startswith('push:'):
    _,jx,jy,N=kind.split(':'); jx=int(jx); jy=int(jy); N=int(N)
    script=lambda k:(jx,jy,(1 if (k//N)%2 else 0),0)
else: script=L.make_script(kind,1)
C.hook(hookpc); C.hook(0x077E08)
L.apply_input(C.io,script(0))
hits=0; res=[]
while True:
    r=C.run(50_000_000)
    if r!=1 or C.hit_pc()!=hookpc: print('stop',r,hex(C.hit_pc())); break
    hits+=1
    res.append(C.rd16(0x030000+yv))
    if hits>=passes: break
    inp=script(hits)
    if fire_at>=0 and hits>=fire_at and hits<fire_at+3 and not kind.startswith('push:'): inp=(inp[0],inp[1],1,inp[3])
    L.apply_input(C.io,inp)
    hp=C.hit_pc(); C.hook(hp,False); C.run(1); C.hook(hp,True)
print('SUMMARY rows moved', (res[-1]-res[0])//160, 'in', len(res), 'passes; max per pass', max(((b-a)//160 if 0<=(b-a)<=8*160 else 0) for a,b in zip(res,res[1:])))
import collections
print('TIMELINE rows at every 25 passes', [res[i]//160 for i in range(0,len(res),25)])
last=None
for i,y in enumerate(res):
    if True: break
    if i%5==0 or i<3: print(i,hex(y),y//160, '' if last is None else (y-last)//160)
    last=y
