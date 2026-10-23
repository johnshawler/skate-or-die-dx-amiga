/* Minimal WDC 65C816 core (native mode, binary arithmetic) for running the Skate or Die IIGS overlays headless.
 * Flat 16 MB memory. No interrupts, no decimal mode. Execution stops at addresses registered with hook_set().
 * Cycle count is an approximation: 1 per bus access (3 for the slow banks $E0/$E1) plus 1 internal per instruction. */
#include <stdint.h>
#include <string.h>
#include "optab.h"
#include "../t816/runtime/bcd.h"
#include "../t816/runtime/iigs_io.h"
IIGSIO io;
static int poll_n;
void io_poll(void) { if (++poll_n >= 2000) { poll_n = 0; io.key = ' '; io.key_waiting = 1; io.anykey = 1; } }

typedef struct { uint16_t a, x, y, s, d, pc; uint8_t db, pb, p; } Regs;
enum { FC = 1, FZ = 2, FI = 4, FD = 8, FX = 0x10, FM = 0x20, FV = 0x40, FN = 0x80 };

static uint8_t mem[1 << 24];
static uint8_t hooks[1 << 24 >> 3];
Regs cpu;
uint64_t icount, cycles;
uint32_t hit_pc;
int fault;
uint32_t fault_pc;

/* ---- optional profiling (used to study the event overlays): per-address execution counts for banks 0-7, unique code-write,
   I/O and indirect-branch records ---- */
int prof_on;
static uint32_t pc_cnt[8 << 16];
uint32_t code_lo, code_hi;                          /* writes into [code_lo, code_hi) are recorded */
#define NREC 65536
typedef struct { uint64_t key; uint64_t cnt; } Rec;
static Rec recs[3][NREC];                            /* 0 code writes, 1 i/o, 2 indirect branches: open-addressing sets */
static int nrec[3];
static void rec(int k, uint64_t key) {
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ULL) >> 48) & (NREC - 1);
    while (recs[k][h].cnt && recs[k][h].key != key) h = (h + 1) & (NREC - 1);
    if (!recs[k][h].cnt) { if (nrec[k] > NREC * 3 / 4) return; nrec[k]++; recs[k][h].key = key; }
    recs[k][h].cnt++;
}
void prof_enable(int on) { prof_on = on; }
uint32_t *prof_pc(void) { return pc_cnt; }
int prof_dump(int k, uint64_t *out, int max) { int n = 0; for (int i = 0; i < NREC && n < max; i++) if (recs[k][i].cnt) { out[2 * n] = recs[k][i].key; out[2 * n + 1] = recs[k][i].cnt; n++; } return n; }
void prof_reset(void) { memset(pc_cnt, 0, sizeof pc_cnt); memset(recs, 0, sizeof recs); memset(nrec, 0, sizeof nrec); }
uint32_t cur_pc24;
uint32_t *tr_buf; uint64_t tr_cap, tr_n; int tr_on;
void trace_start(uint32_t *b, uint64_t cap) { tr_buf = b; tr_cap = cap; tr_n = 0; tr_on = 1; }
void trace_stop(void) { tr_on = 0; }
uint64_t get_trace_n(void) { return tr_n; }
int wlog_on; uint64_t wlog_hash, wlog_n; uint32_t *wlog_buf; uint64_t wlog_cap;
uint64_t get_wlog_hash(void) { return wlog_hash; } uint64_t get_wlog_n(void) { return wlog_n; }
void wlog_start(uint32_t *buf, uint64_t cap) { wlog_buf = buf; wlog_cap = cap; wlog_n = 0; wlog_hash = 1469598103934665603ULL; wlog_on = 1; }
void wlog_stop(void) { wlog_on = 0; }
uint32_t bank_rd[256], bank_wr[256];
uint32_t *prof_banks(int w) { return w ? bank_wr : bank_rd; }

IIGSIO *io_ptr(void) { return &io; }
uint8_t *mem_ptr(void) { return mem; }
Regs *regs_ptr(void) { return &cpu; }
void hook_set(uint32_t a, int on) { if (on) hooks[a >> 3] |= 1 << (a & 7); else hooks[a >> 3] &= ~(1 << (a & 7)); }
uint64_t get_icount(void) { return icount; }
uint64_t get_cycles(void) { return cycles; }
uint32_t get_hit_pc(void) { return hit_pc; }
int get_fault(void) { return fault; }
uint32_t get_fault_pc(void) { return fault_pc; }

static inline int slow(uint32_t a) { uint8_t b = a >> 16; return b == 0xE0 || b == 0xE1; }
static inline uint8_t rd8(uint32_t a) { a &= 0xFFFFFF; if (io.ramrd && (a >> 16) == 0 && (a & 0xFFFF) >= 0x200 && (a & 0xFFFF) < 0xC000) a |= 0x10000; cycles += slow(a) ? 3 : 1; if (prof_on) bank_rd[a >> 16]++; if (io_is_reg(a)) { uint8_t bv = io_rd8(a, mem[a]); if (prof_on) rec(1, ((uint64_t)cur_pc24 << 32) | (a & 0xFFFFFF)); return bv; } return mem[a]; }
/* IIGS shadowing: writes to bank 01 $2000-$9FFF are mirrored into bank E1 (the displayed SHR page) unless the
 * game has set the inhibit bit (bit 3) of the shadow register at $E0C035 while it draws into the off-screen copy. */
static inline void wr8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF; if (io.ramwrt && (a >> 16) == 0 && (a & 0xFFFF) >= 0x200 && (a & 0xFFFF) < 0xC000) a |= 0x10000;
    cycles += slow(a) ? 3 : 1; mem[a] = v;
    if (io_is_reg(a)) io_wr8(a, v);
    if (wlog_on) { wlog_hash = (wlog_hash ^ ((uint64_t)a << 8 | v)) * 1099511628211ULL; if (wlog_buf && wlog_n < wlog_cap) { wlog_buf[2 * wlog_n] = cur_pc24; wlog_buf[2 * wlog_n + 1] = a << 8 | v; } wlog_n++; }
    if (prof_on) { bank_wr[a >> 16]++; if ((a & 0xFEF000) == 0xE0C000) rec(1, ((uint64_t)cur_pc24 << 32) | (a & 0xFFFFFF) | (1ULL << 31)); if (a >= code_lo && a < code_hi) rec(0, ((uint64_t)cur_pc24 << 32) | a); }
    if ((a >> 16) == 1) { uint16_t o = a & 0xFFFF; if (o >= 0x2000 && o < 0xA000 && !(mem[0xE0C035] & 8)) mem[0xE10000 | o] = v; }
}
static inline uint16_t rd16(uint32_t a) { uint16_t l = rd8(a); return l | (rd8(a + 1) << 8); }
static inline void wr16(uint32_t a, uint16_t v) { wr8(a, v & 255); wr8(a + 1, v >> 8); }
static inline uint32_t rd24(uint32_t a) { uint32_t l = rd16(a); return l | (rd8(a + 2) << 16); }

#define PCADDR ((uint32_t)cpu.pb << 16 | cpu.pc)
static inline uint8_t f8(void) { uint8_t v = rd8(PCADDR); cpu.pc++; return v; }
static inline uint16_t f16(void) { uint16_t l = f8(); return l | (f8() << 8); }

#define M16 (!(cpu.p & FM))
#define X16 (!(cpu.p & FX))

static void push8(uint8_t v) { wr8(cpu.s, v); cpu.s--; }
static void push16(uint16_t v) { push8(v >> 8); push8(v & 255); }
static uint8_t pull8(void) { cpu.s++; return rd8(cpu.s); }
static uint16_t pull16(void) { uint16_t l = pull8(); return l | (pull8() << 8); }

static inline void setnz(uint16_t v, int w16) {
    cpu.p &= ~(FN | FZ);
    if (w16) { if (v & 0x8000) cpu.p |= FN; if (!(v & 0xFFFF)) cpu.p |= FZ; }
    else { if (v & 0x80) cpu.p |= FN; if (!(v & 0xFF)) cpu.p |= FZ; }
}

/* effective address of a memory-referencing mode (fetches operand bytes) */
static uint32_t ea(int md) {
    uint32_t t; uint16_t o;
    switch (md) {
    case MD_dp: return (uint16_t)(cpu.d + f8());
    case MD_dpx: return (uint16_t)(cpu.d + f8() + (X16 ? cpu.x : (cpu.x & 255)));
    case MD_dpy: return (uint16_t)(cpu.d + f8() + (X16 ? cpu.y : (cpu.y & 255)));
    case MD_abs: return ((uint32_t)cpu.db << 16) | f16();
    case MD_absx: return (((uint32_t)cpu.db << 16) | f16()) + (X16 ? cpu.x : (cpu.x & 255));
    case MD_absy: return (((uint32_t)cpu.db << 16) | f16()) + (X16 ? cpu.y : (cpu.y & 255));
    case MD_long: t = rd24(PCADDR); cpu.pc += 3; return t;
    case MD_longx: t = rd24(PCADDR); cpu.pc += 3; return t + (X16 ? cpu.x : (cpu.x & 255));
    case MD_ind: o = (uint16_t)(cpu.d + f8()); return ((uint32_t)cpu.db << 16) | rd16(o);
    case MD_indx: o = (uint16_t)(cpu.d + f8() + (X16 ? cpu.x : (cpu.x & 255))); return ((uint32_t)cpu.db << 16) | rd16(o);
    case MD_indy: o = (uint16_t)(cpu.d + f8()); return (((uint32_t)cpu.db << 16) | rd16(o)) + (X16 ? cpu.y : (cpu.y & 255));
    case MD_indl: o = (uint16_t)(cpu.d + f8()); return rd24(o);
    case MD_indly: o = (uint16_t)(cpu.d + f8()); return rd24(o) + (X16 ? cpu.y : (cpu.y & 255));
    case MD_sr: return (uint16_t)(cpu.s + f8());
    case MD_sry: o = (uint16_t)(cpu.s + f8()); return (((uint32_t)cpu.db << 16) | rd16(o)) + (X16 ? cpu.y : (cpu.y & 255));
    }
    fault = 1; return 0;
}

static uint16_t operand(int md, int w16, uint32_t *addr) {
    if (md == MD_immM || md == MD_immX) { *addr = 0xFFFFFFFF; return w16 ? f16() : f8(); }
    *addr = ea(md);
    return w16 ? rd16(*addr) : rd8(*addr);
}

static void do_adc(uint16_t v, int w16) {
    uint32_t a = w16 ? cpu.a : (cpu.a & 255), c = cpu.p & FC, r;
    uint32_t mask = w16 ? 0xFFFF : 0xFF, sign = w16 ? 0x8000 : 0x80;
    if (cpu.p & FD) {
        int co, vo; r = bcd_add(a, v, (int)c, w16 ? 4 : 2, &co, &vo);
        cpu.p &= ~(FC | FV); if (co) cpu.p |= FC; if (vo) cpu.p |= FV;
    } else {
        r = a + v + c;
        cpu.p &= ~(FC | FV);
        if (r > mask) cpu.p |= FC;
        if (~(a ^ v) & (a ^ r) & sign) cpu.p |= FV;
    }
    if (w16) cpu.a = r & 0xFFFF; else cpu.a = (cpu.a & 0xFF00) | (r & 0xFF);
    setnz(r, w16);
}
static void do_sbc(uint16_t v, int w16) {
    if (cpu.p & FD) {
        uint32_t a = w16 ? cpu.a : (cpu.a & 255), r; int co, vo;
        r = bcd_sub(a, v & (w16 ? 0xFFFF : 0xFF), (cpu.p & FC) ? 1 : 0, w16 ? 4 : 2, &co, &vo);
        cpu.p &= ~(FC | FV); if (co) cpu.p |= FC; if (vo) cpu.p |= FV;
        if (w16) cpu.a = r & 0xFFFF; else cpu.a = (cpu.a & 0xFF00) | (r & 0xFF);
        setnz(r, w16);
    } else do_adc((uint16_t)(~v & (w16 ? 0xFFFF : 0xFF)), w16);
}
static void do_cmp(uint16_t r, uint16_t v, int w16) {
    uint32_t mask = w16 ? 0xFFFF : 0xFF, a = r & mask, vv = v & mask, d = a - vv;
    cpu.p &= ~FC; if (a >= vv) cpu.p |= FC; setnz(d, w16);
}
static uint16_t shift(int mn, uint16_t v, int w16) {
    uint32_t mask = w16 ? 0xFFFF : 0xFF, top = w16 ? 0x8000 : 0x80, c = cpu.p & FC, r;
    v &= mask;
    switch (mn) {
    case MN_ASL: cpu.p = (cpu.p & ~FC) | ((v & top) ? FC : 0); r = (v << 1) & mask; break;
    case MN_LSR: cpu.p = (cpu.p & ~FC) | (v & 1); r = v >> 1; break;
    case MN_ROL: cpu.p = (cpu.p & ~FC) | ((v & top) ? FC : 0); r = ((v << 1) | c) & mask; break;
    default: cpu.p = (cpu.p & ~FC) | (v & 1); r = (v >> 1) | (c ? top : 0); break;
    }
    setnz(r, w16); return r;
}

static void branch(int cond) { int8_t o = (int8_t)f8(); if (cond) cpu.pc += o; cycles += 1; }

static void setx(uint16_t *r, uint16_t v) { if (X16) *r = v; else *r = v & 255; setnz(*r, X16); }

/* run until max instructions or a hooked address; returns 0 = limit, 1 = hook, 2 = fault */
int cpu_run(uint64_t max) {
    uint64_t n = 0;
    while (n < max) {
        uint32_t pc24 = PCADDR;
        if (n && (hooks[pc24 >> 3] & (1 << (pc24 & 7)))) { hit_pc = pc24; return 1; }
        cur_pc24 = pc24;
        if (tr_on) { if (tr_buf && tr_n < tr_cap) { uint32_t *r = tr_buf + 4 * tr_n; r[0] = pc24; r[1] = cpu.a | ((uint32_t)cpu.x << 16); r[2] = cpu.y | ((uint32_t)cpu.s << 16); r[3] = cpu.d | ((uint32_t)cpu.db << 16) | ((uint32_t)cpu.p << 24); } tr_n++; }
        if (prof_on && (pc24 >> 16) < 8) pc_cnt[pc24]++;
        uint8_t op = f8();
        int mn = op_mn[op], md = op_md[op]; icount++; n++; cycles += 1;
        int w16 = M16, xw = X16; uint32_t addr; uint16_t v;
        switch (mn) {
        case MN_LDA: v = operand(md, w16, &addr); if (w16) cpu.a = v; else cpu.a = (cpu.a & 0xFF00) | (v & 255); setnz(v, w16); break;
        case MN_LDX: v = operand(md, xw, &addr); setx(&cpu.x, v); break;
        case MN_LDY: v = operand(md, xw, &addr); setx(&cpu.y, v); break;
        case MN_STA: addr = ea(md); if (w16) wr16(addr, cpu.a); else wr8(addr, cpu.a & 255); break;
        case MN_STX: addr = ea(md); if (xw) wr16(addr, cpu.x); else wr8(addr, cpu.x & 255); break;
        case MN_STY: addr = ea(md); if (xw) wr16(addr, cpu.y); else wr8(addr, cpu.y & 255); break;
        case MN_STZ: addr = ea(md); if (w16) wr16(addr, 0); else wr8(addr, 0); break;
        case MN_ORA: v = operand(md, w16, &addr); if (w16) cpu.a |= v; else cpu.a |= (v & 255); setnz(cpu.a, w16); break;
        case MN_AND: v = operand(md, w16, &addr); if (w16) cpu.a &= v; else cpu.a = (cpu.a & 0xFF00) | (cpu.a & v & 255); setnz(cpu.a, w16); break;
        case MN_EOR: v = operand(md, w16, &addr); if (w16) cpu.a ^= v; else cpu.a = (cpu.a & 0xFF00) | ((cpu.a ^ v) & 255); setnz(cpu.a, w16); break;
        case MN_ADC: v = operand(md, w16, &addr); do_adc(v, w16); break;
        case MN_SBC: v = operand(md, w16, &addr); do_sbc(v, w16); break;
        case MN_CMP: v = operand(md, w16, &addr); do_cmp(cpu.a, v, w16); break;
        case MN_CPX: v = operand(md, xw, &addr); do_cmp(cpu.x, v, xw); break;
        case MN_CPY: v = operand(md, xw, &addr); do_cmp(cpu.y, v, xw); break;
        case MN_BIT: v = operand(md, w16, &addr);
            if (addr != 0xFFFFFFFF) { cpu.p &= ~(FN | FV); if (v & (w16 ? 0x8000 : 0x80)) cpu.p |= FN; if (v & (w16 ? 0x4000 : 0x40)) cpu.p |= FV; }
            cpu.p &= ~FZ; if (!((w16 ? cpu.a : cpu.a & 255) & v)) cpu.p |= FZ; break;
        case MN_ASL: case MN_LSR: case MN_ROL: case MN_ROR:
            if (md == MD_acc) { v = shift(mn, w16 ? cpu.a : cpu.a & 255, w16); if (w16) cpu.a = v; else cpu.a = (cpu.a & 0xFF00) | (v & 255); }
            else { addr = ea(md); v = w16 ? rd16(addr) : rd8(addr); v = shift(mn, v, w16); if (w16) wr16(addr, v); else wr8(addr, v); }
            break;
        case MN_INC: case MN_DEC: {
            int d = mn == MN_INC ? 1 : -1;
            if (md == MD_acc) { if (w16) cpu.a += d; else cpu.a = (cpu.a & 0xFF00) | ((cpu.a + d) & 255); setnz(cpu.a, w16); }
            else { addr = ea(md); v = (w16 ? rd16(addr) : rd8(addr)) + d; if (w16) wr16(addr, v); else wr8(addr, v); setnz(v, w16); }
            break; }
        case MN_TSB: case MN_TRB: addr = ea(md); v = w16 ? rd16(addr) : rd8(addr);
            cpu.p &= ~FZ; if (!((w16 ? cpu.a : cpu.a & 255) & v)) cpu.p |= FZ;
            v = mn == MN_TSB ? (v | cpu.a) : (v & ~cpu.a); if (w16) wr16(addr, v); else wr8(addr, v); break;
        case MN_INX: setx(&cpu.x, cpu.x + 1); break;
        case MN_INY: setx(&cpu.y, cpu.y + 1); break;
        case MN_DEX: setx(&cpu.x, cpu.x - 1); break;
        case MN_DEY: setx(&cpu.y, cpu.y - 1); break;
        case MN_BPL: branch(!(cpu.p & FN)); break; case MN_BMI: branch(cpu.p & FN); break;
        case MN_BVC: branch(!(cpu.p & FV)); break; case MN_BVS: branch(cpu.p & FV); break;
        case MN_BCC: branch(!(cpu.p & FC)); break; case MN_BCS: branch(cpu.p & FC); break;
        case MN_BNE: branch(!(cpu.p & FZ)); break; case MN_BEQ: branch(cpu.p & FZ); break;
        case MN_BRA: branch(1); break;
        case MN_BRL: { int16_t o = (int16_t)f16(); cpu.pc += o; } break;
        case MN_JMP: if (md == MD_abs) cpu.pc = f16();
            else if (md == MD_absind) { uint16_t p = f16(); cpu.pc = rd16(p); if (prof_on) rec(2, ((uint64_t)pc24 << 32) | PCADDR); }
            else { uint16_t p = f16(); cpu.pc = rd16(((uint32_t)cpu.pb << 16) | (uint16_t)(p + (xw ? cpu.x : cpu.x & 255))); if (prof_on) rec(2, ((uint64_t)pc24 << 32) | PCADDR); } break;
        case MN_JML: if (md == MD_long) { uint32_t t = rd24(PCADDR); cpu.pc += 3; cpu.pb = t >> 16; cpu.pc = t & 0xFFFF; }
            else { uint16_t p = f16(); uint32_t t = rd24(p); cpu.pb = t >> 16; cpu.pc = t & 0xFFFF; if (prof_on) rec(2, ((uint64_t)pc24 << 32) | PCADDR); } break;
        case MN_JSR: if (md == MD_abs) { uint16_t t = f16(); push16(cpu.pc - 1); cpu.pc = t; }
            else { uint16_t p = f16(); push16(cpu.pc - 1); cpu.pc = rd16(((uint32_t)cpu.pb << 16) | (uint16_t)(p + (xw ? cpu.x : cpu.x & 255))); if (prof_on) rec(2, ((uint64_t)pc24 << 32) | PCADDR); } break;
        case MN_JSL: { uint32_t t = rd24(PCADDR); cpu.pc += 3; push8(cpu.pb); push16(cpu.pc - 1); cpu.pb = t >> 16; cpu.pc = t & 0xFFFF; } break;
        case MN_RTS: cpu.pc = pull16() + 1; if (prof_on) rec(2, ((uint64_t)pc24 << 32) | PCADDR); break;
        case MN_RTL: cpu.pc = pull16() + 1; cpu.pb = pull8(); if (prof_on) rec(2, ((uint64_t)pc24 << 32) | PCADDR); break;
        case MN_PHA: if (w16) push16(cpu.a); else push8(cpu.a & 255); break;
        case MN_PLA: if (w16) cpu.a = pull16(); else cpu.a = (cpu.a & 0xFF00) | pull8(); setnz(cpu.a, w16); break;
        case MN_PHX: if (xw) push16(cpu.x); else push8(cpu.x & 255); break;
        case MN_PHY: if (xw) push16(cpu.y); else push8(cpu.y & 255); break;
        case MN_PLX: if (xw) cpu.x = pull16(); else cpu.x = pull8(); setnz(cpu.x, xw); break;
        case MN_PLY: if (xw) cpu.y = pull16(); else cpu.y = pull8(); setnz(cpu.y, xw); break;
        case MN_PHP: push8(cpu.p); break;
        case MN_PLP: cpu.p = pull8(); if (cpu.p & FX) { cpu.x &= 255; cpu.y &= 255; } break;
        case MN_PHB: push8(cpu.db); break; case MN_PLB: cpu.db = pull8(); setnz(cpu.db, 0); break;
        case MN_PHD: push16(cpu.d); break; case MN_PLD: cpu.d = pull16(); setnz(cpu.d, 1); break;
        case MN_PHK: push8(cpu.pb); break;
        case MN_PEA: v = f16(); push16(v); break;
        case MN_PEI: addr = ea(MD_dp); push16(rd16(addr)); break;
        case MN_PER: { int16_t o = (int16_t)f16(); push16(cpu.pc + o); } break;
        case MN_TAX: setx(&cpu.x, xw ? cpu.a : cpu.a & 255); break;
        case MN_TAY: setx(&cpu.y, xw ? cpu.a : cpu.a & 255); break;
        case MN_TXA: if (w16) cpu.a = cpu.x; else cpu.a = (cpu.a & 0xFF00) | (cpu.x & 255); setnz(cpu.a, w16); break;
        case MN_TYA: if (w16) cpu.a = cpu.y; else cpu.a = (cpu.a & 0xFF00) | (cpu.y & 255); setnz(cpu.a, w16); break;
        case MN_TXY: setx(&cpu.y, cpu.x); break; case MN_TYX: setx(&cpu.x, cpu.y); break;
        case MN_TSX: setx(&cpu.x, cpu.s); break;
        case MN_TXS: cpu.s = cpu.x; break;
        case MN_TCD: cpu.d = cpu.a; setnz(cpu.d, 1); break; case MN_TDC: cpu.a = cpu.d; setnz(cpu.a, 1); break;
        case MN_TCS: cpu.s = cpu.a; break; case MN_TSC: cpu.a = cpu.s; setnz(cpu.a, 1); break;
        case MN_XBA: cpu.a = (cpu.a >> 8) | (cpu.a << 8); setnz(cpu.a & 255, 0); break;
        case MN_CLC: cpu.p &= ~FC; break; case MN_SEC: cpu.p |= FC; break;
        case MN_CLI: cpu.p &= ~FI; break; case MN_SEI: cpu.p |= FI; break;
        case MN_CLD: cpu.p &= ~FD; break; case MN_SED: cpu.p |= FD; break;
        case MN_CLV: cpu.p &= ~FV; break;
        case MN_REP: v = f8(); cpu.p &= ~v; break;
        case MN_SEP: v = f8(); cpu.p |= v; if (cpu.p & FX) { cpu.x &= 255; cpu.y &= 255; } break;
        case MN_XCE: break;
        case MN_NOP: break; case MN_WDM: f8(); break;
        case MN_MVN: case MN_MVP: { uint8_t dst = f8(), src = f8(); cpu.db = dst; int d = mn == MN_MVN ? 1 : -1;
            for (;;) { uint8_t b = rd8(((uint32_t)src << 16) | cpu.x); wr8(((uint32_t)dst << 16) | cpu.y, b);
                cpu.x += d; cpu.y += d; if (!xw) { cpu.x &= 255; cpu.y &= 255; } cpu.a--; if (cpu.a == 0xFFFF) break; } } break;
        case MN_RTI: cpu.p = pull8(); cpu.pc = pull16(); cpu.pb = pull8(); break;
        default: fault = 2; fault_pc = pc24; return 2;
        }
        if (fault) { fault_pc = pc24; return 2; }
    }
    return 0;
}
