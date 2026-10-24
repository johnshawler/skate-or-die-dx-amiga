#!/usr/bin/env python3
"""Build the data folder of the Amiga game from the earlier asset files: make_data.py <assets dir> <out dir>
 - screens, font, sprite sheet: copied
 - event images (evN.pak): only the bytes inside the page spans the events use (spans.h), see docs/TARGET_020_AGA_2MB.md
 - songs (evN.ima, ovw.ima): 8-bit loops resampled by well under 0.2% to a whole number of 1024-sample blocks and stored as IMA ADPCM
   (file: u32 blocks, u32 loop block, then blocks of 4 header bytes = s16 predictor, u8 step index, 0, and 512 bytes, low nibble first).
   Every block starts from the decoder state the previous block ended in, so blocks decode on their own and the loop point has no seam.
 - sfx.raw, title.ima: copied"""
import os, struct, sys, shutil
import numpy as np
src, out = sys.argv[1:3]
os.makedirs(out, exist_ok=True)
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]
BLK = 1024
# page spans per bank (first, last page): the union of what the four events touched in the 65816 oracle runs (tools/cpu816x/workingset.py), plus margins
SPANS = {0x00: (0, 3), 0x01: (0x1E, 0xA1), 0x02: (0, 0xFF), 0x03: (0, 0x6A), 0x04: (0, 0xFF), 0x05: (0, 0xF5), 0x07: (0, 0x7F), 0xE0: (0x04, 0xC2), 0xE1: (0x1E, 0xC2)}

def encode_block(blk, pred, idx):
    out = bytearray(struct.pack('<hBB', pred, idx, 0)); nib = []
    for s in blk:
        step = STEP[idx]; diff = int(s) - pred; code = 0
        if diff < 0: code = 8; diff = -diff
        d = step >> 3
        if diff >= step: code |= 4; diff -= step; d += step
        if diff >= step >> 1: code |= 2; diff -= step >> 1; d += step >> 1
        if diff >= step >> 2: code |= 1; d += step >> 2
        pred = max(-32768, min(32767, pred - d if code & 8 else pred + d))
        idx = max(0, min(88, idx + INDEX[code & 7]))
        nib.append(code)
    for i in range(0, len(nib), 2): out.append(nib[i] | nib[i + 1] << 4)
    return bytes(out), pred, idx

def make_ima(raw_path, out_path):
    a = np.frombuffer(open(raw_path, 'rb').read(), dtype=np.int8).astype(np.float64)
    k = round(len(a) / BLK); n = k * BLK
    a = np.interp(np.linspace(0, len(a), n, endpoint=False), np.arange(len(a)), a)       # stretch by under 0.2%: the loop becomes whole blocks
    pcm = np.clip(np.round(a * 256), -32768, 32767).astype(np.int32)
    pred, idx = 0, 0
    for _ in range(3):                                                                    # settle the decoder state at the loop point
        blocks = []; p, i = pred, idx
        for b in range(k):
            enc, p, i = encode_block(pcm[b * BLK:(b + 1) * BLK], p, i); blocks.append(enc)
        pred, idx = p, i
    # the header of block 0 is the state the loop ends in: re-encode block 0 from it and keep the pass consistent
    blocks = []; p, i = pred, idx
    for b in range(k):
        enc, p, i = encode_block(pcm[b * BLK:(b + 1) * BLK], p, i); blocks.append(enc)
    open(out_path, 'wb').write(struct.pack('>II', k, 0) + b''.join(blocks))
    return k, k * 516 + 8

def trim_pak(path, outp):
    d = open(path, 'rb').read(); n = struct.unpack('>I', d[4:8])[0]; p = 8; segs = []; kept = 0
    for _ in range(n):
        addr, ln = struct.unpack('>II', d[p:p + 8]); p += 8; body = d[p:p + ln]; p += ln + (ln & 1)
        bank = addr >> 16; off = addr & 0xFFFF
        if bank not in SPANS: continue
        lo, hi = SPANS[bank][0] * 256, SPANS[bank][1] * 256 + 255
        s, e = max(off, lo), min(off + ln - 1, hi)
        if s > e: continue
        segs.append(((bank << 16) | s, body[s - off:e - off + 1])); kept += e - s + 1
    blob = b'T8PK' + struct.pack('>I', len(segs))
    for a, b in segs: blob += struct.pack('>II', a, len(b)) + b + (b'\0' if len(b) & 1 else b'')
    open(outp, 'wb').write(blob)
    return kept, len(blob)

for f in ('map.scr', 'shop.scr', 'list.scr', 'scores.scr', 'board.scr', 'title.scr', 'menu.fnt', 'skater.spr', 'title.ima'):
    shutil.copy(f'{src}/{f}', f'{out}/{f}')
# INTER (the event and competition driver, bank 7 $6800-$7FFF) as the original has it before it first runs; the code is translated into the overlays, the data are read from here
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'inter.bin'), f'{out}/inter.bin')
# the events only ask for effects $09-$0E (tools/cpu816x/sfxuse.py: the calls of the sound driver in long oracle runs): the first 96,872 bytes of sfx.raw
open(f'{out}/sfx.raw', 'wb').write(open(f'{src}/sfx.raw', 'rb').read()[:96872])

def make_raw(raw_path, out_path, rate_in, rate_out):
    """8-bit song loop -> file: u16 sample rate, u16 0, then the samples (length a multiple of 16), resampled with a windowed-sinc low-pass when the rate drops"""
    a = np.frombuffer(open(raw_path, 'rb').read(), dtype=np.int8).astype(np.float64)
    if rate_out != rate_in:
        cut = 0.45 * rate_out / rate_in; t = np.arange(-32, 33); h = np.sinc(2 * cut * t) * np.hamming(65); h /= h.sum()
        a = np.convolve(np.concatenate([a[-32:], a, a[:32]]), h, mode='same')[32:-32]            # filter around the loop point
        n = int(round(len(a) * rate_out / rate_in)) // 16 * 16
        a = np.interp(np.linspace(0, len(a), n, endpoint=False), np.arange(len(a)), a)
    n = len(a) // 16 * 16
    open(out_path, 'wb').write(struct.pack('>HH', rate_out, 0) + np.clip(np.round(a[:n]), -127, 127).astype(np.int8).tobytes())
    return n
extra = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets')
for n in (1, 2, 3, 4, 5):
    d = extra if os.path.exists(f'{extra}/ev{n}.pak') else src
    kept, size = trim_pak(f'{d}/ev{n}.pak', f'{out}/ev{n}.pak'); print('ev%d.pak: %d bytes kept in spans, file %d' % (n, kept, size))
    n_s = make_raw(f'{d}/ev_song{n}.raw', f'{out}/ev{n}.raw', 16000, 12000 if n == 4 else 16000); print('ev%d.raw: %d samples at %d Hz' % (n, n_s, 12000 if n == 4 else 16000))
n_s = make_raw(f'{src}/music.raw', f'{out}/ow.raw', 22050, 22050); print('ow.raw: %d samples' % n_s)
# spans header for the C code
lines = ['/* generated by tools/make_data.py: first and last 256-byte page of each bank the events use (banks 0 1 2 3 4 5 7 E0 E1) */',
         'static const u8 ev_span_lo[9] = {%s};' % ','.join(str(SPANS[b][0]) for b in (0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1)),
         'static const u8 ev_span_hi[9] = {%s};' % ','.join(str(SPANS[b][1]) for b in (0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1))]
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'spans.h'), 'w').write('\n'.join(lines) + '\n')
