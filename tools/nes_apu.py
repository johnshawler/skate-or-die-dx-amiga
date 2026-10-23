#!/usr/bin/env python3
"""Small NES APU renderer (pulse x2, triangle, noise, DMC) driven by a (frame, reg, value) write log.
Used to audition what the extracted sound engine produces. Approximate, not cycle-exact:
register writes are applied at the start of each 60.0988 Hz frame, the frame sequencer runs in
5-step mode ($4017=$C0, as the game sets it), and output is the standard non-linear mix."""
import numpy as np

CPU = 1789773.0
FRAME_CYC = 29780.5
LEN = [10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30]
DUTY = [[0,1,0,0,0,0,0,0],[0,1,1,0,0,0,0,0],[0,1,1,1,1,0,0,0],[1,0,0,1,1,1,1,1]]
NOISE_P = [4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068]
DMC_R = [428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54]
TRI = [15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]


class Env:
    def __init__(s):
        s.start = False; s.div = 0; s.decay = 0; s.vol = 0; s.const = False; s.loop = False
    def write(s, v):
        s.vol = v & 15; s.const = bool(v & 0x10); s.loop = bool(v & 0x20)
    def clock(s):
        if s.start:
            s.start = False; s.decay = 15; s.div = s.vol
        elif s.div == 0:
            s.div = s.vol
            if s.decay > 0: s.decay -= 1
            elif s.loop: s.decay = 15
        else:
            s.div -= 1
    def out(s):
        return s.vol if s.const else s.decay


class Pulse:
    def __init__(s, ones):
        s.ones = ones; s.env = Env(); s.duty = 0; s.timer = 0; s.len = 0; s.en = False; s.halt = False
        s.phase = 0.0
        s.sw_en = False; s.sw_per = 0; s.sw_neg = False; s.sw_shift = 0; s.sw_div = 0; s.sw_reload = False
    def write(s, r, v):
        if r == 0:
            s.duty = v >> 6; s.halt = bool(v & 0x20); s.env.write(v)
        elif r == 1:
            s.sw_en = bool(v & 0x80); s.sw_per = (v >> 4) & 7; s.sw_neg = bool(v & 8); s.sw_shift = v & 7; s.sw_reload = True
        elif r == 2:
            s.timer = (s.timer & 0x700) | v
        else:
            s.timer = (s.timer & 0xFF) | ((v & 7) << 8)
            if s.en: s.len = LEN[v >> 3]
            s.env.start = True; s.phase = 0.0
    def target(s):
        d = s.timer >> s.sw_shift
        if s.sw_neg: return s.timer - d - (1 if s.ones else 0)
        return s.timer + d
    def muted(s):
        return s.timer < 8 or s.target() > 0x7FF
    def clock_half(s):
        if s.len > 0 and not s.halt: s.len -= 1
        if s.sw_div == 0 and s.sw_en and s.sw_shift and not s.muted():
            s.timer = max(0, s.target())
        if s.sw_div == 0 or s.sw_reload:
            s.sw_div = s.sw_per; s.sw_reload = False
        else:
            s.sw_div -= 1
    def block(s, n, sr):
        if not s.en or s.len == 0 or s.muted() or s.timer < 8: return np.zeros(n)
        vol = s.env.out()
        if vol == 0: return np.zeros(n)
        step = (CPU / (16.0 * (s.timer + 1))) / sr * 8.0   # duty steps per sample
        idx = (s.phase + step * np.arange(n)) % 8.0
        s.phase = (s.phase + step * n) % 8.0
        return np.array(DUTY[s.duty])[idx.astype(int)] * float(vol)


class Tri:
    def __init__(s):
        s.timer = 0; s.len = 0; s.en = False; s.ctrl = False; s.reload = 0; s.lin = 0; s.lreload = False; s.phase = 0.0
    def write(s, r, v):
        if r == 0: s.ctrl = bool(v & 0x80); s.reload = v & 0x7F
        elif r == 2: s.timer = (s.timer & 0x700) | v
        else:
            s.timer = (s.timer & 0xFF) | ((v & 7) << 8)
            if s.en: s.len = LEN[v >> 3]
            s.lreload = True
    def clock_quarter(s):
        if s.lreload: s.lin = s.reload
        elif s.lin > 0: s.lin -= 1
        if not s.ctrl: s.lreload = False
    def clock_half(s):
        if s.len > 0 and not s.ctrl: s.len -= 1
    def block(s, n, sr):
        if not s.en or s.len == 0 or s.lin == 0 or s.timer < 2:
            return np.full(n, 0.0) if True else None
        step = (CPU / (32.0 * (s.timer + 1))) / sr * 32.0
        idx = (s.phase + step * np.arange(n)) % 32.0
        s.phase = (s.phase + step * n) % 32.0
        return np.array(TRI)[idx.astype(int)].astype(float)


class Noise:
    _seq = {}
    def __init__(s):
        s.env = Env(); s.mode = False; s.per = 0; s.len = 0; s.en = False; s.halt = False; s.shift = 1; s.acc = 0.0
    def write(s, r, v):
        if r == 0: s.halt = bool(v & 0x20); s.env.write(v)
        elif r == 2: s.mode = bool(v & 0x80); s.per = NOISE_P[v & 15]
        else:
            if s.en: s.len = LEN[v >> 3]
            s.env.start = True
    def clock_half(s):
        if s.len > 0 and not s.halt: s.len -= 1
    def block(s, n, sr):
        vol = s.env.out()
        clocks = (CPU / s.per) / sr if s.per else 0
        out = np.zeros(n)
        active = s.en and s.len > 0 and vol > 0
        for i in range(n):
            s.acc += clocks
            while s.acc >= 1.0:
                s.acc -= 1.0
                fb = (s.shift & 1) ^ ((s.shift >> (6 if s.mode else 1)) & 1)
                s.shift = (s.shift >> 1) | (fb << 14)
            if active and not (s.shift & 1): out[i] = vol
        return out


class DMC:
    def __init__(s, rom_mem):
        s.mem = rom_mem; s.rate = 428; s.loop = False; s.out = 0; s.addr = 0xC000; s.len = 1; s.cur = 0xC000; s.rem = 0
        s.shiftreg = 0; s.bits = 0; s.silence = True; s.acc = 0.0; s.en = False
    def write(s, r, v):
        if r == 0: s.rate = DMC_R[v & 15]; s.loop = bool(v & 0x40)
        elif r == 1: s.out = v & 0x7F
        elif r == 2: s.addr = 0xC000 + v * 64
        else: s.len = v * 16 + 1
    def start(s):
        s.cur = s.addr; s.rem = s.len
    def block(s, n, sr):
        out = np.zeros(n); clocks = (CPU / s.rate) / sr
        for i in range(n):
            s.acc += clocks
            while s.acc >= 1.0:
                s.acc -= 1.0
                if s.bits == 0:
                    if s.rem > 0:
                        s.shiftreg = s.mem[s.cur & 0xFFFF]; s.cur = 0xC000 | ((s.cur + 1) & 0x3FFF); s.rem -= 1
                        s.silence = False; s.bits = 8
                        if s.rem == 0 and s.loop: s.cur = s.addr; s.rem = s.len
                    else:
                        s.silence = True; s.bits = 8
                if not s.silence:
                    if s.shiftreg & 1:
                        if s.out <= 125: s.out += 2
                    else:
                        if s.out >= 2: s.out -= 2
                    s.shiftreg >>= 1
                s.bits -= 1
            out[i] = s.out
        return out


def render(log, frames, rom_mem, sr=22050):
    p1, p2, tri, noi, dmc = Pulse(False), Pulse(True), Tri(), Noise(), DMC(rom_mem)
    p1.en = p2.en = tri.en = noi.en = True       # game writes $4015 = $0F at start
    writes = {}
    for f, reg, v in log: writes.setdefault(f, []).append((reg, v))
    total = int(frames * FRAME_CYC / CPU * sr) + 1
    out = np.zeros(total, dtype=np.float32)
    seq_cycle = 0.0; step = 0
    # 5-step sequencer: quarter clocks at steps 0,1,2,4 ; half clocks at steps 1,4 (cycle times 7457,14913,22371,29829(skip),37281)
    edges = [(3728.5, 'q'), (7456.5, 'qh'), (11185.5, 'q'), (14914.5, ''), (18640.5, 'qh')]
    period = 18641.0
    events = []
    t = 0.0
    while t < frames * FRAME_CYC:
        for c, kind in edges:
            if kind and t + c < frames * FRAME_CYC: events.append((t + c, kind))
        t += period
    for f in range(frames): events.append((f * FRAME_CYC, 'w%d' % f))
    events.sort(key=lambda e: (e[0], 0 if e[1].startswith('w') else 1))
    pos = 0; cur = 0
    def mix(a, b, c, d, e):
        pulse = 95.88 / (8128.0 / np.maximum(a + b, 1e-9) + 100) * ((a + b) > 0)
        tnd_in = c / 8227.0 + d / 12241.0 + e / 22638.0
        tnd = 159.79 / (1.0 / np.maximum(tnd_in, 1e-9) + 100) * (tnd_in > 0)
        return pulse + tnd
    for cyc, kind in events + [(frames * FRAME_CYC, 'end')]:
        nxt = int(cyc * sr / CPU)
        if nxt > cur:
            n = nxt - cur
            blk = mix(p1.block(n, sr), p2.block(n, sr), tri.block(n, sr), noi.block(n, sr), dmc.block(n, sr))
            out[cur:nxt] = blk[:max(0, min(n, total - cur))]
            cur = nxt
        if kind == 'end': break
        if kind.startswith('w'):
            for reg, v in writes.get(int(kind[1:]), []):
                if reg < 4: p1.write(reg, v)
                elif reg < 8: p2.write(reg - 4, v)
                elif reg < 12: tri.write(reg - 8, v)
                elif reg < 16: noi.write(reg - 12, v)
                elif reg < 20:
                    dmc.write(reg - 16, v)
                elif reg == 21:
                    p1.en = bool(v & 1); p2.en = bool(v & 2); tri.en = bool(v & 4); noi.en = bool(v & 8)
                    if not p1.en: p1.len = 0
                    if not p2.en: p2.len = 0
                    if not tri.en: tri.len = 0
                    if not noi.en: noi.len = 0
                    if v & 16:
                        if dmc.rem == 0: dmc.start()
                    else: dmc.rem = 0
        else:
            if 'q' in kind:
                p1.env.clock(); p2.env.clock(); noi.env.clock(); tri.clock_quarter()
            if 'h' in kind:
                p1.clock_half(); p2.clock_half(); tri.clock_half(); noi.clock_half()
    return out
