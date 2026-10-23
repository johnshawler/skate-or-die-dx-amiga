#!/usr/bin/env python3
"""Convert a flat single-section m68k ELF (linked at 0 with --emit-relocs) into an AmigaOS hunk executable.
usage: elf2hunk.py in.elf out.exe"""
import re, struct, subprocess, sys
elf, out = sys.argv[1:3]
flat = subprocess.run(['m68k-linux-gnu-objcopy', '-O', 'binary', elf, '/dev/stdout'], capture_output=True, check=True).stdout
flat += b'\0' * (-len(flat) % 4)
rel = subprocess.run(['m68k-linux-gnu-readelf', '-rW', elf], capture_output=True, text=True, check=True).stdout
offs = []
for line in rel.splitlines():
    m = re.match(r'\s*([0-9a-f]{8,})\s+[0-9a-f]+\s+(R_68K_\w+)', line)
    if m:
        if m.group(2) in ('R_68K_PC8', 'R_68K_PC16', 'R_68K_PC32'): continue      # same hunk: already resolved by the linker
        if m.group(2) != 'R_68K_32': sys.exit('unsupported relocation ' + m.group(2))
        offs.append(int(m.group(1), 16))
offs = sorted(set(offs))
n = len(flat) // 4
b = struct.pack('>IIIII', 0x3F3, 0, 1, 0, 0) + struct.pack('>I', n)
b += struct.pack('>II', 0x3E9, n) + flat
if offs:
    b += struct.pack('>III', 0x3EC, len(offs), 0) + b''.join(struct.pack('>I', o) for o in offs) + struct.pack('>I', 0)
b += struct.pack('>I', 0x3F2)
open(out, 'wb').write(b)
print('hunk exe: %d bytes (code+data %d, %d relocations)' % (len(b), len(flat), len(offs)))
