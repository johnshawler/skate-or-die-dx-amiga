"""glyph_to (planar, used on the Amiga) against the IIGS chunky glyph routine ($1BB7): same pixels for every glyph, position and mask."""
import ctypes, random, os, sys
here = os.path.dirname(os.path.abspath(__file__))
lib = ctypes.CDLL(f'{here}/libglue.so'); lib.gt_buf.restype = ctypes.POINTER(ctypes.c_ubyte)
f2 = open(f'{here}/../../work/res/F2.bin', 'rb').read()[:96 * 32]
lib.gt_set_font(open(f'{here}/../../demo/assets/menu.fnt', 'rb').read())      # the planar font the Amiga build embeds
rng = random.Random(1)
def planar_to_chunky(buf):
    out = bytearray(32000)
    for y in range(200):
        for x in range(320):
            v = 0
            for p in range(4): v |= ((buf[y * 160 + p * 40 + (x >> 3)] >> (7 - (x & 7))) & 1) << p
            i = y * 160 + (x >> 1)
            if x & 1: out[i] |= v
            else: out[i] |= v << 4
    return bytes(out)
bad = 0; n = 0
for trial in range(600):
    lib.gt_clear(0)
    ref = bytearray(32000)
    # random background in both
    bg = bytes(rng.randrange(256) for _ in range(32000))
    import ctypes as C
    # planar background from chunky
    pl = bytearray(32000)
    for y in range(200):
        for x in range(320):
            c = (bg[y * 160 + (x >> 1)] >> (4 if not x & 1 else 0)) & 15
            for p in range(4):
                if (c >> p) & 1: pl[y * 160 + p * 40 + (x >> 3)] |= 0x80 >> (x & 7)
    C.memmove(lib.gt_buf(), bytes(pl), 32000); ref[:] = bg
    for _ in range(6):
        xb = rng.choice([rng.randrange(0, 156), rng.randrange(0, 40) * 4]); y = rng.randrange(0, 193); code = rng.randrange(0, 96)
        mask = rng.choice([0x1111, 0xFFFF, 0x4444, 0x2222, 0x3333, 0x0101, 0x0F0F, rng.randrange(65536)])
        lib.gt_glyph(xb, y, code, mask)
        # chunky reference: $1BB7: 8 rows x 4 bytes = glyph & mask words
        for r in range(8):
            for k in range(4):
                mb = (mask >> 8) & 255 if k & 1 else mask & 255
                ref[(y + r) * 160 + xb + k] = f2[code * 32 + r * 4 + k] & mb
    got = planar_to_chunky(bytes(lib.gt_buf()[:32000]))
    n += 1
    if got != bytes(ref): bad += 1
print('glyph_to vs IIGS routine: %d trials, %d differ' % (n, bad))
