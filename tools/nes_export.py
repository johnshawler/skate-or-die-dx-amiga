#!/usr/bin/env python3
"""Export every NES sound id: register-write log (.log), preview WAV, and a summary.
usage: python3 -I tools/nes_export.py <rom.nes> <outdir> [ids...]"""
import os, sys, wave, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nes_sound, nes_apu

rom_path, outdir = sys.argv[1], sys.argv[2]
ids = [int(x, 0) for x in sys.argv[3:]] or list(range(0x52))
os.makedirs(outdir, exist_ok=True)
rom = nes_sound.load_rom(rom_path)
summary = {}
for sid in ids:
    m = nes_sound.Machine(rom); m.call(0x8008); m.call(0x8040, a=sid)
    insns = []; maxf = 2400 if 0x31 <= sid <= 0x50 or sid == 0x2E else 400
    for f in range(maxf):
        m.frame = f; insns.append(m.call(0x8245))
    log = m.log
    last = max((fr for fr, r, v in log if r != 21), default=0)
    frames = maxf if (0x31 <= sid <= 0x50 or sid == 0x2E) else min(maxf, last + 45)
    audio = nes_apu.render(log, frames, m.mem, sr=22050)
    peak = float(np.max(np.abs(audio))) if len(audio) else 0.0
    if peak > 0: audio = audio / peak * 0.8
    with wave.open(f'{outdir}/snd_{sid:02X}.wav', 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
        w.writeframes((audio * 32767).astype('<i2').tobytes())
    with open(f'{outdir}/snd_{sid:02X}.log', 'w') as fh:
        for e in log: fh.write('%d %d %d\n' % e)
    summary[sid] = dict(writes=len(log), last_frame=last, frames=frames, peak=round(peak, 3),
                        insn_per_frame_avg=round(sum(insns) / len(insns)), insn_per_frame_max=max(insns))
    print('%02X writes=%5d last=%4d wav=%5.1fs peak=%.2f insn/frame avg=%d max=%d' % (
        sid, len(log), last, frames / 60.0988, peak, summary[sid]['insn_per_frame_avg'], summary[sid]['insn_per_frame_max']))
json.dump(summary, open(f'{outdir}/summary.json', 'w'), indent=1)
