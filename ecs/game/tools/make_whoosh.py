#!/usr/bin/env python3
"""data/whoosh.raw: the NES whoosh (sound $0A, played when the skater enters an event) cut out of sfx.raw (22050 Hz, signed 8-bit) using the offsets in src/ev_audio.h.
usage: make_whoosh.py <sfx.raw> <whoosh.raw>"""
import sys
off, ln = 11008, 27888                      # sfx_tab[0x0A - SFX_FIRST] in src/ev_audio.h
d = open(sys.argv[1], 'rb').read()
assert len(d) >= off + ln
open(sys.argv[2], 'wb').write(d[off:off + ln])
