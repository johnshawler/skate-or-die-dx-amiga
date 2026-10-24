#!/usr/bin/env python3
"""Convert a flat single-section m68k ELF (linked at 0 with --emit-relocs) into the relocatable image the WHDLoad build loads without an operating system:
u32 magic ('SODW' program, 'SODO' overlay), u32 image length (padded to 4), u32 number of relocations, u32 total file length (the loader overwrites it),
the image, then the offsets of the 32-bit words that need the load address added (big endian).
usage: elf2whd.py in.elf out.bin SODW|SODO"""
import re, struct, subprocess, sys, tempfile, os
elf, out, magic = sys.argv[1:4]
_t = tempfile.mkstemp(suffix='.bin')[1]
subprocess.run(['m68k-linux-gnu-objcopy', '-O', 'binary', elf, _t], check=True)
flat = open(_t, 'rb').read(); os.unlink(_t)
flat += b'\0' * (-len(flat) % 4)
rel = subprocess.run(['m68k-linux-gnu-readelf', '-rW', elf], capture_output=True, text=True, check=True).stdout
offs = []
for line in rel.splitlines():
    m = re.match(r'\s*([0-9a-f]{8,})\s+[0-9a-f]+\s+(R_68K_\w+)', line)
    if m:
        if m.group(2) in ('R_68K_PC8', 'R_68K_PC16', 'R_68K_PC32'): continue      # same section: resolved by the linker
        if m.group(2) != 'R_68K_32': sys.exit('unsupported relocation ' + m.group(2))
        offs.append(int(m.group(1), 16))
offs = sorted(set(offs))
body = flat + b''.join(struct.pack('>I', o) for o in offs)
total = 16 + len(body)
open(out, 'wb').write(magic.encode('ascii') + struct.pack('>III', len(flat), len(offs), total) + body)
print('%s: %d bytes (image %d, %d relocations)' % (out, total, len(flat), len(offs)))
