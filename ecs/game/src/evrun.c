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
#include "gfxglue.h"


#ifndef EV_HZ
#define EV_HZ ev_hz         /* main-loop passes per second: what a 2.8 MHz IIGS manages in each event (estimated from the 65816 cycle counts) */
#endif

/* the IIGS banks the events use, each only over the pages [lo, hi] (spans.h); the pointers are offset so that B3[0x1234] still means bank 3, offset $1234.
   A bank number the game touches outside its span would land in a neighbour: the spans were taken from long oracle runs (tools/cpu816x/workingset.py). */
#ifdef T816_REGB3
static u8 **const ev_bank_ptr[9] = {&P_B0, &P_B1, &P_B2, &P_B2, &P_B4, &P_B5, &P_B7, &P_BE0, &P_BE1};   /* [3] is a register variable: set by hand */
#else
static u8 **const ev_bank_ptr[9] = {&P_B0, &P_B1, &P_B2, &P_B3, &P_B4, &P_B5, &P_B7, &P_BE0, &P_BE1};
#endif
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
    for (i = 0; i < 9; i++) {
#ifdef T816_REGB3
        if (i == 3) B3 = (u8 *)(mem + off - (u32)ev_span_lo[i] * 256); else if (i == 2) { P_B2 = (u8 *)(mem + off - (u32)ev_span_lo[i] * 256); } else
#endif
        *ev_bank_ptr[i] = (u8 *)(mem + off - (u32)ev_span_lo[i] * 256);
        off += ((u32)ev_span_hi[i] - ev_span_lo[i] + 1) * 256;
    }
    P_BZ = (u8 *)mem + off;                                           /* scratch for banks that do not exist: only ever touched by a game bug */
}
#define EV_ARENA_BYTES (ev_banks_bytes() + C2P_TAB_BYTES + 64000UL)

static Plat *gP = (Plat *)1;           /* initialised non-zero so it lands in .data */
#ifdef WHD_TRACE
static u32 tr_pass, tr_poll, tr_text, tr_key;
#endif
static int ev_hz = 24, ev_vbl = 50, ev_abort = 0, ev_esc = 0, ev_forced = 0;
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

static int txt_on;
static void inter_text_leave(Plat *P);
/* a step of a palette fade (native.mid nat_fade_delay): the palette as it is now goes to the hardware, then the wait the original's counting loop makes (79 ms) */
void rt_fade_wait(void)
{
    Plat *P = gP;
    u16 pal[16]; int i, n;
    if (P == (Plat *)1) return;
    if (txt_on) inter_text_leave(P);
    for (i = 0; i < 16; i++) pal[i] = LE16(BE1 + 0x9E00 + 2 * i);
    set_palette(P, pal);
    n = (ev_vbl * 79 + 500) / 1000;
    for (i = 0; i < n; i++) frame_wait(P);
}

/* ---- INTER, the original's event and competition driver (bank 7 $6800-$7FFF, translated: build/inter.c with the prefix i_), and its Apple II text screens ---- */
u32 i_dispatch_call(u16 addr);
u32 dispatch_call(u16 addr);
extern u32 rt_err_got;
void inter_prodos(void) { cpu.c = 0; }          /* a ProDOS call (file loads of the scenes and the next event): they succeed, the data are in the event images already */
void inter_scenes(void) { }                     /* the scene list runner ($6F7C): the ceremony scenes are not ported */

static const u8 *ev_font;
static int txt_on = 0, btn_armed = 1;
static u8 txt_copy[1024];
static u32 inter_jb[14];
static int inter_jb_set = 0;
/* setjmp / longjmp (no C library): d2-d7, a2-a6, the stack pointer and the return address */
int inter_setjmp(u32 *jb) __attribute__((returns_twice));
void inter_longjmp(u32 *jb, int v) __attribute__((noreturn));
__asm(".text\n"
      ".globl inter_setjmp\n"
      "inter_setjmp:\n"
      "\tmove.l 4(%sp),%a0\n"
      "\tmovem.l %d2-%d7/%a2-%a6,(%a0)\n"
      "\tmove.l %sp,44(%a0)\n"
      "\tmove.l (%sp),48(%a0)\n"
      "\tmoveq #0,%d0\n"
      "\trts\n"
      ".globl inter_longjmp\n"
      "inter_longjmp:\n"
      "\tmove.l 4(%sp),%a0\n"
      "\tmove.l 8(%sp),%d0\n"
      "\tmovem.l (%a0),%d2-%d7/%a2-%a6\n"
      "\tmove.l 44(%a0),%sp\n"
      "\tmove.l 48(%a0),%a1\n"
      "\taddq.l #4,%sp\n"
      "\tjmp (%a1)\n");

/* the 40x24 text page ($E0:0400-07FF; rows are interleaved: row r at $400 + (r & 7) * $80 + (r >> 3) * $28) drawn with the shop's font in white on black:
   $80-$FF normal, $00-$3F inverse, lower case as upper case; $9E (INTER's selector under YES / NO) is a small arrow */
static void inter_render_text(Plat *P)
{
    static const u8 arrow[8] = {0x00, 0x10, 0x38, 0x7C, 0x10, 0x10, 0x00, 0x00};
    static const u16 pal[16] = {0x000, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF};
    int r, c, i;
    u8 *buf;
    P->comp = P->shown ^ 1;
    buf = P->B[P->comp];
    for (i = 0; i < 8000; i++) ((u32 *)buf)[i] = 0;
    for (i = 0; i < 1024; i++) txt_copy[i] = BE0[0x400 + i];
    for (r = 0; r < 24; r++)
        for (c = 0; c < 40; c++) {
            u8 ch = txt_copy[(r & 7) * 0x80 + (r >> 3) * 0x28 + c];
            int y = r * 8, code = ch & 0x7F, inv = ch < 0x40, k;
            if (ch == 0xA0 || ch == 0x20 || ch == 0) continue;
            if (ch == 0x9E) {
                for (k = 0; k < 8; k++) buf[(y + k) * 160 + c] = arrow[k];
                continue;
            }
            if (code < 0x20) code += 0x40;
            if (code >= 0x60) code -= 0x20;
            if (code >= 0x60) code = 0x20;
            glyph_to(buf, ev_font, c * 4, y, code, 0x1111);
            if (inv) for (k = 0; k < 8; k++) buf[(y + k) * 160 + c] ^= 0xFF;
        }
    set_palette(P, pal);
    present(P);
    txt_on = 1;
}
static void inter_text_current(Plat *P)             /* the page on screen must be the page in memory */
{
    int i;
    if (txt_on) for (i = 0; i < 1024; i++) if (txt_copy[i] != BE0[0x400 + i]) break;
    if (!txt_on || i < 1024) inter_render_text(P);
}
static void inter_text_leave(Plat *P)               /* an event switches to the super-hires screen: nothing of the text may stay */
{
    int i;
    txt_on = 0;
    P->comp = P->shown ^ 1;
    for (i = 0; i < 8000; i++) ((u32 *)P->B[P->comp])[i] = 0;
    present(P);
    c2p_all_dirty();
}
/* the Apple IIGS keyboard value INTER's screens expect: as the name entry's plus the cursor keys as Backspace / Ctrl-U */
static u8 inter_key(u8 raw)
{
    u8 k = iigs_key(raw);
    if (!k) k = raw == 0x4F ? 0x88 : raw == 0x4E ? 0x95 : 0;
    return k;
}
static void inter_esc(void)                         /* ESC in a text screen: leave INTER at once, the quit flag is set by the caller */
{
    ev_abort = 1;
    if (inter_jb_set) inter_longjmp(inter_jb, 1);
}
/* a key poll of INTER's text screens: one frame per poll */
static void inter_text_poll(Plat *P)
{
    int fire;
    inter_text_current(P);
    frame_wait(P);
    poll_keyboard(P);
    if (P->keys[0x45]) { inter_esc(); return; }
    fire = !(CIAA_PRA & 0x80);
    if (!fire) btn_armed = 1;
    io.btn[0] = (fire && btn_armed) ? 0x80 : 0;
    if (io.btn[0]) btn_armed = 0;
    {                                                /* the joystick's left and right (and the cursor keys, which FS-UAE and many keyboards give as the joystick) are INTER's arrow keys */
        static int jx_last;
        u16 j = R16(JOY1DAT);
        int jx = ((j >> 1) & 1) - ((j >> 9) & 1);
        if (P->keys[0x4F]) jx = -1;
        if (P->keys[0x4E]) jx = 1;
        if (jx && jx != jx_last) { io.key = jx < 0 ? 0x08 : 0x15; io.key_waiting = 1; io.anykey = 1; }
        jx_last = jx;
    }
    if (!io.key_waiting && P->evh != P->evt) {
        u8 k = inter_key(P->evq[P->evh]);
        P->evh = (P->evh + 1) & 15;
        if (k) { io.key = (u8)(k & 0x7F); io.key_waiting = 1; io.anykey = 1; }
    }
}
/* native.mid of INTER's timed wait ($7755): 1.4 s or a key or the button */
void rt_inter_wait(void)
{
    Plat *P = gP;
    int n, i;
    if (P == (Plat *)1) return;
    inter_text_current(P);
    n = (ev_vbl * 1400 + 500) / 1000;
    for (i = 0; i < n; i++) {
        int fire;
        frame_wait(P);
        poll_keyboard(P);
        if (P->keys[0x45]) { inter_esc(); return; }
        fire = !(CIAA_PRA & 0x80);
        if (!fire) btn_armed = 1;
        if (fire && btn_armed) { btn_armed = 0; break; }
        if (P->evh != P->evt) { P->evh = (P->evh + 1) & 15; break; }
    }
}

/* the game reads the keyboard with no key waiting both in its main loop (once or twice a pass) and in waits (pause, "press a key").
   Only a long run of reads without a pass in between is a wait: then keep the machine alive, a frame a read */
static int poll_run = 0;
void io_poll(void)
{
    Plat *P = gP;
    if (P == (Plat *)1) return;
#ifdef WHD_TRACE
    tr_poll++; if (!io.shr) tr_text++;
    if ((tr_poll & 1023) == 0) { u32 t[4]; t[0] = tr_pass; t[1] = tr_poll; t[2] = tr_text; t[3] = *(volatile u8 *)0xBFE801UL; g_os->save("trace4.bin", t, 16); }
#endif
    if (!io.shr) { inter_text_poll(P); return; }
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
#ifdef WHD_TRACE
    tr_pass++;
    if ((tr_pass & 31) == 0) { u32 t[4]; t[0] = tr_pass; t[1] = tr_poll; t[2] = tr_text; t[3] = *(volatile u8 *)0xBFE801UL; g_os->save("trace3.bin", t, 16); }
#endif
    poll_run = 0;
    if (txt_on) inter_text_leave(P);
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
#ifdef DEBUG_KEYS
        if (keys[0x55]) {                                        /* F6 (test builds): the event is over, with some scores */
            u8 t = *(volatile u8 *)0xBFE801UL; int k;
            for (k = 0; k < 6; k++) { u8 v = (u8)((t * (k + 3) + k * 37) % 100); B7[(k < 3 ? 0x7E04 : 0x7E0F - 3) + k] = (u8)(((v / 10) << 4) | (v % 10)); }
            B7[0x7E06] = 0; B7[0x7E11] = 0;
            ev_forced = 1; return 1;
        }
#endif
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
        } else {
            /* the second player's controls in the two-player events: the digit keys 1-9 as directions (the numeric pad's layout, or the top row) and Shift as the button.
               The original gets its repeat from the IIGS keyboard; here a held key is delivered once a pass */
            static const u8 pad[10] = {0x0F, 0x1D, 0x1E, 0x1F, 0x2D, 0x2E, 0x2F, 0x3D, 0x3E, 0x3F};
            int d;
            for (d = 1; d <= 9; d++) if (keys[d] || keys[pad[d]]) { io.key = (u8)(0x30 + d); io.key_waiting = 1; io.anykey = 1; break; }
        }
        io.mods = (keys[0x60] || keys[0x61]) ? 1 : 0;
    }
    return 0;
}

/* the state the event code starts in, the input and the pacing of its main loop (every run of an event, also the repeats of a competition) */
static void ev_prepare(Plat *P, int mode, u32 vbl_hz)
{
    int i;
    if (txt_on) inter_text_leave(P);
    cpu.a = cpu.x = cpu.y = 0; cpu.s = 0x1FF; cpu.d = 0; cpu.db = 0; cpu.n = cpu.v = cpu.z = cpu.c = cpu.i = 0;
    io.enabled = 1; io.key_waiting = 0; io.key = 0; io.anykey = 0; io.mods = 0; io.btn[0] = io.btn[1] = io.btn[2] = 0; io.shr = 1;
    for (i = 0; i < 4; i++) { io.pdl_len[i] = 60; io.pdl_left[i] = 0; }
    ev_hz = mode == 1 ? 29 : mode == 2 ? 27 : mode == 3 ? 11 : mode == 4 ? 10 : 16;       /* the passes a second the original manages in each event (KEGS, ecs/docs/SOURCE_GATE.md) */
    gP = P; ev_vbl = (int)vbl_hz; ev_abort = 0; ev_forced = 0;
    ev_period = (vbl_hz << 16) / (u32)ev_hz; ev_last_tod = *(volatile u8 *)0xBFE801UL; ev_now = 0; ev_due = 0;
    P->evh = P->evt;
}

#ifdef DEBUG_KEYS
static u32 dbg_t[64]; static int dbg_n;
#define DBG(v) do { if (dbg_n < 64) dbg_t[dbg_n++] = (u32)(v); } while (0)
#else
#define DBG(v) do { } while (0)
#endif
#ifndef BENCH_LOOP
static const u8 *ev_ibin;
/* INTER as the original's boot leaves it ($6908: the template at $7C50 is copied to $7E00) and the shop's players, practice and competition choices ($0768CC-$0768FE, $7E17) */
static void inter_fresh(Menu *M)
{
    int i;
    for (i = 0; i < 6144; i++) B7[0x6800 + i] = ev_ibin[i];
    for (i = 0; i < 0x1A; i++) B7[0x7E00 + i] = B7[0x7C50 + i];
    for (i = 0; i < 0x24; i++) B7[0x68A8 + i] = 0;
    for (i = 0; i < 12; i++) B7[0x68E2 + i] = 0;
    for (i = 0; i < 8; i++) { B7[0x68D2 + i] = 0; B7[0x68DA + i] = (u8)i; }
    for (i = 0; i < 128; i++) B7[0x6810 + i] = ((u8 *)M->names)[i];
    ST16(&B7[0x68CC], M->count);
    ST16(&B7[0x68D0], M->compete_all);
    for (i = 0; i < 8; i++) B7[0x68FE + i] = M->board[i];
    for (i = 0; i < 16; i++) B7[0x68EE + i] = M->colour[i];
    ST16(&B7[0x7E17], M->practice);
    for (i = 0; i < 192; i++) B7[0x7F00 + i] = M->scores[i];
}

/* run INTER from `entry`; 4 = the player pressed ESC in one of its text screens */
static __attribute__((noinline)) int run_inter(u16 entry)
{
    int rc;
    if (inter_setjmp(inter_jb) == 0) {
        inter_jb_set = 1;
        rc = rt_run(i_dispatch_call, entry);
        inter_jb_set = 0;
        return rc;
    }
    inter_jb_set = 0;
    return 4;
}

/* the event with INTER around it, as in the original: INTER prepares the player (a text screen, the scene list), jumps to the event ($031000), the event ends by a jump to
   INTER ($7E08 -> $7578) which scores it, asks "PRACTICE AGAIN?" or hands over to the next skater, the next event, the results. INTER is entered at $692D with X = the event
   and leaves by $031000 with the event it wants in $68CE: 0 (or above 5) means the whole thing is over. A competition in all events needs a different overlay for each event: then the
   state of INTER goes to the Menu and the program loads the next overlay (a->next). Returns the last rt_exit-like code (-1 for a fault). */
static int inter_session(EvArgs *a, Menu *M, Plat *P, int mode, u32 vbl_hz)
{
    int entry, ev, i;
    if (M->inter_valid) { for (i = 0; i < 6144; i++) B7[0x6800 + i] = M->inter[i]; entry = -1; }
    else { inter_fresh(M); entry = 0x692D; }
    a->next = 0;
    for (;;) {
        int rc;
        if (entry >= 0) {
            cpu.a = cpu.y = 0; cpu.x = entry == 0x692D ? (u16)mode : 0; cpu.s = 0x1FF; cpu.d = 0; cpu.db = 3; cpu.n = cpu.v = cpu.z = cpu.c = cpu.i = 0;
            io.enabled = 1; io.key_waiting = 0; io.anykey = 0; io.btn[0] = 0; io.mods = 0;
            P->evh = P->evt;
            DBG(0x1000000 | entry);
            rc = run_inter((u16)entry);
            DBG(0x2000000 | (rc << 16) | (rt_err_got & 0xFFFF)); DBG(rt_err_got);
            if (rc == 4) { B7[0x7E19] = 1; ev_esc = 1; entry = 0x7578; continue; }         /* ESC: INTER's quit path */
            if (rc != 1 || rt_err_got != 0x031000) return -1;
            ev = LE16(&B7[0x68CE]);
            if (ev == 0 || ev > 5) return 1;                                                  /* over: back to the shop */
            if (ev != mode) { a->next = ev; return 1; }                                       /* the next event's overlay goes on */
        }
        ev_prepare(P, mode, vbl_hz);
        DBG(0x3000000 | mode);
        rc = rt_run(dispatch_call, 0x1000);
        DBG(0x4000000 | (rc << 16) | (rt_err_got & 0xFFFF));
        if (ev_abort) { B7[0x7E19] = 1; ev_esc = 1; }                                         /* ESC in the event: INTER's quit flag */
        else if (!ev_forced && (rc != 1 || rt_err_got != 0x077E08)) return -1;
        entry = 0x7578;
    }
}
#endif

/* run the event (game mode a->mode): load its memory image, song and effects, then run the translated game until it ends or the player leaves it. The shop's player
   data are copied into the INTER variables the event reads and the (possibly changed) scores copied back. Called with AmigaDOS running (os_give), returns with it
   running again; a->rc = rt_exit (-1 if something could not be loaded). */
u32 dispatch_call(u16 addr);
int ev_overlay_main(EvArgs *a)
{
    static const char *const pakn[6] = {0, "ev1.pak", "ev2.pak", "ev3.pak", "ev4.pak", "ev5.pak"}, *const songn[6] = {0, "ev1.raw", "ev2.raw", "ev3.raw", "ev4.raw", "ev5.raw"};
    Plat *P = a->P; Menu *M = (Menu *)a->M; u32 vbl_hz = a->vbl_hz; int mode = a->mode;
    u32 mem, pakp, paks = 0, arena = EV_ARENA_BYTES, sfx_size = 0, ibin_size = 0;
    int i, rc;
#ifdef WHDLOAD
    g_os = (OsApi *)a->os;
#endif
    SysBase = a->SysBase; DOSBase = a->DOSBase; g_clock = a->clock; g_oldint = (u16)a->oldint;
    a->rc = -1;
#ifdef WHD_TRACE
    { u32 t[2]; t[0] = 5; t[1] = arena; g_os->save("trace5.bin", t, 8); }
#endif
    mem = AllocMem(arena, MEMF_CLEAR);
    if (!mem) return -1;
    ev_map_banks(mem);
    ev_lut = (u32 *)(mem + ev_banks_bytes());
    c2p_shadow[0] = (u8 *)ev_lut + C2P_TAB_BYTES; c2p_shadow[1] = c2p_shadow[0] + 32000;
    rt_init();
#ifdef T816_REGB3
    BK[3] = B3;
#endif
    c2p_build_lut(ev_lut);
    c2p_all_dirty();
    pakp = asset_load(pakn[mode], 0, &paks);
    if (!pakp) { FreeMem(mem, arena); return -1; }
    ev_load_pak((const u8 *)pakp);
    asset_free((const void *)pakp, paks);
    ev_song = (const u8 *)asset_load(songn[mode], MEMF_CHIP, &ev_song_size);     /* played by Paula: chip RAM */
    sfx_raw = (const u8 *)asset_load("sfx.raw", 0, &sfx_size);
#ifndef BENCH_LOOP
    if (!M->inter_valid) { ev_ibin = (const u8 *)asset_load("inter.bin", 0, &ibin_size); if (!ev_ibin) { FreeMem(mem, arena); return -1; } }
#endif
#ifdef MEMLOG
    ev_free_kb = AvailMem(MEMF_CHIP) >> 10;
#endif
#ifdef WHD_TRACE
    { u32 t[2]; t[0] = 6; t[1] = mem; g_os->save("trace6.bin", t, 8); }
#endif
    os_take(P);
    ev_font = a->font; txt_on = 0; btn_armed = 1; io.shr = 0; ev_esc = 0;
    gP = P; ev_vbl = (int)vbl_hz;
#ifdef TIMING
    tm_buf = (u16 *)AllocMem(TM_MAX * 2, 0); tm_start();
#endif
#ifdef PROFILE_PC
    pf_start();
#endif
#ifdef BENCH_LOOP
    /* test builds: the event alone, with the players INTER would have prepared for it (the timing and profile runs measure the event, not the competition) */
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
    ev_prepare(P, mode, vbl_hz);
    rc = rt_run(dispatch_call, 0x1000);
#else
    rc = inter_session(a, M, P, mode, vbl_hz);
#endif
#ifdef PROFILE_PC
    pf_stop();
#endif
    for (i = 0; i < 192; i++) M->scores[i] = B7[0x7F00 + i];
    if (a->next) { for (i = 0; i < 6144; i++) M->inter[i] = B7[0x6800 + i]; M->inter_valid = 1; }
    else {                                                           /* over: what the shop (F39) takes over from INTER: the players (sorted by their results), the points, the best scores of the events */
        M->compete_all = LE16(&B7[0x68D0]);
        for (i = 0; i < 8; i++) { M->totals[i] = B7[0x68D2 + i]; M->ord[i] = B7[0x68DA + i]; M->board[i] = B7[0x68FE + i]; }
        for (i = 0; i < 128; i++) ((u8 *)M->names)[i] = B7[0x6810 + i];
        for (i = 0; i < 16; i++) M->colour[i] = B7[0x68EE + i];
        for (i = 0; i < 36; i++) M->res_score[i] = B7[0x68A8 + i];
        for (i = 0; i < 12; i++) M->res_ord[i] = B7[0x68E2 + i];
        M->inter_valid = 0;
    }
    io.enabled = 0;
    R16(DMACON) = 0x000F;                                            /* music and effects off */
    P->stream = 0;
    /* ESC that left the event must not also quit the program: wait for the key to come up */
    if (txt_on) txt_on = 0;
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
#ifndef BENCH_LOOP
    if (ev_ibin) { asset_free(ev_ibin, ibin_size); ev_ibin = 0; }
#endif
#ifdef DEBUG_KEYS
    if (dbg_n) { u32 fh = dos_Open("PROGDIR:data/dbg.bin", 1006); if (fh) { dos_Write(fh, dbg_t, dbg_n * 4); dos_Close(fh); } dbg_n = 0; }
#endif
#ifdef WHD_TRACE
    { u32 t[4]; t[0] = tr_pass; t[1] = tr_poll; t[2] = tr_text; t[3] = (u32)ev_forced; g_os->save("trace2.bin", t, 16); }
#endif
    asset_free(ev_song, ev_song_size); ev_song = 0;
    asset_free(sfx_raw, sfx_size); sfx_raw = 0;
    FreeMem(mem, arena);
    a->rc = rc; a->esc = ev_esc;
    return rc;
}

/* Entry: the first thing in the segment (AmigaDOS LoadSeg: the code starts 4 bytes after the segment pointer). */
__asm(".section .text.start,\"ax\"\n"
      ".globl ev_start\n"
      "ev_start:\n"
#ifdef T816_REGB3
      "\tmove.l %a5,-(%sp)\n\tmove.l 8(%sp),-(%sp)\n\tjsr ev_overlay_main\n\taddq.l #4,%sp\n\tmove.l (%sp)+,%a5\n\trts\n"
#else
      "\tjmp ev_overlay_main\n"
#endif
      ".text\n");
