/* Skate or Die overworld demo for Amiga (68020+, OCS/ECS/AGA).
 * Opens in Rodney's skate shop (point the sign at SIGN-UP, PRACTICE, COMPETE and the other hotspots), with the sign-up list, name entry,
 * board colour and high-score screens; going out onto the map gives the skater you steer around the overworld, and the NES overworld
 * track loops on Paula. Freestyle C: no C library. Talks to AmigaOS only to open graphics.library, allocate chip RAM and
 * hand the display back on exit; everything else is direct hardware access.
 * Controls: joystick in port 2 or cursor keys to move, fire or SPACE/Return to select, letters/digits/Backspace/Return in the name
 * entry, ESC to quit (Q too, except while typing a name). */

#include "os.h"
#ifdef WHDLOAD
#include "whd_glue.h"
#endif
#include "skater.h"
#include "platform.h"
#ifdef AUTOWALK
#include "autowalk.h"
#ifndef AWSTART
#define AWSTART 0
#endif
static int aw_n = AWSTART, aw_i = 0;       /* test builds: the n-th walk goes to the n-th event (1..5), steered by the planned control bytes */
#endif
#define TICK_HZ 25   /* original main-loop passes per second (about 107,000 IIGS cycles per pass at 2.8 MHz; an estimate) */
#define MAP_HZ 50    /* the skater on the map: the NES version takes the same stretch of the map (shop door to the wall below it) in about 67 frames at 60 a second;
                        the original code needs 57 passes for it, so 50 passes a second (one a frame on a 50 Hz display) gives the NES speed; the animations stay at the 25 a second they were set for */

/* ---- assets: files of the data folder, loaded as needed ---- */
static const u8 *title_scr, *title_ima, *map_scr, *shop_scr, *list_scr, *scores_scr, *board_scr, *menu_fnt, *skater_spr, *ow_pcm;
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

/* ---- the overworld's own animations (state 0 of F39: $1061 and $10F0): every 16 passes colour 10 (the light on the shop) changes between $0D0D and $0FF0,
   every 4 passes the next of four frames of the pond's water (32 x 26 pixels, bottom left) is copied into the picture ---- */
#include "water.h"
static int map_water_frame;
static void map_draw_water(Plat *P, int frame)
{
    const u8 *src = map_water + frame * 416;
    int r, g, k;
    for (r = 0; r < 26; r++) {
        u8 *row = P->L + (174 + r) * 160;
        for (g = 0; g < 4; g++) {                                   /* 8 pixels = one byte of each bitplane */
            u8 pl[4] = {0, 0, 0, 0};
            for (k = 0; k < 8; k++) {
                u8 v = src[r * 16 + g * 4 + (k >> 1)];
                u8 c = (k & 1) ? (u8)(v & 15) : (u8)(v >> 4);
                pl[0] = (u8)((pl[0] << 1) | (c & 1)); pl[1] = (u8)((pl[1] << 1) | ((c >> 1) & 1));
                pl[2] = (u8)((pl[2] << 1) | ((c >> 2) & 1)); pl[3] = (u8)((pl[3] << 1) | ((c >> 3) & 1));
            }
            row[g] = pl[0]; row[40 + g] = pl[1]; row[80 + g] = pl[2]; row[120 + g] = pl[3];
        }
    }
}
static void map_animate(Plat *P, Menu *M)
{
    M->pass++;                                                      /* F39 counts every pass, also on the map */
    if ((M->pass & 15) == 0) { M->pal[10] = M->pal[10] == 0x0D0D ? 0x0FF0 : 0x0D0D; set_palette(P, M->pal); }
    if ((M->pass & 3) == 0) { map_water_frame = (map_water_frame + 1) & 3; map_draw_water(P, map_water_frame); }
}

/* ---- the files the shop and the map need (screens, font, skater sprites, the overworld song): loaded with AmigaDOS running ---- */
static const char *const base_names[8] = {"map.scr", "shop.scr", "list.scr", "board.scr", "scores.scr", "menu.fnt", "skater.spr", "ow.raw"};
static const u8 **const base_ptr[8] = {&pictures[0], &pictures[1], &pictures[2], &pictures[3], &pictures[4], &menu_fnt, &skater_spr, &ow_pcm};
static int ow_load_base(int with_music)
{
    int i, ok = 1;
    for (i = 0; i < (with_music ? 8 : 7); i++) {
        *base_ptr[i] = (const u8 *)asset_load(base_names[i], i == 7 ? MEMF_CHIP : 0, &base_size[i]);            /* the song is played by Paula: chip RAM */
        if (!*base_ptr[i]) ok = 0;
    }
    return ok;
}
static void ow_free_base(void)
{
    int i;
    for (i = 0; i < 8; i++) { asset_free(*base_ptr[i], base_size[i]); *base_ptr[i] = 0; }
}
/* the overworld song: an 8-bit loop played straight from chip RAM */
static void ow_music_start(Plat *P)
{
    if (ow_pcm) raw_music_start(P, ow_pcm, base_size[7], 64);
}

/* ---- the "SKATING TO ..." screens. The original shows INTER's text screen "SKATING TO <event>" (the event names are in INTER's table at $7BC3: RAMP, HIGH JUMP,
   DOWNHILL, JAM, POOL JOUST; "SKATING TO SKATESHOP" at the end, $7518) while the next event loads from the disk. Here the screen comes before every event, with the event's controls
   from the manual added, and the game waits for a player's button before the event is loaded and started. Text on black in the shop's font, like INTER's other text screens. ---- */
static const char *const sk_title[6] = {0, "SKATING TO RAMP", "SKATING TO HIGH JUMP", "SKATING TO DOWNHILL", "SKATING TO JAM", "SKATING TO POOL JOUST"};
static const char *const sk_ramp[] = {
    "FREESTYLE RAMP",
    "",
    "MOVE THE CONTROLLER FORWARD OR BACK TO",
    "CHOOSE YOUR ENTRY POSITION. YOU GET TEN",
    "PASSES. IN A PUMP ZONE CLICK THE BUTTON",
    "TO PUMP AND PICK A TRICK, AND LEAN THE",
    "CONTROLLER INTO OR AWAY FROM YOUR",
    "DIRECTION OF TRAVEL:",
    "",
    " KICKTURN      AWAY       NO PUMP",
    " ROCK-N-ROLL   INTO       NO PUMP",
    " FOOTPLANT     AWAY       1 PUMP",
    " RAIL SLIDE    INTO       1 PUMP",
    " HANDPLANT     AWAY       2 PUMPS",
    " OLLIE AIR     INTO       2 PUMPS",
    " AERIAL        NO LEAN    0-2 PUMPS",
    "",
    "IN THE AIR LEAN LEFT OR RIGHT TO SPIN,",
    "THE OTHER WAY TO STOP.", 0};
static const char *const sk_hj[] = {
    "HIGH JUMP",
    "",
    "IN THE RAMP MOVE THE CONTROLLER",
    "RAPIDLY IN ANY DIRECTION TO BUILD",
    "SPEED. YOU CAN TAKE A BREATHER WHILE",
    "YOU ARE IN THE AIR. YOU GET UP TO FIVE",
    "PASSES ON THE RIGHT SIDE, WHERE THE",
    "HEIGHT MARKER IS.",
    "",
    "TO GO FOR IT AND RECORD THE ATTEMPT,",
    "CLICK THE BUTTON AT THE HIGHEST",
    "POINT OF YOUR JUMP. GOOD TIMING ADDS A",
    "FEW INCHES. IF NOT, IT'S TIME TO BAIL.", 0};
static const char *const sk_race[] = {
    "DOWNHILL RACE", "",
    "RACE AGAINST TIME AND COLLECT BONUSES.",
    "PICK YOUR STYLE:", "",
    "REGULAR FOOT: PULL THE CONTROLLER",
    " BACK TO GO DOWNHILL, FORWARD TO",
    " SLOW OR STOP.",
    "GOOFY FOOT: FORWARD TO GO DOWNHILL,",
    " BACK TO SLOW OR STOP.", "",
    "BOTH STYLES:",
    " CLICK AND FORWARD     JUMP",
    " CLICK AND BACK        DUCK",
    " CLICK AND LEFT/RIGHT  SLIDE TURN", 0};
static const char *const sk_jam[] = {
    "DOWNHILL JAM",
    "",
    "THE CONTROLS ARE LIKE DOWNHILL RACE,",
    "WITH REGULAR AND GOOFY FOOT. CLICKING",
    "WHILE LEANING THE CONTROLLER DOES THE",
    "CLOBBERIN' STUFF:",
    "",
    " CLICK AND LEAN INTO THE DIRECTION YOU",
    "  ARE FACING: PUNCH",
    " CLICK AND LEAN AWAY FROM IT: KICK",
    "",
    "HIGH, MIDDLE OR LOW DEPENDS ON WHERE",
    "YOU LEAN: HIGH AND LOW ARE ON THE",
    "DIAGONALS, MIDDLE IS STRAIGHT TO THE",
    "SIDE.",
    "",
    "PLAYER 2: KEYPAD 1-9, SHIFT = BUTTON", 0};
static const char *const sk_joust[] = {
    "POOL JOUST",
    "",
    "TWO GO IN, ONE COMES OUT. YOU GET FIVE",
    "PASSES TO GET YOUR OPPONENT, THEN HE",
    "TAKES THE BOFFING STICK AND YOU ARE IT.",
    "FIRST TO THREE SLAMS WINS, BUT YOU",
    "HAVE TO WIN BY TWO.",
    "",
    "THE CONTROLS ARE A SIMPLIFIED RAMP",
    "FREESTYLE: ROCK-N-ROLLS, RAIL SLIDES,",
    "KICKTURNS AND OLLIES. NO PUMPING: A",
    "CLICK READIES THE BOFFING STICK. ONLY",
    "HIT THE OTHER GUY WHEN YOUR STICK IS",
    "FLASHING.",
    "",
    "PLAYER 2: KEYPAD 1-9, SHIFT = BUTTON", 0};
static const char *const *const sk_lines[6] = {0, sk_ramp, sk_hj, sk_race, sk_jam, sk_joust};

static void sk_text(Plat *P, const u8 *font, int row, int col, const char *t)
{
    int i;
    for (i = 0; t[i] && col + i < 40; i++) {
        int c = (u8)t[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 0x20 || c > 0x5F) c = 0x20;
        if (c != 0x20) glyph_to(P->B[P->comp], font, 4 * (col + i), row * 8, c, 0x1111);
    }
}
static int sk_len(const char *t) { int n = 0; while (t[n]) n++; return n; }
/* draw the screen for event `mode` (1-5), or "SKATING TO SKATESHOP" (0) */
static void sk_draw(Plat *P, const u8 *font, int mode)
{
    static const u16 pal[16] = {0x000, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF};
    const char *title = mode ? sk_title[mode] : "SKATING TO SKATESHOP";
    int i, row = 0;
    P->comp = P->shown ^ 1;
    for (i = 0; i < SCREEN_BYTES / 4; i++) ((u32 *)P->B[P->comp])[i] = 0;
    if (!mode) {
        sk_text(P, font, 12, (40 - sk_len(title)) / 2, title);
    } else {
        const char *const *l = sk_lines[mode];
        sk_text(P, font, 0, (40 - sk_len(title)) / 2, title);
        for (i = 0; l[i]; i++, row++) {
            if (i == 0) sk_text(P, font, 2, (40 - sk_len(l[0])) / 2, l[0]);        /* the event's name, centred */
            else sk_text(P, font, 2 + i, 1, l[i]);
        }
        sk_text(P, font, 21, 1, "CONTROLLER: JOYSTICK OR CURSOR KEYS");
        sk_text(P, font, 22, 1, "BUTTON: FIRE, SPACE OR RETURN");
        sk_text(P, font, 23, 1, "ESC: BACK TO THE SKATE SHOP");
        sk_text(P, font, 24, (40 - 19) / 2, "PRESS FIRE TO START");
    }
    set_palette(P, pal);
    present(P);
}
/* wait for a player's button (the joystick in either port, Space, Return or Shift for the second player); the button that brought the skater to the exit must have been let go first.
   Returns 0 if ESC was pressed instead. */
static int sk_wait_fire(Plat *P)
{
    int armed = 0;
#ifdef BENCH_LOOP
    return 1;                                                        /* the timing and profile builds start events with a test key and have nobody to press the button */
#endif
    for (;;) {
        int fire;
        frame_wait(P); poll_keyboard(P);
        if (P->keys[0x45]) return 0;
        fire = !(CIAA_PRA & 0x80) || !(CIAA_PRA & 0x40) || P->keys[0x40] || P->keys[0x44] || P->keys[0x60] || P->keys[0x61];
        if (!fire) armed = 1; else if (armed) { P->evh = P->evt; return 1; }
    }
}

/* play event `mode` (1 ramp, 2 high jump, 3 downhill race, 4 downhill jam): free what the shop and map hold, load the event overlay (data/<name>.ovl, one event's
   code at a time) and run it; it loads its own image, song and effects. Returns the overlay's result, -1 if it could not be loaded. */
static int run_event(Plat *P, Menu *M, int mode, u32 vbl_hz)
{
#ifdef WHDLOAD
    static const char *const ovl[6] = {0, "ramp.ovx", "hj.ovx", "race.ovx", "jam.ovx", "joust.ovx"};      /* relocatable images of the WHDLoad build (tools/elf2whd.py) */
#else
    static const char *const ovl[6] = {0, "PROGDIR:data/ramp.ovl", "PROGDIR:data/hj.ovl", "PROGDIR:data/race.ovl", "PROGDIR:data/jam.ovl", "PROGDIR:data/joust.ovl"};
#endif
    EvArgs a;
    u32 seg;
    int rc;
    sk_draw(P, menu_fnt, mode);                                      /* "SKATING TO <event>" with its controls; the game waits for a player's button */
    if (!sk_wait_fire(P)) { M->inter_valid = 0; return 0; }          /* ESC: back to the shop */
    music_stop(); R16(DMACON) = 0x000F; P->stream = 0;
    { int i; for (i = 0; i < 3072; i++) M->font[i] = menu_fnt[i]; }
    os_give();
    ow_free_base();
    a.P = P; a.M = M; a.SysBase = SysBase; a.DOSBase = DOSBase; a.oldint = g_oldint; a.clock = g_clock; a.vbl_hz = vbl_hz; a.mode = mode; a.rc = -1; a.run = 0; a.esc = 0;
#ifdef WHDLOAD
    a.os = g_os;
#endif
    a.font = M->font; a.next = 0; M->inter_valid = 0;
    seg = 0;
#ifdef BENCH_LOOP
    for (;;) {                                                  /* test builds: play the event again and again until ESC (each run writes its own timing file) */
#endif
    for (;;) {                                                  /* a competition in all events goes on in the next event's overlay: INTER's state travels in the Menu */
        seg = dos_LoadSeg(ovl[a.mode]);
        if (!seg) break;
        ((int (*)(EvArgs *))((seg << 2) + 4))(&a);
        dos_UnLoadSeg(seg);
        if (a.rc < 0 || !a.next) break;
        a.mode = a.next; a.next = 0;
        os_take(P);                                                  /* (the event handed the machine back to AmigaDOS: the keyboard needs it ours) */
        sk_draw(P, M->font, a.mode);                                 /* the next event of a competition in all events */
        if (!sk_wait_fire(P)) { os_give(); a.esc = 1; seg = 1; a.rc = 0; break; }
        os_give();
    }
#ifdef BENCH_LOOP
    if (!seg || a.esc || ++a.run >= 24) break;
    }
#endif
    rc = seg ? a.rc : -1;
    os_take(P);
    sk_draw(P, M->font, 0);                                          /* "SKATING TO SKATESHOP", on screen for a second and a half and while the shop's files load */
    { int w; for (w = 0; w < (int)(vbl_hz * 3 / 2); w++) frame_wait(P); }
    os_give();
    ow_load_base(1);
    os_take(P);
    ow_music_start(P);
    return rc;
}


/* the shop after an event (or a whole competition): F39's start-up work (the high-score table) and Rodney's congratulation; a changed table is saved
   (the original writes /sod/highs; here PROGDIR:data/highs.bin) */
static void after_event(Plat *P, Menu *M)
{
    M->f70 = 0;
    menu_after_event(M);
    if (M->f70) {
        os_give();
        file_save("highs.bin", M->scores, 192);
        os_take(P);
    }
    if (M->compete_all) { M->compete_all = 0; menu_congrats(M); }
}

/* Entry point (first thing in the hunk): preserve the registers AmigaDOS may rely on, run the C code, return its result in d0. */
#ifdef WHDLOAD
/* WHDLoad entry (the slave calls it with a0 = resload, a1 = first free byte of the expansion memory after this image, d0 = bytes free there, d1 = base memory size):
   set up memory and files, then run on a 64 KB stack of our own */
__asm(".section .text.start,\"ax\"\n"
      ".globl start\n"
      "start:\n"
      "\tmovem.l %d2-%d7/%a2-%a6,-(%sp)\n"
      "\tmove.l %d1,-(%sp)\n"
      "\tmove.l %d0,-(%sp)\n"
      "\tmove.l %a1,-(%sp)\n"
      "\tmove.l %a0,-(%sp)\n"
      "\tjsr whd_init\n"
      "\tlea 16(%sp),%sp\n"
      "\ttst.l %d0\n"
      "\tbeq.s 1f\n"
      "\tmove.l %d0,%a2\n"
      "\tlea 65536(%a2),%a0\n"
      "\tmove.l %sp,-(%a0)\n"
      "\tmove.l %a0,%sp\n"
      "\tjsr ow_main\n"
      "\tmove.l (%sp)+,%sp\n"
      "\tbra.s 2f\n"
      "1:\tmoveq #20,%d0\n"
      "2:\tmovem.l (%sp)+,%d2-%d7/%a2-%a6\n"
      "\trts\n"
      ".text\n");
#else
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
#endif

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
#ifndef WHDLOAD
    SysBase = *(volatile u32 *)4;
#endif
#ifdef TEST_NOCACHE
    CacheControl(0, 1);
#else
    CacheControl(1, 1);                                          /* the 68020 instruction cache on (Kickstart does this too; the game code runs from Chip RAM, so it matters a lot) */
#endif
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
    if (!title_scr || !title_ima || !ow_load_base(0)) {
        dos_puts("SkateOrDie: cannot read its data folder (PROGDIR:data) or there is not enough memory.\n");
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
#ifdef WHDLOAD
    u32 oldview = 0, oldcop = 0;
    u8 pal_hz = whd_is_pal() ? 50 : 60;                         /* the display standard (and so the TOD tick rate the pacing counts) */
#else
    u32 oldview = *(volatile u32 *)(gb + 34);
    u32 oldcop = *(volatile u32 *)(gb + 38);
    u8 pal_hz = *(volatile u8 *)(SysBase + 0x212);
#endif
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
    g_clock = pal_hz == 50 ? 3546895 : 3579545;
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
    if (!quit) {                                                /* the shop's song (Chip RAM: it is played by Paula) comes once the title's memory is free */
        music_stop(); os_give();
        ow_pcm = (const u8 *)asset_load("ow.raw", MEMF_CHIP, &base_size[7]);
        {   /* the high scores saved by an earlier session (192 bytes); without the file the table starts empty as in the original's disk */
            u32 hsz = 0; const u8 *hs = (const u8 *)asset_load("highs.bin", 0, &hsz);
            if (hs) { if (hsz >= 192) for (i = 0; i < 192; i++) M->scores[i] = hs[i]; asset_free(hs, hsz); }
            menu_hs_colours(M);
        }
        os_take(P);
        ow_music_start(P);
    }

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
#ifdef DEBUG_KEYS
        if (keys[0x42] && !(M->state == MS_NAME)) {                    /* Tab (testing): out onto the map */
            keys[0x42] = 0; M->practice = 1; M->state = MS_MAP; menu_clear_slots(M); menu_show(M, &gfx, PIC_MAP);
            map_mode = 1; sk_reset(&sk);
            continue;
        }
        if (keys[0x5F] && !(M->state == MS_NAME)) {                    /* Help (testing): the High Scores screen */
            keys[0x5F] = 0; map_mode = 0; menu_enter_shop(M); M->f70 = 1;
            continue;
        }
        if (keys[0x59] && !(M->state == MS_NAME)) {                    /* F10: the shop after a competition (testing the winner's speech) */
            keys[0x59] = 0;
            M->count = 3; M->totals[0] = 3; M->totals[1] = 9; M->totals[2] = 5;
            { int w; static const char nm[3][9] = {"ANNA", "BOB", "CHARLIE"}; int q; for (q = 0; q < 3; q++) for (w = 0; w < 16; w++) M->names[q][w] = w < 9 ? (u8)nm[q][w] : 0; }
            map_mode = 0; menu_enter_shop(M); menu_congrats(M); M->f70 = 1;
            continue;
        }
        if ((keys[0x46] || keys[0x41]) && !(M->state == MS_NAME)) {    /* Delete / Backspace (testing): three skaters compete in the pool joust / the downhill jam alone */
            int q, w, evn = keys[0x46] ? 5 : 4;
            keys[0x46] = keys[0x41] = 0;
            M->count = 3; M->practice = 0; M->compete_all = 0;
            for (q = 0; q < 3; q++) {
                static const char nm[3][9] = {"ANNA", "BOB", "CHARLIE"};
                static const u16 col[3] = {0x0F00, 0x00F0, 0x000F};
                for (w = 0; w < 16; w++) M->names[q][w] = w < 9 ? (u8)nm[q][w] : 0;
                M->colour[2 * q] = (u8)(col[q] & 255); M->colour[2 * q + 1] = (u8)(col[q] >> 8); M->board[q] = (u8)(q + 1);
            }
            if (run_event(P, M, evn, (u32)vbl_hz) < 0) notice = 90;
            map_mode = 0; after_event(P, M);
            continue;
        }
        if ((keys[0x56] || keys[0x57]) && !(M->state == MS_NAME)) {    /* F7 / F8: three skaters sign up and compete in the ramp / in all events (testing) */
            int q, w, all = keys[0x57];
            keys[0x56] = keys[0x57] = 0;
            M->count = 3; M->practice = 0; M->compete_all = (unsigned short)all;
            for (q = 0; q < 3; q++) {
                static const char nm[3][9] = {"ANNA", "BOB", "CHARLIE"};
                static const u16 col[3] = {0x0F00, 0x00F0, 0x000F};
                for (w = 0; w < 16; w++) M->names[q][w] = w < 9 ? (u8)nm[q][w] : 0;
                M->colour[2 * q] = (u8)(col[q] & 255); M->colour[2 * q + 1] = (u8)(col[q] >> 8); M->board[q] = (u8)(q + 1);
            }
            if (run_event(P, M, 1, (u32)vbl_hz) < 0) notice = 90;
            map_mode = 0; after_event(P, M);
            continue;
        }
        if ((keys[0x50] || keys[0x51] || keys[0x52] || keys[0x53] || keys[0x54]) && !(M->state == MS_NAME)) {    /* F1-F5: go straight to an event (testing) */
            int ev = keys[0x50] ? 1 : keys[0x51] ? 2 : keys[0x52] ? 3 : keys[0x53] ? 4 : 5;
            keys[0x50] = keys[0x51] = keys[0x52] = keys[0x53] = keys[0x54] = 0;
            M->practice = 1; M->compete_all = 0;
            if (run_event(P, M, ev, (u32)vbl_hz) < 0) notice = 90;
            map_mode = 0; after_event(P, M);
            continue;
        }

#endif
        /* the game's control byte: up accelerates, down brakes, left/right turn; bit clear = pressed */
        int ctl = 0xFF;
        if (jy < 0) ctl &= ~1;
        if (jy > 0) ctl &= ~2;
        if (jx < 0) ctl &= ~4;
        if (jx > 0) ctl &= ~8;
        if (fire) ctl &= ~0x10;

        int tick = 0;
        static int map_acc, map_half;
        tick_acc += TICK_HZ;
        if (tick_acc >= vbl_hz) { tick_acc -= vbl_hz; tick = 1; }
        if (map_mode) {                                         /* the map has its own, faster pass */
            map_acc += MAP_HZ;
            tick = 0;
            if (map_acc >= vbl_hz) { map_acc -= vbl_hz; tick = 1; map_half ^= 1; }
        }

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
#ifdef AUTOWALK
                    aw_i = 0;
#endif
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
#ifdef AUTOWALK
                ctl = aw_i < aw_len[aw_n % 5] ? aw_seq[aw_n % 5][aw_i++] : 0xFF;
#endif
                sk_step(&sk, ctl);
                if (map_mode && M->state == MS_MAP && map_half) map_animate(P, M);
                if (sk.in_shop) {                               /* the shop door: back to Rodney */
                    sk_reset(&sk); map_mode = 0; menu_enter_shop(M);
                } else if (sk.exit_code) {
                    M->exit_code = (unsigned short)sk.exit_code;
                    if (M->count || M->practice) {
                        int code = sk.exit_code;
                        M->exit_code = 0; M->compete_all = (unsigned short)(sk.compete_all && !M->practice); sk_reset(&sk);
                        if (code >= 1 && code <= 5) {           /* freestyle ramp, high jump, downhill race, downhill jam, pool joust */
                            if (run_event(P, M, code, (u32)vbl_hz) < 0) notice = 90;
#ifdef AUTOWALK
                            aw_n++;
#endif
                            map_mode = 0; after_event(P, M);
                        }
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
