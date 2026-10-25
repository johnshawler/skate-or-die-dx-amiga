/* Native versions of hot routines of the two downhill overlays (F15 race, F21 jam). See engine.h for the conventions.
 * The including file defines the overlay's variable addresses: N_BGSRC (bank 3: offset into bank 2 of the scrolling picture) ... */
#ifndef N_DH_ENGINE_H
#define N_DH_ENGINE_H
#include "m816.h"
#include "common.h"

/* f_3674 / f_3B03: copy the scrolled background. The routine switches writes of bank 0 to the auxiliary bank (bank 1), sets the direct
 * page register to each destination page and stores every word with a direct-page store, in 21 passes of 8 rows of 160 bytes; the net
 * effect (checked on the instruction sequence) is one contiguous copy of $6900 bytes from bank 2 at the offset in N_BGSRC to bank 1 at
 * $3400-$9CFF, with shadowing on, so the hardware mirrors it into bank E1. */
#ifdef N_BG_SCROLL
/* The downhill events (c2p.h, C2P_SCROLL; T816_B2DIRTY): the picture that the original copies from bank 2 into bank 1 / E1 every pass is never read back, and everything that is drawn
 * into it is drawn into bank 2 before the copy (checked on the write log). So the copy is not made: the platform converts the window straight from bank 2 (rt_win_src), and the
 * groups that changed are the ones written into bank 2 since the last fill (rt_b2min/rt_b2max, kept by the stores, the blits and the restore), at the row the window now has.
 * When the window moves by whole rows (rt_scroll) the platform shifts its planar picture; anything else (the first fill, a jump of the offset, an offset that is not a whole row)
 * marks the whole window. */
#ifndef T816_B2DIRTY
#error "N_BG_SCROLL needs T816_B2DIRTY"
#endif
extern u32 rt_bg_prev;
extern const u8 *rt_win_src;
extern void rt_scroll(int dy);
#define BG_ROWS 168
static void bg_window(u16 y0)
{
    u32 prev = rt_bg_prev;
    int r;
    if (prev != 0xFFFFFFFFUL && y0 >= prev && (u32)(y0 - prev) <= 8UL * 160 && (y0 - prev) % 160 == 0 && y0 % 160 == 0) {
        int dy = (int)((y0 - prev) / 160), row0 = y0 / 160;
        if (dy) rt_scroll(dy);
        for (r = 0; r < BG_ROWS; r++) {
            u32 lo = rt_b2min[row0 + r], hi = rt_b2max[row0 + r];
            if (lo <= hi) mark_row_(32 + r, lo, hi);
        }
    } else {
        for (r = 0; r < BG_ROWS; r++) mark_row_(32 + r, 0, 39);
    }
    for (r = 0; r < 416; r++) { rt_b2min[r] = 255; rt_b2max[r] = 0; }
    rt_win_src = B2 + y0;
}
#endif

static int nat_bgfill(void)
{
    u16 dold = cpu.d, y0 = G3(N_BGSRC), s0 = cpu.s, A, Y;
    u8 dbold = cpu.db;
#ifdef N_BG_SCROLL
    if (io.ramrd || io.ramwrt || (u32)y0 + 0x6900UL > 0xFFF0) { rt_bg_prev = 0xFFFFFFFFUL; return 0; }
#else
    if (io.ramrd || io.ramwrt || (u32)y0 + 0x6900UL > 0xFFF0) return 0;
#endif
    BE0[0xC035] = (u8)(BE0[0xC035] & 0xF7);
#ifdef N_BG_SCROLL
    bg_window(y0);
    rt_bg_prev = y0;
#else
    copy2_bytes(B1 + 0x3400, BE1 + 0x3400, B2 + y0, 0x6900);
    MARKE1_RANGE(0x3400, 0x6900);
#endif
    BE0[0xC002] = BE0[0xC003] = BE0[0xC004] = BE0[0xC005] = 0xFF;      /* the writes that switch the auxiliary bank on and off */
    io.ramrd = io.ramwrt = 0;
    B0[s0] = dbold; B0[(u16)(s0 - 1)] = (u8)(dold >> 8); B0[(u16)(s0 - 2)] = (u8)dold;      /* PHB and PHD leave their bytes in the stack page */
    A = 0x9DFF; Y = (u16)((y0 + 0x6900) & 0xFF);
    cpu.a = A; cpu.x = (u16)(cpu.x & 0xFF); cpu.y = Y;
    cpu.n = (u8)(dbold >> 7); cpu.z = dbold == 0; cpu.v = 0; cpu.c = 1;
    return 1;
}

/* ---- the sprite loops of the two overlays ----
 * Each of the four loops below is a hook in the middle of a translated function (cfg native.mid: label -> native, label to continue at).
 * It runs a sprite's whole rectangle (rows of N_PW words, $20 = the number of words in all) at once, or returns 0 and the translated
 * loop runs. The state a loop leaves is: X, Y, A, N, Z, C, V as after its last row (the V and C of the row-end SBCs are produced by the
 * same macros), and the direct-page counters $18 and $20 at 0. The sprite pictures and masks are read through direct-page long pointers
 * at $06 and $0A; they must lie in plain banks 4-7. Writes go to bank 2 (the off-screen picture) and, for the blit, to the save
 * buffer in bank 3 at ($46)+Y, which must not touch the variables the loop reads on the way. */
#define DPL(o) ((u32)B0[(u16)(cpu.d + (o))] | ((u32)B0[(u16)(cpu.d + (o) + 1)] << 8) | ((u32)B0[(u16)(cpu.d + (o) + 2)] << 16))
#define DH_SET_REGS() do { cpu.a = A; cpu.x = X; cpu.y = Y; cpu.n = N; cpu.v = V; cpu.z = Z; cpu.c = C; } while (0)

static inline int dh_src(u32 p24, u16 y, u32 nbytes, const u8 **out)
{
    u32 b = p24 >> 16, off = (u32)(u16)p24 + y;
    if (b < 4 || b > 7 || BK[b] == BZ || off + nbytes > 0xFFF0) return 0;
    *out = BK[b] + off;
    return 1;
}

static inline int dh_apart(u32 a, u32 an, u32 b, u32 bn) { return a + an <= b || b + bn <= a; }

/* the row end of the blits and the restore: TXA CLC ADC #$A0 SEC SBC w SBC w TAX, then LDA $20 */
#define DH_FINISH(rows, w, T) do { \
    X = (u16)(X + 160 * ((rows) - 1) + 2 * (w)); Y = (u16)(Y + 2 * (T)); \
    A = X; C = 0; ADC16(0xA0); C = 1; SBC16(w); SBC16(w); X = A; \
    ST16(B0 + (u16)(cpu.d + 0x20), 0); ST16(B0 + (u16)(cpu.d + 0x18), 0); \
    A = 0; N = 0; Z = 1; DH_SET_REGS(); } while (0)

#define DH_ENTRY() \
    u16 A = cpu.a, X = cpu.x, Y = cpu.y, w = G3(N_PW), T = DPW(0x20), rows; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c; \
    if (cpu.d != 0 || w == 0 || w > 0x50 || T == 0 || T % w) return 0; \
    rows = (u16)(T / w); \
    if ((u32)X + 160UL * (rows - 1) + 2UL * w > 0xFA00) return 0

/* race $4581 / jam $4A04 variants differ in the pixel formula only */
static int nat_dh15_blit(void)
{
    DH_ENTRY();
    {
        u16 sv = DPW(0x46), mk = RAW16(B3 + N_FILL), r, k;
        const u8 *pm, *pd;
        u16 *sd;
        if ((u32)sv + Y + 2UL * T > 0xFFF0 || !dh_apart(sv + Y, 2UL * T, N_PW, 2) || !dh_apart(sv + Y, 2UL * T, N_FILL, 2)) return 0;
        if (!dh_src(DPL(0x0A), Y, 2UL * T, &pm) || !dh_src(DPL(0x06), Y, 2UL * T, &pd)) return 0;
        sd = (u16 *)(B3 + (u16)(sv + Y));
        for (r = 0; r < rows; r++) {
            u16 *scr = (u16 *)(B2 + (u16)(X + 160 * r));
            const u16 *m = (const u16 *)pm, *d = (const u16 *)pd;
            for (k = 0; k < w; k++) { u16 o = scr[k]; sd[k] = o; scr[k] = (u16)(((o & m[k]) | d[k]) & mk); }
            sd += w; pm += 2 * w; pd += 2 * w;
        }
#ifdef T816_B2DIRTY
        { u32 q; for (q = 0; q < rows; q++) B2M((u16)(X + 160 * q), 2 * w); }
#endif
    }
    DH_FINISH(rows, w, T);
    return 1;
}

static int nat_dh21_blit(void)
{
    DH_ENTRY();
    {
        u16 sv = DPW(0x46), sm = RAW16(B3 + N_FILL), r, k, t = 0;
        const u8 *pm, *pd;
        u16 *sd;
        if ((u32)sv + Y + 2UL * T > 0xFFF0 || !dh_apart(sv + Y, 2UL * T, N_PW, 2) || !dh_apart(sv + Y, 2UL * T, N_FILL, 2)) return 0;
        if (!dh_src(DPL(0x0A), Y, 2UL * T, &pm) || !dh_src(DPL(0x06), Y, 2UL * T, &pd)) return 0;
        sd = (u16 *)(B3 + (u16)(sv + Y));
        for (r = 0; r < rows; r++) {
            u16 *scr = (u16 *)(B2 + (u16)(X + 160 * r));
            const u16 *m = (const u16 *)pm, *d = (const u16 *)pd;
            for (k = 0; k < w; k++) { u16 o = scr[k], mm = m[k]; t = (u16)((u16)(~mm) & sm & d[k]); sd[k] = o; scr[k] = (u16)((o & mm) | t); }
            sd += w; pm += 2 * w; pd += 2 * w;
        }
#ifdef T816_B2DIRTY
        { u32 q; for (q = 0; q < rows; q++) B2M((u16)(X + 160 * q), 2 * w); }
#endif
        *(u16 *)(B0 + (u16)(cpu.d + 0x1E)) = t;                  /* the last mask word stays in $1E */
    }
    DH_FINISH(rows, w, T);
    return 1;
}

/* race $4644 / jam $4AD5: put the saved background back */
static int nat_dh_restore(void)
{
    DH_ENTRY();
    {
        u16 sv = DPW(0x46);
        if ((u32)sv + Y + 2UL * T > 0xFFF0) return 0;
        {
            const u16 *sd = (const u16 *)(B3 + (u16)(sv + Y));
            u16 r, k;
            for (r = 0; r < rows; r++) {
                u16 *scr = (u16 *)(B2 + (u16)(X + 160 * r));
                for (k = 0; k < w; k++) scr[k] = sd[k];
                sd += w;
            }
#ifdef T816_B2DIRTY
            for (r = 0; r < rows; r++) B2M((u16)(X + 160 * r), 2 * w);
#endif
        }
    }
    DH_FINISH(rows, w, T);
    return 1;
}


#ifdef N_STEP_K
/* $25B9 (jam): INC $6464,X (the skater's position down the course, X = 0 or 2 for the two skaters), LDA #0, STA $60FA,X, RTS. The position moves N_STEP_K rows instead of one:
 * the skaters run N_STEP_K times as fast along the course at the same number of passes. Called from the speed accumulator ($2577) and from the two boost steps ($2AB0, $2ABF). */
static int nat_step_pos(void)
{
    u16 x = cpu.x;
    if ((u32)0x6464 + x > 0xFFFD || (u32)0x60FA + x > 0xFFFD) return 0;
    ST16(B3 + 0x6464 + x, (u16)(LE16(B3 + 0x6464 + x) + N_STEP_K));
    ST16(B3 + 0x60FA + x, 0);
    cpu.a = 0; cpu.n = 0; cpu.z = 1;
    return 1;
}
#endif

/* ---- 383F (jam): draw the 8x8 character in A at the text cursor (columns at $6470, rows at $6472 of bank 3) straight into bank E1 and move the cursor on ---- */
#ifdef N_JGLYPH
#ifndef N_JG_COL
#define N_JG_COL 0x6470        /* the text cursor: column and row words in bank 3 */
#define N_JG_ROW 0x6472
#define N_JG_RTB B4            /* the table of screen offsets of character rows (a word per 16 bytes of row number): bank and offset */
#define N_JG_RTO 0xDE00
#define N_JG_FB B4             /* the font: 32 bytes a character */
#define N_JG_FO 0xB600
#define N_JG_MASK 0x61E4       /* the word the font is ANDed with (bank 3) */
#endif
static int nat_glyph_jam(void)
{
    u16 ea = cpu.a, ex = cpu.x, ey = cpu.y, s0 = cpu.s, col = G3(N_JG_COL), row = G3(N_JG_ROW), t10, A, dest, fx, r, mask; u8 V = 0, C = 0, N = 0, Z = 0;
    if (cpu.db != 3) return 0;
    t10 = (u16)(col << 2); fx = (u16)(ea << 5);
    if ((u32)fx + N_JG_FO + 32 > 0x10000UL || (u32)((u16)(row << 4)) + N_JG_RTO + 2 > 0x10000UL) return 0;
    A = LE16(N_JG_RTB + N_JG_RTO + (u16)(row << 4)); C = 0; ADC16(0x2000); ADC16(t10); dest = A;
    if ((u32)dest + 8UL * 0xA0 > 0xFFF0 || dest < 0x2000 || (u32)dest + 8UL * 0xA0 > 0x9D00) return 0;     /* inside the picture: the usual case */
    mask = RAW16(B3 + N_JG_MASK);
    {
        const u8 *f = N_JG_FB + N_JG_FO + fx; u32 row0 = E1ROW(dest), cg = (dest - 0x2000) - row0 * 160;
        int fast = (cg & 3) == 0 && cg < 160 - 3;
        for (r = 0; r < 8; r++) {
            u16 a = (u16)(dest + r * 0xA0);
            *(u16 *)(BE1 + a) = (u16)(RAW16(f + 4 * r) & mask); *(u16 *)(BE1 + a + 2) = (u16)(RAW16(f + 4 * r + 2) & mask);
            if (fast) mark_row_(row0 + r, cg >> 2, cg >> 2); else MARKE1_RANGE(a, 4);
        }
    }
    S3(0x18, 0);
    B0[2] = (u8)(dest + 8 * 0xA0); B0[3] = (u8)((u16)(dest + 8 * 0xA0) >> 8); B0[4] = 0xE1; B0[0x10] = (u8)t10; B0[0x11] = (u8)(t10 >> 8);
    /* PHY PHX PHA */
    B0[s0] = (u8)(ey >> 8); B0[(u16)(s0 - 1)] = (u8)ey; B0[(u16)(s0 - 2)] = (u8)(ex >> 8); B0[(u16)(s0 - 3)] = (u8)ex; B0[(u16)(s0 - 4)] = (u8)(ea >> 8); B0[(u16)(s0 - 5)] = (u8)ea;
    A = col; C = 0; ADC16(1); S3(N_JG_COL, A); CMP16(A, 0x28);
    if (!C) goto done;
    S3(N_JG_COL, 0);
    A = row; C = 0; ADC16(1); S3(N_JG_ROW, A); CMP16(A, 0x28);
    if (C) S3(N_JG_ROW, 0);
done:
    cpu.a = ea; cpu.x = ex; cpu.y = ey; cpu.n = (u8)(ey >> 15); cpu.z = ey == 0; cpu.v = V; cpu.c = C;
    (void)N; (void)Z;
    return 1;
}
#endif
#endif
