/* Types, hardware registers, AmigaOS and AmigaDOS calls shared by the program and the event overlays. */
#ifndef OS_H
#define OS_H
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

/* ---- hardware ---- */
#define CUSTOM 0xDFF000UL
#define R16(r) (*(volatile u16 *)(CUSTOM + (r)))
#define R32(r) (*(volatile u32 *)(CUSTOM + (r)))
#define DMACONR 0x002
#define VPOSR 0x004
#define VHPOSR 0x006
#define JOY1DAT 0x00C
#define INTENAR 0x01C
#define INTREQR 0x01E
#define COP1LC 0x080
#define COPJMP1 0x088
#define DIWSTRT 0x08E
#define DIWSTOP 0x090
#define DDFSTRT 0x092
#define DDFSTOP 0x094
#define DMACON 0x096
#define INTENA 0x09A
#define INTREQ 0x09C
#define BPLCON0 0x100
#define BPLCON1 0x102
#define BPLCON2 0x104
#define BPLCON3 0x106
#define BPL1MOD 0x108
#define BPL2MOD 0x10A
#define BPLCON4 0x10C
#define FMODE 0x1FC
#define AUDLC(n) (0x0A0 + (n) * 0x10)
#define AUDLEN(n) (0x0A4 + (n) * 0x10)
#define AUDPER(n) (0x0A6 + (n) * 0x10)
#define AUDVOL(n) (0x0A8 + (n) * 0x10)

#define CIAA_PRA (*(volatile u8 *)0xBFE001UL)
#define CIAA_SDR (*(volatile u8 *)0xBFEC01UL)
#define CIAA_ICR (*(volatile u8 *)0xBFED01UL)
#define CIAA_CRA (*(volatile u8 *)0xBFEE01UL)

#ifdef WHDLOAD
/* ---- WHDLoad build: there is no operating system. The program and its overlays reach memory and files through this table
 * (filled in by the program itself from whd_glue.h; the overlays get its address in EvArgs) ---- */
typedef struct OsApi {
    u32 (*alloc)(u32 size, u32 flags);
    void (*free)(u32 mem, u32 size);
    u32 (*avail)(u32 flags);
    u32 (*load)(const char *name, u32 flags, u32 *size);      /* a file of the data folder into a new block */
    void (*save)(const char *name, const void *data, u32 len);
    u32 (*loadseg)(const char *name);                         /* an overlay: relocated image, the value is what dos_LoadSeg would give */
    void (*unloadseg)(u32 seg);
} OsApi;
static OsApi *g_os = 0;
static u32 SysBase = 4, DOSBase = 4;
static inline u32 AllocMem(u32 size, u32 flags) { return g_os->alloc(size, flags); }
static inline void FreeMem(u32 mem, u32 size) { g_os->free(mem, size); }
static inline u32 AvailMem(u32 flags) { return g_os->avail(flags); }
static inline u32 CacheControl(u32 bits, u32 mask) { (void)bits; (void)mask; return 0; }
static inline void Forbid(void) {}
static inline void Permit(void) {}
static inline u32 OpenLibrary(const char *name, u32 ver) { (void)name; (void)ver; return 1; }
static inline void CloseLibrary(u32 lib) { (void)lib; }
static inline void LoadView(u32 gb, u32 view) { (void)gb; (void)view; }
static inline void WaitTOF(u32 gb) { (void)gb; }
static inline u32 dos_LoadSeg(const char *name) { return g_os->loadseg(name); }
static inline void dos_UnLoadSeg(u32 seg) { g_os->unloadseg(seg); }
static void dos_puts(const char *s) { (void)s; }
#else
/* ---- AmigaOS library calls (LVO offsets) ----
 * Operands are moved into the OS registers explicitly inside the asm; d0/d1/a0/a1 are the scratch registers of the
 * AmigaOS calling convention. The file is compiled with -ffixed-a6 so the compiler never keeps anything in a6 (the
 * library base register), and the entry stub below saves and restores a6 for the caller.
 * (An earlier version bound C variables to registers; it let the compiler lose live values across calls.) */
static u32 SysBase = 4;   /* initialised so it lands in .data, not .bss (flat hunk has no bss) */

#define OSCLOB "d0", "d1", "a0", "a1", "cc", "memory"   /* the wrappers save and restore a6 themselves: the compiler may use it as frame pointer */

static inline u32 AllocMem(u32 size, u32 flags)
{
    u32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%d0\n\tmove.l %2,%%d1\n\tmove.l %3,%%a6\n\tjsr -198(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(size), "r"(flags), "r"(SysBase) : OSCLOB);
    return r;
}
static inline void FreeMem(u32 mem, u32 size)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%a1\n\tmove.l %1,%%d0\n\tmove.l %2,%%a6\n\tjsr -210(%%a6)\n\tmove.l (%%sp)+,%%a6"
                   : : "r"(mem), "r"(size), "r"(SysBase) : OSCLOB);
}
static inline u32 CacheControl(u32 bits, u32 mask)        /* exec -648: the 68020 instruction cache (bit 0) on or off */
{
    register u32 d0 __asm("d0") = bits; register u32 d1 __asm("d1") = mask;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %2,%%a6\n\tjsr -648(%%a6)\n\tmove.l (%%sp)+,%%a6" : "+r"(d0), "+r"(d1) : "r"(SysBase) : "a0", "a1", "cc", "memory");
    return d0;
}
static inline void Forbid(void)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%a6\n\tjsr -132(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(SysBase) : OSCLOB);
}
static inline void Permit(void)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%a6\n\tjsr -138(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(SysBase) : OSCLOB);
}
static inline u32 OpenLibrary(const char *name, u32 ver)
{
    u32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%a1\n\tmove.l %2,%%d0\n\tmove.l %3,%%a6\n\tjsr -552(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(name), "r"(ver), "r"(SysBase) : OSCLOB);
    return r;
}
static inline void CloseLibrary(u32 lib)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%a1\n\tmove.l %1,%%a6\n\tjsr -414(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(lib), "r"(SysBase) : OSCLOB);
}
static inline void LoadView(u32 gb, u32 view)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%a1\n\tmove.l %1,%%a6\n\tjsr -222(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(view), "r"(gb) : OSCLOB);
}
static inline void WaitTOF(u32 gb)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%a6\n\tjsr -270(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(gb) : OSCLOB);
}

/* ---- AmigaDOS (files of the program's data folder) ---- */
static u32 DOSBase = 4;
#define DOSCLOB "d0", "d1", "d2", "d3", "a0", "a1", "cc", "memory"
static inline u32 dos_Open(const char *name, u32 mode)
{
    u32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%d1\n\tmove.l %2,%%d2\n\tmove.l %3,%%a6\n\tjsr -30(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(name), "r"(mode), "r"(DOSBase) : DOSCLOB);
    return r;
}
static inline void dos_Close(u32 fh)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%d1\n\tmove.l %1,%%a6\n\tjsr -36(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(fh), "r"(DOSBase) : DOSCLOB);
}
static inline s32 dos_Read(u32 fh, void *buf, u32 len)
{
    s32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%d1\n\tmove.l %2,%%d2\n\tmove.l %3,%%d3\n\tmove.l %4,%%a6\n\tjsr -42(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(fh), "r"(buf), "r"(len), "r"(DOSBase) : DOSCLOB);
    return r;
}
static inline s32 dos_Seek(u32 fh, s32 pos, s32 mode)
{
    s32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%d1\n\tmove.l %2,%%d2\n\tmove.l %3,%%d3\n\tmove.l %4,%%a6\n\tjsr -66(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(fh), "r"(pos), "r"(mode), "r"(DOSBase) : DOSCLOB);
    return r;
}
static inline u32 dos_LoadSeg(const char *name)
{
    u32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%d1\n\tmove.l %2,%%a6\n\tjsr -150(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(name), "r"(DOSBase) : DOSCLOB);
    return r;
}
static inline void dos_UnLoadSeg(u32 seg)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%d1\n\tmove.l %1,%%a6\n\tjsr -156(%%a6)\n\tmove.l (%%sp)+,%%a6" : : "r"(seg), "r"(DOSBase) : DOSCLOB);
}
static inline u32 dos_Output(void)
{
    u32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%a6\n\tjsr -60(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0" : "=r"(r) : "r"(DOSBase) : DOSCLOB);
    return r;
}
static inline void dos_Write(u32 fh, const void *buf, u32 len)
{
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %0,%%d1\n\tmove.l %1,%%d2\n\tmove.l %2,%%d3\n\tmove.l %3,%%a6\n\tjsr -48(%%a6)\n\tmove.l (%%sp)+,%%a6"
                   : : "r"(fh), "r"(buf), "r"(len), "r"(DOSBase) : DOSCLOB);
}
static void dos_puts(const char *s)
{
    u32 n = 0, out = dos_Output();
    while (s[n]) n++;
    if (out) dos_Write(out, s, n);
}
static inline u32 AvailMem(u32 flags)
{
    u32 r;
    __asm volatile("move.l %%a6,-(%%sp)\n\tmove.l %1,%%d1\n\tmove.l %2,%%a6\n\tjsr -216(%%a6)\n\tmove.l (%%sp)+,%%a6\n\tmove.l %%d0,%0"
                   : "=r"(r) : "r"(flags), "r"(SysBase) : OSCLOB);
    return r;
}
#endif

#define MEMF_CHIP 2UL
#define MEMF_CLEAR 0x10000UL
#define MEMF_LARGEST 0x20000UL
#define SCREEN_BYTES 32000UL
#define COP_BYTES 1024UL
#define NBLOCKS 4
#define SFX_CAP 40000

/* A file of the data folder ("PROGDIR:data/<name>") read into a new block of memory (flags as for AllocMem). Returns its address, 0 if the
   file or the memory is missing; *size gets the length. Interrupts and the OS must be running (see os_give in the callers). */
#ifdef WHDLOAD
static u32 asset_load(const char *name, u32 flags, u32 *size) { return g_os->load(name, flags, size); }
static void asset_free(const void *p, u32 size) { if (p) FreeMem((u32)p, size + 4); }
static void file_save(const char *name, const void *data, u32 len) { g_os->save(name, data, len); }
#else
static u32 asset_load(const char *name, u32 flags, u32 *size)
{
    char path[48];
    const char *pre = "PROGDIR:data/";
    int n = 0, i;
    u32 fh, len, mem;
    for (i = 0; pre[i]; i++) path[n++] = pre[i];
    for (i = 0; name[i] && n < 46; i++) path[n++] = name[i];
    path[n] = 0;
    fh = dos_Open(path, 1005);
    if (!fh) return 0;
    dos_Seek(fh, 0, 1);                                     /* OFFSET_END */
    len = (u32)dos_Seek(fh, 0, -1);                         /* back to the start; returns where the end was */
    mem = AllocMem(len + 4, flags);
    if (mem) {
        if ((u32)dos_Read(fh, (void *)mem, len) != len) { FreeMem(mem, len + 4); mem = 0; }
        else if (size) *size = len;
    }
    dos_Close(fh);
    return mem;
}
static void asset_free(const void *p, u32 size) { if (p) FreeMem((u32)p, size + 4); }
static void file_save(const char *name, const void *data, u32 len)      /* a file of the data folder, written whole */
{
    char path[48];
    const char *pre = "PROGDIR:data/";
    int n = 0, i;
    u32 fh;
    for (i = 0; pre[i]; i++) path[n++] = pre[i];
    for (i = 0; name[i] && n < 46; i++) path[n++] = name[i];
    path[n] = 0;
    fh = dos_Open(path, 1006);
    if (fh) { dos_Write(fh, data, len); dos_Close(fh); }
}
#endif

#endif
