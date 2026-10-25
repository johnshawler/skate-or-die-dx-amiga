#!/usr/bin/env python3
"""title_credit.py <title_orig.scr> <out title.scr>: the title screen's credit list gets one more bullet, "AMIGA VERSION BY BOATOFCAR".
The list is part of the picture (the original's F52): headings and bullets in a small two-tone font (grey 1, white 15), red balls for bullets. The IIGS block is moved up
by five rows (there is nothing else between the two blocks and the copyright line), the new bullet goes below it, in the same font: the letters are cut out of the
list itself (every letter but Y occurs in it; Y is drawn like the V with a stem), set one pixel apart."""
import sys
import numpy as np
src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
HDR = 106
bm = np.frombuffer(bytes(d[HDR:HDR + 32000]), dtype=np.uint8)
img = np.zeros((200, 320), dtype=np.uint8)
for p in range(4):
    plane = np.unpackbits(bm.reshape(200, 4, 40)[:, p, :], axis=1)        # 200 x 320 bits of plane p
    img |= (plane << p).astype(np.uint8)
orig = img.copy()

def glyph(y0, x0, w): return orig[y0:y0 + 5, x0:x0 + w].copy()
G = {'M': glyph(124, 14, 10), 'I': glyph(124, 26, 2), 'C': glyph(124, 30, 5), 'H': glyph(124, 36, 6), 'A': glyph(124, 44, 6), 'E': glyph(124, 52, 5),
     'L': glyph(124, 58, 5), 'K': glyph(124, 66, 6), 'O': glyph(124, 74, 7), 'S': glyph(124, 82, 6), 'T': glyph(132, 22, 7), 'P': glyph(132, 36, 6),
     'N': glyph(132, 58, 6), 'D': glyph(132, 90, 6), 'R': glyph(132, 98, 6), 'U': glyph(132, 106, 6), 'V': glyph(140, 30, 6), 'B': glyph(140, 52, 6),
     'F': glyph(179, 96, 5), 'G': glyph(114, 24, 7)}
y = G['V'].copy()                                                          # Y: the V's two upper rows, its bottom row, then the stem
y[3] = 0; y[4] = 0; y[3, 2:4] = G['V'][4, 1:3]; y[4, 2:4] = G['V'][4, 1:3]
y[2] = G['V'][4]; y[3, 2:4] = (1, 15); y[4, 2:4] = (1, 15)
G['Y'] = y
BULLET = orig[179:184, 4:10].copy()

# the IIGS block (heading at 153, bullets at 163, 171, 179, each 5 rows, text and bullets in columns 0-127) five rows up; rows 145-187 of those columns are cleared first
img[145:188, 0:128] = 0
for old, new in ((153, 148), (163, 157), (171, 165), (179, 173)):
    img[new:new + 5, 0:128] = orig[old:old + 5, 0:128]
# the new bullet at 181-185
ny = 181
img[ny:ny + 5, 4:10] = BULLET
x = 14
for word in 'AMIGA VERSION BY BOATOFCAR'.split(' '):
    for ch in word:
        g = G[ch]; img[ny:ny + 5, x:x + g.shape[1]] = g; x += g.shape[1] + 1
    x += 3
print('new line ends at column', x - 4, '(the skateboard begins at 188 in these rows)')
assert x - 4 < 186

out = np.zeros((200, 4, 40), dtype=np.uint8)
for p in range(4): out[:, p, :] = np.packbits((img >> p) & 1, axis=1)
d[HDR:HDR + 32000] = out.reshape(-1).tobytes()
open(dst, 'wb').write(bytes(d))
