import sys
sys.path.insert(0,'/home/user/sod-amiga/ecs/game/tools')
import lockstep_ecs as L
C=L.C; E=L.E
mode=int(sys.argv[1]); hookpc=0x030000|int(sys.argv[2],0); pref=sys.argv[3]; marks=[int(x) for x in sys.argv[4].split(',')]
C.io.enabled=1
for k in range(4): C.io.pdl_left[k]=0
E.load_mode(mode)
script=lambda k:(0,0,(1 if 20<=k<23 else 0),0)
C.hook(hookpc); C.hook(0x077E08)
L.apply_input(C.io,script(0))
hits=0
while True:
    r=C.run(50_000_000)
    if r!=1 or C.hit_pc()!=hookpc: break
    hits+=1
    if hits in marks: open('%s_%d.e1'%(pref,hits),'wb').write(bytes(C.mem[0xE12000:0xE12000+32000])); open('%s_%d.pal'%(pref,hits),'wb').write(bytes(C.mem[0xE19E00:0xE19E20]))
    if hits>=max(marks): break
    L.apply_input(C.io,script(hits))
    hp=C.hit_pc(); C.hook(hp,False); C.run(1); C.hook(hp,True)
