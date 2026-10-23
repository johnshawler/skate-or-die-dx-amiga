/* Native test build of demo/src/menu.h: exposes the menu logic to Python (ctypes) so it can be compared with the original code. */
#include <string.h>
#include "../../demo/src/menu.h"

#define FIELDS(X) X(state) X(c2) X(c4) X(c6) X(c8) X(ca) X(cc) X(ce) X(d2) X(d4) X(cx) X(cy) X(f4bcd) X(f4bcf) X(f4bd1) X(f4bd3) \
    X(sign_frame) X(pass) X(f60) X(f62) X(f64) X(f66) X(f68) X(f6e) X(f70) X(exit_code) X(practice) X(key) \
    X(gx) X(gy) X(mask) X(tcol) X(trow) X(slot_x) X(slot_y) X(slot_f) X(count) X(f38) X(f3a)
#define NFIELDS 39

static Menu M;
static MenuGfx G;
static unsigned char fb[32768 + 512];     /* logical screen (bank 01) */
static unsigned char disp[32768 + 512];   /* displayed screen (E1) */
static int sp_tab[256 * 3];
static unsigned char sp_img[0x10000], sp_msk[0x10000];
static unsigned char pics[5][33000];
static unsigned char font[0x60 * 32];
static int cur_pic = -1;
static unsigned char which_pic_seen;

static void t_glyph(void *ctx, int xb, int y, int code, unsigned short mask)
{
    int r, k;
    (void)ctx;
    for (r = 0; r < 8; r++)
        for (k = 0; k < 4; k++) {
            int o = (y + r) * 160 + xb + k;
            unsigned char mb = (k & 1) ? (unsigned char)(mask >> 8) : (unsigned char)mask;
            if (o >= 0 && o < 32000) fb[o] = disp[o] = font[code * 32 + r * 4 + k] & mb;
        }
}
static unsigned short pal_now[16];
static unsigned short pics_pal[5][16];
static const unsigned short *t_picture(void *ctx, int which) { (void)ctx; memcpy(fb, pics[which], 32000); memcpy(disp, pics[which], 32000); cur_pic = which; which_pic_seen = (unsigned char)which; memcpy(pal_now, pics_pal[which], 32); return pics_pal[which]; }
static void t_palette(void *ctx, const unsigned short *p) { (void)ctx; memcpy(pal_now, p, 32); }
const unsigned short *t_pal(void) { return M.pal; }

void t_init(void) { G.ctx = 0; G.glyph = t_glyph; G.picture = t_picture; G.palette = t_palette; memset(&M, 0, sizeof M); }
void t_set_font(const unsigned char *f) { memcpy(font, f, sizeof font); }
void t_set_pic(int w, const unsigned char *d) { int i; memcpy(pics[w], d, 32000); for (i = 0; i < 16; i++) pics_pal[w][i] = (unsigned short)(d[32256 + 2 * i] | (d[32257 + 2 * i] << 8)); }
void t_set_sprites(const int *tab, int n, const unsigned char *img, int ni, const unsigned char *msk) { memcpy(sp_tab, tab, n * 12); memcpy(sp_img, img, ni); memcpy(sp_msk, msk, ni); }
const unsigned char *t_disp(void) { return disp; }
const unsigned char *t_fb(void) { return fb; }
int t_cur_pic(void) { return cur_pic; }
unsigned char *t_names(void) { return &M.names[0][0]; }
unsigned char *t_board(void) { return M.board; }
unsigned char *t_scores(void) { return M.scores; }
unsigned char *t_colour(void) { return M.colour; }
void t_set_pal(const unsigned short *p) { memcpy(M.pal, p, 32); }
void t_set_rng(int a, int b) { M.rng0 = (unsigned char)a; M.rng1 = (unsigned char)b; }
int t_get_rng(void) { return M.rng0 | (M.rng1 << 8); }
void t_load(const unsigned short *v)
{
    int i = 0;
#define L(f) M.f = v[i++];
    FIELDS(L)
#undef L
}
void t_dump(unsigned short *v)
{
    int i = 0;
#define D(f) v[i++] = M.f;
    FIELDS(D)
#undef D
}
static void draw_sprite(unsigned sx, unsigned sy, unsigned f)
{
    int w = sp_tab[f * 3], words = sp_tab[f * 3 + 1], off = sp_tab[f * 3 + 2], rows = words / w, x, y, k;
    int left = (int)sx - 24, top = (int)sy - 50;
    for (y = 0; y < rows; y++)
        for (x = 0; x < w * 2; x++) {
            unsigned b = sp_img[off + y * w * 2 + x], m = sp_msk[off + y * w * 2 + x];
            for (k = 0; k < 2; k++) {
                int px = left + x * 2 + k, py = top + y;
                unsigned mm = k == 0 ? m >> 4 : m & 15, v = k == 0 ? b >> 4 : b & 15;
                if (mm == 0 && px >= 0 && px < 320 && py >= 0 && py < 200) {
                    int i = py * 160 + px / 2;
                    if (px & 1) disp[i] = (disp[i] & 0xF0) | v; else disp[i] = (disp[i] & 0x0F) | (v << 4);
                }
            }
        }
}
/* the sprite engine runs first in a pass: it paints the sprite into the display from the shadow copy taken in the previous pass */
int t_pass(unsigned ctl, unsigned kbd)
{
    memcpy(disp, fb, 32000);
    if (M.shadow_y != 0 && M.shadow_x != 0x168) draw_sprite(M.shadow_x, M.shadow_y, M.shadow_f);
    return menu_pass(&M, &G, ctl, kbd);
}
int t_nfields(void) { return NFIELDS; }
void t_enter_shop(void) { menu_enter_shop(&M); }
