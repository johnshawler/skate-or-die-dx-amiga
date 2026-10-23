"""65816 instruction decoding for the translator (opcode table shared with tools/dis816.py)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import dis816

MODES = dis816.MODES
T = dis816.T

class Insn:
    __slots__ = ('pc', 'op', 'mn', 'mode', 'size', 'val', 'raw', 'm', 'x', 'd', 'db', 'dk', 'nxt')
    def __repr__(self):
        return '%04X %s %s %s' % (self.pc, self.mn, self.mode, ('$%X' % self.val) if self.size > 1 else '')

def decode(data, base, pc, m, x):
    """decode the instruction at pc (16-bit address in the overlay's bank); m/x = 1 for 8-bit registers. Returns Insn or None."""
    o = pc - base
    if not (0 <= o < len(data)): return None
    op = data[o]
    if op not in T: return None
    mn, mode = T[op]
    fmt, sz = MODES[mode]
    if sz == -1: sz = 1 if m else 2
    elif sz == -2: sz = 1 if x else 2
    if o + 1 + sz > len(data): return None
    i = Insn()
    i.pc = pc; i.op = op; i.mn = mn; i.mode = mode; i.size = 1 + sz
    i.val = int.from_bytes(data[o + 1:o + 1 + sz], 'little') if sz else 0
    i.raw = data[o:o + 1 + sz]
    i.nxt = (pc + 1 + sz) & 0xFFFF
    if mode == 'long': i.size = 4
    return i

def fix_long(data, base, i):
    """JML/JSL/long-addressing operands are 3 bytes; the shared table calls them 'long' with size 3"""
    return i
