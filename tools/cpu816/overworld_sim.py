#!/usr/bin/env python3
"""Run the original IIGS overworld overlay (F39, game mode 6) headless on the 65816 core.
Memory is set up the way INTER's loader does it (docs/resource_table.txt); sound-driver and file calls are stubbed;
the control byte (bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire, active low) is injected once per main-loop pass."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu816 as C

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
RES = f'{ROOT}/work/res'; RAW = f'{ROOT}/work/raw'
LOOP_TOP = 0x03101D          # JML target at the end of the overlay's main loop
INPUT_FN = 0x034150          # collects joystick/mouse/keyboard into $3062/$3060
INTER_ENTRY = 0x076800

def setup(mode=6):
    C.load(0x076800, open(f'{RAW}/INTER', 'rb').read())
    C.load(0x078000, open(f'{RAW}/DRIVER', 'rb').read())
    for a in range(0x078000, 0x07803C, 3): C.rtl_at(a)          # sound-driver entry points -> RTL
    C.rtl_at(0xE100A8)                                           # GS/OS call -> RTL
    rd = lambda n: open(f'{RES}/F{n}.bin', 'rb').read()
    C.load(0xE01000, rd(29)); C.load(0x040000, rd(30)); C.load(0x048000, rd(31)); C.load(0x050000, rd(32)); C.load(0x026000, rd(33))
    C.load(0x020000, rd(2)); C.load(0x077F00, open(f'{RAW}/HIGHS', 'rb').read()); C.load(0x024A00, rd(35))
    t = rd(36); n = struct.unpack('<H', t[:2])[0]
    for k in range(7): C.load(0x021900 + k * 0x500, t[2 + k * n * 2: 2 + (k + 1) * n * 2])
    C.load(0x058000, rd(37)); C.load(0x05C000, rd(38)); C.load(0x031000, rd(39))
    C.wr16(0x0768CE, mode)
    C.cpu.pb = 3; C.cpu.pc = 0x1000; C.cpu.db = 0; C.cpu.d = 0; C.cpu.s = 0x1FF; C.cpu.p = 0x00
    C.rts_at(INPUT_FN)                                           # we write the inputs ourselves
    C.hook(LOOP_TOP); C.hook(INTER_ENTRY)

def snapshot():
    r16 = lambda a: C.rd16(0x030000 + a)
    return dict(state=r16(0x4BD5), cx=r16(0x4BC7), cy=r16(0x4BC9), hot=r16(0x00C2), lock60=r16(0x0060), c6=r16(0x00C6), c70=r16(0x0070),
                sx=[r16(0x4B8F + 2 * i) for i in range(6)], sy=[r16(0x4B9F + 2 * i) for i in range(6)],
                fr=[r16(0x4BAF + 2 * i) for i in range(6)], frame=r16(0x3038),
                x=r16(0x3C), y=r16(0x3E), h=r16(0x40), spd=r16(0x42), sub=r16(0x44), turn=r16(0x4A), tc=r16(0x4C),
                b8=r16(0xB8), fx=r16(0xBE), fy=r16(0xBC), exitc=r16(0x6C))

def run_frames(script, max_frames=400, quiet=True, pre=None):
    """script(frame) -> control byte. Returns list of per-pass snapshots."""
    log = []; passes = 0
    while passes < max_frames:
        r = C.run(5_000_000)
        if r == 2: raise SystemExit('fault at %06x' % C.fault_pc())
        if r == 0: raise SystemExit('instruction limit without reaching the main loop (pc=%02x:%04x)' % (C.cpu.pb, C.cpu.pc))
        pc = C.hit_pc()
        if pc == INTER_ENTRY:
            log.append(dict(exit=True, mode=C.rd16(0x0768CE))); break
        # main-loop top: one pass
        if pre: pre(passes)
        ctl = script(passes)
        C.wr16(0x033062, 0xFF00 | ctl if False else ctl); C.wr16(0x033060, 0x00FF)
        s = snapshot(); s['pass'] = passes; s['cycles'] = C.cycles(); s['icount'] = C.icount(); s['ctl'] = ctl; log.append(s)
        passes += 1
    return log

if __name__ == '__main__':
    setup()
    log = run_frames(lambda f: 0xFF, 6)
    for s in log: print(s)

_SCREENS = None
def screen_id():
    """Which full-screen resource (F29..F33) is currently in SHR memory, or the count of differing bytes to the closest."""
    global _SCREENS
    if _SCREENS is None:
        _SCREENS = {n: open(f'{RES}/F{n}.bin', 'rb').read()[:32000] for n in (29, 30, 31, 32, 33, 1, 22, 23, 52)}
    cur = bytes(C.mem[0xE12000:0xE12000 + 32000])
    best = min(_SCREENS, key=lambda n: sum(1 for a, b in zip(cur, _SCREENS[n]) if a != b))
    diff = sum(1 for a, b in zip(cur, _SCREENS[best]) if a != b)
    return best, diff
