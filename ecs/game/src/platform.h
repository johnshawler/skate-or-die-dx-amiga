/* The platform side shared by the program and the event overlays: shared state (Plat), audio (Paula, IMA ADPCM streaming), keyboard, palette, display flip. */
#ifndef PLATFORM_H
#define PLATFORM_H
#include "os.h"
#include "ev_audio.h"
static const u8 *sfx_raw;
#define SCR_HDR 106                         /* .scr files: 10-byte header, OCS palette (16 x u16), AGA palette (16 x u32), then the bitmap */


static void copy32(u32 *d, const u32 *s, u32 n)
{
    while (n--) *d++ = *s++;
}

static void wait_vbl(void)
{
    while (!(R16(INTREQR) & 0x0020)) {}
    R16(INTREQ) = 0x0020;
}

/* everything the drawing and audio code shares (allocated with the chip RAM block, so it starts zeroed) */
typedef struct Plat {
    u8 *L;                  /* the logical screen: picture plus everything drawn into it (bank $01 on the IIGS) */
    u8 *B[2];               /* the two display buffers */
    int shown, comp;        /* which one is displayed, which one is being composed */
    u16 *cop;
    int ptidx;              /* copper list index of the bitplane pointers */
    u16 pal[16];            /* palette on screen */
    u32 pcm, blk;           /* audio: the music loop playing now, its DMA block size, */
    int nb, nblk;           /*   the next block to queue, how many blocks there are */
    u32 pcm0, len0;         /* the overworld loop, to come back to after an event */
    int sfx_wait;           /* frames until the effect channels are pointed at silence */
    u16 sper, mper0;        /* Paula periods: effects (22050 Hz), the overworld loop */
    u8 silent[8];
    int stream;             /* the music is IMA ADPCM decoded into the ring on the fly (the title song) */
    const u8 *st_data; u32 st_blk, st_total, st_loop;
    u8 ring[8192];
    u8 sfx[SFX_CAP];        /* chip RAM copy of the effect being played */
    u8 keys[128];
    u8 evq[16];             /* raw key-down codes waiting for the name entry */
    int evh, evt;
} Plat;

/* ---- IMA ADPCM streaming (the title song is 140 s: too long to keep as 8-bit samples) ---- */
static const s16 ima_step[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,
    230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,
    4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const s8 ima_adj[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* The decoder runs on tables built once: ima_delta[state + code] = the signed change of the predictor, ima_next[state + code] = the next state (step index * 16).
   Same arithmetic as the reference decoder (step >> 3, + step, + step >> 1, + step >> 2; predictor clamped to 16 bits; index clamped to 0..88). */
static s16 ima_delta[89 * 16];
static u16 ima_next[89 * 16];
static int ima_ready;
static void ima_build(void)
{
    int i, c;
    for (i = 0; i < 89; i++)
        for (c = 0; c < 16; c++) {
            int step = ima_step[i], d = step >> 3, n = i + ima_adj[c & 7];
            if (c & 4) d += step;
            if (c & 2) d += step >> 1;
            if (c & 1) d += step >> 2;
            ima_delta[i * 16 + c] = (s16)((c & 8) ? -d : d);
            ima_next[i * 16 + c] = (u16)((n < 0 ? 0 : n > 88 ? 88 : n) * 16);
        }
    ima_ready = 1;
}

static void ima_block(const u8 *src, s8 *dst)               /* 1024 samples from 4 + 512 bytes */
{
    int pred = (s16)(src[0] | (src[1] << 8)), i;
    u32 st = (u32)src[2] * 16;
    const s16 *dl = ima_delta;
    const u16 *nx = ima_next;
    if (!ima_ready) ima_build();
    src += 4;
    for (i = 0; i < 512; i++) {
        u32 b = src[i], c = b & 15;
        pred += dl[st + c]; st = nx[st + c];
        if ((u32)(pred + 32768) > 65535u) pred = pred < 0 ? -32768 : 32767;
        *dst++ = (s8)(pred >> 8);
        c = b >> 4;
        pred += dl[st + c]; st = nx[st + c];
        if ((u32)(pred + 32768) > 65535u) pred = pred < 0 ? -32768 : 32767;
        *dst++ = (s8)(pred >> 8);
    }
}

static void stream_fill(Plat *P, u8 *dst)                   /* one DMA block */
{
    u32 n = P->blk / 1024, i;
    for (i = 0; i < n; i++) {
        ima_block(P->st_data + 516UL * P->st_blk, (s8 *)(dst + 1024 * i));
        if (++P->st_blk >= P->st_total) P->st_blk = P->st_loop;
    }
}

/* ---- audio: when a DMA block finishes, queue the next one ---- */
static void audio_service(Plat *P)
{
    int i;
    if (R16(INTREQR) & 0x0180) {
        R16(INTREQ) = 0x0180;
        if (P->stream) stream_fill(P, (u8 *)(P->pcm + P->blk * (u32)P->nb));
        for (i = 0; i < 2; i++) R32(AUDLC(i)) = P->pcm + P->blk * (u32)P->nb;
        P->nb = P->nb + 1 >= P->nblk ? 0 : P->nb + 1;
    }
    if (P->sfx_wait && --P->sfx_wait == 0)                  /* the effect has started: when it ends the channels loop on silence */
        for (i = 2; i < 4; i++) { R32(AUDLC(i)) = (u32)P->silent; R16(AUDLEN(i)) = 2; }
}

static void frame_wait(Plat *P)
{
    wait_vbl();
    audio_service(P);
}

/* (re)start a looped 8-bit mono sample on channels 0 (left) and 1 (right), played in blocks of at most 128 KB (a DMA length is 16 bits of words) */
static void music_start(Plat *P, u32 pcm, u32 len, u16 per, int vol)
{
    u32 nb = (len + 131069) / 131070, blk;
    int i;
    if (nb < 2) nb = 2;
    blk = (len / nb) & ~1UL;
    R16(DMACON) = 0x0003;
    P->pcm = pcm; P->blk = blk; P->nblk = (int)nb; P->stream = 0;
    for (i = 0; i < 2; i++) { R32(AUDLC(i)) = pcm; R16(AUDLEN(i)) = (u16)(blk / 2); R16(AUDPER(i)) = per; R16(AUDVOL(i)) = (u16)vol; }
    R16(INTREQ) = 0x0180;
    R16(DMACON) = 0x8003;
    wait_vbl(); wait_vbl(); wait_vbl();                         /* let the channels latch block 0 */
    R16(INTREQ) = 0x0180;
    for (i = 0; i < 2; i++) R32(AUDLC(i)) = pcm + blk;
    P->nb = 2 >= (int)nb ? 0 : 2;
}

static void music_stop(void)
{
    R16(DMACON) = 0x0003;
}

/* play a sound effect (NES sound id) on channels 2 and 3, which Paula sends right and left: heard in the middle */
static void sfx_play(Plat *P, int id)
{
    u32 off, len;
    int i;
    if (id < SFX_FIRST || id >= SFX_FIRST + SFX_COUNT) return;
    off = sfx_tab[id - SFX_FIRST][0]; len = sfx_tab[id - SFX_FIRST][1];
    if (len < 4 || len > SFX_CAP) return;
    R16(DMACON) = 0x000C;
    copy32((u32 *)P->sfx, (const u32 *)(sfx_raw + off), len / 4);
    for (i = 2; i < 4; i++) { R32(AUDLC(i)) = (u32)P->sfx; R16(AUDLEN(i)) = (u16)(len / 2); R16(AUDPER(i)) = P->sper; R16(AUDVOL(i)) = 64; }
    R16(DMACON) = 0x800C;
    P->sfx_wait = 2;
}

/* ---- keyboard (raw CIA poll; interrupts are off while we own the machine) ---- */
static void poll_keyboard(Plat *P)
{
    u8 icr = CIAA_ICR;
    if (!(icr & 0x08)) return;
    u8 k = CIAA_SDR;
    CIAA_CRA |= 0x40;                      /* handshake: hold SP low for >= 85us */
    u16 l0 = R16(VHPOSR) >> 8;
    int lines = 0;
    while (lines < 3) { u16 l = R16(VHPOSR) >> 8; if (l != l0) { l0 = l; lines++; } }
    CIAA_CRA &= (u8)~0x40;
    k = (u8)~k;
    k = (u8)((k >> 1) | (k << 7));
    P->keys[k & 0x7F] = (k & 0x80) ? 0 : 1;
    if (!(k & 0x80)) {                     /* key down: remember it for the name entry */
        int n = (P->evt + 1) & 15;
        if (n != P->evh) { P->evq[P->evt] = (u8)(k & 0x7F); P->evt = n; }
    }
}

/* the Apple IIGS keyboard value ($E0C000) the original expects for an Amiga raw key code; 0 for keys it ignores */
static u8 iigs_key(u8 raw)
{
    static const char row_q[] = "qwertyuiop", row_a[] = "asdfghjkl", row_z[] = "zxcvbnm";
    int c = 0;
    if (raw >= 0x01 && raw <= 0x09) return (u8)(0xB0 + raw);           /* 1..9 */
    if (raw == 0x0A) return 0xB0;                                       /* 0 */
    if (raw == 0x0B) return 0xAD;                                       /* - */
    if (raw >= 0x10 && raw <= 0x19) c = row_q[raw - 0x10];
    else if (raw >= 0x20 && raw <= 0x28) c = row_a[raw - 0x20];
    else if (raw >= 0x31 && raw <= 0x37) c = row_z[raw - 0x31];
    if (c) return (u8)(0x80 | c);
    if (raw == 0x40) return 0xA0;                                       /* space */
    if (raw == 0x44) return 0x8D;                                       /* Return */
    if (raw == 0x41) return 0x88;                                       /* Backspace */
    if (raw == 0x46) return 0xFF;                                       /* Del */
    return 0;
}

/* ---- palette (copper list: AGA-safe, high nibbles then low nibbles) ---- */
static void set_palette(Plat *P, const u16 *pal)
{
    int i;
    for (i = 0; i < 16; i++) {
        P->pal[i] = pal[i];
        P->cop[3 + i * 2] = pal[i];
        P->cop[37 + i * 2] = pal[i];
    }
}

/* the original's fades ($1DDE out, $1DB1 in): every pass moves each colour component one step, 17 passes, about 30 ms each */
static void fade_step_out(Plat *P)
{
    u16 p[16]; int i;
    for (i = 0; i < 16; i++) {
        u16 v = P->pal[i], r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        if (r) r--; if (g) g--; if (b) b--;
        p[i] = (u16)((r << 8) | (g << 4) | b);
    }
    set_palette(P, p);
}
static void fade_step_in(Plat *P, const u16 *target)
{
    u16 p[16]; int i;
    for (i = 0; i < 16; i++) {
        u16 v = P->pal[i], t = target[i];
        u16 r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        if (r != ((t >> 8) & 15)) r++;
        if (g != ((t >> 4) & 15)) g++;
        if (b != (t & 15)) b++;
        p[i] = (u16)((r << 8) | (g << 4) | b);
    }
    set_palette(P, p);
}

static void present(Plat *P)
{
    int i;
    P->shown = P->comp;
    for (i = 0; i < 4; i++) {
        u32 a = (u32)P->B[P->shown] + i * 40;
        P->cop[P->ptidx + i * 4 + 1] = (u16)(a >> 16);
        P->cop[P->ptidx + i * 4 + 3] = (u16)a;
    }
}


/* an 8-bit looped song file (u16 sample rate, u16 0, samples) played from chip RAM on channels 0+1 */
static u32 g_clock = 3546895;                               /* Paula's clock: PAL; 3579545 on NTSC machines */
static void raw_music_start(Plat *P, const u8 *file, u32 size, int vol)
{
    u32 rate = ((u32)file[0] << 8) | file[1];
    music_start(P, (u32)(file + 4), (size - 4) & ~1UL, (u16)(g_clock / rate), vol);
}

/* what the program hands to an event overlay (data/<event>.ovl) */
typedef struct EvArgs {
    Plat *P; void *M;
    u32 SysBase, DOSBase;
    u32 oldint, clock;
    u32 vbl_hz;
    int mode;
    int rc;
    const u8 *font;                                          /* the shop's font (menu.fnt): INTER's text screens */
    int next;                                               /* out: the competition goes on in this event's overlay (state in Menu.inter); 0 = finished */
    int run, esc;                                           /* test builds: which run of a looped event this is; the player pressed ESC */
} EvArgs;

/* ---- the display and the OS: we own the machine while playing; AmigaDOS is let in between to read files ---- */
static u16 g_oldint = 0;
#ifndef DISPLAY_FMODE
#define DISPLAY_FMODE 0          /* 16-bit bitplane fetch (64-bit fetch was tried in FS-UAE: no measurable gain, so the safe setting stays) */
#define DISPLAY_DDFSTOP 0x00D0
#endif
static void hw_display(Plat *P)
{
    R16(BPLCON0) = 0x4201; R16(BPLCON1) = 0; R16(BPLCON2) = 0; R16(BPLCON4) = 0; R16(FMODE) = DISPLAY_FMODE;
    R16(BPL1MOD) = 120; R16(BPL2MOD) = 120;
    R16(DIWSTRT) = 0x2C81; R16(DIWSTOP) = 0xF4C1;
    R16(DDFSTRT) = 0x0038; R16(DDFSTOP) = DISPLAY_DDFSTOP;
    R32(COP1LC) = (u32)P->cop;
    R16(COPJMP1) = 0;
}
static void os_give(void)                                   /* let AmigaDOS run: interrupts on, multitasking on; our picture stays on the screen */
{
    R16(INTENA) = 0x8000 | g_oldint;
    R16(DMACON) = 0x8000 | 0x0010;                          /* disk DMA for floppies */
    Permit();
}
static void os_take(Plat *P)                                /* the machine is ours again: no interrupts, our display and audio */
{
    int i;
    Forbid();
    R16(INTENA) = 0x7FFF; R16(INTREQ) = 0x7FFF; R16(INTREQ) = 0x7FFF;
    R16(DMACON) = 0x7FFF;
    hw_display(P);
    R16(DMACON) = 0x8000 | 0x0200 | 0x0100 | 0x0080;        /* DMAEN | BPLEN | COPEN */
    for (i = 0; i < 128; i++) P->keys[i] = 0;
    P->evh = P->evt;
}
#endif
