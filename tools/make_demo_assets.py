#!/usr/bin/env python3
"""Build the overworld-demo assets: map screen, skater sprites, and a looped NES-music sample.
usage: python3 -I tools/make_demo_assets.py <rom.nes> <packdir> <outdir>
Music: the NES overworld/shop track (sound id $35) is run through the original 6502 engine, rendered with the
approximate APU renderer at 22050 Hz mono, cut to one exact loop (1296 frames) and written as signed 8-bit PCM."""
import os, shutil, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nes_sound, nes_apu

rom_path, packdir, outdir = sys.argv[1:4]
os.makedirs(outdir, exist_ok=True)
shutil.copy(f'{packdir}/screen_F29.scr', f'{outdir}/map.scr')
shutil.copy(f'{packdir}/sprites_F37.spr', f'{outdir}/skater.spr')

SONG, LOOP, START, RATE = 0x35, 1296, 120, 22050
rom = nes_sound.load_rom(rom_path)
m = nes_sound.Machine(rom); m.call(0x8008); m.call(0x8040, a=SONG)
for f in range(START + LOOP + 30):
    m.frame = f; m.call(0x8245)
audio = nes_apu.render(m.log, START + LOOP + 30, m.mem, sr=RATE)
a0 = int(round(START * nes_apu.FRAME_CYC / nes_apu.CPU * RATE))
n = int(round(LOOP * nes_apu.FRAME_CYC / nes_apu.CPU * RATE)) // 8 * 8      # multiple of 8: 4 blocks of even length
seg = audio[a0:a0 + n].astype(np.float64)
seg -= seg.mean()
seg *= 0.9 / np.max(np.abs(seg))
pcm = np.clip(np.round(seg * 127), -127, 127).astype(np.int8)
open(f'{outdir}/music.raw', 'wb').write(pcm.tobytes())
print('music: %d samples, %.2fs @ %d Hz, %d KB' % (n, n / RATE, RATE, n // 1024))
