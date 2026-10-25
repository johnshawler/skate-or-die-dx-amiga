#!/usr/bin/env python3
"""scr2png.py <file.scr> <out.png> [scale]: render a .scr screen (10-byte header, 16 OCS colours, 16 AGA colours, 4 interleaved planes of 40 bytes per row) to a PNG."""
import sys, struct, zlib
d = open(sys.argv[1], 'rb').read(); sc = int(sys.argv[3]) if len(sys.argv) > 3 else 1
pal = struct.unpack('>16H', d[10:42]); rgb = [((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17) for c in pal]
bm = d[106:106 + 32000]; rows = []
for y in range(200):
    r = bytearray()
    for x in range(320):
        v = 0
        for p in range(4): v |= ((bm[y * 160 + p * 40 + (x >> 3)] >> (7 - (x & 7))) & 1) << p
        r += bytes(rgb[v]) * sc
    for _ in range(sc): rows.append(b'\0' + bytes(r))
def ch(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', 320 * sc, 200 * sc, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(b''.join(rows))) + ch(b'IEND', b''))
