/* Pure drawing helpers shared by the Amiga build and the host tests (no OS access). */
#ifndef GFXGLUE_H
#define GFXGLUE_H
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16;

/* an 8x8 glyph (font nibble AND mask nibble per pixel, opaque) at byte column xb (2 pixels each) and row y, written to the logical
   screen and to the buffer being composed */
static void glyph_to(u8 *buf, const u8 *font, int xb, int y, int code, u16 mask)
{
    int x = xb * 2, s = x & 7, bx = x >> 3, p, r, i;
    u8 pm[4];
    u8 mp[8];
    const u8 *gl = font + code * 32;
    mp[0] = (u8)((mask >> 4) & 15); mp[1] = (u8)(mask & 15); mp[2] = (u8)((mask >> 12) & 15); mp[3] = (u8)((mask >> 8) & 15);
    for (i = 4; i < 8; i++) mp[i] = mp[i - 4];
    for (p = 0; p < 4; p++) {
        u8 v = 0;
        for (i = 0; i < 8; i++) v = (u8)((v << 1) | ((mp[i] >> p) & 1));
        pm[p] = v;
    }
    for (r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= 200) continue;
        for (p = 0; p < 4; p++) {
            u8 g = (u8)(gl[r * 4 + p] & pm[p]);
            u8 *d = buf + yy * 160 + p * 40 + bx;
            if (bx >= 40) continue;
            if (s == 0) d[0] = g;
            else {
                u16 w = (u16)((d[0] << 8) | (bx + 1 < 40 ? d[1] : 0));
                u16 keep = (u16)~(0xFF00 >> s);
                w = (u16)((w & keep) | ((u16)(g << 8) >> s));
                d[0] = (u8)(w >> 8);
                if (bx + 1 < 40) d[1] = (u8)w;
            }
        }
    }
}

#endif
