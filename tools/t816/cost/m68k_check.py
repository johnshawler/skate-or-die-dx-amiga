#!/usr/bin/env python3
"""Run the translated code compiled for the 68020 (as in the Amiga build) in Unicorn and compare memory with the original code on the
oracle after N passes.  usage: m68k_check.py <cfg> <mode> <passes> [opt]   (T816_KIND / T816_NOESC / seed via T816_SEED)"""
import sys, os, json, subprocess, struct
here = os.path.dirname(os.path.abspath(__file__)); t = f'{here}/..'
sys.path.insert(0, t); sys.path.insert(0, f'{t}/../cpu816')
from unicorn import *
from unicorn.m68k_const import *
import lockstep, cpu816 as C, event_sim as E
cfgn, mode, passes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); opt = sys.argv[4] if len(sys.argv) > 4 else '-O2'
seed = int(os.environ.get('T816_SEED', 5)); kind = os.environ.get('T816_KIND', 'random')
CC = 'm68k-linux-gnu-gcc'; FL = ['-m68020', opt, '-fomit-frame-pointer', '-ffreestanding', '-fno-builtin', '-fno-pic', '-fno-strict-aliasing', '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-nostdlib', '-DT816_AMIGA', f'-I{t}/runtime', f'-I{t}/native', f'-I{t}/../../demo/src']
os.makedirs('/tmp/cost', exist_ok=True)
cfg = json.load(open(f'{t}/cfg/{cfgn}.json'))
subprocess.check_call([sys.executable, f'{t}/gen.py', f'{t}/cfg/{cfgn}.json', '/tmp/cost/ev.c'], stderr=subprocess.DEVNULL)
for src, o in (('/tmp/cost/ev.c', 'ev.o'), (f'{t}/runtime/m816rt.c', 'rt.o'), (f'{here}/costdrv.c', 'drv.o')): subprocess.check_call([CC] + FL + ['-c', src, '-o', f'/tmp/cost/{o}'])
subprocess.check_call(['m68k-linux-gnu-ld', '-T', f'{t}/../../demo/src/link.ld', '-o', '/tmp/cost/ev.elf', '/tmp/cost/ev.o', '/tmp/cost/rt.o', '/tmp/cost/drv.o'], stderr=subprocess.DEVNULL)
subprocess.check_call(['m68k-linux-gnu-objcopy', '-O', 'binary', '/tmp/cost/ev.elf', '/tmp/cost/ev.bin'])
nm = subprocess.run(['m68k-linux-gnu-nm', '/tmp/cost/ev.elf'], capture_output=True, text=True).stdout
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
code = open('/tmp/cost/ev.bin', 'rb').read()
BANKS = [0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1]
script = lockstep.make_script(kind, seed); tab = [script(k) for k in range(passes + 3)]
hooks = [0x030000 | int(cfg['pass_hook'], 0)] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
# ---- oracle
E.load_mode(mode); C.io.enabled = 1
for k in range(4): C.io.pdl_left[k] = 0
init = {b: bytes(C.mem[b << 16:(b << 16) + 65536]) for b in BANKS}
regs0 = (C.cpu.a, C.cpu.x, C.cpu.y, C.cpu.s, C.cpu.d, C.cpu.db, C.cpu.p)
[C.hook(h) for h in hooks]; C.hook(0x077E08)
hits = 0; lockstep.apply_input(C.io, tab[0])
while True:
    r = C.run(50_000_000)
    if r != 1 or C.hit_pc() not in hooks: print('oracle: event ended after', hits, 'passes'); break
    hits += 1
    if hits > passes: break
    lockstep.apply_input(C.io, tab[hits])
    hp = C.hit_pc(); C.hook(hp, False); C.run(1); C.hook(hp, True)
passes = min(passes, hits)
om = {b: bytes(C.mem[b << 16:(b << 16) + 65536]) for b in BANKS}
# ---- 68k binary
mu = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); mu.ctl_set_cpu_model(UC_CPU_M68K_M68020)
mu.mem_map(0, (len(code) + 0xFFFFF) & ~0xFFFFF); mu.mem_write(0, code)
mu.mem_map(0x1000000, 0xB0000); mu.mem_map(0x2000000, 0x100000)
for i, b in enumerate(BANKS): mu.mem_write(0x1000000 + i * 0x10000, init[b])
io_addr = sym['io']; passn = [0]
def set_input(k):
    jx, jy, fire, key = tab[k]
    pl0 = 0 if jx < 0 else 300 if jx > 0 else 60; pl1 = 0 if jy < 0 else 300 if jy > 0 else 60
    mu.mem_write(io_addr + 8, struct.pack('>HHHH', pl0, pl1, 60, 60)); mu.mem_write(io_addr + 5, bytes([0x80 if fire else 0, 0, 0]))
    mu.mem_write(io_addr + 0, bytes([key & 0x7F, 1 if key else 0, 1 if key else 0]))
def hook_code(uc, addr, size, ud):
    if addr == sym['rt_pass']:
        passn[0] += 1
        if passn[0] > passes: uc.emu_stop()
        else: set_input(passn[0])
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0, end=len(code))
mu.reg_write(UC_M68K_REG_A7, 0x20F0000); set_input(0)
mu.mem_write(0x20F0000, struct.pack('>I', 0xFFFFFFF0))
try: mu.emu_start(sym['ev_start'], 0xFFFFFFF0, timeout=0, count=0)
except UcError as e: print('stopped:', e)
bad = 0
for i, b in enumerate(BANKS):
    tm = bytes(mu.mem_read(0x1000000 + i * 0x10000, 65536))
    d = [j for j in range(65536) if tm[j] != om[b][j]]
    if d: bad += 1; print('bank %02x: %d bytes differ, first %s' % (b, len(d), [(hex(j), om[b][j], tm[j]) for j in d[:6]]))
print('passes %d, opt %s: %s' % (passes, opt, 'IDENTICAL memory' if not bad else 'DIFFERENT'))
