/* Helpers shared by the native routines (engine.h: ramp and high jump, dh_engine.h: the downhill events). */
#ifndef N_COMMON_H
#define N_COMMON_H
#include "m816.h"

#define DPW(o) LE16(B0 + ((u16)(cpu.d + (o))))
#define S3(o, v) ST16(B3 + (o), (v))
#define G3(o) LE16(B3 + (o))
#define RAW16(p) (*(const u16 *)(p))
#define SHADOW_ON() (!(BE0[0xC035] & 8))

/* copy n bytes (a multiple of 2); the 68020 and the host both allow unaligned word and long accesses */
static inline void copy_bytes(u8 *d, const u8 *s, u16 n)
{
    while (n >= 4) { *(u32 *)d = *(const u32 *)s; d += 4; s += 4; n -= 4; }
    if (n) *(u16 *)d = *(const u16 *)s;
}

/* copy n bytes (a multiple of 32) to two places at once */
static inline void copy2_bytes(u8 *d1, u8 *d2, const u8 *s, u16 n)
{
#ifdef __mc68000__
    const u8 *e = s + n;
    __asm__ volatile(
        "1: movem.l (%0)+,%%d0-%%d7\n\t"
        "movem.l %%d0-%%d7,(%1)\n\tlea 32(%1),%1\n\t"
        "movem.l %%d0-%%d7,(%2)\n\tlea 32(%2),%2\n\t"
        "cmp.l %0,%3\n\tbne 1b"
        : "+a"(s), "+a"(d1), "+a"(d2) : "a"(e) : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "memory", "cc");
#else
    copy_bytes(d1, s, n); copy_bytes(d2, s, n);
#endif
}


/* ---- the joystick routine of every event that reads the paddles (ramp $44FC, high jump $3EE7, jam $532C, joust $475B) ----
 * It waits for the paddle timers to run out, triggers them and counts reads of $C064/$C065 in windows of 40 until each timer has gone low twice (the 65816 code
 * is the same in all four; only the two variables and, in the jam, a PHX/PHY at the start differ). Here the same loops run on the timers directly. The result byte is
 * in N_JY_RES ($1F: no joystick; bits 0/1: up/down, 2/3: left/right, 4: button, all active low). N_JY_CNT is the window counter. */
#ifdef N_JY_RES
static int nat_joy(void)
{
    u16 ex = cpu.x, ey = cpu.y, ea = cpu.a, s0 = cpu.s, t0 = io.pdl_left[0], t1 = io.pdl_left[1], cnt, k;
    u8 x8 = 0xFF, y8 = 0xFF, res, c = 1, a8;
    if (cpu.db != 3 || t0 > 1000 || t1 > 1000) return 0;
    io.pdl_left[0] = io.pdl_left[1] = 0;
    for (k = 0; k < 4; k++) io.pdl_left[k] = io.pdl_len[k];
    t0 = io.pdl_len[0]; t1 = io.pdl_len[1];
    for (;;) {                                           /* 3F0E: a window of 40 reads */
        cnt = 0x28;
        for (;;) {
            c = x8 >= 1; if (x8 == 1) goto done;
            c = y8 >= 1; if (y8 == 1) goto done;
            { u8 hi = 0; if (t0) { t0--; hi = 1; } if (t1) { t1--; hi = 1; } if (!hi) goto done; }
            if (--cnt) continue;
            break;
        }
        if (t0) { t0--; x8++; }                         /* the timers are read once more: still high counts */
        if (t1) { t1--; y8++; }
    }
done:
    io.pdl_left[0] = t0; io.pdl_left[1] = t1;
    res = 0xFF;
    if (y8) res &= (y8 < 0x80) ? 0xFD : 0xFE;
    if (x8) res &= (x8 < 0x80) ? 0xF7 : 0xFB;
    if (io.btn[0] & 0x80) res &= 0xEF;
    B3[N_JY_RES] = res; B3[N_JY_CNT] = (u8)cnt;
    a8 = res;
#ifdef N_JY_SAVE                                        /* jam: PHX PHY ... PLY PLX */
    B0[s0] = (u8)(ex >> 8); B0[(u16)(s0 - 1)] = (u8)ex; B0[(u16)(s0 - 2)] = (u8)(ey >> 8); B0[(u16)(s0 - 3)] = (u8)ey;
    cpu.x = ex; cpu.y = ey; cpu.n = (u8)(ex >> 15); cpu.z = ex == 0;
#else
    cpu.x = x8; cpu.y = y8; cpu.n = a8 >> 7; cpu.z = a8 == 0;
#endif
    cpu.a = (u16)((ea & 0xFF00) | a8); cpu.c = c; cpu.i = 0;
    return 1;
}
#endif

/* ---- the palette fades (every overlay has two): between the steps the 65816 code counts X up to $6000 sixteen-plus times, about 79 ms a step on the 2.8 MHz IIGS
 * (9 cycles an iteration). Running that loop as code takes the Amiga about twice as long and shows nothing, because the palette is only sent to the hardware once
 * a pass. Here: show the palette as it is now and wait the same 79 ms; the loop's end state is X = $6000 and the flags of CPX (N 0, Z 1, C 1). ---- */
#ifdef T816_AMIGA
void rt_fade_wait(void);
#else
static inline void rt_fade_wait(void) {}
#endif
static int nat_fade_delay(void)
{
    rt_fade_wait();
    cpu.x = 0x6000; cpu.n = 0; cpu.z = 1; cpu.c = 1;
    return 1;
}
#endif
