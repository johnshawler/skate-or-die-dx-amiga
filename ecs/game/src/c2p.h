/* Chunky to planar for the displayed IIGS picture ($E1:2000, 160 bytes a row, two 4-bit pixels a byte, left pixel in the high nibble)
 * to four interleaved bitplanes (40 bytes a plane a row). Only the 8-pixel groups the game changed are looked at: rt_dmin/rt_dmax
 * (m816.h) give the first and last changed group of each row, each of the two display buffers keeps its own pending span per row, and
 * inside a span a group is converted only if its four chunky bytes differ from what that buffer was last converted from (a shadow copy).
 * The conversion uses four 256-entry tables (4 KB): one source byte (two pixels) -> the plane bytes' bits for those two pixels. */
#ifndef C2P_H
#define C2P_H
#include "m816.h"

#define C2P_TAB_LONGS 1024
#define C2P_TAB_BYTES (C2P_TAB_LONGS * 4)
static void c2p_build_lut(u32 *t)
{
    int j, b, k;
    u32 part[2][256];
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
    for (b = 0; b < 256; b++) { t[b] = part[0][b]; t[256 + b] = part[1][b]; t[512 + b] = part[0][b] >> 4; t[768 + b] = part[1][b] >> 4; }
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
static void c2p_convert(const u32 *tab, const u8 *src, u8 *dst, int buf)
{
    u8 *pmin = c2p_pmin[buf], *pmax = c2p_pmax[buf];
    int y, force = c2p_force[buf];
    for (y = 0; y < 200; y++) {
        int g0 = pmin[y], g1 = pmax[y], n;
        const u8 *s;
        u8 *d;
        u8 *h;
        if (g0 > g1) continue;
        pmin[y] = 255; pmax[y] = 0;
        s = src + y * 160 + g0 * 4;
        d = dst + y * 160 + g0;
        h = c2p_shadow[buf] + y * 160 + g0 * 4;
        n = g1 - g0 + 1;
#ifdef __mc68000__
        {
            u32 t2, v, idx = 0;
            n--;
            if (force) {                                        /* the whole picture: convert every group and remember the chunky bytes */
                __asm__ volatile(
                    "moveq #0,%[i]\n"
                    "1: move.l (%[s])+,%[v]\n\t"
                    "move.l %[v],(%[h])+\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tmove.l (%[l],%[i].l*4),%[a]\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tor.l (1024,%[l],%[i].l*4),%[a]\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tor.l (2048,%[l],%[i].l*4),%[a]\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tor.l (3072,%[l],%[i].l*4),%[a]\n\t"
                    "move.b %[a],120(%[d])\n\tlsr.l #8,%[a]\n\t"
                    "move.b %[a],80(%[d])\n\tlsr.l #8,%[a]\n\t"
                    "move.b %[a],40(%[d])\n\tlsr.l #8,%[a]\n\t"
                    "move.b %[a],(%[d])+\n\t"
                    "dbra %[n],1b"
                    : [s] "+a"(s), [d] "+a"(d), [h] "+a"(h), [n] "+d"(n), [i] "+d"(idx), [a] "=&d"(t2), [v] "=&d"(v)
                    : [l] "a"(tab) : "memory", "cc");
            } else {
                __asm__ volatile(
                    "moveq #0,%[i]\n"
                    "1: move.l (%[s])+,%[v]\n\t"
                    "cmp.l (%[h])+,%[v]\n\t"
                    "beq.s 2f\n\t"
                    "move.l %[v],-4(%[h])\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tmove.l (%[l],%[i].l*4),%[a]\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tor.l (1024,%[l],%[i].l*4),%[a]\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tor.l (2048,%[l],%[i].l*4),%[a]\n\t"
                    "rol.l #8,%[v]\n\tmove.b %[v],%[i]\n\tor.l (3072,%[l],%[i].l*4),%[a]\n\t"
                    "move.b %[a],120(%[d])\n\tlsr.l #8,%[a]\n\t"
                    "move.b %[a],80(%[d])\n\tlsr.l #8,%[a]\n\t"
                    "move.b %[a],40(%[d])\n\tlsr.l #8,%[a]\n\t"
                    "move.b %[a],(%[d])\n\t"
                    "2: addq.l #1,%[d]\n\t"
                    "dbra %[n],1b"
                    : [s] "+a"(s), [d] "+a"(d), [h] "+a"(h), [n] "+d"(n), [i] "+d"(idx), [a] "=&d"(t2), [v] "=&d"(v)
                    : [l] "a"(tab) : "memory", "cc");
            }
        }
#else
        {
            int x;
            for (x = 0; x < n; x++) {
                const u8 *q = s + 4 * x;
                u32 v = tab[q[0]] | tab[256 + q[1]] | tab[512 + q[2]] | tab[768 + q[3]];
                u8 *hh = h + 4 * x;
                if (!force && hh[0] == q[0] && hh[1] == q[1] && hh[2] == q[2] && hh[3] == q[3]) continue;
                hh[0] = q[0]; hh[1] = q[1]; hh[2] = q[2]; hh[3] = q[3];
                d[x] = (u8)(v >> 24); d[40 + x] = (u8)(v >> 16); d[80 + x] = (u8)(v >> 8); d[120 + x] = (u8)v;
            }
        }
#endif
    }
    c2p_force[buf] = 0;
}
#endif
