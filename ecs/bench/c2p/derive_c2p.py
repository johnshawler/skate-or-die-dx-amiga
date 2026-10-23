#!/usr/bin/env python3
"""Derive a table-free chunky->planar kernel for 32 pixels (4 longs of 8 packed 4-bit pixels, left pixel in the high nibble) -> 4 plane longs
(plane q = bit q of the pixel, leftmost pixel in the MSB). Searches the shortest sequence of 'exchange' steps (swap long-index bit x with bit-index bit y)
and checks the result against a direct bit-by-bit conversion on random data."""
import itertools, random, collections
# index bit names: L1 L0 (which long), b4..b0 (bit within the long). source arrangement and target arrangement as the *meaning* of each position
SRC = ['i1', 'i0', 'm2', 'm1', 'm0', 'q1', 'q0']          # positions [L1 L0 b4 b3 b2 b1 b0]; i = long number counted from the last long (3-i), m' = 7 - nibble index from the left
DST = ['q1', 'q0', 'i1', 'i0', 'm2', 'm1', 'm0']
def swaps():
    for x in (0, 1):
        for y in range(2, 7): yield (x, y)
start = tuple(SRC); goal = tuple(DST); prev = {start: None}; q = collections.deque([start])
while q:
    s = q.popleft()
    if s == goal: break
    for x, y in swaps():
        t = list(s); t[x], t[y] = t[y], t[x]; t = tuple(t)
        if t not in prev: prev[t] = (s, (x, y)); q.append(t)
path = []; s = goal
while prev[s]: s, op = prev[s][0], prev[s][1]; path.append(op); s = prev[goal][0] if False else s
# rebuild path properly
path = []; s = goal
while prev[s]: p, op = prev[s]; path.append(op); s = p
path.reverse(); print('exchange steps:', path)
def pixels_to_longs(px):    # px: 32 pixel values -> 4 longs, long k holds pixels 8k..8k+7, pixel 8k in the top nibble
    return [sum(px[8 * k + m] << (28 - 4 * m) for m in range(8)) for k in range(4)]
def reference(px):          # plane q long: bit (31 - n) = bit q of pixel n
    return [sum(((px[n] >> q) & 1) << (31 - n) for n in range(32)) for q in range(4)]
def exchange(L, x, y):      # swap long-index bit x (0 = L1 = long number bit 1) with bit-index bit y (position 2.. -> b(6-y)); returns new longs
    lb = 1 - x                                  # long index bit (L1 -> bit 1, L0 -> bit 0)
    bb = 6 - y                                  # bit position within a long
    s = 1 << bb; mask = sum(1 << j for j in range(32) if not (j >> bb) & 1)
    out = list(L)
    for a in range(4):
        if (a >> lb) & 1: continue
        b = a | (1 << lb)
        t = ((L[a] >> s) ^ L[b]) & mask; out[b] = L[b] ^ t; out[a] = L[a] ^ (t << s) & 0xFFFFFFFF
    return out
random.seed(1)
for _ in range(2000):
    px = [random.randrange(16) for _ in range(32)]
    L = pixels_to_longs(px)[::-1]               # long number i' = 3 - k
    for x, y in path: L = exchange(L, x, y)
    assert L == reference(px), (path,)
print('verified on 2000 random blocks')
