/* Host side of the picture conversion for tools/lockstep_ecs.py: the platform's two display buffers, driven exactly as evrun.c's rt_pass drives them (collect, convert the
   buffer that is not shown, swap), using the same c2p.h. The test compares the converted buffer with a conversion of the whole picture. */
#define C2P_SCROLL_HOST 1
#include <string.h>
#include "m816.h"
#include "c2p.h"
static u8 hb[2][32000], hs[2][32000];
static u32 htab[C2P_TAB_LONGS];
static int hshown;
void c2p_host_init(void)
{
    memset(hb, 0xA5, sizeof hb);
    c2p_build_lut(htab);
    c2p_shadow[0] = hs[0]; c2p_shadow[1] = hs[1];
    c2p_all_dirty();
    hshown = 0;
}
/* one pass: returns the buffer just converted (32000 bytes) */
const u8 *c2p_host_pass(void)
{
    int comp = hshown ^ 1;
    c2p_collect();
    c2p_convert(htab, BE1 + 0x2000, hb[comp], comp);
    hshown = comp;
    return hb[comp];
}
