/* Bare-metal driver to measure how many 68020 instructions one pass of translated game code takes (run in Unicorn). */
#include "m816.h"
#include "c2p.h"
static int parity;
u32 dispatch_call(u16 addr);
int rt_pass(u32 pc) { (void)pc; c2p_collect(); c2p_convert((u32 *)0x2000000, BE1 + 0x2000, (u8 *)0x2050000, parity); parity ^= 1; return 0; }        /* Python hooks this address; the picture conversion is part of the pass */
void io_poll(void) { static int n = 1; if (++n >= 2000) { n = 1; io.key = ' '; io.key_waiting = 1; io.anykey = 1; } }
void ev_start(void)
{
    P_B0 = (u8 *)0x1000000; P_B1 = (u8 *)0x1010000; P_B2 = (u8 *)0x1020000; P_B3 = (u8 *)0x1030000; P_B4 = (u8 *)0x1040000; P_B5 = (u8 *)0x1050000;
    P_B7 = (u8 *)0x1060000; P_BE0 = (u8 *)0x1070000; P_BE1 = (u8 *)0x1080000; P_BZ = (u8 *)0x1090000;
    rt_init();
    c2p_build_lut((u32 *)0x2000000); c2p_all_dirty();
    cpu.s = 0x1FF; cpu.a = cpu.x = cpu.y = 0; cpu.d = 0; cpu.db = 0;
    io.enabled = 1; io.key_waiting = 0; io.key = 0;
    rt_run(dispatch_call, 0x1000);
}
