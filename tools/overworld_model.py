#!/usr/bin/env python3
"""Skater movement on the Skate or Die (Apple IIGS) overworld map, transcribed from overlay F39 (state 0).
Every constant and branch below comes from the disassembly (work/disasm/F39_full.txt); the fuzz test in
tools/cpu816/fuzz_model.py checks this model pass-for-pass against the original code running on a 65816 core.

Control byte (active low, as the game builds it): bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire.
  up = accelerate, down = brake, left/right = turn (the skater always moves along its heading).
Coordinates: x in units of 2 pixels, y in pixels; sprite origin on screen = (2*x - 4, y - 20) for the torso."""

VX = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0]   # $49DB
VY = [16 - i for i in range(33)]                                                                                        # $49FB (vy[i] = vx[i+16])

START_X, START_Y = 0x4B, 0x54


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Skater:
    def __init__(self):
        self.x = START_X; self.y = START_Y          # $3C, $3E
        self.h = 0                                  # $40 heading, -32..32 (64 steps per turn, 0 = south)
        self.spd = 0; self.sub = 0                  # $42 speed level 0..5, $44 its repeat counter
        self.turn = 0; self.tc = 0                  # $4A turn rate -1..1, $4C
        self.b8 = 0                                 # $B8 heading accumulator
        self.fx = 0; self.fy = 0                    # $BE, $BC fine position accumulators
        self.exit_code = 0                          # $6C (1..5 when the skater leaves the map)
        self.in_shop = False                        # set when the skater goes through the shop door

    # ---- input handlers (rows of the table at $4A47, state 0 column) ----
    def _decay_turn(self):                          # $357D
        if self.turn == 0: return
        if self.turn > 0: self._turn_right_like()   # $3588 path
        else: self._turn_left_like()                # $35A5 path

    def _turn_right_like(self):                     # $3588: toward -1
        if self.turn == -1: return
        self.tc -= 1
        if self.tc == -1:
            self.tc = 0; self.turn -= 1

    def _turn_left_like(self):                      # $35A5: toward +1
        if self.turn == 1: return
        self.tc += 1
        if self.tc == 1:
            self.tc = 0; self.turn += 1

    def _speed_up(self):                            # $35C5
        if self.spd == 5: return
        self.sub += 1
        if self.sub >= 8:
            self.spd += 1; self.sub = 0

    def _speed_down(self):                          # $35E5
        if self.spd == 0: return
        self.sub -= 1
        if self.sub < 0:
            self.spd -= 1; self.sub = 8

    def _handle_input(self, ctl):
        n = ctl & 15
        if n == 15: self._decay_turn()                                   # $3579
        elif n == 14: self._decay_turn(); self._speed_up()               # up      $35C2
        elif n == 13: self._decay_turn(); self._speed_down()             # down    $35E2
        elif n == 11: self._turn_left_like()                             # left    $35A5
        elif n == 7: self._turn_right_like()                             # right   $3588
        elif n == 10: self._speed_up(); self._turn_left_like()           # up+left $360A
        elif n == 6: self._speed_up(); self._turn_right_like()           # up+right $3611
        elif n == 9: self._speed_down(); self._turn_left_like()          # down+left $35FC
        elif n == 5: self._speed_down(); self._turn_right_like()         # down+right $3603
        # other combinations (up+down, left+right) cannot be produced by the input devices

    # ---- heading and velocity ($32D9 / $327D) ----
    def _heading(self):
        self.b8 = s16(self.b8 + self.turn)
        while True:
            if self.b8 < 0:
                self.b8 += 2
                if self.h == -32: self.h = 32
                self.h -= 1
            elif self.b8 >= 2:
                self.b8 -= 2
                if self.h == 32: self.h = -32
                self.h += 1
            else:
                break
        a = abs(self.h)
        vx, vy = VX[a], VY[a]
        if self.h < 0: vx = -vx
        return vx, vy

    # ---- collisions ($3384 x+1, $33EC x-1, $345E y+1, $34C6 y-1) ----
    def _x_plus(self):
        x, y = self.x, self.y
        if x == 0x98: self.exit_code = 2 if y >= 0x28 else 1; return
        if x == 0x25 and y < 0x6C: return
        if x == 0x4F and y < 0x6C: return
        if x == 0x29 and y >= 0x90: return
        if x == 0x7D and y >= 0x98: return
        if x == 0x81 and 0x27 <= y < 0x85: return
        self.x += 1

    def _x_minus(self):
        x, y = self.x, self.y
        if x == 0:
            if y >= 0x28: self.exit_code = 5
            else: self.exit_code = 1
            return
        if x == 0x47 and y < 0x6C: return
        if x == 0x71 and y < 0x6C: return
        if x == 0x6D and y >= 0x90: return
        if x == 0x19 and y >= 0x98: return
        if x == 0x16 and 0x27 <= y < 0x85: return
        self.x -= 1

    def _y_plus(self):
        x, y = self.x, self.y
        if y == 0xC0: self.exit_code = 4 if x >= 0x2A else 3; return
        if y == 0x27 and x < 0x16: return
        if y == 0x27 and x >= 0x81: return
        if y == 0x98 and x < 0x19: return
        if y == 0x98 and x >= 0x7D: return
        if y == 0x90 and 0x29 <= x < 0x6D: return
        self.y += 1

    def _y_minus(self):
        x, y = self.x, self.y
        if y == 0x15: return
        if y == 0x6C and 0x25 <= x < 0x47: return
        if y == 0x6C and 0x4F <= x < 0x71: return
        if y == 0x85 and x < 0x16: return
        if y == 0x85 and x >= 0x81: return
        if y < 0x53 and 0x47 <= x < 0x50:            # the shop door
            self.in_shop = True
            return
        self.y -= 1

    def _move(self, vx, vy):                        # $3301
        for _ in range(self.spd):
            self.fx = s16(self.fx + vx); self.fy = s16(self.fy + vy)
            if self.fx < 0:
                if (self.fx & 0xFFFF) < 0xFFD1:
                    self.fx += 0x30; self._x_minus()
            elif self.fx >= 0x30:
                self.fx -= 0x30; self._x_plus()
            if self.fy < 0:
                if (self.fy & 0xFFFF) < 0xFFD1:
                    self.fy += 0x30; self._y_minus()
                    if self.in_shop: return
            elif self.fy >= 0x30:
                self.fy -= 0x30; self._y_plus()

    def step(self, ctl):
        """One main-loop pass in state 0."""
        self._handle_input(ctl)
        vx, vy = self._heading()
        self._move(vx, vy)

    # ---- sprite frames ($3138): returns (torso, legs, board, shadow) frame numbers in the F36/F37 sprite set ----
    def frames(self):
        v = ((self.h + 0) - 2) & 0xFFFF
        ae = ((v >> 2) & 15) ^ 15
        return ((ae - self.turn) & 15, 0x10 + ae, 0x20 + ae, 0x30 + ae)
