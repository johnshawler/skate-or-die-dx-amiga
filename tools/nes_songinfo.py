#!/usr/bin/env python3
"""Loop length, channel use and a rough key for each NES song (full-mix ids).
usage: python3 -I tools/nes_songinfo.py <rom.nes>"""
import sys, os, collections, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nes_sound
rom = nes_sound.load_rom(sys.argv[1])
FPS = 60.0988
NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def song(sid, frames=9000):
    m = nes_sound.Machine(rom); m.call(0x8008); m.call(0x8040, a=sid)
    for f in range(frames): m.frame = f; m.call(0x8245)
    return m.log
def find_loop(log, frames):
    per = collections.defaultdict(list)
    for f, r, v in log: per[f].append((r, v))
    sig = [tuple(per.get(f, ())) for f in range(frames)]
    best = (0, 0.0)
    for P in range(120, frames // 2):
        n = frames - P - 600
        if n < 400: break
        same = sum(1 for f in range(600, 600 + n) if sig[f] == sig[f + P])
        sc = same / n
        if sc > best[1]: best = (P, sc)
    return best
for sid in (0x31, 0x35, 0x39, 0x3D, 0x41, 0x45, 0x49, 0x4D):
    log = song(sid)
    P, sc = find_loop(log, 9000)
    cur = {}; pcs = collections.Counter(); wr = collections.Counter()
    for f, r, v in log:
        wr[{0:'p1',1:'p1',2:'p1',3:'p1',4:'p2',5:'p2',6:'p2',7:'p2',8:'tri',9:'tri',10:'tri',11:'tri',12:'noise',13:'noise',14:'noise',15:'noise'}.get(r,'dmc' if 16<=r<20 else 'ctl')] += 1
        if r in (10,):
            cur[r] = (cur.get(r, 0) & 0x700) | v
        if r == 11:
            t = (cur.get(10, 0) & 0xFF) | ((v & 7) << 8); fr = 1789773 / (32 * (t + 1))
            if fr > 30:
                n = round(69 + 12 * math.log2(fr / 440)); pcs[n % 12] += 1
    top = [NAMES[k] for k, _ in pcs.most_common(5)]
    print('$%02X loop=%s frames (%.1fs, match %.0f%%)  writes=%s  bass pitch classes=%s' % (
        sid, P, P / FPS, sc * 100, dict(wr), top))
