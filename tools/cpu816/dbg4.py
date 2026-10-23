import sys, random
sys.path.insert(0,'/home/user/sod-amiga/tools/cpu816')
import menu_fuzz as F, cpu816 as C
t=int(sys.argv[1]); seed=int(sys.argv[2]) if len(sys.argv)>2 else 1; lo=int(sys.argv[3]); hi=int(sys.argv[4])
rng = random.Random(seed*1000+t)
script, pokes = F.make_random(rng)
def tr(k,ev,mv):
    if lo<=k<=hi:
        d=[f for f in F.FIELDS if ev[f]!=mv[f]]
        print(k,'emu st',ev['state'],'c2',ev['c2'],'c4',ev['c4'],'c6',ev['c6'],'pass',ev['pass'],'cnt',ev['count'],'| model st',mv['state'],'c2',mv['c2'],'c4',mv['c4'],'c6',mv['c6'],'diff',d)
res = F.trial(script, 400, pokes=pokes, trace=tr)
print(res['bad'], res.get('pass_'), res['log'][-8:])
