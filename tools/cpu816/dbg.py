import sys, random
sys.path.insert(0,'/home/user/sod-amiga/tools/cpu816')
import menu_fuzz as F, cpu816 as C
t=int(sys.argv[1]); seed=int(sys.argv[2]) if len(sys.argv)>2 else 1
rng = random.Random(seed*1000+t)
script, pokes = F.make_random(rng)
sc = bytearray()
for e in range(12):
    nm = bytes(rng.choice(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-') for _ in range(rng.randint(0, 9)))
    ent = nm.ljust(10, b'\0') + bytes([rng.randrange(256) if rng.random() < 0.5 else 0 for _ in range(3)]) + b'\0\0' + bytes([rng.randint(1, 15)])
    sc += ent
res = F.trial(script, 400, pokes=pokes, scores=bytes(sc))
print(res['bad'], res.get('pass_'), res['emu'] if not res['ok'] else '')
if res['ok']: sys.exit()
mv=F.model_vars(); ev=F.emu_vars()
print({k:(ev[k],mv[k]) for k in F.FIELDS if k in('state','c2','cx','cy','slot_x','slot_y','slot_f')})
ef,mf,hh=res['frames']
print('hist',hh)
d=[i for i in range(32000) if ef[i]!=mf[i]]
print(len(d))
if d:
    ys=sorted(set(i//160 for i in d)); xs=sorted(set(i%160 for i in d)); print('rows',ys[0],ys[-1],'cols',xs[0],xs[-1])
    for y in range(ys[0],min(ys[-1],ys[0]+30)+1):
        print('%3d'%y, ''.join('%X%X'%(ef[y*160+x]>>4,ef[y*160+x]&15) for x in range(xs[0],min(xs[-1],xs[0]+24)+1)),'|', ''.join('%X%X'%(mf[y*160+x]>>4,mf[y*160+x]&15) for x in range(xs[0],min(xs[-1],xs[0]+24)+1)))
import ctypes
sc_e=bytes(C.mem[0x077F00:0x077F00+192]); sc_m=bytes(F.lib.t_scores()[:192])
print('scores equal', sc_e==sc_m)
for i in range(6,9): print(i, sc_e[i*16:i*16+16], sc_m[i*16:i*16+16])
