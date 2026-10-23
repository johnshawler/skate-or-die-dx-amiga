/* Bare-metal driver to measure how many 68020 instructions one pass of translated game code takes (run in Unicorn). */
#include "m816.h"
static int parity;
u32 dispatch_call(u16 addr);
int rt_pass(u32 pc) { (void)pc; return 0; }        /* Python hooks this address; the picture conversion is part of the pass */
void io_poll(void) { static int n = 1; if (++n >= 2000) { n = 1; io.key = ' '; io.key_waiting = 1; io.anykey = 1; } }
void ev_start(void)
{
    P_B0 = (u8 *)0x1000000; P_B1 = (u8 *)0x1010000; P_B2 = (u8 *)0x1020000; P_B3 = (u8 *)0x1030000; P_B4 = (u8 *)0x1040000; P_B5 = (u8 *)0x1050000;
    P_B7 = (u8 *)0x1060000; P_BE0 = (u8 *)0x1070000; P_BE1 = (u8 *)0x1080000; P_BZ = (u8 *)0x1090000;
    rt_init();
    cpu.s = 0x1FF; cpu.a = cpu.x = cpu.y = 0; cpu.d = 0; cpu.db = 0;
    io.enabled = 1; io.key_waiting = 0; io.key = 0;
    rt_run(dispatch_call, 0x1000);
}

/* the 68000 has no 32-bit multiply/divide: minimal replacements for libgcc's (used by init and rare paths only) */
unsigned int __udivmodsi4(unsigned int n, unsigned int d, unsigned int *r)
{
    unsigned int q = 0, rem = 0; int i;
    if (d == 0) { *r = 0; return 0; }
    for (i = 31; i >= 0; i--) { rem = (rem << 1) | ((n >> i) & 1); if (rem >= d) { rem -= d; q |= 1u << i; } }
    *r = rem; return q;
}
unsigned int __udivsi3(unsigned int n, unsigned int d) { unsigned int r; return __udivmodsi4(n, d, &r); }
unsigned int __umodsi3(unsigned int n, unsigned int d) { unsigned int r; __udivmodsi4(n, d, &r); return r; }
int __divsi3(int n, int d) { int neg = (n < 0) ^ (d < 0); unsigned int r, q = __udivmodsi4(n < 0 ? -n : n, d < 0 ? -d : d, &r); return neg ? -(int)q : (int)q; }
int __modsi3(int n, int d) { unsigned int r; __udivmodsi4(n < 0 ? -n : n, d < 0 ? -d : d, &r); return n < 0 ? -(int)r : (int)r; }
int __mulsi3(int a, int b) { unsigned int x = a, y = b, p = 0; while (y) { if (y & 1) p += x; x <<= 1; y >>= 1; } return (int)p; }
