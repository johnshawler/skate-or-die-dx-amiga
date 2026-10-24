/* Runtime for code translated from 65816 by tools/t816: the memory of the IIGS (only the banks the game uses), the register file,
 * and the macros the generated C uses. Written to compile unchanged for the host (tests) and for the 68020 (Amiga).
 *
 * Conventions (see tools/t816/emit.py):
 *  - every translated routine is `u32 f_XXXX(void)`; registers live in the global `cpu` between calls and in locals inside a routine
 *  - the 65816 stack is real (bank 0); JSR pushes the return address, RTS pops it and returns it (+1) so inline data after a JSR works
 *  - memory is little-endian bytes; 16-bit accesses are done with byte swaps on big-endian hosts
 *  - flags are separate 0/1 bytes; M/X/D are known statically per instruction and not stored */
#ifndef M816_H
#define M816_H
#include <stdint.h>
#include <string.h>
#include "iigs_io.h"

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8; typedef int16_t s16;

typedef struct Cpu { u16 a, x, y, s, d; u8 db, n, v, z, c, i; } Cpu;
extern Cpu cpu;

/* the banks the overlays touch: 0-5, 7, E0, E1 (bank 6 holds sound samples and is only read by the sound driver) */
#ifdef T816_AMIGA
/* the banks are allocated at run time; the generated code indexes them like arrays */
extern u8 *P_B0, *P_B1, *P_B2, *P_B3, *P_B4, *P_B5, *P_B7, *P_BE0, *P_BE1, *P_BZ;
#define B0 P_B0
#define B1 P_B1
#define B2 P_B2
#define B3 P_B3
#define B4 P_B4
#define B5 P_B5
#define B7 P_B7
#define BE0 P_BE0
#define BE1 P_BE1
#define BZ P_BZ
#else
extern u8 B0[65536], B1[65536], B2[65536], B3[65536], B4[65536], B5[65536], B7[65536], BE0[65536], BE1[65536], BZ[65536];
#endif
extern u8 *BK[256];

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define LE16(p) __builtin_bswap16(*(const u16 *)(p))
#define ST16(p, v) (*(u16 *)(p) = __builtin_bswap16((u16)(v)))
#else
#define LE16(p) (*(const u16 *)(p))
#define ST16(p, v) (*(u16 *)(p) = (u16)(v))
#endif

/* ---- tracing and write log (debug builds compare with the oracle) ---- */
#ifdef T816_DEBUG
extern u32 wr_hash_lo, wr_hash_hi; extern uint64_t wr_n; extern uint64_t wr_hash;
void rt_log_write(u32 pc, u32 a, u8 v);
extern u32 cur_pc;
#define WLOG(a, v) rt_log_write(cur_pc, (a), (v))
void rt_trace(u32 pc, u16 a, u16 x, u16 y, u16 s, u16 d, u8 db, u8 p);
#define TRACE(pc, m, xx, dd) do { cur_pc = (pc); rt_trace((pc), A, X, Y, S, cpu.d, cpu.db, (u8)((N << 7) | (V << 6) | ((m) << 5) | ((xx) << 4) | ((dd) << 3) | (I << 2) | (Z << 1) | C)); } while (0)
#else
#define WLOG(a, v) ((void)0)
#define TRACE(pc, m, xx, dd) ((void)0)
#endif

/* soft-switch page reads/writes at fixed addresses (generated code uses these for $E0/$E1:C000-CFFF) */
#define IORD8(a, arr, off) (io_is_reg(a) ? io_rd8((a), (arr)[off]) : (arr)[off])
static inline void IOWR8(u32 a, u8 *arr, u16 off, u8 v) { WLOG(a, v); arr[off] = v; if (io_is_reg(a)) io_wr8(a, v); }

/* what changed on the displayed picture ($E1:2000, 160 bytes per row): for each row the first and last 8-pixel group (4 bytes) written since
   the platform last converted it; first > last means unchanged */
extern u8 rt_dmin[200], rt_dmax[200];
#define E1ROW(off) (((((u32)(off) - 0x2000) >> 5) * 205) >> 10)
static inline void mark_row_(u32 r, u32 lo, u32 hi) { if (lo < rt_dmin[r]) rt_dmin[r] = (u8)lo; if (hi > rt_dmax[r]) rt_dmax[r] = (u8)hi; }
extern u8 rt_rowof[1000]; extern u16 rt_rowbase[200];     /* row of picture offset (o - $2000) >> 5, and the first offset of each row (set up by rt_init) */
static inline void MARKE1_RANGE(u16 o, u16 n)             /* bytes [o, o+n) of bank E1 */
{
    u32 a = o, b = (u32)o + n - 1, ra, rb, ga, gb;
    if (a >= 0x2000 && b < 0x9D00) {                       /* the usual case: inside the picture and inside one row */
        u32 rel = a - 0x2000, r = rt_rowof[rel >> 5], col = rel - rt_rowbase[r];
        if (col + n <= 160) { mark_row_(r, col >> 2, (col + n - 1) >> 2); return; }
    }
    if (b < 0x2000 || a >= 0x9D00) return;
    if (a < 0x2000) a = 0x2000;
    if (b >= 0x9D00) b = 0x9CFF;
    ra = E1ROW(a); rb = E1ROW(b);
    ga = ((a - 0x2000) - ra * 160) >> 2; gb = ((b - 0x2000) - rb * 160) >> 2;
    if (ra == rb) mark_row_(ra, ga, gb);
    else { mark_row_(ra, ga, 39); while (++ra < rb) mark_row_(ra, 0, 39); mark_row_(rb, 0, gb); }
}
static inline void MARKE1(u16 off) { MARKE1_RANGE(off, 1); }

/* ---- memory ---- */
#ifdef T816_AUX      /* an overlay that switches bank 0 reads/writes to the auxiliary bank ($E0:C002-C005) */
static inline u8 RD8(u32 a) { u32 bk = (a >> 16) & 255; if (io.ramrd && bk == 0 && (u16)a >= 0x200 && (u16)a < 0xC000) bk = 1; return BK[bk][(u16)a]; }
#else
#define RD8(a) (BK[((u32)(a) >> 16) & 255][(u16)(a)])
#endif
/* (ecs/game copy of tools/t816/runtime/m816.h: RD16/WR16 take the direct way when the two bytes are in one bank that has no special rules; everything else as before) */
#define M816_INLINE static inline __attribute__((always_inline))
static __attribute__((noinline)) u16 RD16_slow(u32 a) { u8 l = RD8(a); u8 h = RD8(a + 1); return (u16)(l | (h << 8)); }
static inline u16 RD16(u32 a)
{
#ifdef T816_AUX
    if ((u16)a != 0xFFFF && ((a >> 16) & 255) != 0) return LE16(&BK[(a >> 16) & 255][(u16)a]);
#else
    if ((u16)a != 0xFFFF) return LE16(&BK[(a >> 16) & 255][(u16)a]);
#endif
    return RD16_slow(a);
}
static inline u32 RD24(u32 a) { u8 l = RD8(a); u8 m = RD8(a + 1); u8 h = RD8(a + 2); return (u32)l | ((u32)m << 8) | ((u32)h << 16); }
/* bank 1 $2000-$9FFF is shadowed into bank E1 unless bit 3 of the shadow register ($E0:C035) is set */
static inline void WR8(u32 a, u8 v)
{
    a &= 0xFFFFFF;
#ifdef T816_AUX
    if (io.ramwrt && (a >> 16) == 0 && (u16)a >= 0x200 && (u16)a < 0xC000) a |= 0x10000;
#endif
    WLOG(a, v);
    BK[a >> 16][(u16)a] = v;
    if ((a >> 16) == 1 && (u16)a >= 0x2000 && (u16)a < 0xA000 && !(BE0[0xC035] & 8)) { BE1[(u16)a] = v; MARKE1((u16)a); }
    else if ((a >> 16) == 0xE1) MARKE1((u16)a);
}
/* banks that take a plain store: 0 (not with the auxiliary-memory switches), 2, 3, 4, 5, 7 (bank 1 and E1 are shadowed, E0 holds the soft switches) */
#ifdef T816_AUX
#define M816_FASTBANKS 0xBCu
#else
#define M816_FASTBANKS 0xBDu
#endif
static __attribute__((noinline)) void WR16_slow(u32 a, u16 v)
{
    u32 bk = (a >> 16) & 255; u16 o = (u16)a;
#ifndef T816_AUX
    if (o != 0xFFFF && o != 0x1FFF && o != 0x9FFF && (bk == 1 || bk == 0xE1)) {        /* the shadowed picture: bank 1 (mirrored into E1 unless shadowing is off) and E1 itself */
        WLOG(a & 0xFFFFFF, (u8)v); WLOG((a & 0xFFFFFF) + 1, (u8)(v >> 8));
        ST16(&BK[bk][o], v);
        if (bk == 1) { if (o >= 0x2000 && o < 0xA000 && !(BE0[0xC035] & 8)) { ST16(&BE1[o], v); MARKE1_RANGE(o, 2); } }
        else MARKE1_RANGE(o, 2);
        return;
    }
#endif
    WR8(a, (u8)v); WR8(a + 1, (u8)(v >> 8));
}
#ifdef T816_AUX
static inline void WR16(u32 a, u16 v)
#else
M816_INLINE void WR16(u32 a, u16 v)
#endif
{
    u32 bk = (a >> 16) & 255;
    if ((u16)a != 0xFFFF && bk <= 7 && ((M816_FASTBANKS >> bk) & 1)) { WLOG(a & 0xFFFFFF, (u8)v); WLOG((a & 0xFFFFFF) + 1, (u8)(v >> 8)); ST16(&BK[bk][(u16)a], v); return; }
    WR16_slow(a, v);
}
static inline void WRB1(u16 o, u8 v)    /* static store into bank 1 */
{
    WLOG(0x010000 | o, v);
    B1[o] = v;
    if (o >= 0x2000 && o < 0xA000 && !(BE0[0xC035] & 8)) { BE1[o] = v; MARKE1(o); }
}
/* static stores into the other banks (the shadow rule only concerns bank 1) */
#define WRS8(arr, bank, o, v) do { WLOG(((u32)(bank) << 16) | (o), (v)); (arr)[o] = (v); } while (0)
#define WRS16(arr, bank, o, v) do { u16 _mw = (v); WLOG(((u32)(bank) << 16) | (o), (u8)_mw); WLOG(((u32)(bank) << 16) | ((o) + 1), (u8)(_mw >> 8)); ST16(&(arr)[o], _mw); } while (0)
#define WRE1_8(o, v) do { WRS8(BE1, 0xE1, o, v); MARKE1(o); } while (0)
#define WRE1_16(o, v) do { WRS16(BE1, 0xE1, o, v); MARKE1_RANGE(o, 2); } while (0)
#define RDS16(arr, o) LE16(&(arr)[o])
/* bank 0 $0200-$BFFF in an overlay that uses the auxiliary-memory switches */
#ifdef T816_AUX
#define RDB0_8(o) ((io.ramrd ? B1 : B0)[o])
#define RDB0_16(o) LE16(&(io.ramrd ? B1 : B0)[o])
#define WRB0_8(o, v) do { u8 _bv = (v); if (io.ramwrt) WRB1((o), _bv); else WRS8(B0, 0, (o), _bv); } while (0)
#define WRB0_16(o, v) do { u16 _bw = (v); WRB0_8((o), (u8)_bw); WRB0_8((o) + 1, (u8)(_bw >> 8)); } while (0)
#endif

/* ---- stack (bank 0) ---- */
#define PUSH8(v) do { u8 _mp = (v); WLOG(S, _mp); B0[S] = _mp; S--; } while (0)
#define PUSH16(v) do { u16 _mq = (v); PUSH8(_mq >> 8); PUSH8(_mq & 255); } while (0)
static inline u8 pull8_(u16 *S) { (*S)++; return B0[*S]; }
#define PULL8() pull8_(&S)
#define PULL16() ({ u16 _ml = PULL8(); u16 _mh = PULL8(); (u16)(_ml | (_mh << 8)); })

/* a JSL to code that is not translated: the original stack traffic (3 bytes pushed, popped by the callee's RTL) is only replayed in debug builds so the write log matches the oracle */
#ifdef T816_DEBUG
#define STUB_JSL(ret) do { PUSH8(0x03); PUSH16(ret); S += 3; } while (0)
#else
#define STUB_JSL(ret) ((void)0)
#endif

/* ---- arithmetic helpers: all take and return values through the local flag variables of the caller ---- */
#include "bcd.h"
#define SETNZ16(v) do { u32 _nz = (v); N = (_nz >> 15) & 1; Z = (_nz & 0xFFFF) == 0; } while (0)
#define SETNZ8(v) do { u32 _nz = (v); N = (_nz >> 7) & 1; Z = (_nz & 0xFF) == 0; } while (0)

#define ADC16(vv) do { u32 _m_a = A, _m_v = (vv), _m_r = _m_a + _m_v + C; V = ((~(_m_a ^ _m_v) & (_m_a ^ _m_r)) >> 15) & 1; C = _m_r > 0xFFFF; A = (u16)_m_r; SETNZ16(_m_r); } while (0)
#define ADC8(vv)  do { u32 _m_a = A & 255, _m_v = (vv) & 255, _m_r = _m_a + _m_v + C; V = ((~(_m_a ^ _m_v) & (_m_a ^ _m_r)) >> 7) & 1; C = _m_r > 0xFF; A = (u16)((A & 0xFF00) | (_m_r & 255)); SETNZ8(_m_r); } while (0)
#define SBC16(vv) ADC16(~(u32)(vv) & 0xFFFF)
#define SBC8(vv)  ADC8(~(u32)(vv) & 0xFF)
#define DADC16(vv) do { int _m_c, _m_v2; u32 _m_r = bcd_add(A, (vv) & 0xFFFF, C, 4, &_m_c, &_m_v2); C = _m_c; V = _m_v2; A = (u16)_m_r; SETNZ16(_m_r); } while (0)
#define DADC8(vv)  do { int _m_c, _m_v2; u32 _m_r = bcd_add(A & 255, (vv) & 255, C, 2, &_m_c, &_m_v2); C = _m_c; V = _m_v2; A = (u16)((A & 0xFF00) | _m_r); SETNZ8(_m_r); } while (0)
#define DSBC16(vv) do { int _m_c, _m_v2; u32 _m_r = bcd_sub(A, (vv) & 0xFFFF, C, 4, &_m_c, &_m_v2); C = _m_c; V = _m_v2; A = (u16)_m_r; SETNZ16(_m_r); } while (0)
#define DSBC8(vv)  do { int _m_c, _m_v2; u32 _m_r = bcd_sub(A & 255, (vv) & 255, C, 2, &_m_c, &_m_v2); C = _m_c; V = _m_v2; A = (u16)((A & 0xFF00) | _m_r); SETNZ8(_m_r); } while (0)
#define CMP16(r, vv) do { u32 _m_a = (r) & 0xFFFF, _m_v = (vv) & 0xFFFF; C = _m_a >= _m_v; SETNZ16(_m_a - _m_v); } while (0)
#define CMP8(r, vv)  do { u32 _m_a = (r) & 0xFF, _m_v = (vv) & 0xFF; C = _m_a >= _m_v; SETNZ8(_m_a - _m_v); } while (0)

/* shifts on a value; return the result and set C, N, Z */
#define ASL16(v) ({ u32 _m_x = (v) & 0xFFFF; C = _m_x >> 15; _m_x = (_m_x << 1) & 0xFFFF; SETNZ16(_m_x); _m_x; })
#define ASL8(v)  ({ u32 _m_x = (v) & 0xFF; C = _m_x >> 7; _m_x = (_m_x << 1) & 0xFF; SETNZ8(_m_x); _m_x; })
#define LSR16(v) ({ u32 _m_x = (v) & 0xFFFF; C = _m_x & 1; _m_x >>= 1; SETNZ16(_m_x); _m_x; })
#define LSR8(v)  ({ u32 _m_x = (v) & 0xFF; C = _m_x & 1; _m_x >>= 1; SETNZ8(_m_x); _m_x; })
#define ROL16(v) ({ u32 _m_x = (v) & 0xFFFF, _m_c = C; C = _m_x >> 15; _m_x = ((_m_x << 1) | _m_c) & 0xFFFF; SETNZ16(_m_x); _m_x; })
#define ROL8(v)  ({ u32 _m_x = (v) & 0xFF, _m_c = C; C = _m_x >> 7; _m_x = ((_m_x << 1) | _m_c) & 0xFF; SETNZ8(_m_x); _m_x; })
#define ROR16(v) ({ u32 _m_x = (v) & 0xFFFF, _m_c = C; C = _m_x & 1; _m_x = (_m_x >> 1) | (_m_c << 15); SETNZ16(_m_x); _m_x; })
#define ROR8(v)  ({ u32 _m_x = (v) & 0xFF, _m_c = C; C = _m_x & 1; _m_x = (_m_x >> 1) | (_m_c << 7); SETNZ8(_m_x); _m_x; })

/* register file sync around calls */
#define SAVE() do { cpu.a = A; cpu.x = X; cpu.y = Y; cpu.s = S; cpu.n = N; cpu.v = V; cpu.z = Z; cpu.c = C; cpu.i = I; } while (0)
#define LOAD() do { A = cpu.a; X = cpu.x; Y = cpu.y; S = cpu.s; N = cpu.n; V = cpu.v; Z = cpu.z; C = cpu.c; I = cpu.i; } while (0)

void rt_init(void);                     /* call after the banks are set up */
#ifdef T816_AMIGA
int rt_run(u32 (*dispatch)(u16), u16 entry);   /* run a translated routine of one overlay (its generated dispatch_call); returns rt_exit */
#else
int rt_run(u16 entry);                  /* run a translated routine; returns rt_exit */
#endif

/* hooks supplied by the platform / harness */
u32 rt_jsl(u32 target);                 /* a JSL/JML to code that is not translated (sound driver, INTER) */
u32 dispatch_call(u16 addr);            /* call a translated routine by address (generated) */
void rt_bad_return(u32 pc, u32 got, u32 want);
void rt_leave(u32 target);              /* the event hands control back (JML out of the overlay): unwinds to rt_run */
extern int rt_exit;                     /* set when the event hands control back to INTER */
int rt_pass(u32 pc);                    /* called at the top of the main loop; nonzero stops the run */

#endif
