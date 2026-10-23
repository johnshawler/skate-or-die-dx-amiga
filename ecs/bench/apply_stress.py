#!/usr/bin/env python3
"""Bench-only: synthetic stress input (seeded waggle: fast left/right alternation, fire pulses, now and then up/down) and a three-number counter at the
bottom left: passes in the last second, the lowest second so far (after 8 s) and the average. usage: apply_stress.py <work dir>"""
import sys
p = sys.argv[1] + '/evplat.h'
s = open(p).read()
a = 'static int fps_on = 1,'
assert a in s
s = s.replace(a, 'static int fps_min = 999, fps_sum = 0, fps_n = 0, fps_warm = 0;\n' + a, 1)
a = 'if (fps_frames >= ev_vbl) { fps_shown = fps_passes; fps_passes = 0; fps_frames = 0; }'
assert a in s
s = s.replace(a, 'if (fps_frames >= ev_vbl) { fps_shown = fps_passes; if (++fps_warm > 8) { if (fps_shown < fps_min) fps_min = fps_shown; fps_sum += fps_shown; fps_n++; } fps_passes = 0; fps_frames = 0; }', 1)
i = s.index('        int v = fps_shown, k;'); j = s.index('            v /= 10;\n        }\n    }', i) + len('            v /= 10;\n        }\n    }')
new = '''        int g, k;
        static const u8 dig[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
        for (g = 0; g < 3; g++) {                       /* current, lowest, average passes a second */
            int v = g == 0 ? fps_shown : g == 1 ? (fps_min == 999 ? 0 : fps_min) : (fps_n ? fps_sum / fps_n : 0);
            for (k = 2; k >= 0; k--) {
                int r, p;
                for (r = 0; r < 10; r++) {
                    u8 pt = dig[v % 10][r >> 1], b = (u8)(((pt & 4) ? 0xC0 : 0) | ((pt & 2) ? 0x30 : 0) | ((pt & 1) ? 0x0C : 0));
                    u8 *d = P->B[P->comp] + (188 + r) * 160 + k + 4 * g;
                    d[0] = b; for (p = 1; p < 4; p++) d[40 * p] = 0;
                }
                v /= 10;
            }
        }
    }'''
s = s[:i] + new + s[j:]
a = '        if (keys[0x45] || ev_abort) { ev_abort = 1; return 1; }  /* ESC leaves the event */\n'
assert a in s
s = s.replace(a, a + '''#ifdef BENCH_STRESS
        {   static u32 rs = 12345; static int hold = 0, sx = 1, sy = 0, sf = 0;
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
            if (--hold <= 0) { u32 q = (rs >> 8) % 6; sx = -sx; sy = q == 4 ? -1 : q == 5 ? 1 : 0; hold = 1 + (int)((rs >> 16) % 4); sf = ((rs >> 20) % 10) < 3; }
            jx = sx; jy = sy; fire = sf;
        }
#endif
''', 1)
open(p, 'w').write(s)
