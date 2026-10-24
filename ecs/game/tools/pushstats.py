import sys, os, json
G = os.path.dirname(os.path.abspath(__file__)) + '/..'
here = os.path.abspath(G + '/../../tools/t816')
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, lockstep
mode=int(sys.argv[1]); entry=0x030000|int(sys.argv[2],16); passes=int(sys.argv[3])
K={'N_VCNT':0x360C,'N_VX':0x35EC,'N_VW':0x35AC,'N_VOFF':0x358C,'N_VH':0x35CC}
cfg=json.load(open(f'{G}/cfg/ev28.json')); hook=0x030000|int(cfg['pass_hook'],0)
E.load_mode(mode); C.io.enabled=1; script=lockstep.make_script('waggle',1)
C.hook(hook); C.hook(entry); lockstep.apply_input(C.io,script(0))
k=0; tot=[];cur=0;calls=0;rects=0
while k<passes:
    r=C.run(50_000_000); p=(C.cpu.pb<<16)|C.cpu.pc
    if r!=1 or p==0x077E08: break
    if p==hook:
        k+=1; tot.append(cur); cur=0; lockstep.apply_input(C.io,script(k))
    elif p==entry:
        calls+=1
        n=C.rd16(0x030000|K['N_VCNT'])//2
        for i in range(n):
            ec=C.rd16(0x030000|K['N_VW']+2*i) if False else C.rd16(0x030000|(K['N_VW']+2*i)); ee=C.rd16(0x030000|(K['N_VH']+2*i))
            cur+=ee*2; rects+=1
    C.hook(p,False); C.run(1); C.hook(p,True)
tot=[t for t in tot if t>0]
print('passes with pushes',len(tot),'mean bytes/pass',sum(tot)/max(1,len(tot)),'max',max(tot),'calls',calls,'rects',rects)
