/* WHDLoad build (-DWHDLOAD): the program runs without an operating system. The slave (whd_slave.S) loads the program's image into the
 * expansion (Fast) memory, relocates it and calls `start` with the resload pointer; everything the AmigaOS build gets from exec and dos
 * comes from here:
 *   memory   two arenas (Chip: the base memory WHDLoad gives, from $2000; Fast: the rest of the expansion memory), first fit, free ranges merged
 *   files    resload_GetFileSize / LoadFile / SaveFile on the files of the slave's data directory (the names of the data folder, no path)
 *   overlays "<name>.ovx" images made by tools/elf2whd.py (header, image, table of 32-bit relocation offsets), relocated here
 * resload calls: a2 = resload, arguments in d0/d1/a0/a1 as documented in whdload.i, `jsr (offset,a2)`. */
#ifndef WHD_GLUE_H
#define WHD_GLUE_H
#include "os.h"

static u32 whd_rl;                                          /* the resload base the slave got in a0 */

#define RLCLOB "d0", "d1", "a0", "a1", "a2", "cc", "memory"      /* a2 holds resload during the call, so no operand may live there either */
static inline u32 rl_GetFileSize(const char *name)          /* 0 if the file does not exist (WHDLF_NoError) */
{
    u32 r;
    __asm volatile("move.l %2,%%a2\n\tmove.l %1,%%a0\n\tjsr 0x24(%%a2)\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(name), "r"(whd_rl) : RLCLOB);
    return r;
}
static inline u32 rl_LoadFile(const char *name, u32 dest)   /* size of the file, 0 on failure */
{
    u32 r;
    __asm volatile("move.l %3,%%a2\n\tmove.l %1,%%a0\n\tmove.l %2,%%a1\n\tjsr 0x08(%%a2)\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(name), "r"(dest), "r"(whd_rl) : RLCLOB);
    return r;
}
static inline u32 rl_SaveFile(const char *name, const void *data, u32 len)
{
    u32 r;
    __asm volatile("move.l %4,%%a2\n\tmove.l %3,%%d0\n\tmove.l %1,%%a0\n\tmove.l %2,%%a1\n\tjsr 0x0C(%%a2)\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(name), "r"(data), "r"(len), "r"(whd_rl) : RLCLOB);
    return r;
}
static inline void rl_FlushCache(void)
{
    __asm volatile("move.l %0,%%a2\n\tjsr 0x20(%%a2)" : : "r"(whd_rl) : RLCLOB);
}

/* ---- memory: free ranges of an arena, sorted by address ---- */
#define WH_NFREE 64
typedef struct { u32 lo, hi; } WhRange;
typedef struct { WhRange r[WH_NFREE]; int n; u32 lo, hi; } WhArena;
static WhArena wh_chip, wh_fast;
static u32 wh_peak_fast, wh_peak_chip;                     /* most memory in use (test builds read these) */

static void wh_arena_init(WhArena *a, u32 lo, u32 hi)
{
    a->lo = lo; a->hi = hi; a->n = 0;
    if (hi > lo) { a->r[0].lo = lo; a->r[0].hi = hi; a->n = 1; }
}
static u32 wh_used(const WhArena *a)
{
    u32 f = 0; int i;
    for (i = 0; i < a->n; i++) f += a->r[i].hi - a->r[i].lo;
    return (a->hi - a->lo) - f;
}
static u32 wh_arena_alloc(WhArena *a, u32 size)
{
    int i, k;
    size = (size + 7) & ~7UL;
    for (i = 0; i < a->n; i++) {
        if (a->r[i].hi - a->r[i].lo >= size) {
            u32 m = a->r[i].lo;
            a->r[i].lo += size;
            if (a->r[i].lo == a->r[i].hi) { for (k = i; k + 1 < a->n; k++) a->r[k] = a->r[k + 1]; a->n--; }
            return m;
        }
    }
    return 0;
}
static void wh_arena_free(WhArena *a, u32 mem, u32 size)
{
    int i, k;
    u32 hi;
    size = (size + 7) & ~7UL; hi = mem + size;
    for (i = 0; i < a->n && a->r[i].lo < mem; i++) {}
    if (i > 0 && a->r[i - 1].hi == mem) {                   /* joins the range before */
        a->r[i - 1].hi = hi;
        if (i < a->n && a->r[i].lo == hi) { a->r[i - 1].hi = a->r[i].hi; for (k = i; k + 1 < a->n; k++) a->r[k] = a->r[k + 1]; a->n--; }
    } else if (i < a->n && a->r[i].lo == hi) {              /* joins the range after */
        a->r[i].lo = mem;
    } else if (a->n < WH_NFREE) {
        for (k = a->n; k > i; k--) a->r[k] = a->r[k - 1];
        a->r[i].lo = mem; a->r[i].hi = hi; a->n++;
    }                                                       /* (a full table loses the block: a leak, never a corruption) */
}
static u32 whd_alloc(u32 size, u32 flags)
{
    u32 m = 0;
    if (!(flags & MEMF_CHIP)) m = wh_arena_alloc(&wh_fast, size);        /* the CPU's own data go to Fast RAM when there is some */
    if (!m) m = wh_arena_alloc(&wh_chip, size);
    if (m && (flags & MEMF_CLEAR)) { u32 *p = (u32 *)m, n = (size + 3) >> 2; while (n--) *p++ = 0; }
    { u32 f = wh_used(&wh_fast), c = wh_used(&wh_chip); if (f > wh_peak_fast) wh_peak_fast = f; if (c > wh_peak_chip) wh_peak_chip = c; }
    return m;
}
static void whd_free(u32 mem, u32 size)
{
    if (!mem) return;
    if (mem >= wh_fast.lo && mem < wh_fast.hi) wh_arena_free(&wh_fast, mem, size); else wh_arena_free(&wh_chip, mem, size);
}
static u32 whd_avail(u32 flags)                             /* the largest free block, as AvailMem(MEMF_LARGEST) */
{
    const WhArena *a = (flags & MEMF_CHIP) || !wh_fast.n ? &wh_chip : &wh_fast;
    u32 best = 0; int i;
    for (i = 0; i < a->n; i++) if (a->r[i].hi - a->r[i].lo > best) best = a->r[i].hi - a->r[i].lo;
    return best;
}

/* ---- files ---- */
static u32 whd_load(const char *name, u32 flags, u32 *size)
{
    u32 len = rl_GetFileSize(name), mem;
    if (!len) return 0;
    mem = whd_alloc(len + 4, flags);
    if (!mem) return 0;
    if (!rl_LoadFile(name, mem)) { whd_free(mem, len + 4); return 0; }
    if (size) *size = len;
    return mem;
}
static void whd_save(const char *name, const void *data, u32 len) { rl_SaveFile(name, data, len); }

/* ---- overlays ("SODO" files, see tools/elf2whd.py): u32 magic, image length, number of relocations, total length; the image (padded to 4); the offsets of
   the 32-bit words to which the load address is added. Returns what dos_LoadSeg returns for the caller's ((seg << 2) + 4): the image address. ---- */
static u32 whd_loadseg(const char *name)
{
    u32 len = rl_GetFileSize(name), blk, img, i, n;
    const u32 *rel;
    if (!len) return 0;
    blk = whd_alloc(len + 8, 0);
#ifdef WHD_TRACE
    { u32 t[4]; t[0] = len; t[1] = blk; t[2] = wh_fast.n ? wh_fast.r[0].lo : 0; t[3] = wh_fast.n ? wh_fast.r[0].hi : 0; rl_SaveFile("trace1.bin", t, 16); }
#endif
    if (!blk) return 0;
    if (!rl_LoadFile(name, blk) || ((u32 *)blk)[0] != 0x534F444FUL) { whd_free(blk, len + 8); return 0; }
    ((u32 *)blk)[3] = len + 8;                              /* what whd_unloadseg frees */
    img = blk + 16; n = ((u32 *)blk)[2];
    rel = (const u32 *)(img + ((u32 *)blk)[1]);
    for (i = 0; i < n; i++) *(u32 *)(img + rel[i]) += img;
    rl_FlushCache();
    return (img - 4) >> 2;
}
static void whd_unloadseg(u32 seg)
{
    u32 blk = (seg << 2) + 4 - 16;
    whd_free(blk, ((u32 *)blk)[3]);
}

static OsApi whd_os = {whd_alloc, whd_free, whd_avail, whd_load, whd_save, whd_loadseg, whd_unloadseg};

/* 50 Hz (PAL) or 60 Hz (NTSC) machine: the number of raster lines in a frame (about 312 or 262), the highest line seen over several frames */
static int whd_is_pal(void)
{
    u32 i, mx = 0;
    for (i = 0; i < 400000; i++) {
        u32 line = ((u32)(R16(VPOSR) & 1) << 8) | (R16(VHPOSR) >> 8);
        if (line > mx) mx = line;
    }
    return mx > 285;
}

/* called by `start` (ow.c): resload, the free part of the expansion memory after the program image (address, size), the base memory size.
   Returns a 64 KB block for the program's stack (the translated game code needs more than the slave's), 0 if there is no memory. */
u32 whd_init(u32 resload, u32 fast, u32 fastsize, u32 chipsize);
u32 whd_init(u32 resload, u32 fast, u32 fastsize, u32 chipsize)
{
    whd_rl = resload;
    fast = (fast + 7) & ~7UL;
    wh_arena_init(&wh_fast, fast, fast + (fastsize & ~7UL));
    wh_arena_init(&wh_chip, 0x2000, chipsize);
    g_os = &whd_os;
#ifdef WHD_TRACE
    {
        u32 t[8]; int k;
        t[0] = *(volatile u8 *)0xBFE801UL; t[2] = *(volatile u8 *)0xBFEF01UL; t[3] = *(volatile u8 *)0xBFEE01UL;
        for (k = 0; k < 100; k++) { while (!(R16(INTREQR) & 0x0020)) {} R16(INTREQ) = 0x0020; }
        t[1] = *(volatile u8 *)0xBFE801UL; t[4] = *(volatile u8 *)0xBFEA01UL; t[5] = *(volatile u8 *)0xBFE901UL; t[6] = R16(VPOSR); t[7] = R16(INTENAR);
        rl_SaveFile("trace0.bin", t, 32);
    }
#endif
    return whd_alloc(65536, 0);
}
#endif
