/* Chunky to planar for the displayed IIGS picture ($E1:2000, 160 bytes a row, two 4-bit pixels a byte, left pixel in the high nibble)
 * to four interleaved bitplanes (40 bytes a plane a row). Only the 8-pixel groups the game changed are converted: rt_dmin/rt_dmax
 * (m816.h) give the first and last changed group of each row; each of the two display buffers keeps its own pending span per row. */
#ifndef C2P_BIG_H
#define C2P_BIG_H
#include "m816.h"

/* One table of 65536 longs: for two source bytes (four pixels) the plane bytes (plane 0 in the top byte) with the four pixels' bits in
   the upper nibble of each plane byte. A group of 8 pixels is lut[first two bytes] | (lut[next two bytes] >> 4). */
#define C2P_LUT_LONGS 65536
#define C2P_TAB_BYTES (C2P_LUT_LONGS * 4)
static void c2p_build_lut(u32 *lut)
{
    u32 part[2][256];
    int j, b, k, hi, lo;
    for (j = 0; j < 2; j++)
        for (b = 0; b < 256; b++) {
            u32 v = 0;
            int p = b >> 4, q = b & 15;
            for (k = 0; k < 4; k++) {
                u32 byte = (u32)(((p >> k) & 1) << (7 - 2 * j)) | (u32)(((q >> k) & 1) << (6 - 2 * j));
                v |= byte << (8 * (3 - k));
            }
            part[j][b] = v;
        }
    for (hi = 0; hi < 256; hi++)
        for (lo = 0; lo < 256; lo++) lut[(hi << 8) | lo] = part[0][hi] | part[1][lo];
}

static u8 c2p_pmin[2][200], c2p_pmax[2][200];
static u8 *c2p_shadow[2]; static u8 c2p_force[2] = { 1, 1 };

static void c2p_all_dirty(void)
{
    int i;
    c2p_force[0] = c2p_force[1] = 1;
    for (i = 0; i < 200; i++) { c2p_pmin[0][i] = c2p_pmin[1][i] = 0; c2p_pmax[0][i] = c2p_pmax[1][i] = 39; }
}

/* move the game's changes into the pending spans of both buffers */
static void c2p_collect(void)
{
    int i;
    for (i = 0; i < 200; i++) {
        u8 lo = rt_dmin[i], hi = rt_dmax[i];
        if (lo > hi) continue;
        rt_dmin[i] = 255; rt_dmax[i] = 0;
        if (lo < c2p_pmin[0][i]) c2p_pmin[0][i] = lo;
        if (hi > c2p_pmax[0][i]) c2p_pmax[0][i] = hi;
        if (lo < c2p_pmin[1][i]) c2p_pmin[1][i] = lo;
        if (hi > c2p_pmax[1][i]) c2p_pmax[1][i] = hi;
    }
}

/* convert the pending groups of display buffer `buf` */
static void c2p_convert(const u32 *lut, const u8 *src, u8 *dst, int buf)
{
    u8 *pmin = c2p_pmin[buf], *pmax = c2p_pmax[buf];
    int y;
    for (y = 0; y < 200; y++) {
        int g0 = pmin[y], g1 = pmax[y], n, x;
        const u8 *s;
        u8 *d;
        if (g0 > g1) continue;
        pmin[y] = 255; pmax[y] = 0;
        s = src + y * 160 + g0 * 4;
        d = dst + y * 160 + g0;
        n = g1 - g0 + 1;
#ifdef __mc68000__
        if (!c2p_force[buf]) {
            u32 t2, t3, idx = 0, v;
            const u8 *h = c2p_shadow[buf] + (s - src);
            n--;
            __asm__ volatile(
                "moveq #0,%[i]\n"
                "1: move.l (%[s])+,%[v]\n\t"
                "cmp.l (%[h])+,%[v]\n\t"
                "beq.s 2f\n\t"
                "move.l %[v],-4(%[h])\n\t"
                "move.w %[v],%[i]\n\t"
                "move.l (%[l],%[i].l*4),%[b]\n\t"
                "lsr.l #4,%[b]\n\t"
                "swap %[v]\n\t"
                "move.w %[v],%[i]\n\t"
                "move.l (%[l],%[i].l*4),%[a]\n\t"
                "or.l %[b],%[a]\n\t"
                "move.b %[a],120(%[d])\n\t"
                "lsr.l #8,%[a]\n\t"
                "move.b %[a],80(%[d])\n\t"
                "lsr.l #8,%[a]\n\t"
                "move.b %[a],40(%[d])\n\t"
                "lsr.l #8,%[a]\n\t"
                "move.b %[a],(%[d])\n\t"
                "2: addq.l #1,%[d]\n\t"
                "dbra %[n],1b"
                : [s] "+a"(s), [d] "+a"(d), [h] "+a"(h), [n] "+d"(n), [i] "+d"(idx), [a] "=&d"(t2), [b] "=&d"(t3), [v] "=&d"(v)
                : [l] "a"(lut) : "memory", "cc");
        } else {
            u32 t2, t3, idx;
            { u32 *sh = (u32 *)(c2p_shadow[buf] + (s - src)); const u32 *sp = (const u32 *)s; int k; for (k = 0; k < n; k++) sh[k] = sp[k]; }
            n--;
            __asm__ volatile(
                "moveq #0,%[i]\n"
                "1: move.w (%[s])+,%[i]\n\t"
                "move.l (%[l],%[i].l*4),%[a]\n\t"
                "move.w (%[s])+,%[i]\n\t"
                "move.l (%[l],%[i].l*4),%[b]\n\t"
                "lsr.l #4,%[b]\n\t"
                "or.l %[b],%[a]\n\t"
                "move.b %[a],120(%[d])\n\t"
                "lsr.l #8,%[a]\n\t"
                "move.b %[a],80(%[d])\n\t"
                "lsr.l #8,%[a]\n\t"
                "move.b %[a],40(%[d])\n\t"
                "lsr.l #8,%[a]\n\t"
                "move.b %[a],(%[d])+\n\t"
                "dbra %[n],1b"
                : [s] "+a"(s), [d] "+a"(d), [n] "+d"(n), [i] "=&d"(idx), [a] "=&d"(t2), [b] "=&d"(t3)
                : [l] "a"(lut) : "memory", "cc");
        }
#else
        for (x = 0; x < n; x++) {
            u32 v = lut[*(const u16 *)s] | (lut[*(const u16 *)(s + 2)] >> 4);
            d[x] = (u8)(v >> 24); d[40 + x] = (u8)(v >> 16); d[80 + x] = (u8)(v >> 8); d[120 + x] = (u8)v;
            s += 4;
        }
#endif
    }
    c2p_force[buf] = 0;
}
#endif
