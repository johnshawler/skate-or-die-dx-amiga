#!/usr/bin/env python3
"""Run the NES Skate or Die sound engine (bank 0) in a 6502 emulator and log APU register writes.
Engine entry points found by static analysis of the fixed bank:
  $8008  reset/stop all      $8040  start sound id in A (0..$57)      $8245  per-frame update (called from NMI)
usage as module: run(rom_path, sound_id, frames) -> list of (frame, reg, value)"""
import sys
from py65.devices.mpu6502 import MPU
from py65.memory import ObservableMemory

def load_rom(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'NES\x1a'
    return d[16:16 + 0x20000]

class Machine:
    def __init__(self, rom):
        self.log = []; self.frame = 0
        self.mem = ObservableMemory()
        banks = [rom[i*0x4000:(i+1)*0x4000] for i in range(8)]
        for i, b in enumerate(banks[0]): self.mem[0x8000 + i] = b       # switchable bank: sound engine lives in bank 0
        for i, b in enumerate(banks[7]): self.mem[0xC000 + i] = b       # fixed bank (also holds the DPCM sample area)
        self.mem.subscribe_to_write(range(0x4000, 0x4018), self._apu_write)
        self.mem.subscribe_to_write(range(0x8000, 0x10000), lambda a, v: None)  # ignore mapper writes; bank 0 stays mapped
        self.cpu = MPU(memory=self.mem)
        self.stub = 0x7F00
        self.mem[self.stub] = 0x20; self.mem[self.stub+3] = 0x4C; self.mem[self.stub+4] = (self.stub+3) & 0xFF; self.mem[self.stub+5] = (self.stub+3) >> 8
    def _apu_write(self, addr, val):
        self.log.append((self.frame, addr - 0x4000, val))
    def call(self, addr, a=0, x=0, y=0, limit=200000):
        self.mem[self.stub+1] = addr & 0xFF; self.mem[self.stub+2] = addr >> 8
        c = self.cpu; c.a, c.x, c.y = a, x, y; c.sp = 0xFD; c.pc = self.stub
        n = 0
        while c.pc != self.stub + 3 and n < limit:
            c.step(); n += 1
        return n

def run(rom_path, sound_id, frames):
    m = Machine(load_rom(rom_path))
    m.call(0x8008)
    m.call(0x8040, a=sound_id)
    for f in range(frames):
        m.frame = f
        m.call(0x8245)
    return m.log

if __name__ == '__main__':
    log = run(sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3]))
    for e in log[:60]: print(e)
    print(len(log), 'writes')
