/* Events: an event overlay of the original game, translated from 65816 to C (tools/t816), running on a model of the IIGS memory.
 * This file is the platform side: it owns the memory banks, loads the event's initial memory image, converts the displayed IIGS
 * screen ($E1:2000, 4 bits per pixel) to Amiga bitplanes, maps joystick and keys to the soft switches the game reads, and paces the
 * game's main loop. The game calls rt_pass() once per main-loop pass. */
#ifndef EVPLAT_H
#define EVPLAT_H

#include "m816.h"
#include "c2p.h"

#ifndef EV_HZ
#define EV_HZ ev_hz         /* main-loop passes per second: what a 2.8 MHz IIGS manages in each event (estimated from the 65816 cycle counts) */
#endif
#define EV_BANKS 10          /* banks 0-5, 7, E0, E1 and a scratch bank standing in for the ones that do not exist */
#define EV_BYTES (EV_BANKS * 65536UL)

static Plat *gP = (Plat *)1;           /* initialised non-zero so it lands in .data */
static int ev_hz = 24, ev_acc = 0, ev_vbl = 50, ev_frames = 1, ev_abort = 0;
#ifdef FPS_ON
static int fps_on = 1, fps_frames = 0, fps_passes = 0, fps_shown = 0, fps_f10 = 0;
#else
static int fps_on = 0, fps_frames = 0, fps_passes = 0, fps_shown = 0, fps_f10 = 0;      /* F9: show how many passes a second the machine manages */
#endif
static u32 *ev_lut = (u32 *)1;

static void ev_load_pak(const u8 *p)
{
    u32 n = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7], i, k;
    p += 8;
    for (i = 0; i < n; i++) {
        u32 a = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        u32 len = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7];
        u8 *d = BK[(a >> 16) & 255] + (a & 0xFFFF);
        p += 8;
        for (k = 0; k < len; k++) d[k] = p[k];
        p += len + (len & 1);
    }
}

/* the event's song (copied to chip RAM for the length of the event) and the platform's side of the sound driver in INTER bank 7:
   $8003 = play effect A (the IIGS driver's effect numbers are used as NES sound ids), $8006 = start the event's music, $8009 = stop it */
static u32 ev_mbuf = 0, ev_mlen = 0;
u32 rt_jsl(u32 target)
{
    Plat *P = gP;
    if (P == (Plat *)1) return 0;
    switch (target) {
    case 0x078003: sfx_play(P, (int)(cpu.a & 0xFF)); break;
    case 0x078006: if (ev_mbuf) music_start(P, ev_mbuf, ev_mlen, P->mper0 == 161 ? 222 : 224, 48); break;      /* 16 kHz */
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
    c2p_collect();
    c2p_convert(ev_lut, BE1 + 0x2000, P->B[P->comp], P->comp);
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
    present(P);
    /* pacing: EV_HZ passes a second */
#ifdef EV_UNPACED                                                   /* speed test: no pacing; count 50/60 Hz ticks of CIA-A's time-of-day clock */
    { static u8 last_tod = 0; u8 t = *(volatile u8 *)0xBFE801UL; fps_frames += (u8)(t - last_tod); last_tod = t; }
    audio_service(P);
#else
    do {
        frame_wait(P);
        ev_acc += EV_HZ;
        fps_frames++;
    } while (ev_acc < ev_vbl);
    ev_acc -= ev_vbl;
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
        io.pdl_len[0] = jx < 0 ? 0 : jx > 0 ? 300 : 60;        /* the paddle timers the original joystick routine measures */
        io.pdl_len[1] = jy < 0 ? 0 : jy > 0 ? 300 : 60;
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

/* run an event overlay (game mode `mode`, memory image `pak`) until it ends or the player leaves it; the shop's player data are
   copied into the INTER variables the event reads and the (possibly changed) scores copied back. Returns rt_exit. */
static int ev_run(Plat *P, Menu *M, const u8 *pak, u32 (*dispatch)(u16), u32 vbl_hz, int mode)
{
    u32 mem = AllocMem(EV_BYTES + C2P_LUT_LONGS * 4, MEMF_CLEAR);
    int i, rc;
    if (!mem) return -1;
    P_B0 = (u8 *)mem; P_B1 = P_B0 + 65536; P_B2 = P_B1 + 65536; P_B3 = P_B2 + 65536; P_B4 = P_B3 + 65536; P_B5 = P_B4 + 65536;
    P_B7 = P_B5 + 65536; P_BE0 = P_B7 + 65536; P_BE1 = P_BE0 + 65536; P_BZ = P_BE1 + 65536;      /* P_BZ: scratch for banks that do not exist */
    ev_lut = (u32 *)(mem + EV_BYTES);
    rt_init();
    c2p_build_lut(ev_lut);
    c2p_all_dirty();
    ev_load_pak(pak);
    music_stop();                                                    /* the overworld loop; the event starts its own song */
    {
        const u8 *ms = mode == 1 ? ev_song1 : mode == 2 ? ev_song2 : mode == 3 ? ev_song3 : mode == 4 ? ev_song4 : 0;
        ev_mlen = mode == 1 ? (u32)(ev_song1_end - ev_song1) : mode == 2 ? (u32)(ev_song2_end - ev_song2) :
                  mode == 3 ? (u32)(ev_song3_end - ev_song3) : mode == 4 ? (u32)(ev_song4_end - ev_song4) : 0;
        ev_mbuf = ms ? AllocMem(ev_mlen, MEMF_CHIP) : 0;
        if (ev_mbuf) copy32((u32 *)ev_mbuf, (const u32 *)ms, ev_mlen / 4);
    }
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
    ev_hz = mode == 1 ? 29 : mode == 2 ? 26 : mode == 3 ? 12 : 11;
    gP = P; ev_vbl = (int)vbl_hz; ev_acc = 0; ev_abort = 0;
    P->evh = P->evt;
    rc = rt_run(dispatch, 0x1000);
    for (i = 0; i < 192; i++) M->scores[i] = B7[0x7F00 + i];
    FreeMem(mem, EV_BYTES + C2P_LUT_LONGS * 4);
    io.enabled = 0;
    R16(DMACON) = 0x000C;                                            /* effects off */
    music_start(P, P->pcm0, P->len0, P->mper0, 64);                  /* back to the overworld loop */
    if (ev_mbuf) FreeMem(ev_mbuf, ev_mlen);
    ev_mbuf = 0;
    /* ESC that left the event must not also quit the program: wait for the key to come up */
    for (i = 0; i < 200; i++) { poll_keyboard(P); if (!P->keys[0x45]) break; frame_wait(P); }
    P->evh = P->evt;
    return rc;
}

#endif
