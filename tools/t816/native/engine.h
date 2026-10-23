/* Native versions of the sprite engine's hot routines (the same routines appear in the ramp and high-jump overlays at shifted
 * addresses). Each returns 1 if it did the whole job (registers, flags, memory and stack bytes exactly as the 65816 routine would
 * leave them; the caller then pops the return address) or 0 if the situation is unusual, in which case the translated routine runs.
 * tools/t816/nativetest.py compares each one with the original machine code on real game states.
 * The including file defines the overlay's variable addresses (all in bank 3):
 *   N_VCNT, N_VX, N_VW, N_VOFF, N_VH   the dirty-rectangle lists (count, x, width in words, screen offset, words in rect)
 *   N_SCR1, N_SCR13, N_MASKV           scratch words and the mask word the masked blit ANDs in
 *   N_TA, N_TB                         the sprite descriptor tables read by the masked blits
 *   N_GCOL, N_GROW, N_GTMP             the text cursor (column, row) and a scratch word of the character routine
 * Bank-3 variables $0E, $10, $12, $EC, $EE, $F0 and the direct-page pointers $DD, $E2, $E6 are at the same places in both overlays. */
#ifndef N_ENGINE_H
#define N_ENGINE_H
#include "m816.h"
#include "common.h"

/* The end of a row in the blits is: TXA CLC ADC #$A0 SEC SBC EC SBC EC TAX; CPX #$9C40; BCS done; LDA $F0; BNE again.
 * With 2*EC <= $A0 nothing overflows or borrows, so a row simply moves X on by $A0; the flags (C, V, N, Z) and A of the last row are
 * worked out once at the end by running the original sequence on the last row's X. */
#define ROW_END(A, X, N, V, Z, C, ECV) do { \
    A = X; C = 0; ADC16(0xA0); C = 1; SBC16(ECV); SBC16(ECV); X = A; CMP16(X, 0x9C40); \
    if (!C) { A = G3(0xF0); SETNZ16(A); } } while (0)
#define FINISH(A, X, Xpre, N, V, Z, C, ec, f0) do { S3(0xF0, f0); S3(N_SCR1, 0); X = Xpre; ROW_END(A, X, N, V, Z, C, ec); } while (0)
#define SET_REGS() do { cpu.a = A; cpu.x = X; cpu.y = Y; cpu.n = N; cpu.v = V; cpu.z = Z; cpu.c = C; } while (0)

/* ---- 49EC: copy rows of sprite words ($DD),Y -> bank 1 $B4+X ---- */
static int nat_copy_fwd(void)
{
    u16 A = cpu.a, X = cpu.x, Y = cpu.y, Xpre; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c;
    u16 ec = G3(0xEC), ee = G3(0xEE), ptr = DPW(0xDD), f0;
    if (cpu.d != 0 || cpu.db != 3 || SHADOW_ON() || ec == 0 || ec > 0x50 || ee == 0 || ee % ec) return 0;
    {
        u32 rows = ee / ec;
        if ((u32)ptr + Y + 2UL * ee > 0xFFF0 || (u32)X + 0xB4 + 2UL * ee + 160UL * rows > 0xFFF0) return 0;
    }
    f0 = ee;
    for (;;) {
        copy_bytes(B1 + (u16)(X + 0xB4), B3 + (u16)(ptr + Y), (u16)(2 * ec));
        Xpre = (u16)(X + 2 * ec); Y += 2 * ec; f0 -= ec; X += 0xA0;
        if (X >= 0x9C40 || f0 == 0) break;
    }
    FINISH(A, X, Xpre, N, V, Z, C, ec, f0);
    SET_REGS();
    return 1;
}

/* ---- 4A22: as 49EC, but each row of sprite words is read from its end back to its start (a flipped sprite) ---- */
static int nat_copy_rev(void)
{
    u16 A = cpu.a, X = cpu.x, Y = cpu.y, Xpre, y1 = 0; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c;
    u16 ec = G3(0xEC), ee = G3(0xEE), ptr = DPW(0xDD), f0;
    if (cpu.d != 0 || cpu.db != 3 || SHADOW_ON() || ec == 0 || ec > 0x50 || ee == 0 || ee % ec) return 0;
    {
        u32 rows = ee / ec;
        if ((u32)ptr + Y + 2UL * ee + 2 > 0xFFF0 || (u32)X + 0xB4 + 2UL * ee + 160UL * rows > 0xFFF0) return 0;
    }
    f0 = ee;
    for (;;) {
        u16 *d = (u16 *)(B1 + (u16)(X + 0xB4)); const u16 *s; u16 k;
        y1 = (u16)(Y + 2 * ec);                                         /* TYA CLC ADC EC ADC EC TAY: no carries in this domain */
        s = (const u16 *)(B3 + (u16)(ptr + y1 - 2));
        for (k = 0; k < ec; k++) d[k] = *(const u16 *)((const u8 *)s - 2 * k);
        Xpre = (u16)(X + 2 * ec); Y = y1; f0 -= ec; X += 0xA0;
        if (X >= 0x9C40 || f0 == 0) break;
    }
    FINISH(A, X, Xpre, N, V, Z, C, ec, f0);
    B0[(u16)cpu.s] = (u8)(y1 >> 8); B0[(u16)(cpu.s - 1)] = (u8)y1;      /* the last PHY's bytes stay in the stack page */
    SET_REGS();
    return 1;
}

/* the prologue shared by the masked blits: the descriptor at table X is stored in the list, and X becomes the screen index */
#define MASK_PROLOGUE() do { \
    u16 vy; \
    S3(0x12, 0); \
    A = LE16(B3 + (u16)(N_TA + X)); \
    vy = G3(N_VCNT); S3((u16)(N_VX + vy), A); \
    Y = 0; \
    C = A & 1; A >>= 1; S3(N_SCR13, A); \
    A = LE16(B3 + (u16)(N_TB + X)); A <<= 1; X = A; \
    A = LE16(B2 + (u16)(0x3E00 + X)); \
    C = 0; ADC16(G3(N_SCR13)); S3(N_SCR13, A); X = A; S3(0x10, A); \
    S3(0xF0, G3(0xEE)); } while (0)

static inline int mask_domain(u16 ec, u16 ee, u16 X, u16 Y)
{
    u32 rows = ee / ec;
    u16 e2 = DPW(0xE2), e6 = DPW(0xE6), dd = DPW(0xDD);
    u8 b2 = B0[(u16)(cpu.d + 0xE4)], b6 = B0[(u16)(cpu.d + 0xE8)];
    if (b2 == 1 || b6 == 1 || BK[b2] == BZ || BK[b6] == BZ) return 0;
    /* data read from bank 3 must not overlap the area the blit saves the background into (the translated routine would see its own writes) */
    if ((b2 == 3 && (u32)e2 < (u32)dd + 2UL * ee + 2 * Y + 8 && (u32)dd < (u32)e2 + 2UL * ee + 2 * Y + 8) ||
        (b6 == 3 && (u32)e6 < (u32)dd + 2UL * ee + 2 * Y + 8 && (u32)dd < (u32)e6 + 2UL * ee + 2 * Y + 8)) return 0;
    if ((u32)e2 + Y + 2UL * ee + 4 > 0xFFF0 || (u32)e6 + Y + 2UL * ee + 4 > 0xFFF0 || (u32)dd + Y + 2UL * ee + 4 > 0xFFF0) return 0;
    if ((u32)X + 0xB4 + 2UL * ee + 160UL * rows > 0xFFF0) return 0;
    return 1;
}

/* check the domain of a masked blit before anything is changed (the prologue only reads, then stores) */
#define MASK_CHECK() do { \
    u16 X2 = LE16(B3 + (u16)(N_TB + X)); X2 = (u16)(X2 << 1); \
    u16 st = (u16)(LE16(B2 + (u16)(0x3E00 + X2)) + (LE16(B3 + (u16)(N_TA + X)) >> 1)); \
    ec = G3(0xEC); ee = G3(0xEE); \
    if (ec == 0 || ec > 0x50 || ee == 0 || ee % ec || (u32)X2 + 0x3E00 > 0xFFF0 || !mask_domain(ec, ee, st, 0)) return 0; } while (0)

/* ---- 4867: masked blit of a sprite onto the off-screen copy in bank 1, saving what was there ---- */
static int nat_mask_fwd(void)
{
    u16 A = cpu.a, X = cpu.x, Y = cpu.y, Xpre; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c;
    u16 ec, ee, f0, mk;
    if (cpu.d != 0 || cpu.db != 3 || SHADOW_ON()) return 0;
    MASK_CHECK();
    MASK_PROLOGUE();
    mk = RAW16(B3 + N_MASKV);
    {
        u16 dd = DPW(0xDD), e2 = DPW(0xE2), e6 = DPW(0xE6);
        const u8 *p2 = BK[B0[(u16)(cpu.d + 0xE4)]], *p6 = BK[B0[(u16)(cpu.d + 0xE8)]];
        f0 = ee;
        for (;;) {
            u16 *scr = (u16 *)(B1 + (u16)(X + 0xB4)), *sv = (u16 *)(B3 + (u16)(dd + Y));
            const u16 *m6 = (const u16 *)(p6 + (u16)(e6 + Y)), *d2 = (const u16 *)(p2 + (u16)(e2 + Y));
            u16 k;
            for (k = 0; k < ec; k++) {
                u16 w = scr[k];
                sv[k] = w;
                scr[k] = (u16)(((w & m6[k]) | d2[k]) & mk);
            }
            Xpre = (u16)(X + 2 * ec); Y += 2 * ec; f0 -= ec; X += 0xA0;
            if (X >= 0x9C40 || f0 == 0) break;
        }
    }
    FINISH(A, X, Xpre, N, V, Z, C, ec, f0);
    SET_REGS();
    return 1;
}

/* ---- 48D5: the same, flipped (rows read from the end, bytes of each word swapped, no extra mask) ---- */
static int nat_mask_rev(void)
{
    u16 A = cpu.a, X = cpu.x, Y = cpu.y, Xpre, y1 = 0; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c;
    u16 ec, ee, f0;
    if (cpu.d != 0 || cpu.db != 3 || SHADOW_ON()) return 0;
    MASK_CHECK();
    MASK_PROLOGUE();
    {
        u16 dd = DPW(0xDD), e2 = DPW(0xE2), e6 = DPW(0xE6);
        const u8 *p2 = BK[B0[(u16)(cpu.d + 0xE4)]], *p6 = BK[B0[(u16)(cpu.d + 0xE8)]];
        f0 = ee;
        for (;;) {
            u16 *scr = (u16 *)(B1 + (u16)(X + 0xB4)), k;
            const u8 *m6, *d2; u8 *sv;
            y1 = (u16)(Y + 2 * ec);
            if ((u32)dd + y1 > 0xFFF0 || (u32)e2 + y1 > 0xFFF0 || (u32)e6 + y1 > 0xFFF0) return 0;
            sv = B3 + (u16)(dd + y1 - 2); m6 = p6 + (u16)(e6 + y1 - 2); d2 = p2 + (u16)(e2 + y1 - 2);
            for (k = 0; k < ec; k++) {
                u16 w = scr[k];
                *(u16 *)(sv - 2 * k) = w;
                w = (u16)((w >> 8) | (w << 8));
                w = (u16)((w & *(const u16 *)(m6 - 2 * k)) | *(const u16 *)(d2 - 2 * k));
                scr[k] = (u16)((w >> 8) | (w << 8));
            }
            Xpre = (u16)(X + 2 * ec); Y = y1; f0 -= ec; X += 0xA0;
            if (X >= 0x9C40 || f0 == 0) break;
        }
    }
    FINISH(A, X, Xpre, N, V, Z, C, ec, f0);
    B0[(u16)cpu.s] = (u8)(y1 >> 8); B0[(u16)(cpu.s - 1)] = (u8)y1;
    SET_REGS();
    return 1;
}

/* ---- 4A63: show the changed rectangles: each visible word of each rectangle is read and written back in bank 1 with shadowing on,
 * which makes the hardware copy it to bank E1 (the displayed picture). Here: copy the words to E1 and mark the rows as changed. ---- */
static int nat_push(void)
{
    u16 A = cpu.a, X = cpu.x, Xpre = 0, lastec = 0; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c;
    u16 cnt = G3(N_VCNT), idx, ahigh = (u16)(cpu.a >> 8);
    int rects = 0;
    if (cpu.d != 0 || cpu.db != 3 || (cnt & 1) || cnt > 0x30) return 0;
    for (idx = cnt; idx; ) {                       /* check every rectangle first: this routine either does everything or nothing */
        u16 ec, ee, x0, xo;
        idx -= 2;
        ec = G3(N_VW + idx); ee = G3(N_VH + idx); x0 = (u16)(G3(N_VX + idx) & 0xFFFE); xo = G3(N_VOFF + idx);
        if (ec == 0 || ec > 0x50 || ee == 0 || ee % ec || (u32)x0 + 4UL * ec > 0xFF00 || (u32)xo + 0xB5 + 2UL * ee + (160UL * (ee / ec)) > 0xFF00) return 0;
    }
    BE0[0xC035] = (u8)(BE0[0xC035] & 0xF7);          /* shadowing on */
    for (idx = cnt; idx; ) {
        u16 ec, ee, x0, xo, rows, r, klo, khi, k16, k156;
        idx -= 2;
        ec = G3(N_VW + idx); ee = G3(N_VH + idx); x0 = (u16)(G3(N_VX + idx) & 0xFFFE); xo = G3(N_VOFF + idx);
        S3(0x0E, x0); S3(0xEC, ec);
        X = xo; rows = ee / ec;
        /* which words of a row are rewritten: those at x = 24..343 (x0 + 4k), the words at x = 22 and x = 342 one byte off / two bytes before */
        khi = x0 >= 0x158 ? 0 : (u16)((0x158 - x0 + 3) >> 2); if (khi > ec) khi = ec;
        klo = x0 >= 0x18 ? 0 : (u16)((0x18 - x0 + 3) >> 2); if (klo > khi) klo = khi;
        k16 = (x0 <= 0x16 && ((0x16 - x0) & 3) == 0 && ((0x16 - x0) >> 2) < ec) ? (u16)((0x16 - x0) >> 2) : 0xFFFF;
        k156 = (x0 <= 0x156 && ((0x156 - x0) & 3) == 0 && ((0x156 - x0) >> 2) < ec && (u16)((0x156 - x0) >> 2) >= klo && (u16)((0x156 - x0) >> 2) < khi) ? (u16)((0x156 - x0) >> 2) : 0xFFFF;
        {   /* the span a row of this rectangle touches is the same in every row; rows follow each other in the picture */
            u16 lo0 = 0xFFFF, hi0 = 0, mg0 = 0, mg1 = 0, row0 = 0; int fastmark = 0;
#define SPAN_NOTE(a0, nw) do { u16 _a = (a0), _n = (nw); if (_n) { if (_a < lo0) lo0 = _a; if ((u16)(_a + 2 * _n - 2) > hi0) hi0 = (u16)(_a + 2 * _n - 2); } } while (0)
            if (k16 != 0xFFFF) SPAN_NOTE((u16)(2 * k16 + 0xB5), 1);
            if (k156 == 0xFFFF) SPAN_NOTE((u16)(2 * klo + 0xB4), khi - klo);
            else { SPAN_NOTE((u16)(2 * klo + 0xB4), k156 - klo); SPAN_NOTE((u16)(2 * k156 + 0xB3), 1); SPAN_NOTE((u16)(2 * (k156 + 1) + 0xB4), khi - k156 - 1); }
            if (lo0 != 0xFFFF) {
                u32 a = (u32)X + lo0, b = (u32)X + hi0 + 1;
                if (a >= 0x2000 && b < 0x9D00) {
                    u32 ra = E1ROW(a), rb = E1ROW(b);
                    if (ra == rb && ra + rows <= 200) { row0 = (u16)ra; mg0 = (u16)(((a - 0x2000) - ra * 160) >> 2); mg1 = (u16)(((b - 0x2000) - ra * 160) >> 2); fastmark = 1; }
                }
            }
            for (r = 0; r < rows; r++) {
                u16 lo = 0xFFFF, hi = 0;
#define PUSH_RUN(a0, nw) do { u16 _a = (a0), _n = (nw); \
                    if (_n) { u16 _nb = (u16)(2 * _n); \
                        if (_a >= 0x2000 && (u32)_a + _nb <= 0xA000) copy_bytes(BE1 + _a, B1 + _a, _nb); \
                        else { u16 _i; for (_i = 0; _i < _nb; _i++) { u16 _b = (u16)(_a + _i); if (_b >= 0x2000 && _b < 0xA000) BE1[_b] = B1[_b]; } } \
                        if (_a < lo) lo = _a; if ((u16)(_a + _nb - 2) > hi) hi = (u16)(_a + _nb - 2); } } while (0)
                if (k16 != 0xFFFF) PUSH_RUN((u16)(X + 2 * k16 + 0xB5), 1);
                if (k156 == 0xFFFF) PUSH_RUN((u16)(X + 2 * klo + 0xB4), khi - klo);
                else {
                    PUSH_RUN((u16)(X + 2 * klo + 0xB4), k156 - klo);
                    PUSH_RUN((u16)(X + 2 * k156 + 0xB3), 1);
                    PUSH_RUN((u16)(X + 2 * (k156 + 1) + 0xB4), khi - k156 - 1);
                }
                if (fastmark) mark_row_(row0 + r, mg0, mg1);
                else if (lo != 0xFFFF) MARKE1_RANGE(lo, (u16)(hi - lo + 2));
                Xpre = (u16)(X + 2 * ec); X += 0xA0;
            }
        }
        lastec = ec;
        S3(0xF0, 0); S3(N_SCR1, 0);
        rects++; ahigh = 0;
    }
    if (rects) {
        B0[(u16)cpu.s] = 0; B0[(u16)(cpu.s - 1)] = 0;                      /* the last PHY (rectangle 0) leaves its bytes in the stack page */
        A = Xpre; C = 0; ADC16(0xA0); C = 1; SBC16(lastec); SBC16(lastec);  /* TXA CLC ADC #$A0 SEC SBC EC SBC EC of the last row: the last C and V */
    }
    cpu.a = (u16)(0x3D | (ahigh << 8)); cpu.y = 0x00FE;                      /* the routine ends with SEP #$30: the index registers lose their high bytes */
    cpu.x = (rects ? X : cpu.x) & 0xFF;
    cpu.n = 0; cpu.z = 0; cpu.v = V; cpu.c = C;
    return 1;
}

/* ---- 2C83 (ramp) / 26B5 (high jump): draw the next 8x8 character (index in A) at the cursor and advance the cursor ---- */
static int nat_glyph(void)
{
    u16 entry_a = cpu.a, entry_x = cpu.x, entry_y = cpu.y;
    u16 A = cpu.a, glyph = (u16)(cpu.a & 0xFF), col = G3(N_GCOL), row = G3(N_GROW), gtmp = (u16)(col << 2), rx = (u16)(row << 4), start, r, s0 = cpu.s;
    u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c;
    if (cpu.d != 0 || cpu.db != 3 || rx >= 0x1000) return 0;
    A = LE16(B2 + 0x3E00 + rx); C = 0; ADC16(0x2000); ADC16(gtmp); start = A;
    if (start < 0x2000 || (u32)start + 8UL * 0xA0 > 0xA000) return 0;
    B0[s0] = (u8)(entry_y >> 8); B0[(u16)(s0 - 1)] = (u8)entry_y; B0[(u16)(s0 - 2)] = (u8)(entry_x >> 8); B0[(u16)(s0 - 3)] = (u8)entry_x;
    B0[(u16)(s0 - 4)] = (u8)(entry_a >> 8); B0[(u16)(s0 - 5)] = (u8)entry_a;
    BE0[0xC035] = (u8)(BE0[0xC035] | 8);                                  /* shadowing is switched on for the drawing and off again afterwards */
    S3(N_GTMP, gtmp);
    {
        u32 row0 = E1ROW(start), col = (start - 0x2000) - row0 * 160;
        int fast = (col & 3) == 0 && (u32)start + 8UL * 0xA0 <= 0x9D00;
        for (r = 0; r < 8; r++) {
            u16 a = (u16)(start + r * 0xA0);
            u32 w = *(const u32 *)(B2 + (u16)(glyph * 32 + r * 4));
            *(u32 *)(B1 + a) = w; *(u32 *)(BE1 + a) = w;
            if (fast) mark_row_(row0 + r, col >> 2, col >> 2); else MARKE1_RANGE(a, 4);
        }
    }
    B0[0xDA] = (u8)(start + 8 * 0xA0); B0[0xDB] = (u8)((start + 8 * 0xA0) >> 8); B0[0xDC] = 1;
    S3(N_SCR1, 0);
    A = col; C = 0; ADC16(1); S3(N_GCOL, A); CMP16(A, 0x28);
    if (C) {
        S3(N_GCOL, 0);
        A = row; C = 0; ADC16(1); S3(N_GROW, A); CMP16(A, 0x19);
        if (C) S3(N_GROW, 0);
    }
    cpu.a = entry_a; cpu.x = entry_x; cpu.y = entry_y;
    cpu.n = (u8)(entry_y >> 15); cpu.z = entry_y == 0; cpu.v = V; cpu.c = C;
    return 1;
}
#endif
