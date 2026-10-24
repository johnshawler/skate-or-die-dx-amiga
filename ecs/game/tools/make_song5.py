#!/usr/bin/env python3
"""Render the Pool Joust song (NES sound id $4D, by elimination: the other four event songs are $3D $41 $45 $49) like tools/make_event_audio.py does for the other events.
usage: make_song5.py <rom.nes> <out ev_song5.raw>   (signed 8-bit mono, 16 kHz, one loop of 1932 frames)"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'tools'))
import nes_sound, nes_apu
rom = nes_sound.load_rom(sys.argv[1])
SONG_RATE, START, LOOP = 16000, 120, 1932
frames = START + LOOP + 30
m = nes_sound.Machine(rom); m.call(0x8008); m.call(0x8040, a=0x4D)
for f in range(frames):
    m.frame = f; m.call(0x8245)
audio = nes_apu.render(m.log, frames, m.mem, sr=SONG_RATE)
a0 = int(round(START * nes_apu.FRAME_CYC / nes_apu.CPU * SONG_RATE))
n = int(round(LOOP * nes_apu.FRAME_CYC / nes_apu.CPU * SONG_RATE)) // 16 * 16
seg = audio[a0:a0 + n].astype(np.float64); seg -= seg.mean(); seg *= 0.9 / np.max(np.abs(seg))
open(sys.argv[2], 'wb').write(np.clip(np.round(seg * 127), -127, 127).astype(np.int8).tobytes())
print('ev_song5: %d samples, %.1f s' % (n, n / SONG_RATE))
