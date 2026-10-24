/* Runtime state and debug hooks for translated code (host test build and Amiga build share this file). */
#include "m816.h"
#ifndef T816_AMIGA
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
static jmp_buf rt_jb; static int rt_jb_set;
#endif
u32 rt_err_pc, rt_err_got, rt_err_want;

/* globals are initialised to non-zero values so that they land in .data: the Amiga hunk executables built here have no .bss */
Cpu cpu = {1};
IIGSIO io = {1, 1};
#ifdef T816_AMIGA
u8 *P_B0 = (u8 *)1, *P_B1 = (u8 *)1, *P_B2 = (u8 *)1, *P_B3 = (u8 *)1, *P_B4 = (u8 *)1, *P_B5 = (u8 *)1, *P_B7 = (u8 *)1, *P_BE0 = (u8 *)1, *P_BE1 = (u8 *)1, *P_BZ = (u8 *)1;
#else
u8 B0[65536], B1[65536], B2[65536], B3[65536], B4[65536], B5[65536], B7[65536], BE0[65536], BE1[65536], BZ[65536];
#endif
u8 *BK[256] = {(u8 *)1};
int rt_exit = 1;
u8 rt_dmin[200] = {1}, rt_dmax[200] = {1};
u8 rt_rowof[1000] = {1}; u16 rt_rowbase[200] = {1};
#ifdef T816_B2DIRTY
u8 rt_b2min[416] = {255}, rt_b2max[416] = {0};
#endif

#ifdef T816_DEBUG
uint64_t wr_n, wr_hash; u32 cur_pc;
u32 *wr_buf; uint64_t wr_cap;
void rt_log_write(u32 pc, u32 a, u8 v)
{
    wr_hash = (wr_hash ^ ((uint64_t)a << 8 | v)) * 1099511628211ULL;
    if (wr_buf && wr_n < wr_cap) { wr_buf[2 * wr_n] = pc; wr_buf[2 * wr_n + 1] = a << 8 | v; }
    wr_n++;
}
/* instruction trace: 4 x u32 per record: pc, a|x<<16, y|s<<16, d | db<<16 | p<<24 */
u32 *tr_buf; uint64_t tr_cap, tr_n;
void rt_trace(u32 pc, u16 a, u16 x, u16 y, u16 s, u16 d, u8 db, u8 p)
{
    if (tr_buf && tr_n < tr_cap) {
        u32 *r = tr_buf + 4 * tr_n;
        r[0] = pc; r[1] = a | ((u32)x << 16); r[2] = y | ((u32)s << 16); r[3] = d | ((u32)db << 16) | ((u32)p << 24);
    }
    tr_n++;
}
void rt_set_trace(u32 *buf, uint64_t cap) { tr_buf = buf; tr_cap = cap; tr_n = 0; }
uint64_t rt_get_trace_n(void) { return tr_n; }
void rt_set_wlog(u32 *buf, uint64_t cap) { wr_buf = buf; wr_cap = cap; wr_n = 0; wr_hash = 1469598103934665603ULL; }
uint64_t rt_get_wr_n(void) { return wr_n; }
uint64_t rt_get_wr_hash(void) { return wr_hash; }
#endif

/* default for the host tests: after many polls without a key, press the space bar (both the oracle and this runtime do the same) */
#ifndef T816_AMIGA
static int poll_n;
void io_poll(void) { if (++poll_n >= 2000) { poll_n = 0; io.key = ' '; io.key_waiting = 1; io.anykey = 1; } }
#endif

void rt_init(void)
{
    int i;
    for (i = 0; i < 256; i++) BK[i] = BZ;
    BK[0] = B0; BK[1] = B1; BK[2] = B2; BK[3] = B3; BK[4] = B4; BK[5] = B5; BK[7] = B7; BK[0xE0] = BE0; BK[0xE1] = BE1;
    rt_exit = 0;
    for (i = 0; i < 200; i++) { rt_dmin[i] = 0; rt_dmax[i] = 39; rt_rowbase[i] = (u16)(i * 160); }
    for (i = 0; i < 1000; i++) rt_rowof[i] = (u8)(i / 5);
#ifdef T816_B2DIRTY
    for (i = 0; i < 416; i++) { rt_b2min[i] = 255; rt_b2max[i] = 0; }
#endif
}

#ifndef T816_AMIGA
u8 *rt_bank_ptr(int b) { return BK[b]; }
Cpu *rt_cpu(void) { return &cpu; }
IIGSIO *rt_io_ptr(void) { return &io; }
u8 *rt_dmin_ptr(void) { return rt_dmin; }
u8 *rt_dmax_ptr(void) { return rt_dmax; }
#endif

void rt_bad_return(u32 pc, u32 got, u32 want)
{
#ifndef T816_AMIGA
    fprintf(stderr, "t816: unexpected return/transfer at %06X: got %06X want %06X\n", pc, got, want);
#endif
    rt_exit = 2; rt_err_pc = pc; rt_err_got = got; rt_err_want = want;
#ifndef T816_AMIGA
    if (rt_jb_set) longjmp(rt_jb, 1);
#endif
}

/* calls into code that is not translated (sound driver in bank 7, INTER): the platform overrides this */
__attribute__((weak)) u32 rt_jsl(u32 target) { (void)target; return 0; }

void rt_leave(u32 target) { rt_exit = 1; rt_err_got = target; }

/* run a translated routine by address; returns rt_exit: 1 = the event left the overlay, 2 = unexpected transfer, 3 = stopped by the platform */
#ifdef T816_AMIGA
int rt_run(u32 (*dispatch)(u16), u16 entry)
{
    rt_exit = 0;
    dispatch(entry);
    return rt_exit;
}
#else
int rt_run(u16 entry)
{
    rt_exit = 0;
    rt_jb_set = 1;
    if (setjmp(rt_jb) == 0) { dispatch_call(entry); rt_jb_set = 0; return rt_exit; }
    rt_jb_set = 0; return 2;
}
#endif

#ifndef T816_AMIGA
int (*rt_pass_cb)(u32 pc);
int rt_pass(u32 pc) { return rt_pass_cb ? rt_pass_cb(pc) : 0; }
void rt_set_pass_cb(int (*cb)(u32)) { rt_pass_cb = cb; }
#endif
