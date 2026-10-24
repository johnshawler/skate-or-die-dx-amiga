/* Skate or Die overworld demo for Amiga (68020+, OCS/ECS/AGA).
 * Opens in Rodney's skate shop (point the sign at SIGN-UP, PRACTICE, COMPETE and the other hotspots), with the sign-up list, name entry,
 * board colour and high-score screens; going out onto the map gives the skater you steer around the overworld, and the NES overworld
 * track loops on Paula. Freestyle C: no C library. Talks to AmigaOS only to open graphics.library, allocate chip RAM and
 * hand the display back on exit; everything else is direct hardware access.
 * Controls: joystick in port 2 or cursor keys to move, fire or SPACE/Return to select, letters/digits/Backspace/Return in the name
 * entry, ESC to quit (Q too, except while typing a name). */

#include "os.h"
#include "skater.h"
#include "platform.h"
#define TICK_HZ 25   /* original main-loop passes per second (about 107,000 IIGS cycles per pass at 2.8 MHz; an estimate) */

/* ---- assets: files of the data folder, loaded as needed ---- */
static const u8 *title_scr, *title_ima, *map_scr, *shop_scr, *list_scr, *scores_scr, *board_scr, *menu_fnt, *skater_spr, *ow_ima;
static const u8 *pictures[5];                                 /* map, shop, list, board, scores: indexed by PIC_* */
static u32 title_ima_size, base_size[8];

#include "menu.h"
#include "gfxglue.h"

/* ---- drawing: masked, shifted blit of one planar sprite part into the interleaved screen ---- */
static void blit_part(u8 *screen, int idx, int x, int y)
{
    const u8 *rec = skater_spr + 6 + idx * 16;
    int rows = *(const u16 *)(rec + 2);
    int ww = *(const u16 *)(rec + 4);
    const u8 *body = skater_spr + 6 + (*(const u16 *)(skater_spr + 4)) * 16;
    const u8 *img = body + *(const u32 *)(rec + 8);
    const u8 *msk = body + *(const u32 *)(rec + 12);
    int s = x & 15, dxw = x >> 4;
    for (int r = 0; r < rows; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= 200) continue;
        const u16 *mrow = (const u16 *)(msk + r * ww * 2);
        for (int p = 0; p < 4; p++) {
            const u16 *srow = (const u16 *)(img + r * ww * 8 + p * ww * 2);
            u16 *d = (u16 *)(screen + yy * 160 + p * 40);
            u16 ps = 0, pm = 0;
            for (int j = 0; j <= ww; j++) {
                u16 cs = (j < ww) ? srow[j] : 0;
                u16 cm = (j < ww) ? mrow[j] : 0;
                int wx = dxw + j;
                if (wx >= 0 && wx < 20) {
                    u16 m = (u16)((((u32)pm << 16) | cm) >> s);
                    u16 sv = (u16)((((u32)ps << 16) | cs) >> s);
                    d[wx] = (u16)((d[wx] & ~m) | (sv & m));
                }
                ps = cs;
                pm = cm;
            }
        }
    }
}

static void draw_skater(u8 *screen, const int *frames, int px, int py)
{
    static const u8 order[4] = {3, 2, 1, 0};            /* shadow, board, legs, torso (frames[] is torso, legs, board, shadow) */
    for (int i = 0; i < 4; i++) {
        int idx = frames[order[i]];
        u16 hot = *(const u16 *)(skater_spr + 6 + idx * 16 + 6);
        blit_part(screen, idx, px + (hot & 255), py + (hot >> 8));
    }
}

/* ---- the menu code's drawing hooks ---- */
static void p_glyph(void *ctx, int xb, int y, int code, u16 mask)
{
    Plat *P = (Plat *)ctx;
    glyph_to(P->L, menu_fnt, xb, y, code, mask);
    glyph_to(P->B[P->comp], menu_fnt, xb, y, code, mask);
}

static const u16 *p_picture(void *ctx, int which)
{
    Plat *P = (Plat *)ctx;
    const u8 *scr = pictures[which];
    const u16 *pal = (const u16 *)(scr + 10);
    int i, black = 1;
    for (i = 0; i < 16; i++) if (P->pal[i]) black = 0;
    if (!black) for (i = 0; i < 17; i++) { fade_step_out(P); frame_wait(P); frame_wait(P); }
    copy32((u32 *)P->L, (const u32 *)(scr + SCR_HDR), SCREEN_BYTES / 4);
    copy32((u32 *)P->B[P->comp], (const u32 *)(scr + SCR_HDR), SCREEN_BYTES / 4);
    present(P);
    for (i = 0; i < 17; i++) { fade_step_in(P, pal); frame_wait(P); frame_wait(P); }
    return pal;
}

static void p_palette(void *ctx, const u16 *pal)
{
    set_palette((Plat *)ctx, pal);
}

/* ---- the files the shop and the map need (screens, font, skater sprites, the overworld song): loaded with AmigaDOS running ---- */
static const char *const base_names[8] = {"map.scr", "shop.scr", "list.scr", "board.scr", "scores.scr", "menu.fnt", "skater.spr", "ovw.ima"};
static const u8 **const base_ptr[8] = {&pictures[0], &pictures[1], &pictures[2], &pictures[3], &pictures[4], &menu_fnt, &skater_spr, &ow_ima};
static int ow_load_base(void)
{
    int i, ok = 1;
    for (i = 0; i < 8; i++) {
        *base_ptr[i] = (const u8 *)asset_load(base_names[i], 0, &base_size[i]);
        if (!*base_ptr[i]) ok = 0;
    }
    return ok;
}
static void ow_free_base(void)
{
    int i;
    for (i = 0; i < 8; i++) { asset_free(*base_ptr[i], base_size[i]); *base_ptr[i] = 0; }
}
/* the overworld song: IMA ADPCM, streamed through the ring like the title song */
static void ow_music_start(Plat *P)
{
    const u8 *d = ow_ima;
    if (!d) return;
    P->st_data = d + 8;
    P->st_total = ((u32)d[0] << 24) | ((u32)d[1] << 16) | ((u32)d[2] << 8) | d[3];
    P->st_loop = ((u32)d[4] << 24) | ((u32)d[5] << 16) | ((u32)d[6] << 8) | d[7];
    P->st_blk = 0;
    music_stop();
    P->blk = 4096;
    stream_fill(P, P->ring); stream_fill(P, P->ring + 4096);
    music_start(P, (u32)P->ring, 8192, P->mper0, 64);           /* 22 kHz */
    P->stream = 1;
}

/* play event `mode` (1 ramp, 2 high jump, 3 downhill race, 4 downhill jam): free what the shop and map hold, load the event overlay (data/<name>.ovl, one event's
   code at a time) and run it; it loads its own image, song and effects. Returns the overlay's result, -1 if it could not be loaded. */
static int run_event(Plat *P, Menu *M, int mode, u32 vbl_hz)
{
    static const char *const ovl[5] = {0, "PROGDIR:data/ramp.ovl", "PROGDIR:data/hj.ovl", "PROGDIR:data/race.ovl", "PROGDIR:data/jam.ovl"};
    EvArgs a;
    u32 seg;
    int rc;
    music_stop(); R16(DMACON) = 0x000F; P->stream = 0;
    os_give();
    ow_free_base();
    seg = dos_LoadSeg(ovl[mode]);
    a.P = P; a.M = M; a.SysBase = SysBase; a.DOSBase = DOSBase; a.oldint = g_oldint; a.vbl_hz = vbl_hz; a.mode = mode; a.rc = -1;
    if (seg) {
        ((int (*)(EvArgs *))((seg << 2) + 4))(&a);
        dos_UnLoadSeg(seg);
    }
    rc = seg ? a.rc : -1;
    ow_load_base();
    os_take(P);
    ow_music_start(P);
    return rc;
}

/* Entry point (first thing in the hunk): preserve the registers AmigaDOS may rely on, run the C code, return its result in d0. */
__asm(".section .text.start,\"ax\"\n"
      ".globl start\n"
      "start:\n"
      "\tmovem.l %d2-%d7/%a2-%a6,-(%sp)\n"
      /* run on a 64 KB stack of our own: the default CLI stack (4 KB) is too small for the translated game code */
      "\tmove.l 4.w,%a6\n"
      "\tmove.l #65536,%d0\n"
      "\tmove.l #0x10001,%d1\n"
      "\tjsr -198(%a6)\n"
      "\ttst.l %d0\n"
      "\tbeq.s 1f\n"
      "\tmove.l %d0,%a2\n"
      "\tlea 65536(%a2),%a0\n"
      "\tmove.l %sp,-(%a0)\n"
      "\tmove.l %a2,-(%a0)\n"
      "\tmove.l %a0,%sp\n"
      "\tjsr ow_main\n"
      "\tmove.l %d0,%d2\n"
      "\tmove.l (%sp)+,%a1\n"
      "\tmove.l (%sp)+,%sp\n"
      "\tmove.l 4.w,%a6\n"
      "\tmove.l #65536,%d0\n"
      "\tjsr -210(%a6)\n"
      "\tmove.l %d2,%d0\n"
      "\tbra.s 2f\n"
      "1:\tjsr ow_main\n"
      "2:\tmovem.l (%sp)+,%d2-%d7/%a2-%a6\n"
      "\trts\n"
      ".text\n");

/* the title screen (the original's start-up stub: picture F52, song 0, wait for a key or the button, then the shop). Returns 1 to quit. */
static int title_screen(Plat *P)
{
    const u8 *scr = title_scr, *d = title_ima;
    int i;
    P->st_data = d + 8;
    P->st_total = ((u32)d[0] << 24) | ((u32)d[1] << 16) | ((u32)d[2] << 8) | d[3];
    P->st_loop = ((u32)d[4] << 24) | ((u32)d[5] << 16) | ((u32)d[6] << 8) | d[7];
    P->st_blk = 0;
    music_stop();
    P->blk = 4096;                                              /* two DMA blocks in the ring; fill both before starting */
    stream_fill(P, P->ring); stream_fill(P, P->ring + 4096);
    P->comp = P->shown ^ 1;
    copy32((u32 *)P->L, (const u32 *)(scr + SCR_HDR), SCREEN_BYTES / 4);
    copy32((u32 *)P->B[P->comp], (const u32 *)(scr + SCR_HDR), SCREEN_BYTES / 4);
    present(P);
    set_palette(P, (const u16 *)(scr + 10));
    music_start(P, (u32)P->ring, 8192, P->mper0 == 161 ? 222 : 224, 64);       /* 16 kHz */
    P->stream = 1;
    P->evh = P->evt;
    for (;;) {
        frame_wait(P);
        poll_keyboard(P);
        if (P->keys[0x45]) return 1;
        if (P->evh != P->evt || !(CIAA_PRA & 0x80)) break;
    }
    P->stream = 0;
    music_stop();
    for (i = 0; i < 100; i++) {                                 /* let go of the button and keys before the shop reads them */
        frame_wait(P); poll_keyboard(P);
        if ((CIAA_PRA & 0x80) && !P->keys[0x40] && !P->keys[0x44]) break;
    }
    P->evh = P->evt;
    return 0;
}

__attribute__((noinline)) int ow_main(void)
{
    SysBase = *(volatile u32 *)4;
    u32 total = SCREEN_BYTES * 3 + COP_BYTES + sizeof(Plat) + sizeof(Menu) + 16;     /* Plat holds the effect buffer and the stream ring */
    u32 mem = AllocMem(total, MEMF_CHIP | MEMF_CLEAR);
    if (!mem) return 20;
    Plat *P = (Plat *)(mem + SCREEN_BYTES * 3 + COP_BYTES);
    Menu *M = (Menu *)((u32)P + sizeof(Plat));
    P->L = (u8 *)mem;
    P->B[0] = (u8 *)(mem + SCREEN_BYTES);
    P->B[1] = (u8 *)(mem + SCREEN_BYTES * 2);
    P->cop = (u16 *)(mem + SCREEN_BYTES * 3);
    u32 gb = OpenLibrary("graphics.library", 0);
    DOSBase = OpenLibrary("dos.library", 0);
    if (!gb || !DOSBase) { FreeMem(mem, total); return 20; }
    int i;

    /* the data folder: everything the shop and the map need, and the title picture and song (loaded while the OS runs) */
    u32 title_scr_size = 0;
    title_scr = (const u8 *)asset_load("title.scr", 0, &title_scr_size);
    title_ima = (const u8 *)asset_load("title.ima", 0, &title_ima_size);
    if (!title_scr || !title_ima || !ow_load_base()) {
        asset_free(title_scr, title_scr_size); asset_free(title_ima, title_ima_size); ow_free_base();
        CloseLibrary(DOSBase); CloseLibrary(gb); FreeMem(mem, total);
        return 10;                                              /* the data folder is missing or incomplete */
    }

    /* copper list: AGA-safe palette (hi then lo nibbles), then bitplane pointers; the palette starts black and is faded in by the first picture */
    u16 *cop = P->cop;
    int n = 0;
    cop[n++] = BPLCON3; cop[n++] = 0x0C20;                      /* bit 5 blanks the border: it stays black whatever colour 0 is */
    for (i = 0; i < 16; i++) { cop[n++] = 0x180 + i * 2; cop[n++] = 0; }
    cop[n++] = BPLCON3; cop[n++] = 0x0E20;
    for (i = 0; i < 16; i++) { cop[n++] = 0x180 + i * 2; cop[n++] = 0; }
    cop[n++] = BPLCON3; cop[n++] = 0x0C20;
    P->ptidx = n;
    for (i = 0; i < 8; i++) { cop[n++] = 0x0E0 + i * 2; cop[n++] = 0; }
    cop[n++] = 0xFFFF; cop[n++] = 0xFFFE;
    P->shown = 0; P->comp = 0;
    present(P);

    /* take over the machine */
    u32 oldview = *(volatile u32 *)(gb + 34);
    u32 oldcop = *(volatile u32 *)(gb + 38);
    u8 pal_hz = *(volatile u8 *)(SysBase + 0x212);
    LoadView(gb, 0);
    WaitTOF(gb);
    WaitTOF(gb);
    Forbid();
    u16 olddma = R16(DMACONR) & 0x07FF;
    u16 oldint = R16(INTENAR) & 0x7FFF;
    g_oldint = oldint;
    R16(INTENA) = 0x7FFF; R16(INTREQ) = 0x7FFF; R16(INTREQ) = 0x7FFF;
    R16(DMACON) = 0x7FFF;
    hw_display(P);

    /* audio: the music streams through a ring on channels 0+1 (left+right); effects use channels 2+3 */
    P->mper0 = pal_hz == 50 ? 161 : 162;                        /* 22050 Hz */
    P->sper = P->mper0;
    R16(DMACON) = 0x8000 | 0x0200 | 0x0100 | 0x0080;            /* DMAEN | BPLEN | COPEN */

    MenuGfx gfx;
    gfx.ctx = P; gfx.glyph = p_glyph; gfx.picture = p_picture; gfx.palette = p_palette;
    M->rng0 = 0x5A; M->rng1 = 0xA5;
    M->f60 = 1;                                                 /* the game opens in Rodney's shop */

    Skater sk;
    sk_reset(&sk);
    int map_mode = 0, notice = 0;
    int tick_acc = 0, quit = 0;
    int vbl_hz = pal_hz == 60 ? 60 : 50;
    if (title_screen(P)) quit = 1;
    asset_free(title_ima, title_ima_size); title_ima = 0;       /* 1.1 MB: only the title needs it */
    asset_free(title_scr, title_scr_size); title_scr = 0;
    ow_music_start(P);

    while (!quit) {
        poll_keyboard(P);
        poll_keyboard(P);
        u16 j = R16(JOY1DAT);
        int jx = ((j >> 1) & 1) - ((j >> 9) & 1);
        int jy = (((j >> 1) ^ j) & 1) - (((j >> 9) ^ (j >> 8)) & 1);
        int fire = !(CIAA_PRA & 0x80);
        u8 *keys = P->keys;
        if (keys[0x4F]) jx = -1;
        if (keys[0x4E]) jx = 1;
        if (keys[0x4C]) jy = -1;
        if (keys[0x4D]) jy = 1;
        if (M->state != MS_NAME && (keys[0x40] || keys[0x44])) fire = 1;
        if (keys[0x45] || (keys[0x10] && M->state != MS_NAME)) quit = 1;
        if ((keys[0x50] || keys[0x51] || keys[0x52] || keys[0x53]) && !(M->state == MS_NAME)) {    /* F1-F4: go straight to an event (testing) */
            int ev = keys[0x50] ? 1 : keys[0x51] ? 2 : keys[0x52] ? 3 : 4;
            keys[0x50] = keys[0x51] = keys[0x52] = keys[0x53] = 0;
            M->practice = 1;
            if (run_event(P, M, ev, (u32)vbl_hz) < 0) notice = 90;
            map_mode = 0; menu_enter_shop(M);
            continue;
        }

        /* the game's control byte: up accelerates, down brakes, left/right turn; bit clear = pressed */
        int ctl = 0xFF;
        if (jy < 0) ctl &= ~1;
        if (jy > 0) ctl &= ~2;
        if (jx < 0) ctl &= ~4;
        if (jx > 0) ctl &= ~8;
        if (fire) ctl &= ~0x10;

        int tick = 0;
        tick_acc += TICK_HZ;
        if (tick_acc >= vbl_hz) { tick_acc -= vbl_hz; tick = 1; }

        if (!map_mode) {
            if (tick) {
                /* one pass of the shop code: the sprite engine paints the cursor sign from last pass's shadow slot, then the logic runs */
                u32 kbd = 0;
                if (M->state == MS_NAME && P->evh != P->evt) { kbd = iigs_key(P->evq[P->evh]); P->evh = (P->evh + 1) & 15; }
                else if (M->state != MS_NAME) P->evh = P->evt;
                P->comp = P->shown ^ 1;
                copy32((u32 *)P->B[P->comp], (const u32 *)P->L, SCREEN_BYTES / 4);
                if (M->shadow_y != 0 && M->shadow_x != 0x168)
                    blit_part(P->B[P->comp], M->shadow_f, (int)M->shadow_x - 24, (int)M->shadow_y - 50);
                if (menu_pass(M, &gfx, (unsigned)ctl, kbd)) {
                    map_mode = 1; sk_reset(&sk);
                }
                if (notice > 0 && M->state == MS_RODNEY) {          /* the events need about 700 KB of free memory */
                    static const char msg[] = "NOT ENOUGH MEMORY FOR THE EVENTS";
                    int q;
                    for (q = 0; msg[q]; q++) {
                        glyph_to(P->B[P->comp], menu_fnt, 4 + 4 * q, 100, msg[q], 0xFFFF);
                        glyph_to(P->L, menu_fnt, 4 + 4 * q, 100, msg[q], 0xFFFF);
                    }
                    notice--;
                }
                present(P);
            }
        } else {
            if (tick) {
                sk_step(&sk, ctl);
                if (sk.in_shop) {                               /* the shop door: back to Rodney */
                    sk_reset(&sk); map_mode = 0; menu_enter_shop(M);
                } else if (sk.exit_code) {
                    M->exit_code = (unsigned short)sk.exit_code;
                    if (M->count || M->practice) {
                        int code = sk.exit_code;
                        M->exit_code = 0; sk_reset(&sk);
                        if (code >= 1 && code <= 4) {           /* freestyle ramp, high jump, downhill race, downhill jam */
                            if (run_event(P, M, code, (u32)vbl_hz) < 0) notice = 90;
                            map_mode = 0; menu_enter_shop(M);
                        }                                       /* the pool joust is not ported yet: the skater starts again */
                    } else {
                        P->comp = P->shown ^ 1;
                        copy32((u32 *)P->B[P->comp], (const u32 *)P->L, SCREEN_BYTES / 4);
                        menu_pass(M, &gfx, 0xFF, 0);            /* shows "MUST REGISTER AT SKATESHOP" */
                        present(P);
                        sk_reset(&sk); map_mode = 0;
                    }
                }
            }
            if (map_mode) {
                int frames[4];
                sk_frames(&sk, frames);
                P->comp = P->shown ^ 1;
                copy32((u32 *)P->B[P->comp], (const u32 *)P->L, SCREEN_BYTES / 4);
                draw_skater(P->B[P->comp], frames, 2 * sk.x - 4, sk.y - 20);
                present(P);
            }
        }
        frame_wait(P);
    }

    /* give the machine back */
    R16(DMACON) = 0x7FFF; R16(INTENA) = 0x7FFF; R16(INTREQ) = 0x7FFF;
    for (i = 0; i < 4; i++) R16(AUDVOL(i)) = 0;
    R32(COP1LC) = oldcop;
    R16(COPJMP1) = 0;
    R16(DMACON) = 0x8000 | olddma;
    R16(INTENA) = 0x8000 | oldint;
    Permit();
    LoadView(gb, oldview);
    WaitTOF(gb);
    WaitTOF(gb);
    CloseLibrary(gb);
    ow_free_base(); asset_free(title_ima, title_ima_size); asset_free(title_scr, title_scr_size);
    CloseLibrary(DOSBase);
    FreeMem(mem, total);
    return 0;
}
