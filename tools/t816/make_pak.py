#!/usr/bin/env python3
"""Write the initial memory image of an event (what INTER's loader leaves in memory for game mode N) as a pak the Amiga build loads.
pak: 'T8PK', u32 nseg, then per segment u32 addr24, u32 len, bytes (padded to 2); big-endian.  usage: make_pak.py <mode> <out.pak>"""
import sys, os, struct
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, f'{here}/../cpu816')
import cpu816 as C, event_sim as E
mode, out = int(sys.argv[1]), sys.argv[2]
E.load_mode(mode)
BANKS = [0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1]
segs = []
for b in BANKS:
    m = bytes(C.mem[b << 16:(b << 16) + 65536])
    i = 0
    while i < 65536:
        if m[i] == 0: i += 1; continue
        j = i
        zeros = 0; end = i
        while j < 65536 and zeros < 64:
            if m[j] == 0: zeros += 1
            else: zeros = 0; end = j
            j += 1
        segs.append(((b << 16) | i, m[i:end + 1])); i = end + 1
blob = b'T8PK' + struct.pack('>I', len(segs))
for a, d in segs:
    blob += struct.pack('>II', a, len(d)) + d + (b'\0' if len(d) & 1 else b'')
open(out, 'wb').write(blob)
print('%d segments, %d bytes' % (len(segs), len(blob)))
