#!/usr/bin/env python3
"""Convert decompressed IIGS resources to Amiga-native (big-endian, planar) data.
usage: python3 -I tools/amiga_pack.py <resdir> <packdir> [--verify]

Screens  -> *.scr : interleaved 4-plane bitmap, 40 bytes/plane/row, rows of 160 bytes, 200 rows (32000 B),
                    followed by 16 x u16 OCS-style $0RGB and 16 x u32 $00RRGGBB (AGA, nibble*17).
Sprites  -> *.spr : header + per-frame record; pixels as interleaved 4-plane words, 1bpp mask plane.
All multi-byte values are big-endian (68k)."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sprites as spr

def c2p_row(nibbles, width_words):
    """nibbles: list of 4-bit pixels (len <= width_words*16, zero padded). Returns 4 planes x width_words words as bytes (interleaved per row)."""
    px = list(nibbles) + [0] * (width_words * 16 - len(nibbles))
    out = bytearray()
    for plane in range(4):
        for wd in range(width_words):
            v = 0
            for i in range(16):
                v = (v << 1) | ((px[wd*16 + i] >> plane) & 1)
            out += struct.pack('>H', v)
    return bytes(out)

def mask_row(opaque, width_words):
    px = list(opaque) + [0] * (width_words * 16 - len(opaque))
    out = bytearray()
    for wd in range(width_words):
        v = 0
        for i in range(16): v = (v << 1) | px[wd*16 + i]
        out += struct.pack('>H', v)
    return bytes(out)

def palette_blocks(pal32):
    w12, w24 = bytearray(), bytearray()
    for i in range(16):
        v = pal32[i*2] | (pal32[i*2+1] << 8)
        r, g, b = (v >> 8) & 15, (v >> 4) & 15, v & 15
        w12 += struct.pack('>H', (r << 8) | (g << 4) | b)
        w24 += struct.pack('>I', ((r*17) << 16) | ((g*17) << 8) | (b*17))
    return bytes(w12), bytes(w24)

def pack_screen(data):
    pix = data[:32000]; pal = data[32256:32288]   # all shipped screens use palette 0, 320 mode, SCB all 0
    out = bytearray()
    for y in range(200):
        nib = []
        for x in range(160): b = pix[y*160 + x]; nib += [b >> 4, b & 15]
        out += c2p_row(nib, 20)
    p12, p24 = palette_blocks(pal)
    return b'SODS' + struct.pack('>HHH', 320, 200, 4) + p12 + p24 + bytes(out)

def pack_sprites(table_path, img, msk):
    table = spr.load_table(table_path)
    body = bytearray(); recs = []
    for fr in table:
        w = fr['w']; rows = fr['words'] // w; o = fr['off']; ww = (w*4 + 15) // 16
        img_off = len(body)
        for y in range(rows):
            nib = []
            for x in range(w*2):
                b = img[o + y*w*2 + x]; nib += [b >> 4, b & 15]
            body += c2p_row(nib, ww)
        mask_off = len(body)
        for y in range(rows):
            opq = []
            for x in range(w*2):
                m = msk[o + y*w*2 + x]; opq += [1 if (m >> 4) == 0 else 0, 1 if (m & 15) == 0 else 0]
            body += mask_row(opq, ww)
        recs.append(struct.pack('>HHHHII', w*4, rows, ww, fr['hot'], img_off, mask_off))
    return b'SODP' + struct.pack('>H', len(table)) + b''.join(recs) + bytes(body)

def pack_font(f2):
    """F2 offset $400: 8x8 glyphs, 4bpp (any non-zero nibble = ink), ASCII $20..$5A (space, punctuation, digits, upper case).
    Output: 'SODF', u16 first_char, u16 count, then count x 8 bytes (1bpp, MSB = left pixel)."""
    out = bytearray()
    for g in range(0x5A - 0x20 + 1):
        base = 0x400 + g*32
        for y in range(8):
            row = 0
            for x in range(4):
                b = f2[base + y*4 + x]
                row = (row << 2) | ((1 if b >> 4 else 0) << 1) | (1 if b & 15 else 0)
            out.append(row)
    return b'SODF' + struct.pack('>HH', 0x20, 0x5A - 0x20 + 1) + bytes(out)

def pack_anim(data, width_px, frames):
    """Equal-sized opaque 4bpp frames stacked vertically (F35: 4 frames of 32x26). Output: 'SODA', u16 w, u16 h, u16 frames, interleaved planar frames."""
    stride = width_px // 2; rows_total = len(data) // stride; h = rows_total // frames
    out = bytearray()
    for f in range(frames):
        for y in range(h):
            nib = []
            for x in range(stride):
                b = data[(f*h + y)*stride + x]; nib += [b >> 4, b & 15]
            out += c2p_row(nib, (width_px + 15) // 16)
    return b'SODA' + struct.pack('>HHH', width_px, h, frames) + bytes(out)

# ---- decoders used for verification ----
def unpack_screen(blob):
    p12 = blob[10:42]; body = blob[10 + 32 + 64:]
    pix = bytearray(32000)
    for y in range(200):
        row = body[y*160:(y+1)*160]
        for x in range(320):
            v = 0
            for plane in range(4):
                byte = row[plane*40 + (x >> 3)]; v |= ((byte >> (7 - (x & 7))) & 1) << plane
            if x & 1: pix[y*160 + (x >> 1)] |= v
            else:     pix[y*160 + (x >> 1)] = v << 4
    return bytes(pix)

def unpack_sprites(blob):
    n = struct.unpack('>H', blob[4:6])[0]; recs = []
    base = 6 + n*16; body = blob[base:]
    for i in range(n):
        wpx, rows, ww, hot, io, mo = struct.unpack('>HHHHII', blob[6+i*16:6+(i+1)*16])
        px = []; op = []
        for y in range(rows):
            row = body[io + y*ww*8: io + (y+1)*ww*8]; mrow = body[mo + y*ww*2: mo + (y+1)*ww*2]
            line = []; ml = []
            for x in range(wpx):
                v = 0
                for plane in range(4):
                    word = struct.unpack('>H', row[plane*ww*2 + (x >> 4)*2:][:2])[0]
                    v |= ((word >> (15 - (x & 15))) & 1) << plane
                line.append(v)
                ml.append((struct.unpack('>H', mrow[(x >> 4)*2:][:2])[0] >> (15 - (x & 15))) & 1)
            px.append(line); op.append(ml)
        recs.append((wpx, rows, hot, px, op))
    return recs

if __name__ == '__main__':
    resdir, packdir = sys.argv[1], sys.argv[2]; verify = '--verify' in sys.argv
    os.makedirs(packdir, exist_ok=True)
    rd = lambda n: open(f'{resdir}/F{n}.bin', 'rb').read()
    total = 0; bad = 0
    for n in (1, 22, 23, 29, 30, 31, 32, 33, 52):
        blob = pack_screen(rd(n)); open(f'{packdir}/screen_F{n}.scr', 'wb').write(blob); total += len(blob)
        if verify:
            ok = unpack_screen(blob) == rd(n)[:32000]; bad += not ok
            print(f'screen F{n}: {len(blob)} B  roundtrip {"OK" if ok else "FAIL"}')
    for t, i, m in [(4,5,6),(12,13,14),(18,19,20),(25,26,27),(36,37,38)]:
        blob = pack_sprites(f'{resdir}/F{t}.bin', rd(i), rd(m)); open(f'{packdir}/sprites_F{i}.spr', 'wb').write(blob); total += len(blob)
        if verify:
            recs = unpack_sprites(blob); table = spr.load_table(f'{resdir}/F{t}.bin'); img, msk = rd(i), rd(m); ok = True
            for fr, (wpx, rows, hot, px, op) in zip(table, recs):
                w = fr['w']; o = fr['off']
                for y in range(rows):
                    for x in range(w*4):
                        b = img[o + y*w*2 + (x >> 1)]; v = (b >> 4) if x % 2 == 0 else (b & 15)
                        mm = msk[o + y*w*2 + (x >> 1)]; mv = (mm >> 4) if x % 2 == 0 else (mm & 15)
                        if px[y][x] != v or op[y][x] != (1 if mv == 0 else 0): ok = False
                ok &= (wpx == w*4 and rows == fr['words']//w and hot == fr['hot'])
            bad += not ok
            print(f'sprites F{i}: {len(table)} frames {len(blob)} B  roundtrip {"OK" if ok else "FAIL"}')
    blob = pack_font(rd(2)); open(f'{packdir}/font8.fnt', 'wb').write(blob); total += len(blob)
    blob = pack_anim(rd(35), 32, 4); open(f'{packdir}/water_F35.anm', 'wb').write(blob); total += len(blob)
    if verify:
        f2 = rd(2); fb = open(f'{packdir}/font8.fnt', 'rb').read()[8:]
        ok = all(((fb[g*8 + y] >> (7 - i)) & 1) == (1 if ((f2[0x400 + g*32 + y*4 + i//2] >> (4 if i % 2 == 0 else 0)) & 15) else 0)
                 for g in range(59) for y in range(8) for i in range(8))
        bad += not ok; print('font8: 59 glyphs  roundtrip', 'OK' if ok else 'FAIL')
        ab = open(f'{packdir}/water_F35.anm', 'rb').read(); w, h, nf = struct.unpack('>HHH', ab[4:10]); body = ab[10:]
        src = rd(35); ok = True
        for f in range(nf):
            for y in range(h):
                row = body[(f*h + y)*16:(f*h + y + 1)*16]
                for x in range(32):
                    v = 0
                    for plane in range(4): v |= ((row[plane*4 + (x >> 3)] >> (7 - (x & 7))) & 1) << plane
                    b = src[(f*h + y)*16 + x//2]; ok &= v == ((b >> 4) if x % 2 == 0 else (b & 15))
        bad += not ok; print(f'water anim: {nf} frames {w}x{h}  roundtrip', 'OK' if ok else 'FAIL')
    print('pack total', total, 'bytes;', 'ALL OK' if verify and not bad else ('FAILURES' if bad else ''))
