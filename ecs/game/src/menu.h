/* Rodney's skate shop, sign-up list, name entry and board-colour screens: the logic of overlay F39 for game states 1-9
 * (state 0 is the skater on the map, see skater.h). Portable C with no OS or hardware access, so the same source runs on the Amiga
 * and in the native test that compares it with the original code running on a 65816 core (tools/cpu816/menu_fuzz.py).
 *
 * Source: work/disasm/F39_full.txt. Names in comments are overlay addresses (data bank 3), e.g. $4BD5 = state.
 * The platform supplies the drawing (glyph, picture, sprites); this file only decides what is drawn and where. */
#ifndef MENU_H
#define MENU_H

#include "menu_data.h"

enum { MS_MAP = 0, MS_SHOP_ENTER = 1, MS_RODNEY = 2, MS_PICK = 3, MS_BOARD_WAIT = 4, MS_BOARD = 5, MS_NAME = 6, MS_SIGNUP = 7, MS_NOTICE = 8, MS_SCORES = 9 };
enum { PIC_MAP = 0, PIC_RODNEY = 1, PIC_LIST = 2, PIC_BOARD = 3, PIC_SCORES = 4 };

typedef struct MenuGfx {
    void *ctx;
    /* 8x8 glyph from the overlay font, drawn opaque at byte column xb (2 pixels per byte) and pixel row y; each pixel is
       (font nibble AND mask nibble), exactly the original's word AND (routine $1BB7). */
    void (*glyph)(void *ctx, int xb, int y, int code, unsigned short mask);
    /* replace the whole screen with picture `which`; returns that picture's 16-colour palette ($0RGB words). The original fades the old
       picture out and the new one in around this; the platform may do the same with the palette. */
    const unsigned short *(*picture)(void *ctx, int which);
    /* the palette changed (colour cycling); called with the 16 current $0RGB words */
    void (*palette)(void *ctx, const unsigned short *pal);
} MenuGfx;

typedef struct Menu {
    /* game state */
    unsigned short state;                 /* $4BD5 */
    unsigned short c2, c4, c6, c8, ca, cc, ce, d2, d4;
    unsigned short cx, cy;                /* $4BC7 (x, 2-pixel units), $4BC9 (y): the cursor sign */
    unsigned short f4bcd, f4bcf, f4bd1, f4bd3;
    unsigned short sign_frame;            /* $3036: 0..2, picks the flashing colour of the sign */
    unsigned short pass;                  /* $3038: counts logic passes */
    unsigned short f60, f62, f64, f66, f68, f6e, f70;   /* screen-setup requests handled at the start of the next pass */
    unsigned short exit_code;             /* $6C */
    unsigned short practice;              /* $077E17 */
    unsigned short compete_all;           /* $0768D0: the competition goes through all five events */
    unsigned short key;                   /* $3064: translated key for the name entry */
    /* the glyph "pen" ($3044 x in bytes, $3046 y in pixels, $2E colour mask) and the text cursor ($303E col, $3040 row) */
    unsigned short gx, gy, mask, tcol, trow;
    /* sprite slot 0 = the cursor sign: x, y, frame ($4B8F, $4B9F, $4BAF); x == 0 && y == 0 means hidden */
    unsigned short slot_x, slot_y, slot_f;
    unsigned short shadow_x, shadow_y, shadow_f;   /* copy of the slot taken each pass after the setup requests ($114A); the sprite engine draws from it in the next pass */
    /* players */
    unsigned short pal[16];               /* current palette ($E19E00) */
    unsigned short count;                 /* $0768CC */
    unsigned short f38, f3a;              /* digit printing: a digit has been printed / a non-zero byte has been seen */
    unsigned char scores[192];            /* $077F00: twelve 16-byte high-score entries (name, ... , BCD score at +10..+12, colour at +15) */
    unsigned char names[8][16];           /* $076810 */
    unsigned char board[8];               /* $0768FE */
    unsigned char colour[16];             /* $0768EE: eight little-endian words */
    unsigned char rng0, rng1;             /* $3078, $3079 */
    unsigned char congrats[36];           /* Rodney's last speech with the winner's name in its second row ($496C) */
    unsigned char res_score[36];          /* $0768A8: the best three scores of each of the first four events of a competition (BCD, 3 bytes each) */
    unsigned char res_ord[12];            /* $0768E2: the players who made them (their number in $0768DA's order) */
    unsigned char ord[8];                 /* $0768DA: the order INTER sorted the players into */
    unsigned char totals[8];              /* $0768D2: the competition's points per player (BCD), for the winner of a competition in all events */
    unsigned char inter[6144];            /* INTER's variables and tables ($076800-$077FFF) while a competition moves from one event overlay to the next */
    int inter_valid;
    unsigned char font[3072];             /* a copy of the font for INTER's text screens (the overlays are loaded while the shop's files are freed) */
    int carry;                            /* 6502 carry flag as left by the code before $3CDA (only the sign's flashing depends on it) */
    unsigned short palette_blink;         /* number of the palette change requested this pass (see menu_blink) */
} Menu;

static unsigned char menu_rand(Menu *m)                          /* $1D93 (8-bit registers) */
{
    unsigned r0 = m->rng0, r1 = m->rng1, a, c, d;
    c = (r0 >> 1) & 1;                                          /* LDA r0; LSR; LSR: carry = bit 1 */
    a = r0 >> 2;
    d = a - r0 - (c ? 0 : 1);                                   /* SBC r0 */
    a = d & 0xFF;
    c = a & 1;                                                  /* LSR: carry = bit 0 */
    d = (r1 >> 7) & 1;                                          /* ROL r1 */
    r1 = ((r1 << 1) | c) & 0xFF;
    r0 = ((r0 >> 1) | (d << 7)) & 0xFF;                         /* ROR r0 */
    m->rng0 = (unsigned char)r0; m->rng1 = (unsigned char)r1;
    return (unsigned char)(r0 + r1);                            /* CLC; ADC */
}

/* ---- drawing helpers ---- */
static void menu_glyph(Menu *m, const MenuGfx *g, int code)       /* $1BB7 */
{
    int y = m->gy; if (y >= 0xC0) y = 0xC0;
    g->glyph(g->ctx, m->gx, y, code, m->mask);
    m->gx += 4;
    if (m->gx >= 0x160) { m->gx = 0; m->gy += 8; }
}

static void menu_tglyph(Menu *m, const MenuGfx *g, int code)      /* $1C57: character cell at (col, row) */
{
    m->gx = (unsigned short)(m->tcol * 4);
    g->glyph(g->ctx, m->gx, m->trow * 8, code, m->mask);
    m->tcol++;
    if (m->tcol >= 0x28) { m->tcol = 0; m->trow += 8; if (m->trow >= 0xC8) m->trow = 0; }
}

static void menu_str(Menu *m, const MenuGfx *g, const unsigned char *s)   /* $1D28 */
{
    while (*s) menu_glyph(m, g, *s++);
}

static void menu_tstr(Menu *m, const MenuGfx *g, const unsigned char *s) /* $1D08 */
{
    while (*s) menu_tglyph(m, g, *s++);
}

static void menu_show(Menu *m, const MenuGfx *g, int which)
{
    const unsigned short *p = g->picture(g->ctx, which);
    int i;
    for (i = 0; i < 16; i++) m->pal[i] = p[i];
}

static void menu_clear_slots(Menu *m) { m->slot_x = m->slot_y = m->slot_f = 0; }

static void menu_draw_names(Menu *m, const MenuGfx *g)             /* $1AD7 */
{
    int i;
    for (i = 0; i < 8; i++) {
        m->mask = 0x1111; m->gx = 0x30;
        m->gy = (unsigned short)((i * 2 + 8) * 8 + 4);
        menu_str(m, g, m->names[i]);
    }
}

static void menu_to_rodney_request(Menu *m)                        /* $37F5: back to the shop counter */
{
    menu_clear_slots(m);
    m->c6 = 8; m->state = MS_SHOP_ENTER; m->f60 = 1; m->cc = 0x0B; m->c2 = 0;
}

static void menu_request_name_entry(Menu *m)                       /* $3832 */
{
    m->state = MS_NAME; m->c6 = 8; m->f66 = 1;
}

static void menu_board_color(Menu *m, int player)                  /* the board index sets the colour word (table $307A) */
{
    unsigned short v = menu_colour_tab[m->board[player]];
    m->colour[player * 2] = (unsigned char)v; m->colour[player * 2 + 1] = (unsigned char)(v >> 8);
}

static void menu_digits(Menu *m, const MenuGfx *g, unsigned a)    /* $1D48: a BCD byte as two digits, leading zeros dropped */
{
    unsigned hi = (a >> 4) & 15;
    if (!m->f3a) { if (!a) return; m->f3a = 1; }
    if (hi || m->f38) { m->f38 = 1; menu_tglyph(m, g, menu_hexchars[hi]); }
    menu_tglyph(m, g, menu_hexchars[a & 15]);
    m->f38 = 1;
}

/* ---- setup requests, in the order of the main loop ---- */
static void menu_setup(Menu *m, const MenuGfx *g)
{
    if (m->f60) {                                                   /* $15E1: the counter with Rodney */
        menu_clear_slots(m);
        menu_show(m, g, PIC_RODNEY);
        m->c2 = 0xFF; m->cy = 0x74; m->cx = 0x4E; m->slot_f = 0x40;
        m->state = MS_RODNEY; m->practice = 0; m->f60 = 0;
    }
    if (m->f62) {                                                   /* $18BD: board colour */
        int len = 0;
        m->c8 = 10; m->ca = 9; m->cc = 10;
        menu_clear_slots(m);
        menu_show(m, g, PIC_BOARD);
        m->mask = 0x1111;
        m->gy = 9 * 8 + 5;
        while (m->names[m->d4][len]) len++;
        m->mask = 0x4444;
        m->tcol = (unsigned short)(len >> 1);
        m->gx = (unsigned short)((0x13 - m->tcol) * 4 + 2);
        menu_str(m, g, m->names[m->d4]);
        m->state = MS_BOARD; m->f62 = 0;
        m->c2 = (unsigned short)(m->board[m->d4] - 1);
        menu_board_color(m, m->d4);
    }
    if (m->f64) {                                                   /* $16FA: list of existing players */
        menu_clear_slots(m);
        menu_show(m, g, PIC_LIST);
        menu_draw_names(m, g);
        m->gy = (unsigned short)((m->count * 2 + 8) * 8 + 4); m->gx = 0x30; m->mask = 0x2222;
        menu_str(m, g, menu_txt_cancel);
        m->f64 = 0;
    }
    if (m->f66) {                                                   /* $19BB: name entry */
        menu_clear_slots(m);
        if (m->c2 < 8) {
            m->gy = (unsigned short)((m->c2 * 2 + 8) * 8 + 4); m->gx = 0x20; m->mask = 0x1111;
            menu_glyph(m, g, 0x3E); menu_glyph(m, g, 0x20);
            m->gx = 0x6C; menu_glyph(m, g, 0x3C);
            m->f4bcf = 0;
        }
        menu_draw_names(m, g);
        m->f66 = 0; m->ce = 0; m->key = 0; m->d2 = 0;
    }
    if (m->f68) {                                                   /* $1A40: sign-up list */
        int n;
        menu_clear_slots(m);
        menu_show(m, g, PIC_LIST);
        menu_draw_names(m, g);
        n = m->count; if (n != 8) n++;
        m->gy = (unsigned short)((n * 2 + 8) * 8 + 4); m->gx = 0x30; m->mask = 0x2222;
        menu_str(m, g, menu_txt_cancel);
        m->f68 = 0;
    }
    if (m->f6e) {                                                   /* $178E: high scores */
        int i;
        menu_clear_slots(m);
        menu_show(m, g, PIC_SCORES);
        for (i = 0; i < 12; i++) {
            unsigned char *e = m->scores + i * 16;
            m->tcol = menu_hs_col[i]; m->trow = menu_hs_row[i];
            m->mask = (unsigned short)(e[15] | (e[15] << 8));
            e[15] = 0x33;
            menu_tstr(m, g, e);
            m->f38 = 0; m->f3a = 0;
            m->tcol = (unsigned short)(menu_hs_col[i] + 10);
            if (i < 3 || i >= 6) {                                  /* score */
                menu_digits(m, g, e[12]); menu_digits(m, g, e[11]); m->f3a++; menu_digits(m, g, e[10]);
            } else {                                                /* time: minutes' seconds" */
                m->f3a++; menu_digits(m, g, e[11]); menu_tglyph(m, g, 0x27);
                m->f38 = 0; menu_digits(m, g, e[10]); menu_tglyph(m, g, 0x22);
            }
        }
        m->f6e = 0;
    }
    if (m->exit_code) {                                             /* $1229: the map reports an exit */
        if (m->count || m->practice) { /* an event starts: handled by the caller through exit_code */ }
        else {
            m->state = MS_NOTICE; m->c6 = 8;
            menu_clear_slots(m);
            m->mask = 0x1111;
            m->trow = 10; m->tcol = 7; menu_tstr(m, g, menu_txt_blank);
            m->trow = 11; m->tcol = 7; menu_tstr(m, g, menu_txt_register);
            m->trow = 12; m->tcol = 7; menu_tstr(m, g, menu_txt_blank);
            m->exit_code = 0;
        }
    }
}

/* ---- colour cycling on the list and name screens ($1061) ---- */
static void menu_blink(Menu *m, const MenuGfx *g)
{
    if (m->state != MS_PICK && m->state != MS_NAME && m->state != MS_SIGNUP) return;
    if (m->pass & 0x7F) return;
    if (m->pal[7] != 0x0FE0) m->pal[7] = 0x0FE0; else m->pal[7] = 0x0FFF;
    if (m->pal[4] == 0x0E0F) m->pal[4] = 0x0E00;
    else if (m->pal[4] == 0x0E00) m->pal[4] = 0x000F;
    else m->pal[4] = 0x0E0F;
    g->palette(g->ctx, m->pal);
}

/* ---- Rodney's speech ($1669) ---- */
static void menu_speech(Menu *m, const MenuGfx *g)
{
    unsigned x;
    int row, i;
    if (m->state != MS_RODNEY) return;
    m->mask = 0xFFFF;
    x = m->c2;
    if (x == m->f4bcd) return;
    m->f4bcd = (unsigned short)x;
    if (x == 0xFF) x = m->f4bd3 ? 0x14 : 0x13;
    m->f4bd3 = 0;
    x &= 0x7F;
    if (x == 0x0B) x = 0x0B + (menu_rand(m) & 7);
    m->tcol = 5; m->trow = 6;
    for (row = 0; row < 4; row++) {
        const unsigned char *t = x == 0x14 ? m->congrats : menu_speech_txt[x];
        for (i = 0; i < 9; i++) menu_tglyph(m, g, t[row * 9 + i]);
        m->tcol = 5; m->trow++;
    }
}

/* ---- input: the keyboard routine ($3EE9) ---- */
static void menu_read_key(Menu *m, unsigned kbd)
{
    unsigned k = kbd & 0xFF;
    m->key = 0; m->tcol = 0; m->trow = 0;
    if (k < 0x80) { m->d2 = 0; return; }
    if (k == m->d2) return;
    m->d2 = (unsigned short)k;
    if (k == 0x8D) m->key = 0xFF;
    else if (k == 0x88 || k == 0xFF) m->key = 0xFE;
    else if (k == 0xA0) m->key = 0x20;
    else if (k == 0xAD) m->key = 0x2D;
    else if (k < 0xB0 || k >= 0xFB) m->key = 0;
    else if (k < 0xBA) m->key = (unsigned short)(k - 0x80);
    else if (k < 0xE1) m->key = 0;
    else m->key = (unsigned short)(k - 0xA0);
}

/* ---- the per-state handlers ---- */
static void menu_prev(Menu *m)                                      /* $3C15 */
{
    if ((short)(--m->c4) >= 0) return;
    m->c4 = 8;
    if ((short)(--m->c2) >= 0) return;
    m->c2 = m->ca;
}

static void menu_next(Menu *m)                                      /* $3C51 */
{
    if ((short)(--m->c4) >= 0) return;
    m->c4 = 8;
    m->c2++;
    if (m->c2 >= m->c8) m->c2 = 0;
}

static void menu_arrow(Menu *m, const MenuGfx *g, int row, int code)
{
    m->gy = (unsigned short)((row * 2 + m->cc) * 8 + 4); m->gx = 0x24;
    menu_glyph(m, g, code);
}

/* $3CDA: advance the sign's flashing, place it, and look up the hotspot under it */
static void menu_cursor(Menu *m)
{
    unsigned a = m->sign_frame, i;
    if ((m->pass & 15) == 0) {
        a += 1 + (m->carry ? 1 : 0);                               /* ADC #1 with the carry the code leaves there */
        if (a >= 3) a = 0;
        m->sign_frame = (unsigned short)a;
    }
    m->slot_f = (unsigned short)(0x40 + m->sign_frame);
    m->slot_x = (unsigned short)(m->cx * 2 + 0x18);
    m->slot_y = (unsigned short)(m->cy + 0x32);
    for (i = 0; i < 14; i++) {
        const unsigned char *h = menu_hotspots + i * 5;
        unsigned x = m->cx & 0xFF, y = m->cy & 0xFF;
        if (x < h[0] || x >= h[1] || y < h[2] || y >= h[3]) continue;
        m->c2 = (unsigned short)((m->c2 & 0xFF00) | h[4]);       /* STA $C2 in 8-bit mode */
        return;
    }
    m->c2 = (unsigned short)((m->c2 & 0xFF00) | 0xFF);
}

/* the fire handler of states 1 and 2 ($3618); returns 1 when the shop leaves for the map */
static int menu_fire_counter(Menu *m, const MenuGfx *g, int fire)                     /* fire = button pressed */
{
    unsigned id;
    if (m->f60) return 0;
    if (m->c6) { if (!fire) m->c6--; return 0; }
    m->c6 = 0;
    menu_cursor(m);
    if (!fire) return 0;
    id = m->c2;
    if (id >= 0x80) return 0;
    switch (id) {
    case 0: case 1:                                                 /* $3A46: the sign-up list */
        m->state = MS_SIGNUP; m->c6 = 8; m->f68 = 1;
        m->c8 = (unsigned short)(m->count + 2); m->ca = (unsigned short)(m->count + 1); m->cc = 8; m->c2 = m->count;
        return 0;
    case 2:                                                         /* $36DD: choose an existing player */
        m->f64 = 1; m->state = MS_PICK; m->c6 = 8;
        m->c8 = (unsigned short)(m->count + 1); m->ca = m->count; m->cc = 8; m->c2 = m->count;
        return 0;
    case 3:                                                         /* $3BB5: high scores */
        m->state = MS_SCORES; m->c6 = 8; m->f6e = 1;
        return 0;
    case 4: m->practice = 1;                                        /* $3F7F: practice */
        /* fall through */
    case 5:                                                         /* $366E: out onto the map */
        m->state = MS_MAP; menu_clear_slots(m);
        menu_show(m, g, PIC_MAP);
        return 1;
    default: return 0;
    }
}

/* delete the entry in the row being edited (name erased and Return pressed): $3ADB.
   The original shifts the colour words with byte indices instead of word indices; that is reproduced. */
static void menu_delete_entry(Menu *m)
{
    int x, y;
    if (m->c2 >= m->count) { menu_to_rodney_request(m); return; }
    m->count--;
    y = m->c2 * 16;
    for (x = y + 16; x != 0x80; x++, y++) ((unsigned char *)m->names)[y] = ((unsigned char *)m->names)[x];
    while (y != 0x7F) ((unsigned char *)m->names)[y++] = 0;
    for (x = m->c2, y = x + 1; y < 8; x++, y++) {
        m->board[x] = m->board[y];
        m->colour[x] = m->colour[y];
        m->colour[x + 1] = m->colour[y + 1];
    }
    m->board[7] = 1;
    menu_board_color(m, m->d4);
    menu_to_rodney_request(m);
}

static void menu_name_entry(Menu *m, const MenuGfx *g, unsigned kbd, int fire)   /* $3848 -> $38DF */
{
    int i;
    unsigned row_y;
    if (m->c6) { if (!fire) m->c6--; return; }
    if (m->c2 == 8) return;
    menu_read_key(m, kbd);
    row_y = (unsigned)((m->c2 * 2 + 8) * 8 + 4);
    if (m->key == 0) goto cursor;
    if (m->key == 0xFF) {                                           /* Return */
        if (m->ce == 0) {
            if (!m->f4bcf) { menu_to_rodney_request(m); return; }
            menu_delete_entry(m); return;
        }
        if (m->c2 == m->count) m->count++;
        m->f4bd1 = 1; m->f62 = 1; m->state = MS_BOARD_WAIT; m->c6 = 8;
        m->d4 = m->c2;
        m->board[m->d4] = 1;
        menu_board_color(m, m->d4);
        return;
    }
    if (m->key == 0xFE) {                                           /* Delete */
        if (m->ce == 0) goto cursor;
        m->ce--; m->key = 0;
        m->gy = (unsigned short)row_y; m->gx = (unsigned short)(m->ce * 4 + 0x30);
        menu_glyph(m, g, 0);
        m->names[m->c2][m->ce] = 0;
        if (m->ce != 14) menu_glyph(m, g, 0x20);
        goto cursor;
    }
    if (!m->f4bcf) {                                                /* first character: the old name is wiped */
        m->f4bcf = 1;
        m->trow = (unsigned short)((m->c2 * 2 + 8) * 8 + 4);        /* the original stores this in $3040, not $3046 */
        m->gx = (unsigned short)(m->ce * 4 + 0x30);
        menu_str(m, g, menu_txt_clear15);
        for (i = 0; i < 15; i++) m->names[m->c2][i] = 0;
    }
    m->gy = (unsigned short)row_y; m->gx = (unsigned short)(m->ce * 4 + 0x30);
    if (m->ce == 15) return;
    if (m->key == 0x20 && m->ce == 0) return;
    menu_glyph(m, g, m->key);
    m->names[m->c2][m->ce] = (unsigned char)m->key;
    m->ce++;
    return;
cursor:
    if (m->ce == 15) return;
    m->gy = (unsigned short)row_y; m->gx = (unsigned short)(m->ce * 4 + 0x30);
    menu_glyph(m, g, 0x2A);
}

/* cursor moves of state 2 ($3CAA, $3CB6, $3CC5, $3CCE); carry is the 6502 flag the CMPs leave behind */
static void cur_up(Menu *m)    { if (m->cy) m->cy -= 2; }
static void cur_down(Menu *m)  { m->carry = m->cy >= 0xB0; if (m->cy < 0xB0) m->cy += 2; }
static void cur_left(Menu *m)  { if (m->cx) m->cx--; }
static void cur_right(Menu *m) { m->carry = m->cx >= 0x94; if (m->cx < 0x94) m->cx++; }

/* Enter the shop through the door on the map ($351E) */
/* ---- the high scores a competition changes: F39's start-up code at $1B5A ($1B9D, $1414, $1495, $1538) ---- */
static void menu_hs_shift(Menu *m, int g, int a)                   /* $1538: entry g + a moves down one place */
{
    int x = (a + g) * 16, k;
    for (k = 0; k < 16; k++) m->scores[x + 16 + k] = m->scores[x + k];
}
static void menu_hs_insert(Menu *m, int g, int r, int s)           /* $1495: the player's score s of this group goes in at place r */
{
    int y, x, k;
    unsigned char p;
    const unsigned char *res;
    m->f70 = 1;                                                     /* the high scores are shown first */
    if (r < 2) { menu_hs_shift(m, g, 1); if (r < 1) menu_hs_shift(m, g, 0); }
    y = (r + g) * 16;
    p = m->res_ord[s + g];
    for (x = 7; x > 0; x--) if (m->ord[x] == p) break;
    for (k = 0; k < 9; k++) m->scores[y + k] = m->names[x][k];
    m->scores[y + 9] = 0;
    res = m->res_score + (s + g) * 3;
    m->scores[y + 10] = res[0]; m->scores[y + 11] = res[1]; m->scores[y + 12] = res[2];
    m->scores[y + 15] = 0x44;                                       /* shown in its own colour once */
}
static void menu_hs_update(Menu *m)                                /* $1414: every new score of the competition against the table */
{
    int g;
    if (!m->count) return;
    for (g = 0; g < 12; g += 3) {
        int r = 0, s = 0;
        for (;;) {
            const unsigned char *h = m->scores + (r + g) * 16 + 10, *v = m->res_score + (s + g) * 3;
            unsigned long hs = (unsigned long)h[0] | ((unsigned long)h[1] << 8) | ((unsigned long)h[2] << 16);
            unsigned long vs = (unsigned long)v[0] | ((unsigned long)v[1] << 8) | ((unsigned long)v[2] << 16);
            if (hs < vs) { menu_hs_insert(m, g, r, s); if (++s >= 3) break; }
            if (++r >= 3) break;
        }
    }
}
static void menu_hs_colours(Menu *m)                               /* $1B9D */
{
    int i;
    for (i = 0; i < 12; i++) m->scores[i * 16 + 15] = 0x33;
}

/* back in the shop after a competition in all events ($139D): Rodney congratulates the player with the most points (a later player wins a tie) */
static void menu_congrats(Menu *m)
{
    int best = 0, x, k;
    unsigned hi = 0;
    for (x = 0; x < m->count && x < 8; x++) if (m->totals[x] >= hi) { hi = m->totals[x]; best = x; }
    for (k = 0; k < 36; k++) m->congrats[k] = menu_speech_txt[0x14][k];
    for (k = 0; k < 9; k++) { unsigned char c = m->names[best][k]; m->congrats[9 + k] = c ? c : 32; }
    m->f4bd3 = 1;
}

static void menu_enter_shop(Menu *m);
/* what F39 does when it is loaded again after INTER ($1B5A): the shop, the high-score table brought up to date (and shown if something changed) */
static void menu_after_event(Menu *m)
{
    menu_hs_colours(m);
    menu_hs_update(m);
    menu_enter_shop(m);
}
static void menu_enter_shop(Menu *m)
{
    menu_clear_slots(m);
    m->state = MS_SHOP_ENTER; m->cc = 0x0B; m->f60 = 1;
    m->f4bcd = 0;                                                   /* the original loads the shop overlay afresh: Rodney speaks again */
}

/* One logic pass of states 1-9 in the order of the original main loop: pass counter, setup requests, Rodney's speech,
   the palette blink, the state handler, the random number generator.
   ctl = active-low control byte (bit0 up, 1 down, 2 left, 3 right, 4 fire), kbd = $E0C000 (bit 7 set while a key waits).
   Returns 1 when the shop sends the skater onto the map (state is then MS_MAP).
   When an exit of the map is reported (exit_code != 0) with a player signed up or practice chosen, nothing is done here:
   the caller starts the event. */
static int menu_pass(Menu *m, const MenuGfx *g, unsigned ctl, unsigned kbd)
{
    unsigned nib = ctl & 15;
    int fire = !(ctl & 0x10), leave = 0, ev;
    m->pass++;
    menu_setup(m, g);
    menu_speech(m, g);
    menu_blink(m, g);
    m->shadow_x = m->slot_x; m->shadow_y = m->slot_y; m->shadow_f = m->slot_f;
    ev = (m->pass & 7) == 0;
    m->carry = 0;                                   /* the dispatcher's ASL leaves carry clear */
    if (m->f70) {                                   /* $3BB5 */
        m->f70 = 0; m->state = MS_SCORES; m->c6 = 8; m->f6e = 1;
    } else switch (m->state) {
    case MS_RODNEY:
        switch (nib) {
        case 0xD: cur_down(m); break;
        case 0xE: cur_up(m); break;
        case 0xB: cur_left(m); break;
        case 0x7: cur_right(m); break;
        case 0x5: cur_down(m); cur_right(m); break;
        case 0x6: cur_up(m); cur_right(m); break;
        case 0x9: cur_down(m); cur_left(m); break;
        case 0xA: cur_up(m); cur_left(m); break;
        default: break;
        }
        /* fall through */
    case MS_SHOP_ENTER:
        leave = menu_fire_counter(m, g, fire);
        break;
    case MS_PICK:
        if (ev && (nib == 0xD || nib == 0xE)) {         /* $3DCC / $3DF8: the row changes even during the lock-out */
            menu_arrow(m, g, m->c2, 0x20);
            if (nib == 0xD) menu_next(m); else menu_prev(m);
        }
        if (m->c6) { if (!fire) m->c6--; break; }       /* $3712 */
        m->mask = 0x1111; menu_arrow(m, g, m->c2, 0x3E);
        if (fire) {
            if (m->c2 != m->count) {                  /* an existing player: pick the board colour */
                m->d4 = m->c2; m->f4bd1 = 0; m->f62 = 1; m->state = MS_BOARD_WAIT; m->c6 = 8;
            } else menu_to_rodney_request(m);
        }
        break;
    case MS_BOARD_WAIT:
    case MS_BOARD:
        if (m->state == MS_BOARD && ev && (nib == 0xB || nib == 0x7 || nib == 0xD || nib == 0xE)) {     /* $3E24 (left, up) / $3E88 (right, down) */
            m->mask = 0x4444;
            m->gy = menu_board_y[m->c2]; m->gx = menu_board_x[m->c2]; menu_glyph(m, g, 0x20);
            if (nib == 0xB || nib == 0xE) { m->trow++; menu_prev(m); } else menu_next(m);
            m->board[m->d4] = (unsigned char)(m->c2 + 1);
            menu_board_color(m, m->d4);
        }
        if (m->c6) { if (!fire) m->c6--; break; }       /* $3781 */
        if (m->state == MS_BOARD) {
            m->gy = menu_board_y[m->c2]; m->gx = menu_board_x[m->c2]; m->mask = 0xFFFF; menu_glyph(m, g, 0x0B);
        }
        if (fire) {
            if (m->f4bd1) {                             /* just signed up: back to the list */
                menu_clear_slots(m);
                m->state = MS_SIGNUP; m->c6 = 8; m->f68 = 1;
                m->c8 = (unsigned short)(m->count + 2); m->ca = (unsigned short)(m->count + 1); m->cc = 8; m->c2 = m->count;
            } else menu_to_rodney_request(m);
        }
        break;
    case MS_NAME:
        menu_name_entry(m, g, kbd, fire);
        break;
    case MS_SIGNUP:
        if (ev && (nib == 0xD || nib == 0xE)) {         /* $3D64 / $3D98 */
            unsigned r = m->c2 >= 8 ? 8 : m->c2;
            menu_arrow(m, g, r, 0x20);
            if (nib == 0xD) menu_next(m); else menu_prev(m);
        }
        if (m->c6) { if (!fire) m->c6--; break; }       /* $3A7B */
        {
            unsigned r = m->c2 >= 8 ? 8 : m->c2;
            m->mask = 0x1111; menu_arrow(m, g, r, 0x3E);
        }
        if (fire) {
            if (m->c2 > m->count || m->c2 == 8) menu_to_rodney_request(m);   /* CANCEL */
            else menu_request_name_entry(m);
        }
        break;
    case MS_NOTICE:
    case MS_SCORES:
        if (m->c6) { if (!fire) m->c6--; break; }
        if (fire) menu_to_rodney_request(m);
        break;
    default: break;
    }
    menu_rand(m);
    return leave;
}

#endif
