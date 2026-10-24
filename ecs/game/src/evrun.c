/* Event overlay of the program: loaded from data/<event>.ovl with LoadSeg while an event is played. Holds the translated 65816 code of one event
 * (tools/t816), the runtime (m816rt.c) and this platform side: the IIGS memory banks (only the pages the events use, see spans.h), the event's initial
 * memory image, the conversion of the displayed IIGS screen ($E1:2000, 4 bits per pixel) to Amiga bitplanes, the joystick and keys as the soft
 * switches the game reads, the pacing of the game's main loop (rt_pass is called once per pass) and the event's music and effects. */
#include "os.h"
#include "platform.h"
#include "menu.h"
#include "m816.h"
#include "c2p.h"
#include "spans.h"


#ifndef EV_HZ
#define EV_HZ ev_hz         /* main-loop passes per second: what a 2.8 MHz IIGS manages in each event (estimated from the 65816 cycle counts) */
#endif

/* the IIGS banks the events use, each only over the pages [lo, hi] (spans.h); the pointers are offset so that B3[0x1234] still means bank 3, offset $1234.
   A bank number the game touches outside its span would land in a neighbour: the spans were taken from long oracle runs (tools/cpu816x/workingset.py). */
static u8 **const ev_bank_ptr[9] = {&P_B0, &P_B1, &P_B2, &P_B3, &P_B4, &P_B5, &P_B7, &P_BE0, &P_BE1};
static const u8 ev_bank_no[9] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0xE0, 0xE1};
#define EV_BZ_BYTES 4096UL
static u32 ev_banks_bytes(void)
{
    u32 n = EV_BZ_BYTES; int i;
    for (i = 0; i < 9; i++) n += ((u32)ev_span_hi[i] - ev_span_lo[i] + 1) * 256;
    return n;
}
static void ev_map_banks(u32 mem)
{
    u32 off = 0; int i;
    for (i = 0; i < 9; i++) { *ev_bank_ptr[i] = (u8 *)(mem + off - (u32)ev_span_lo[i] * 256); off += ((u32)ev_span_hi[i] - ev_span_lo[i] + 1) * 256; }
    P_BZ = (u8 *)mem + off;                                           /* scratch for banks that do not exist: only ever touched by a game bug */
}
#define EV_ARENA_BYTES (ev_banks_bytes() + C2P_TAB_BYTES + 64000UL)

static Plat *gP = (Plat *)1;           /* initialised non-zero so it lands in .data */
static int ev_hz = 24, ev_vbl = 50, ev_abort = 0;
static u32 ev_now = 0, ev_due = 0, ev_period = 0;
static u8 ev_last_tod = 0;
#ifdef TIMING
/* test builds: the work of every pass (game code, picture conversion, input: everything except the wait for the next slot) measured with CIA-A timers A+B cascaded
   (1.41 us a tick), in units of 64 ticks (90 us), written to PROGDIR:data/tm_<mode><run>.bin when the event ends: pairs (whole pass, of which the picture conversion) */
#define TM_MAX 20000
static u16 *tm_buf; static u32 tm_n, tm_last, tm_on; static u16 tm_c2p;
static u32 tm_now(void)
{
    u32 b, a, b2;
    do { b = ((u32)*(volatile u8 *)0xBFE701UL << 8) | *(volatile u8 *)0xBFE601UL; a = ((u32)*(volatile u8 *)0xBFE501UL << 8) | *(volatile u8 *)0xBFE401UL;
         b2 = ((u32)*(volatile u8 *)0xBFE701UL << 8) | *(volatile u8 *)0xBFE601UL; } while (b != b2);
    return (b << 16) | a;
}
static void tm_start(void)
{
    *(volatile u8 *)0xBFEE01UL &= 0xC0; *(volatile u8 *)0xBFEF01UL = 0;
    *(volatile u8 *)0xBFE401UL = 0xFF; *(volatile u8 *)0xBFE501UL = 0xFF; *(volatile u8 *)0xBFE601UL = 0xFF; *(volatile u8 *)0xBFE701UL = 0xFF;
    *(volatile u8 *)0xBFEF01UL = 0x41; *(volatile u8 *)0xBFEE01UL = (u8)((*(volatile u8 *)0xBFEE01UL & 0xC0) | 0x01);
    tm_n = 0; tm_on = 0;
}
#endif
#ifdef PROFILE_PC
/* profiling builds: CIA-B timer A interrupts about every 1.4 ms (level 6) and counts the interrupted PC in 16-byte buckets of the overlay; the histogram is written to
   PROGDIR:data/pf_<mode><run>.bin (u32 counts, big-endian) when the event ends. Real time on the real bus: what a sampling profiler on the Amiga would see. */
#define PF_BUCKETS 16384
u32 *pf_hist; u32 pf_base;
extern char ev_start[];
void pf_irq(void);
__asm(".globl pf_irq\n"
      "pf_irq:\n"
      "\tmovem.l %d0/%a0,-(%sp)\n"
      "\tmove.w #0x2000,0xDFF09C\n"
      "\ttst.b 0xBFDD00\n"
      "\tmove.l 10(%sp),%d0\n"
      "\tsub.l pf_base,%d0\n"
      "\tbcs.s 1f\n"
      "\tlsr.l #4,%d0\n"
      "\tcmp.l #16384,%d0\n"
      "\tbcc.s 1f\n"
      "\tmove.l pf_hist,%a0\n"
      "\taddq.l #1,(%a0,%d0.l*4)\n"
      "1:\tmovem.l (%sp)+,%d0/%a0\n"
      "\trte\n");
static u32 pf_oldvec;
static void pf_start(void)
{
    pf_hist = (u32 *)AllocMem(PF_BUCKETS * 4, MEMF_CLEAR); pf_base = (u32)ev_start;
    if (!pf_hist) return;
    pf_oldvec = *(volatile u32 *)0x78UL; *(volatile u32 *)0x78UL = (u32)pf_irq;
    *(volatile u8 *)0xBFDE00UL = 0x00; *(volatile u8 *)0xBFDD00UL = 0x7F;
    *(volatile u8 *)0xBFD400UL = 0xFF; *(volatile u8 *)0xBFD500UL = 0x03;       /* 1023 ticks of 1.4 us */
    *(volatile u8 *)0xBFDD00UL = 0x81; *(volatile u8 *)0xBFDE00UL = 0x01;     /* continuous, on */
    R16(INTREQ) = 0x2000; R16(INTENA) = 0xE000;
}
static void pf_stop(void)
{
    R16(INTENA) = 0x2000; *(volatile u8 *)0xBFDE00UL = 0x00; *(volatile u8 *)0xBFDD00UL = 0x7F; R16(INTREQ) = 0x2000;
    if (pf_hist) *(volatile u32 *)0x78UL = pf_oldvec;
}
#endif
#ifdef FPS_ON
static int fps_on = 1, fps_frames = 0, fps_passes = 0, fps_shown = 0, fps_f10 = 0;
#else
static int fps_on = 0, fps_frames = 0, fps_passes = 0, fps_shown = 0, fps_f10 = 0;      /* F9: show how many passes a second the machine manages */
#endif
static u32 *ev_lut = (u32 *)1;
static const u8 *ev_song = 0; static u32 ev_song_size = 0;
#ifdef MEMLOG
static u32 ev_free_kb = 0;                                         /* test builds: the Chip RAM still free after the event's files are loaded, shown at the bottom */
static void ev_draw_number(Plat *P, u32 v, int col, int nd)
{
    static const u8 dig[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
    int k, r, p;
    for (k = nd - 1; k >= 0; k--) {
        for (r = 0; r < 10; r++) {
            u8 pt = dig[v % 10][r >> 1], b = (u8)(((pt & 4) ? 0xC0 : 0) | ((pt & 2) ? 0x30 : 0) | ((pt & 1) ? 0x0C : 0));
            u8 *d = P->B[P->comp] + (188 + r) * 160 + col + k;
            d[0] = b; for (p = 1; p < 4; p++) d[40 * p] = 0;
        }
        v /= 10;
    }
}
#endif

static void ev_load_pak(const u8 *p)
{
    u32 n = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7], i, k;
    p += 8;
    for (i = 0; i < n; i++) {
        u32 a = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        u32 len = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7];
        u32 bank = (a >> 16) & 255, off = a & 0xFFFF;
        int b, ok = 0;
        p += 8;
        for (b = 0; b < 9; b++) if (ev_bank_no[b] == bank) ok = 1 + b;
        if (ok) {                                                    /* bytes outside the bank's span are not stored (the pak was made with the same spans) */
            u32 lo = (u32)ev_span_lo[ok - 1] * 256, hi = (u32)ev_span_hi[ok - 1] * 256 + 255;
            u8 *d = BK[bank] + off;
            for (k = 0; k < len; k++) if (off + k >= lo && off + k <= hi) d[k] = p[k];
        }
        p += len + (len & 1);
    }
}

/* the event's song (IMA ADPCM, streamed through the ring) and the platform's side of the sound driver in INTER bank 7:
   $8003 = play effect A (the IIGS driver's effect numbers are used as NES sound ids), $8006 = start the event's music, $8009 = stop it */
u32 rt_jsl(u32 target)
{
    Plat *P = gP;
    if (P == (Plat *)1) return 0;
    switch (target) {
    case 0x078003: sfx_play(P, (int)(cpu.a & 0xFF)); break;
    case 0x078006: if (ev_song) raw_music_start(P, ev_song, ev_song_size, 48); break;
    case 0x078009: music_stop(); break;
    }
    return 0;
}

/* the game reads the keyboard with no key waiting both in its main loop (once or twice a pass) and in waits (pause, "press a key").
   Only a long run of reads without a pass in between is a wait: then keep the machine alive, a frame a read */
static int poll_run = 0;
void io_poll(void)
{
    Plat *P = gP;
    if (P == (Plat *)1) return;
    if (++poll_run < 32) return;
    frame_wait(P);
    poll_keyboard(P);
    if (P->keys[0x45]) { ev_abort = 1; io.key = 0x1B; io.key_waiting = 1; io.anykey = 1; return; }   /* ESC ends a wait; rt_pass then leaves the event */
    if (P->evh != P->evt) {
        u8 k = iigs_key(P->evq[P->evh]);
        P->evh = (P->evh + 1) & 15;
        if (k) { io.key = (u8)(k & 0x7F); io.key_waiting = 1; io.anykey = 1; }
    }
}

/* the platform's side of the main loop: input, picture, pacing. Returns nonzero to stop the event. */
int rt_pass(u32 pc)
{
    Plat *P = gP;
    int i;
    (void)pc;
    poll_run = 0;
    /* picture */
    P->comp = P->shown ^ 1;
#ifdef TIMING
    u32 tm_c0 = tm_now();
#endif
    c2p_collect();
    c2p_convert(ev_lut, BE1 + 0x2000, P->B[P->comp], P->comp);
#ifdef TIMING
    tm_c2p = (u16)((tm_c0 - tm_now()) >> 6);
#endif
    {
        u16 pal[16];
        for (i = 0; i < 16; i++) pal[i] = LE16(BE1 + 0x9E00 + 2 * i);
        set_palette(P, pal);
    }
    if (fps_on) {
        int v = fps_shown, k;
        static const u8 dig[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
        for (k = 2; k >= 0; k--) {                     /* three digits of 6x10 pixels, colour 1, at the bottom left */
            int r, p;
            for (r = 0; r < 10; r++) {
                u8 pt = dig[v % 10][r >> 1], b = (u8)(((pt & 4) ? 0xC0 : 0) | ((pt & 2) ? 0x30 : 0) | ((pt & 1) ? 0x0C : 0));
                u8 *d = P->B[P->comp] + (188 + r) * 160 + k;
                d[0] = b; for (p = 1; p < 4; p++) d[40 * p] = 0;
            }
            v /= 10;
        }
    }
#ifdef MEMLOG
    ev_draw_number(P, ev_free_kb, 14, 4);
#endif
    present(P);
#ifdef TIMING
    { u32 now = tm_now(); if (tm_on && tm_n + 1 < TM_MAX && tm_buf) { tm_buf[tm_n++] = (u16)((tm_last - now) >> 6); tm_buf[tm_n++] = tm_c2p; } }
#endif
    /* pacing: EV_HZ passes a second, measured against CIA-A's time-of-day clock (one tick per frame, 50 or 60 a second). A pass that ran late is followed at once by
       the next one (no extra wait); a pass far behind does not try to catch up. With EV_UNPACED (speed tests) there is no wait at all. */
    {
        u8 t = *(volatile u8 *)0xBFE801UL;
        u32 dt = (u8)(t - ev_last_tod);
        ev_last_tod = t; ev_now += dt; fps_frames += (int)dt;
#ifndef EV_UNPACED
        ev_due += ev_period;
        while ((s32)(ev_due - (ev_now << 16)) > 0) {
            frame_wait(P);
            t = *(volatile u8 *)0xBFE801UL; dt = (u8)(t - ev_last_tod); ev_last_tod = t; ev_now += dt; fps_frames += (int)dt;
        }
        if ((s32)((ev_now << 16) - ev_due) > (s32)(3 * ev_period)) ev_due = ev_now << 16;
#endif
        audio_service(P);
    }
#ifdef TIMING
    tm_last = tm_now(); tm_on = 1;
#endif
    fps_passes++;
    if (fps_frames >= ev_vbl) { fps_shown = fps_passes; fps_passes = 0; fps_frames = 0; }
    /* input */
    poll_keyboard(P);
    poll_keyboard(P);
    {
        u16 j = R16(JOY1DAT);
        int jx = ((j >> 1) & 1) - ((j >> 9) & 1);
        int jy = (((j >> 1) ^ j) & 1) - (((j >> 9) ^ (j >> 8)) & 1);
        int fire = !(CIAA_PRA & 0x80);
        u8 *keys = P->keys;
        if (keys[0x4F]) jx = -1;
        if (keys[0x4E]) jx = 1;
        if (keys[0x4C]) jy = -1;
        if (keys[0x4D]) jy = 1;
        if (keys[0x40] || keys[0x44]) fire = 1;
        if (keys[0x58] && !fps_f10) { fps_on ^= 1; fps_f10 = 1; } else if (!keys[0x58]) fps_f10 = 0;
        if (keys[0x45] || ev_abort) { ev_abort = 1; return 1; }  /* ESC leaves the event */
        /* the paddle timers the original joystick routine measures: it waits up to 40 reads for each of two windows, so under 41 reads is "left/up", 41-80 "centre",
           more "right/down". Short timers (the oracle runs used 0/60/300) save a few thousand instructions a pass; the decisions are the same. */
        io.pdl_len[0] = jx < 0 ? 0 : jx > 0 ? 96 : 50;
        io.pdl_len[1] = jy < 0 ? 0 : jy > 0 ? 96 : 50;
        io.btn[0] = fire ? 0x80 : 0;
        io.anykey = 0;
        if (P->evh != P->evt) {
            u8 k = iigs_key(P->evq[P->evh]);
            P->evh = (P->evh + 1) & 15;
            if (k) { io.key = (u8)(k & 0x7F); io.key_waiting = 1; io.anykey = 1; }
        }
    }
    return 0;
}

/* run the event (game mode a->mode): load its memory image, song and effects, then run the translated game until it ends or the player leaves it. The shop's player
   data are copied into the INTER variables the event reads and the (possibly changed) scores copied back. Called with AmigaDOS running (os_give), returns with it
   running again; a->rc = rt_exit (-1 if something could not be loaded). */
u32 dispatch_call(u16 addr);
int ev_overlay_main(EvArgs *a)
{
    static const char *const pakn[6] = {0, "ev1.pak", "ev2.pak", "ev3.pak", "ev4.pak", "ev5.pak"}, *const songn[6] = {0, "ev1.raw", "ev2.raw", "ev3.raw", "ev4.raw", "ev5.raw"};
    Plat *P = a->P; Menu *M = (Menu *)a->M; u32 vbl_hz = a->vbl_hz; int mode = a->mode;
    u32 mem, pakp, paks = 0, arena = EV_ARENA_BYTES, sfx_size = 0;
    int i, rc;
    SysBase = a->SysBase; DOSBase = a->DOSBase; g_clock = a->clock; g_oldint = (u16)a->oldint;
    a->rc = -1;
    mem = AllocMem(arena, MEMF_CLEAR);
    if (!mem) return -1;
    ev_map_banks(mem);
    ev_lut = (u32 *)(mem + ev_banks_bytes());
    c2p_shadow[0] = (u8 *)ev_lut + C2P_TAB_BYTES; c2p_shadow[1] = c2p_shadow[0] + 32000;
    rt_init();
    c2p_build_lut(ev_lut);
    c2p_all_dirty();
    pakp = asset_load(pakn[mode], 0, &paks);
    if (!pakp) { FreeMem(mem, arena); return -1; }
    ev_load_pak((const u8 *)pakp);
    asset_free((const void *)pakp, paks);
    ev_song = (const u8 *)asset_load(songn[mode], MEMF_CHIP, &ev_song_size);     /* played by Paula: chip RAM */
    sfx_raw = (const u8 *)asset_load("sfx.raw", 0, &sfx_size);
#ifdef MEMLOG
    ev_free_kb = AvailMem(MEMF_CHIP) >> 10;
#endif
    os_take(P);
    /* INTER's variables for the players */
    for (i = 0; i < 128; i++) B7[0x6810 + i] = ((u8 *)M->names)[i];
    ST16(&B7[0x68CC], M->count);
    ST16(&B7[0x68CE], (u16)mode);
    for (i = 0; i < 8; i++) B7[0x68FE + i] = M->board[i];
    for (i = 0; i < 16; i++) B7[0x68EE + i] = M->colour[i];
    ST16(&B7[0x7E17], M->practice);
    {   /* what INTER derives from the player list before it starts an event ($76991-$769EC, $7544) */
        int n = M->count, k, computer = 0;
        ST16(&B7[0x7C2F], (u16)n);
        for (k = 0; k <= n && k < 16; k++) B7[0x7C1E + k] = (u8)k;
        ST16(&B7[0x7E13], 0); ST16(&B7[0x7B8A], 0);
        if (M->practice) { ST16(&B7[0x7E00], 0x7B08); computer = 1; }
        else {
            ST16(&B7[0x7E02], LE16(&B7[0x68EE])); ST16(&B7[0x7E00], 0x6810);
            if (mode == 4 || mode == 5) {                  /* the two-player events: the second skater is the next player or the computer */
                ST16(&B7[0x7B8A], 1);
                if (n > 1) { ST16(&B7[0x7E0D], LE16(&B7[0x68EE + 2])); ST16(&B7[0x7E0B], 0x6810 + 16); } else computer = 1;
            }
        }
        if (computer) { ST16(&B7[0x7E13], 0xFF); ST16(&B7[0x7E0B], 0x7AFF); ST16(&B7[0x7E0D], 0xF0); ST16(&B7[0x7E02], 0x0F00); }
    }
    for (i = 0; i < 192; i++) B7[0x7F00 + i] = M->scores[i];
    cpu.a = cpu.x = cpu.y = 0; cpu.s = 0x1FF; cpu.d = 0; cpu.db = 0; cpu.n = cpu.v = cpu.z = cpu.c = cpu.i = 0;
    io.enabled = 1; io.key_waiting = 0; io.key = 0; io.anykey = 0; io.mods = 0; io.btn[0] = io.btn[1] = io.btn[2] = 0;
    for (i = 0; i < 4; i++) { io.pdl_len[i] = 60; io.pdl_left[i] = 0; }
    ev_hz = mode == 1 ? 29 : mode == 2 ? 27 : mode == 3 ? 11 : mode == 4 ? 10 : 29;       /* the passes a second the original manages in each event (KEGS, ecs/docs/SOURCE_GATE.md) */
    gP = P; ev_vbl = (int)vbl_hz; ev_abort = 0;
    ev_period = (vbl_hz << 16) / (u32)ev_hz; ev_last_tod = *(volatile u8 *)0xBFE801UL; ev_now = 0; ev_due = 0;
    P->evh = P->evt;
#ifdef TIMING
    tm_buf = (u16 *)AllocMem(TM_MAX * 2, 0); tm_start();
#endif
#ifdef PROFILE_PC
    pf_start();
#endif
    rc = rt_run(dispatch_call, 0x1000);
#ifdef PROFILE_PC
    pf_stop();
#endif
    for (i = 0; i < 192; i++) M->scores[i] = B7[0x7F00 + i];
    io.enabled = 0;
    R16(DMACON) = 0x000F;                                            /* music and effects off */
    P->stream = 0;
    /* ESC that left the event must not also quit the program: wait for the key to come up */
    for (i = 0; i < 200; i++) { poll_keyboard(P); if (!P->keys[0x45]) break; frame_wait(P); }
    P->evh = P->evt;
    os_give();
#ifdef PROFILE_PC
    if (pf_hist) {
        char nm[32] = "PROGDIR:data/pf_0a.bin"; u32 fh;
        nm[16] = (char)('0' + mode); nm[17] = (char)('a' + a->run);
        fh = dos_Open(nm, 1006);
        if (fh) { dos_Write(fh, pf_hist, PF_BUCKETS * 4); dos_Close(fh); }
        FreeMem((u32)pf_hist, PF_BUCKETS * 4); pf_hist = 0;
    }
#endif
#ifdef TIMING
    if (tm_buf) {
        char nm[32] = "PROGDIR:data/tm_0a.bin"; u32 fh;
        nm[16] = (char)('0' + mode); nm[17] = (char)('a' + a->run);
        fh = dos_Open(nm, 1006);
        if (fh) { dos_Write(fh, tm_buf, tm_n * 2); dos_Close(fh); }
        FreeMem((u32)tm_buf, TM_MAX * 2);
    }
#endif
    asset_free(ev_song, ev_song_size); ev_song = 0;
    asset_free(sfx_raw, sfx_size); sfx_raw = 0;
    FreeMem(mem, arena);
    a->rc = rc; a->esc = ev_abort;
    return rc;
}

/* Entry: the first thing in the segment (AmigaDOS LoadSeg: the code starts 4 bytes after the segment pointer). */
__asm(".section .text.start,\"ax\"\n"
      ".globl ev_start\n"
      "ev_start:\n"
      "\tjmp ev_overlay_main\n"
      ".text\n");
