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

#endif
