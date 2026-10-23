#!/usr/bin/env python3
"""Render the title song (NES sound id $31) and store it as IMA ADPCM blocks for streaming on the Amiga.
usage: make_title_audio.py <rom.nes> <out.ima>
The song repeats exactly every 8448 frames after an intro of about 300 frames (found by comparing the engine's register writes).
The sample rate is chosen (about 16 kHz) so that the repeat is a whole number of 1024-sample blocks.
File: u32 total_blocks, u32 loop_block (first block of the repeating part), then blocks of 4 header bytes (s16 predictor, u8 step index, 0) + 512 bytes (2 samples a byte, low nibble first)."""
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nes_sound, nes_apu
rom = nes_sound.load_rom(sys.argv[1])
LOOP_FRAMES, INTRO_FRAMES, BLK = 8448, 300, 1024
loop_secs = LOOP_FRAMES * nes_apu.FRAME_CYC / nes_apu.CPU
K = round(loop_secs * 16000 / BLK)
sr = K * BLK / loop_secs
n0 = -(-int(INTRO_FRAMES * nes_apu.FRAME_CYC / nes_apu.CPU * sr) // BLK)            # first block of the repeating part
frames = INTRO_FRAMES + LOOP_FRAMES + 40
m = nes_sound.Machine(rom); m.call(0x8008); m.call(0x8040, a=0x31)
for f in range(frames):
    m.frame = f; m.call(0x8245)
audio = nes_apu.render(m.log, frames, m.mem, sr=sr).astype(np.float64)
total = n0 + K
audio = audio[:total * BLK]; audio -= audio.mean(); audio *= 0.9 / np.max(np.abs(audio))
pcm = np.clip(np.round(audio * 30000), -32768, 32767).astype(np.int32)
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
INDEX = [-1,-1,-1,-1,2,4,6,8]
def encode(block):
    pred = int(block[0]); idx = 0
    out = bytearray(struct.pack('<hBB', pred, idx, 0)); nib = []
    # the header holds the predictor and index the decoder starts with: the first sample is coded from it
    for s in block:
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
    return bytes(out)
blob = struct.pack('>II', total, n0)
for b in range(total):
    blob += encode(pcm[b * BLK:(b + 1) * BLK])
open(sys.argv[2], 'wb').write(blob)
print('title: %d blocks (%.1f s), loop from block %d, rate %.1f Hz, %d KB' % (total, total * BLK / sr, n0, sr, len(blob) // 1024))
