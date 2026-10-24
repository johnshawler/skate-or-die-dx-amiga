/* test driver for the c2p kernels (run in Unicorn by c2ptest.py) */
#include "c2p.h"
void tst_build(u32 *tab) { c2p_build_lut(tab); }
void tst_row(const u32 *tab, const u8 *s, u8 *d, u8 *h, int g0, int n, int force) { c2p_row(tab, s, d, h, g0, n, force); }
