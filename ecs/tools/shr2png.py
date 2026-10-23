#!/usr/bin/env python3
"""Convert harness SHR dumps (E1:$2000-$9FFF, 32 KB) to PNG: shr2png.py in.bin out.png  (or  shr2png.py dir  to convert all).
320 mode and 640 mode are rendered at 640x200 (320 mode pixel doubled); per-line palette select and fill mode are honoured."""
import sys, os, zlib, struct
def render(d):
    pix = d[0:0x7D00]; scb = d[0x7D00:0x7DC8]; pal = d[0x7E00:0x8000]
    W = 640; rows = []
    for y in range(200):
        s = scb[y]; p = s & 15; mode640 = s & 0x80; fill = s & 0x20
        cols = []
        for i in range(16):
            v = pal[p * 32 + i * 2] | (pal[p * 32 + i * 2 + 1] << 8)
            cols.append((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))
        row = bytearray([0])
        line = pix[y * 160:(y + 1) * 160]
        if not mode640:
            last = 0
            for b in line:
                for n in (b >> 4, b & 15):
                    if fill and n == 0: n = last
                    else: last = n
                    row += bytes(cols[n]) * 2
        else:
            for b in line:
                for k in range(4):
                    n = (b >> (6 - 2 * k)) & 3
                    row += bytes(cols[(n + 8 * (k & 1) * 0) + (8 if k & 1 else 0) if False else n + (4 * (k & 1)) ])
        rows.append(bytes(row))
    raw = b''.join(rows)
    def chunk(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, 200, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
if __name__ == '__main__':
    a = sys.argv[1]
    if os.path.isdir(a):
        for f in sorted(os.listdir(a)):
            if f.startswith('shr_') and f.endswith('.bin'):
                open(os.path.join(a, f[:-4] + '.png'), 'wb').write(render(open(os.path.join(a, f), 'rb').read()))
    else:
        open(sys.argv[2], 'wb').write(render(open(a, 'rb').read()))
