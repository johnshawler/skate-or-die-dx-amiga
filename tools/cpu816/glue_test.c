#include <string.h>
#include "../../demo/src/gfxglue.h"
static u8 font[96 * 32];
static u8 buf[32000 + 4096];
void gt_set_font(const u8 *f) { memcpy(font, f, sizeof font); }
void gt_clear(int v) { memset(buf, v, sizeof buf); }
u8 *gt_buf(void) { return buf; }
void gt_glyph(int xb, int y, int code, unsigned mask) { glyph_to(buf, font, xb, y, code, (u16)mask); }
