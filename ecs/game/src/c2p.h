/* Chunky to planar for the displayed IIGS picture ($E1:2000, 160 bytes a row, two 4-bit pixels a byte, left pixel in the high nibble)
 * to four interleaved bitplanes (40 bytes a plane a row). Only the 8-pixel groups the game changed are looked at: rt_dmin/rt_dmax
 * (m816.h) give the first and last changed group of each row, each of the two display buffers keeps its own pending span per row, and
 * inside a span a group is converted only if its four chunky bytes differ from what that buffer was last converted from (a shadow copy).
 * The conversion uses four 256-entry tables (4 KB): one source byte (two pixels) -> the plane bytes' bits for those two pixels. */
#ifndef C2P_H
#define C2P_H
#include "m816.h"
#ifdef C2P_BIGLUT
/* One table of 65536 longs: for two source bytes (four pixels) the plane bytes (plane 0 in the top byte) with the four pixels' bits in the upper nibble of
   each plane byte. A group of 8 pixels is lut[first two bytes] | (lut[next two bytes] >> 4). 256 KB of Chip RAM. */
#define C2P_TAB_LONGS 65536
#define C2P_TAB_BYTES (C2P_TAB_LONGS * 4)
#else
#define C2P_TAB_LONGS 1024                            /* four tables of 256: one source byte (two pixels) -> the plane bytes' bits for those two pixels */
#define C2P_TAB_BYTES (C2P_TAB_LONGS * 4)
#endif
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
#ifdef C2P_BIGLUT
    { int hi, lo; for (hi = 0; hi < 256; hi++) for (lo = 0; lo < 256; lo++) t[(hi << 8) | lo] = part[0][hi] | part[1][lo]; }
#else
    for (b = 0; b < 256; b++) { t[b] = part[0][b]; t[256 + b] = part[1][b]; t[512 + b] = part[0][b] >> 4; t[768 + b] = part[1][b] >> 4; }
#endif
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

#ifdef __mc68000__
/* the plane bytes of one 8-pixel group (four chunky bytes in register `v`, destroyed) into register `r` (plane 0 in the top byte); %[i] is a data register whose
   upper bytes are zero, %[t0]-%[t3] address the tables */
#ifdef C2P_BIGLUT
#define C2P_GROUP(v, r) \
    "move.w " v ",%[i]\n\tmove.l (%[t0],%[i].l*4)," r "\n\tlsr.l #4," r "\n\t" \
    "swap " v "\n\tmove.w " v ",%[i]\n\tor.l (%[t0],%[i].l*4)," r "\n\t"
#else
#define C2P_GROUP(v, r) \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tmove.l (%[t0],%[i].l*4)," r "\n\t" \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tor.l (%[t1],%[i].l*4)," r "\n\t" \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tor.l (%[t2],%[i].l*4)," r "\n\t" \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tor.l (%[t3],%[i].l*4)," r "\n\t"
#endif
#ifdef C2P_BIGLUT
#define C2P_GROUP_D C2P_GROUP
#define C2P_T1(tab) (tab)
#define C2P_T2(tab) (tab)
#define C2P_T3(tab) (tab)
#else
#define C2P_GROUP_D(v, r) \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tmove.l (%[t0],%[i].l*4)," r "\n\t" \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tor.l (1024,%[t0],%[i].l*4)," r "\n\t" \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tor.l (2048,%[t0],%[i].l*4)," r "\n\t" \
    "rol.l #8," v "\n\tmove.b " v ",%[i]\n\tor.l (3072,%[t0],%[i].l*4)," r "\n\t"
#define C2P_T1(tab) ((tab) + 256)
#define C2P_T2(tab) ((tab) + 512)
#define C2P_T3(tab) ((tab) + 768)
#endif
#define C2P_TABS : [t0] "a"(tab), [t1] "a"(C2P_T1(tab)), [t2] "a"(C2P_T2(tab)), [t3] "a"(C2P_T3(tab))

/* One group at a time. */
static void c2p_row_one(const u32 *tab, const u8 *s, u8 *d, u8 *h, int n, int force)
{
    u32 t2, v, idx = 0;
    n--;
    if (force) {                                        /* convert every group and remember the chunky bytes */
        __asm__ volatile(
            "1: move.l (%[s])+,%[v]\n\t"
            "move.l %[v],(%[h])+\n\t"
            C2P_GROUP_D("%[v]", "%[a]")
            "move.b %[a],120(%[d])\n\tlsr.l #8,%[a]\n\t"
            "move.b %[a],80(%[d])\n\tlsr.l #8,%[a]\n\t"
            "move.b %[a],40(%[d])\n\tlsr.l #8,%[a]\n\t"
            "move.b %[a],(%[d])+\n\t"
            "dbra %[n],1b"
            : [s] "+a"(s), [d] "+a"(d), [h] "+a"(h), [n] "+d"(n), [i] "+d"(idx), [a] "=&d"(t2), [v] "=&d"(v)
            : [t0] "a"(tab) : "memory", "cc");
    } else {
        __asm__ volatile(
            "1: move.l (%[s])+,%[v]\n\t"
            "cmp.l (%[h])+,%[v]\n\t"
            "beq.s 2f\n\t"
            "move.l %[v],-4(%[h])\n\t"
            C2P_GROUP_D("%[v]", "%[a]")
            "move.b %[a],120(%[d])\n\tlsr.l #8,%[a]\n\t"
            "move.b %[a],80(%[d])\n\tlsr.l #8,%[a]\n\t"
            "move.b %[a],40(%[d])\n\tlsr.l #8,%[a]\n\t"
            "move.b %[a],(%[d])\n\t"
            "2: addq.l #1,%[d]\n\t"
            "dbra %[n],1b"
            : [s] "+a"(s), [d] "+a"(d), [h] "+a"(h), [n] "+d"(n), [i] "+d"(idx), [a] "=&d"(t2), [v] "=&d"(v)
            : [t0] "a"(tab) : "memory", "cc");
    }
}
#ifdef C2P_PAIR
/* (measured on the A1200 model: slower than one group at a time - the loop no longer fits the 68020's 256-byte instruction cache - so it is off) Two groups (16 pixels) at a time, writing a word to each plane instead of two bytes: the two lookups' plane bytes (a0 a1 a2 a3, b0 b1 b2 b3) are
   interleaved into the words a0b0 a1b1 a2b2 a3b3. The shadow copy sits at a constant distance from the picture, so it is addressed with an index register
   (no register for a second pointer). d must be even. */
#define C2P_INTERLEAVE \
    "move.l %[a],%[v0]\n\tand.l #0xFF00FF00,%[v0]\n\t" \
    "move.l %[b],%[v1]\n\tlsr.l #8,%[v1]\n\tand.l #0x00FF00FF,%[v1]\n\tor.l %[v1],%[v0]\n\t" \
    "lsl.l #8,%[a]\n\tand.l #0xFF00FF00,%[a]\n\tand.l #0x00FF00FF,%[b]\n\tor.l %[b],%[a]\n\t" \
    "move.w %[v0],80(%[d])\n\tswap %[v0]\n\tmove.w %[v0],(%[d])\n\t" \
    "move.w %[a],120(%[d])\n\tswap %[a]\n\tmove.w %[a],40(%[d])\n\t"
static void c2p_row_pairs(const u32 *tab, const u8 *s, u8 *d, u8 *h, int npairs, int force)
{
    u32 delta = (u32)h - (u32)s, v0, v1, a, b, i = 0;
    npairs--;
    if (force) {
        __asm__ volatile(
            "1: move.l (%[s])+,%[v0]\n\tmove.l (%[s])+,%[v1]\n\t"
            "move.l %[v0],-8(%[s],%[dl].l)\n\tmove.l %[v1],-4(%[s],%[dl].l)\n\t"
            C2P_GROUP("%[v0]", "%[a]")
            C2P_GROUP("%[v1]", "%[b]")
            C2P_INTERLEAVE
            "addq.l #2,%[d]\n\t"
            "dbra %[n],1b"
            : [s] "+a"(s), [d] "+a"(d), [n] "+d"(npairs), [i] "+d"(i), [a] "=&d"(a), [b] "=&d"(b), [v0] "=&d"(v0), [v1] "=&d"(v1)
            : [dl] "d"(delta), [t0] "a"(tab), [t1] "a"(C2P_T1(tab)), [t2] "a"(C2P_T2(tab)), [t3] "a"(C2P_T3(tab)) : "memory", "cc");
    } else {
        __asm__ volatile(
            "1: move.l (%[s])+,%[v0]\n\tmove.l (%[s])+,%[v1]\n\t"
            "cmp.l -8(%[s],%[dl].l),%[v0]\n\tbne.s 3f\n\t"
            "cmp.l -4(%[s],%[dl].l),%[v1]\n\tbeq.w 2f\n\t"
            "3: move.l %[v0],-8(%[s],%[dl].l)\n\tmove.l %[v1],-4(%[s],%[dl].l)\n\t"
            C2P_GROUP("%[v0]", "%[a]")
            C2P_GROUP("%[v1]", "%[b]")
            C2P_INTERLEAVE
            "2: addq.l #2,%[d]\n\t"
            "dbra %[n],1b"
            : [s] "+a"(s), [d] "+a"(d), [n] "+d"(npairs), [i] "+d"(i), [a] "=&d"(a), [b] "=&d"(b), [v0] "=&d"(v0), [v1] "=&d"(v1)
            : [dl] "d"(delta), [t0] "a"(tab), [t1] "a"(C2P_T1(tab)), [t2] "a"(C2P_T2(tab)), [t3] "a"(C2P_T3(tab)) : "memory", "cc");
    }
}
#endif
/* the groups g0..g0+n-1 of a row: `s`, `d`, `h` point at group g0 of the chunky picture, the planes and the shadow copy */
static void c2p_row(const u32 *tab, const u8 *s, u8 *d, u8 *h, int g0, int n, int force)
{
#ifdef C2P_PAIR
    if (g0 & 1) { c2p_row_one(tab, s, d, h, 1, force); s += 4; d += 1; h += 4; n--; }
    if (n >= 2) { int np = n >> 1; c2p_row_pairs(tab, s, d, h, np, force); s += 8 * np; d += 2 * np; h += 8 * np; n -= 2 * np; }
    if (n) c2p_row_one(tab, s, d, h, n, force);
#else
    (void)g0;
    c2p_row_one(tab, s, d, h, n, force);
#endif
}
#endif

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
        c2p_row(tab, s, d, h, g0, n, force);
#else
        {
            int x;
            for (x = 0; x < n; x++) {
                const u8 *q = s + 4 * x;
#ifdef C2P_BIGLUT
                u32 v = tab[(q[0] << 8) | q[1]] | (tab[(q[2] << 8) | q[3]] >> 4);
#else
                u32 v = tab[q[0]] | tab[256 + q[1]] | tab[512 + q[2]] | tab[768 + q[3]];
#endif
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
