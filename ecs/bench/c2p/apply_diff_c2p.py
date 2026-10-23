#!/usr/bin/env python3
"""Bench-only experiment: make the picture conversion skip 8-pixel groups whose chunky bytes did not change since that display buffer was last converted
(a shadow copy of the chunky picture per buffer, 2 x 32,000 bytes). usage: apply_diff_c2p.py <work dir>"""
import sys
w = sys.argv[1]
c = open(f'{w}/c2p.h').read()
c = c.replace('static u8 c2p_pmin[2][200], c2p_pmax[2][200];', 'static u8 c2p_pmin[2][200], c2p_pmax[2][200];\nstatic u8 *c2p_shadow[2]; static u8 c2p_force[2] = { 1, 1 };')
c = c.replace('static void c2p_all_dirty(void)\n{\n    int i;', 'static void c2p_all_dirty(void)\n{\n    int i;\n    c2p_force[0] = c2p_force[1] = 1;')
old = '#ifdef __mc68000__\n        {\n            u32 t2, t3, idx;\n            n--;'
assert old in c
new = '''#ifdef __mc68000__
        if (!c2p_force[buf]) {
            u32 t2, t3, idx = 0, v;
            const u8 *h = c2p_shadow[buf] + (s - src);
            n--;
            __asm__ volatile(
                "moveq #0,%[i]\\n"
                "1: move.l (%[s])+,%[v]\\n\\t"
                "cmp.l (%[h])+,%[v]\\n\\t"
                "beq.s 2f\\n\\t"
                "move.l %[v],-4(%[h])\\n\\t"
                "move.w %[v],%[i]\\n\\t"
                "move.l (%[l],%[i].l*4),%[b]\\n\\t"
                "lsr.l #4,%[b]\\n\\t"
                "swap %[v]\\n\\t"
                "move.w %[v],%[i]\\n\\t"
                "move.l (%[l],%[i].l*4),%[a]\\n\\t"
                "or.l %[b],%[a]\\n\\t"
                "move.b %[a],120(%[d])\\n\\t"
                "lsr.l #8,%[a]\\n\\t"
                "move.b %[a],80(%[d])\\n\\t"
                "lsr.l #8,%[a]\\n\\t"
                "move.b %[a],40(%[d])\\n\\t"
                "lsr.l #8,%[a]\\n\\t"
                "move.b %[a],(%[d])\\n\\t"
                "2: addq.l #1,%[d]\\n\\t"
                "dbra %[n],1b"
                : [s] "+a"(s), [d] "+a"(d), [h] "+a"(h), [n] "+d"(n), [i] "+d"(idx), [a] "=&d"(t2), [b] "=&d"(t3), [v] "=&d"(v)
                : [l] "a"(lut) : "memory", "cc");
        } else {
            u32 t2, t3, idx;
            { u32 *sh = (u32 *)(c2p_shadow[buf] + (s - src)); const u32 *sp = (const u32 *)s; int k; for (k = 0; k < n; k++) sh[k] = sp[k]; }
            n--;'''
c = c.replace(old, new, 1)
c = c.replace('static void c2p_convert(const u32 *lut, const u8 *src, u8 *dst, int buf)\n{', 'static void c2p_convert(const u32 *lut, const u8 *src, u8 *dst, int buf)\n{', 1)
# the force flag is cleared once the whole picture has been converted for that buffer
assert c.rstrip().endswith('    }\n}\n#endif')
c = c.rstrip()[:-len('    }\n}\n#endif')] + '    }\n    c2p_force[buf] = 0;\n}\n#endif\n'
open(f'{w}/c2p.h', 'w').write(c)
e = open(f'{w}/evplat.h').read()
e = e.replace('AllocMem(EV_BYTES + C2P_LUT_LONGS * 4, MEMF_CLEAR)', 'AllocMem(EV_BYTES + C2P_LUT_LONGS * 4 + 64000, MEMF_CLEAR)')
e = e.replace('    ev_lut = (u32 *)(mem + EV_BYTES);', '    ev_lut = (u32 *)(mem + EV_BYTES);\n    c2p_shadow[0] = (u8 *)(mem + EV_BYTES + C2P_LUT_LONGS * 4); c2p_shadow[1] = c2p_shadow[0] + 32000;')
e = e.replace('FreeMem(mem, EV_BYTES + C2P_LUT_LONGS * 4)', 'FreeMem(mem, EV_BYTES + C2P_LUT_LONGS * 4 + 64000)')
open(f'{w}/evplat.h', 'w').write(e)
