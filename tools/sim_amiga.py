#!/usr/bin/env python3
"""CPU-level test of demo/build/OverworldDemo: a 68020 (Unicorn) runs the real hunk executable against a small
simulation of the Amiga hardware it touches (custom registers, CIA keyboard/fire, exec/graphics library calls).
It checks, against an independent Python model:
  * the hunk loads and relocates, and the program takes over and gives back the machine in the expected order
  * the screen it builds (copper-list palette + bitplane pointers) matches map + skater composited per pixel
  * steering from joystick and cursor keys, and the 16-direction logic, match the model frame by frame
  * the audio DMA block chaining cycles 0,1,2,3,0,... both when the hardware raises a flag at DMA start and when it does not
This proves the program logic against MY model of the hardware. It does not replace a run under Kickstart or MiSTer."""
import math, os, struct, sys
from unicorn import *
from unicorn.m68k_const import *
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amiga_pack
from overworld_model import Skater

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
EXE = f'{ROOT}/demo/build/OverworldDemo'
MAP = open(f'{ROOT}/demo/assets/map.scr', 'rb').read()
SPR = open(f'{ROOT}/demo/assets/skater.spr', 'rb').read()
MUSIC_LEN = os.path.getsize(f'{ROOT}/demo/assets/music.raw')

BASE = 0x00100000; CHIP = 0x00200000; STACK = 0x00500000
EXEC = 0x2000; GFX = 0x3000; CUSTOM = 0xDFF000

HEAD = {'vx': [round(-math.sin(math.radians(22.5*d))*256) for d in range(16)],
        'vy': [round(math.cos(math.radians(22.5*d))*256) for d in range(16)],
        'dir': [6, 8, 10, 4, 0, 12, 2, 0, 14]}

def joy(right=0, left=0, up=0, down=0):
    j = 0
    if right: j |= 2
    if left: j |= 0x200
    if (1 if right else 0) ^ (1 if down else 0): j |= 1
    if (1 if left else 0) ^ (1 if up else 0): j |= 0x100
    return j

def load_hunk(data):
    p = 0
    def u32():
        nonlocal p
        v = struct.unpack('>I', data[p:p+4])[0]; p += 4; return v
    assert u32() == 0x3F3 and u32() == 0
    tab = u32(); first = u32(); last = u32(); size = u32()
    assert tab == 1 and first == 0 and last == 0
    assert u32() == 0x3E9; n = u32(); code = bytearray(data[p:p+n*4]); p += n*4
    relocs = []
    while True:
        t = u32()
        if t == 0x3EC:
            while True:
                cnt = u32()
                if cnt == 0: break
                assert u32() == 0
                relocs += [u32() for _ in range(cnt)]
        elif t == 0x3F2: break
        else: raise SystemExit('unexpected hunk id %x' % t)
    return code, relocs

class Sim:
    def __init__(self, script, aud_flag_at_start, aud_block_frames=12, max_iter=400):
        self.script = script; self.flag_at_start = aud_flag_at_start; self.blk_frames = aud_block_frames
        self.max_iter = max_iter
        self.log = []; self.intreq = 0; self.reads = 0; self.vbl_count = 0; self.iter = -3
        self.dma = 0; self.aud_on_frame = None; self.blocks_played = []; self.lc = [0, 0]
        self.captures = {}; self.alloc_ptr = CHIP; self.allocs = []; self.pcm = None
        self.keyq = []; self.key_pending = False; self.line = 0
        self.writes = {}; self.w32 = {}; self.state = {}
        mu = self.mu = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        mu.ctl_set_cpu_model(UC_CPU_M68K_M68020)
        mu.mem_map(0, 0x4000); mu.mem_map(BASE, 0x100000); mu.mem_map(CHIP, 0x200000); mu.mem_map(STACK, 0x10000)
        mu.mem_map(CUSTOM, 0x1000); mu.mem_map(0xBFC000, 0x4000)
        code, relocs = load_hunk(open(EXE, 'rb').read())
        for o in relocs:
            v = struct.unpack('>I', code[o:o+4])[0] + BASE; code[o:o+4] = struct.pack('>I', v)
        mu.mem_write(BASE, bytes(code))
        mu.mem_write(4, struct.pack('>I', EXEC)); mu.mem_write(EXEC + 0x212, bytes([50]))
        mu.mem_write(GFX + 34, struct.pack('>I', 0x1234)); mu.mem_write(GFX + 38, struct.pack('>I', 0x5678))
        for lvo in (-132, -138, -198, -210, -552, -414): mu.mem_write(EXEC + lvo, b'\x4e\x75')
        for lvo in (-222, -270): mu.mem_write(GFX + lvo, b'\x4e\x75')
        mu.mem_write(0xBFE001, b'\xFF')
        mu.hook_add(UC_HOOK_CODE, self.on_code, begin=EXEC - 600, end=EXEC)
        mu.hook_add(UC_HOOK_CODE, self.on_code, begin=GFX - 300, end=GFX)
        mu.hook_add(UC_HOOK_MEM_WRITE, self.on_write, begin=CUSTOM, end=CUSTOM + 0xFFF)
        mu.hook_add(UC_HOOK_MEM_READ, self.on_read, begin=CUSTOM, end=CUSTOM + 0xFFF)
        mu.hook_add(UC_HOOK_MEM_READ, self.on_cia_read, begin=0xBFE000, end=0xBFEFFF)

    # ---- library calls ----
    def on_code(self, uc, addr, size, ud):
        reg = lambda r: uc.reg_read(r)
        if addr == EXEC - 198:
            sz, fl = reg(UC_M68K_REG_D0), reg(UC_M68K_REG_D1)
            assert fl & 2, 'AllocMem should ask for chip RAM'
            a = (self.alloc_ptr + 7) & ~7; self.alloc_ptr = a + sz; self.allocs.append((a, sz))
            self.pcm = a + 32000 * 2 + 1024
            uc.reg_write(UC_M68K_REG_D0, a); self.log.append(('AllocMem', sz))
        elif addr == EXEC - 210: self.log.append(('FreeMem', reg(UC_M68K_REG_A1), reg(UC_M68K_REG_D0)))
        elif addr == EXEC - 552:
            name = uc.mem_read(reg(UC_M68K_REG_A1), 17).decode(); assert name.startswith('graphics.library')
            uc.reg_write(UC_M68K_REG_D0, GFX); self.log.append(('OpenLibrary', 'graphics.library'))
        elif addr == EXEC - 414: self.log.append(('CloseLibrary', reg(UC_M68K_REG_A1)))
        elif addr == EXEC - 132: self.log.append(('Forbid',))
        elif addr == EXEC - 138: self.log.append(('Permit',))
        elif addr == GFX - 222: self.log.append(('LoadView', reg(UC_M68K_REG_A1)))
        elif addr == GFX - 270: self.log.append(('WaitTOF',))

    # ---- chipset model ----
    def put16(self, a, v): self.mu.mem_write(a, struct.pack('>H', v & 0xFFFF))
    def on_read(self, uc, access, addr, size, value, ud):
        r = addr - CUSTOM
        if r == 0x01E:
            self.reads += 1
            if self.reads % 8 == 0: self.intreq |= 0x20
            self.put16(addr, self.intreq)
        elif r == 0x002: self.put16(addr, self.dma & 0x07FF | 0x4000)
        elif r == 0x01C: self.put16(addr, 0x6020)
        elif r == 0x006: self.line = (self.line + 1) & 0xFF; self.put16(addr, self.line << 8)
        elif r == 0x00C: self.put16(addr, self.script.get('joy', lambda i: 0)(self.iter))
    def on_cia_read(self, uc, access, addr, size, value, ud):
        if addr == 0xBFE001:
            uc.mem_write(addr, bytes([0xFF & ~(0x80 if self.script.get('fire', lambda i: 0)(self.iter) else 0)]))
        elif addr == 0xBFED01:
            ev = self.script.get('keys', {}).get(self.iter)
            if ev is not None and ev not in self.keyq and self.iter not in self.state.get('keys_done', set()):
                self.keyq.append(ev); self.state.setdefault('keys_done', set()).add(self.iter)
            uc.mem_write(addr, bytes([0x08 if self.keyq else 0]))
        elif addr == 0xBFEC01:
            kc = self.keyq.pop(0) if self.keyq else 0
            rol = ((kc << 1) | (kc >> 7)) & 0xFF
            uc.mem_write(addr, bytes([(~rol) & 0xFF]))
    def on_write(self, uc, access, addr, size, value, ud):
        r = addr - CUSTOM
        if size == 4:
            self.w32[r] = value
            if r in (0x0A0, 0x0B0): self.lc[(r - 0x0A0) // 16] = value
            return
        v = value & 0xFFFF; self.writes[r] = v
        if r == 0x09C:                                   # INTREQ
            if v & 0x8000: self.intreq |= v & 0x7FFF
            else:
                self.intreq &= ~v
                if v & 0x20 and v == 0x20:
                    self.vbl_count += 1; self.iter += 1; self.on_frame()
        elif r == 0x096:                                  # DMACON
            if v & 0x8000:
                was = self.dma
                self.dma |= v & 0x7FFF
                if (self.dma & 0x0203) == 0x0203 and not (was & 0x0003) and self.aud_on_frame is None:
                    self.aud_on_frame = self.iter; self.latch();
                    if self.flag_at_start: self.intreq |= 0x180
            else: self.dma &= ~v
        elif r in (0x0A0, 0x0B0): pass
    def latch(self):
        blk = MUSIC_LEN // 4
        idx = (self.lc[0] - self.pcm) // blk if self.pcm else -1
        self.blocks_played.append(idx)
    def on_frame(self):
        # audio: a block ends every blk_frames frames after start
        if self.aud_on_frame is not None and self.iter > self.aud_on_frame and (self.iter - self.aud_on_frame) % self.blk_frames == 0:
            self.latch(); self.intreq |= 0x180
        if 1 <= self.iter < self.max_iter and self.iter in self.script.get('capture', ()):
            self.captures[self.iter] = self.screen()
        if self.iter >= self.max_iter and 'end' not in self.state:
            self.state['end'] = True; self.script.get('end', lambda s: None)(self)
    def screen(self):
        mu = self.mu; cop = self.w32[0x080]
        words = []; a = cop
        while True:
            w1, w2 = struct.unpack('>HH', mu.mem_read(a, 4)); a += 4
            if w1 == 0xFFFF and w2 == 0xFFFE: break
            words.append((w1, w2))
        ptr = {}; pal = {}
        for w1, w2 in words:
            if 0x0E0 <= w1 < 0x100: ptr[w1] = w2
            if 0x180 <= w1 < 0x1A0: pal[(w1 - 0x180) // 2] = w2
        base = (ptr[0x0E0] << 16) | ptr[0x0E2]
        buf = bytes(mu.mem_read(base, 32000))
        return base, buf, pal, words

    def run(self):
        mu = self.mu; sentinel = 0x3F00
        mu.mem_write(sentinel, b'\x4e\x71'); sp = STACK + 0xF000
        sp -= 4; mu.mem_write(sp, struct.pack('>I', sentinel))
        mu.reg_write(UC_M68K_REG_A7, sp)
        mu.reg_write(UC_M68K_REG_D0, 0)
        mu.emu_start(BASE, sentinel, count=400_000_000)
        return mu.reg_read(UC_M68K_REG_D0)

# ---- Python model of the demo ----
def model_screen_pixels():
    return amiga_pack.unpack_screen(MAP)

def planar_to_idx(buf):
    out = []
    for y in range(200):
        row = buf[y*160:(y+1)*160]; line = []
        for x in range(320):
            v = 0
            for p in range(4): v |= ((row[p*40 + (x >> 3)] >> (7 - (x & 7))) & 1) << p
            line.append(v)
        out.append(line)
    return out

def model_render(frames, px, py):
    """frames = (torso, legs, board, shadow) sprite numbers; px, py = screen position of the skater's torso origin."""
    pix = model_screen_pixels(); img = []
    for y in range(200):
        line = []
        for x in range(160):
            b = pix[y*160 + x]; line += [b >> 4, b & 15]
        img.append(line)
    recs = amiga_pack.unpack_sprites(SPR)
    for idx in (frames[3], frames[2], frames[1], frames[0]):          # shadow, board, legs, torso
        wpx, rows, hot, spx, opq = recs[idx]
        ox, oy = px + (hot & 255), py + (hot >> 8)
        for yy in range(rows):
            for xx in range(wpx):
                X, Y = ox + xx, oy + yy
                if opq[yy][xx] and 0 <= X < 320 and 0 <= Y < 200: img[Y][X] = spx[yy][xx]
    return img

def ctl_from(jx, jy):
    c = 0xFF
    if jy < 0: c &= ~1
    if jy > 0: c &= ~2
    if jx < 0: c &= ~4
    if jx > 0: c &= ~8
    return c

def main():
    # script: accelerate, steer right, steer left, brake, cursor keys, boost button, then ESC
    def joy_fn(i):
        if 0 <= i < 60: return joy(up=1)                       # accelerate (heading south)
        if 60 <= i < 110: return joy(up=1, right=1)            # accelerate while turning
        if 110 <= i < 150: return joy(up=1, left=1)
        if 150 <= i < 175: return joy(down=1)                  # brake
        return 0
    fire_fn = lambda i: 1 if 20 <= i < 30 else 0               # the fire button has no effect on movement
    keys = {180: 0x4C, 200: 0x4C | 0x80, 205: 0x4F, 240: 0x4F | 0x80, 250: 0x45}
    capture = {7, 31, 61, 91, 121, 151, 177, 183, 201, 221, 243}
    results = {}
    for variant in (True, False):
        sim = Sim({'joy': joy_fn, 'fire': fire_fn, 'keys': keys, 'capture': capture}, aud_flag_at_start=variant, aud_block_frames=12, max_iter=400)
        ret = sim.run()
        results[variant] = sim
        print('--- run (hardware flags audio at DMA start: %s) ---' % variant)
        print('return code', ret, '| iterations', sim.iter)
        names = [e[0] for e in sim.log]
        expect = ['AllocMem', 'OpenLibrary', 'LoadView', 'WaitTOF', 'WaitTOF', 'Forbid', 'Permit', 'LoadView', 'WaitTOF', 'WaitTOF', 'CloseLibrary', 'FreeMem']
        assert names == expect, names
        assert sim.log[2] == ('LoadView', 0) and sim.log[7] == ('LoadView', 0x1234), (sim.log[2], sim.log[7])
        assert sim.log[-1][1] == sim.allocs[0][0] and sim.log[-1][2] == sim.allocs[0][1]
        print('OS calls in the expected order; view restored; memory freed')
        bp = sim.blocks_played[:10]
        assert bp[:9] == [0, 1, 2, 3, 0, 1, 2, 3, 0], bp
        print('audio block sequence', bp)
        # screen vs the original-movement model. Iteration it renders the state after its (possible) logic tick; the frame
        # captured at key k shows the render of iteration k-2.
        sk = Skater(); acc = 0; mismatches = 0; checked = 0; resets = 0; moved = set()
        for it in range(0, 251):
            jv = joy_fn(it); right = (jv >> 1) & 1; left = (jv >> 9) & 1
            down = ((jv >> 1) ^ jv) & 1; up = ((jv >> 9) ^ (jv >> 8)) & 1
            jx, jy = right - left, down - up
            if 205 <= it < 240: jx = -1
            if 180 <= it < 200: jy = -1
            acc += 25
            if acc >= 50:
                acc -= 50
                sk.step(ctl_from(jx, jy))
                if sk.exit_code or sk.in_shop: resets += 1; sk = Skater()
            if it + 2 in sim.captures:
                base, buf, pal, words = sim.captures[it + 2]
                got = planar_to_idx(buf)
                exp = model_render(sk.frames(), 2 * sk.x - 4, sk.y - 20)
                bad = sum(1 for y in range(200) for x in range(320) if got[y][x] != exp[y][x])
                checked += 1; mismatches += bad; moved.add((sk.x, sk.y, sk.h))
                if bad: print('  frame %d: %d pixel mismatches (x=%d y=%d h=%d spd=%d)' % (it + 2, bad, sk.x, sk.y, sk.h, sk.spd))
        assert checked >= 10, checked
        print('screen check: %d frames compared pixel-for-pixel against the original-movement model, %d mismatches (%d distinct poses/positions, %d restarts)' % (checked, mismatches, len(moved), resets))
        assert mismatches == 0 and len(moved) >= 8
        base, buf, pal, words = next(iter(sim.captures.values()))
        want = struct.unpack('>16H', MAP[10:42])
        assert [pal[i] for i in range(16)] == list(want)
        print('copper palette matches the asset (both AGA nibble passes)')
    print('ALL CHECKS PASSED')

if __name__ == '__main__':
    main()
