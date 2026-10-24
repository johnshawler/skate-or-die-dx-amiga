#!/usr/bin/env python3
"""Count 68000 or 68020 instructions per game pass of the translated game code (the generated C, -O2) in Unicorn; a copy of tools/t816/cost/run.py for the ECS study.
usage: run68k.py <cfg> <mode> <passes> <cpu 68000|68020> <natives 0|1>   (the picture conversion is not part of the pass: the ECS design draws planar)"""
import sys, os, json, subprocess, struct
here = os.path.abspath(f'{os.path.dirname(os.path.abspath(__file__))}/../../tools/cost68k'); G = os.path.abspath(f'{here}/../../game'); t = os.path.abspath(f'{here}/../../../tools/t816')
sys.path.insert(0, t); sys.path.insert(0, f'{t}/../cpu816')
from unicorn import *
from unicorn.m68k_const import *
import lockstep
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); cpu = sys.argv[4]; natives = sys.argv[5] == '1'; opt = '-O2'
W = '/tmp/cost68k_%s_%s' % (cfgn, cpu)
CC = 'm68k-linux-gnu-gcc'; FL = ['-m' + cpu, opt, '-fomit-frame-pointer', '-ffreestanding', '-fno-builtin', '-fno-pic', '-fno-strict-aliasing', '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-nostdlib', '-DT816_AMIGA', f'-I{t}/runtime', f'-I{G}/native', f'-I{t}/native', f'-I{t}/../../ecs/tools/cost68k']
os.makedirs(W, exist_ok=True)
subprocess.check_call([sys.executable, f'{t}/gen.py', f'{G}/cfg/{cfgn}.json', W + '/ev.c'], stderr=subprocess.DEVNULL)
if not natives:
    s_ = open(W + '/ev.c').read().replace('#define N_USE_NATIVE 1', ''); open(W + '/ev.c', 'w').write(s_)
for src, o in ((W + '/ev.c', 'ev.o'), (f'{t}/runtime/m816rt.c', 'rt.o'), (f'{here}/costdrv.c', 'drv.o')): subprocess.check_call([CC] + FL + ['-c', src, '-o', W + '/' + o])
ld = f'{t}/../../demo/src/link.ld'
subprocess.check_call(['m68k-linux-gnu-ld', '-T', ld, '-o', W + '/ev.elf', W + '/ev.o', W + '/rt.o', W + '/drv.o'], stderr=subprocess.DEVNULL)
subprocess.check_call(['m68k-linux-gnu-objcopy', '-O', 'binary', W + '/ev.elf', W + '/ev.bin'])
nm = subprocess.run(['m68k-linux-gnu-nm', W + '/ev.elf'], capture_output=True, text=True).stdout
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
code = open(W + '/ev.bin', 'rb').read()
mu = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); mu.ctl_set_cpu_model(UC_CPU_M68K_M68000 if cpu == '68000' else UC_CPU_M68K_M68020)
mu.mem_map(0, (len(code) + 0xFFFFF) & ~0xFFFFF); mu.mem_write(0, code)
mu.mem_map(0x1000000, 0xB0000); mu.mem_map(0x2000000, 0x100000)
# initial memory image from the oracle
import cpu816 as C, event_sim as E
E.load_mode(mode)
for i, b in enumerate([0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1]): mu.mem_write(0x1000000 + i * 0x10000, bytes(C.mem[b << 16:(b << 16) + 65536]))
io_addr = sym['io']
count = [0]; marks = []; passn = [0]
script = lockstep.make_script(os.environ.get('T816_KIND', 'random'), 5)
tab = [script(k) for k in range(passes + 3)]
def set_input(k):
    jx, jy, fire, key = tab[k]
    pl0 = 0 if jx < 0 else 300 if jx > 0 else 60; pl1 = 0 if jy < 0 else 300 if jy > 0 else 60
    # struct IIGSIO: key, key_waiting, anykey, mods, enabled, btn[3], pdl_len[4] (u16, offset 8), pdl_left[4]
    mu.mem_write(io_addr + 8, struct.pack('>HHHH', pl0, pl1, 60, 60)); mu.mem_write(io_addr + 5, bytes([0x80 if fire else 0, 0, 0]))
    mu.mem_write(io_addr + 0, bytes([key & 0x7F, 1 if key else 0, 1 if key else 0]))
import bisect, collections
syms = sorted((a, n) for n, a in sym.items()); saddr = [a for a, n in syms]; prof = collections.Counter()
bus = [0, 0, 0]          # 68000 bus cycles: instruction words, data accesses, taken transfers (each 4 clocks, a taken branch/jump/return costs about 4 more)
last = [None]; marks_bus = []
def hook_mem(uc, access, addr, size, value, ud):
    if addr < 0x20000000 and not (0x20E0000 <= addr < 0x2100000): bus[1] += (size + 1) // 2
    else: bus[1] += (size + 1) // 2
def hook_code(uc, addr, size, ud):
    count[0] += 1
    bus[0] += (size + 1) // 2
    if last[0] is not None and addr != last[0][0] + last[0][1]: bus[2] += 1
    last[0] = (addr, size)
    if os.environ.get('PROFILE') and passn[0] >= 3: prof[syms[bisect.bisect_right(saddr, addr) - 1][1]] += 1
    if addr == sym['rt_pass']:
        marks.append(count[0]); marks_bus.append(4 * (bus[0] + bus[1] + bus[2] * 1)); passn[0] += 1
        if passn[0] > passes: uc.emu_stop()
        else: set_input(passn[0])
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0, end=len(code))
mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, hook_mem)
mu.reg_write(UC_M68K_REG_A7, 0x20F0000)
set_input(0)
mu.reg_write(UC_M68K_REG_PC, sym['ev_start'])
mu.mem_write(0x20F0000, struct.pack('>I', 0xFFFFFFF0))
try: mu.emu_start(sym['ev_start'], 0xFFFFFFF0, timeout=0, count=0)
except UcError as e: print('stopped:', e)
d = [marks[i + 1] - marks[i] for i in range(len(marks) - 1)]
print('code size: %d bytes' % len(code))
db = [marks_bus[i + 1] - marks_bus[i] for i in range(len(marks_bus) - 1)]
print('bus-cycle estimate (4 clocks per instruction word / data access, +4 per taken transfer; lower bound, no internal cycles): mean %d, median %d, max %d clocks per pass' % (sum(db[1:]) / len(db[1:]), sorted(db[1:])[len(db[1:]) // 2], max(db[1:])))
print('passes measured:', len(d), ' %s instructions per pass' % cpu + ': first pass %d, then min %d, median %d, mean %d, max %d' % (d[0], min(d[1:]), sorted(d[1:])[len(d[1:]) // 2], sum(d[1:]) / len(d[1:]), max(d[1:])))

if os.environ.get('PROFILE'):
    tot = sum(prof.values())
    for n, c in prof.most_common(25): print('  %-22s %5.1f%%  %d per pass' % (n, 100.0 * c / tot, c / max(passes - 3, 1)))
