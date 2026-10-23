"""Drive the original overworld overlay (F39) with joystick bits and keyboard characters through the real input code.
$3F89 (paddles) and $4075 (mouse) are stubbed; the control byte is placed in $3058, the keyboard strobe in $E0C010."""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu816 as C, overworld_sim as O

JOY_FN, MOUSE_FN = 0x033F89, 0x034075
def setup(mode=6):
    O.setup(mode)
    C.wr8(0x034150, 0xAD) if False else None
    # undo the sim's RTS patch on the input collector: restore the original bytes
    C.wr8(O.INPUT_FN, open(f'{O.RES}/F39.bin', 'rb').read()[0x4150 - 0x1000])
    C.rts_at(JOY_FN); C.rts_at(MOUSE_FN)
    C.wr16(0x033058, 0x00FF); C.wr16(0x03305C, 0x00FF)

def r16(a): return C.rd16(0x030000 + a)
def snap():
    return dict(state=r16(0x4BD5), cx=r16(0x4BC7), cy=r16(0x4BC9), hot=r16(0x00C2), x=r16(0x3C), y=r16(0x3E),
                key=r16(0x3066), c6=r16(0xC6), mode=C.rd16(0x0768CE), fr=[r16(0x4BAF + 2 * i) for i in range(6)])

def run(script, max_passes=600, stop_on_exit=True, on_pass=None):
    """script(pass) -> (ctl, key) with ctl the active-low control byte and key an ASCII char or None. Returns list of snapshots."""
    log = []; k = 0
    while k < max_passes:
        r = C.run(8_000_000)
        if r == 2: raise SystemExit('fault at %06x' % C.fault_pc())
        if r == 0: raise SystemExit('limit without reaching the main loop')
        pc = C.hit_pc()
        if pc == O.INTER_ENTRY:
            log.append(dict(exit=True, mode=C.rd16(0x0768CE))); break
        ctl, key = script(k)
        C.wr16(0x033058, ctl); C.wr16(0x03305C, 0x00FF)
        C.wr8(0xE0C010, (0x80 | ord(key)) if key else 0)
        C.wr8(0xE0C000, (0x80 | ord(key)) if key else 0)
        s = snap(); s['pass'] = k; s['cycles'] = C.cycles(); log.append(s)
        if on_pass: on_pass(k, s)
        k += 1
    return log

def screen():
    return O.screen_id()

def shr_image(path, scale=2, base=0xE12000):
    from PIL import Image
    shr = bytes(C.mem[base:base + 32768])
    pix, scb, pal = shr[:32000], shr[32000:32200], shr[32256:32768]
    im = Image.new('RGB', (320, 200)); px = im.load()
    for y in range(200):
        p = scb[y] & 15; cols = []
        for i in range(16):
            v = pal[p * 32 + i * 2] | (pal[p * 32 + i * 2 + 1] << 8); cols.append((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))
        for x in range(160):
            b = pix[y * 160 + x]; px[2 * x, y] = cols[b >> 4]; px[2 * x + 1, y] = cols[b & 15]
    if scale != 1: im = im.resize((320 * scale, 200 * scale), 0)
    im.save(path); return im
