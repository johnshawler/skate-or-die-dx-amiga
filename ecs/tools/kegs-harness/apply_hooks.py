#!/usr/bin/env python3
"""Add the harness hooks to a KEGS 1.38 source tree: apply_hooks.py <kegs/src dir>"""
import sys, re
d = sys.argv[1]
def edit(fn, old, new, count=1):
    s = open(f'{d}/{fn}').read()
    assert old in s, (fn, old)
    open(f'{d}/{fn}', 'w').write(s.replace(old, new, count))
DECL = 'extern int hx_active; extern unsigned hx_doc_writes; void hx_init(void), hx_frame_begin(void), hx_frame_end(void), hx_paddle_trigger(dword64);\n'
# sim65816.c: per-frame hooks, no sleeping, init
edit('sim65816.c', 'word32 g_cycs_end_16ms = 0;\n', 'word32 g_cycs_end_16ms = 0;\n' + DECL)
edit('sim65816.c', '\t\t\tret = run_a2_one_vbl();\n', '\t\t\thx_frame_begin();\n\t\t\tret = run_a2_one_vbl();\n')
edit('sim65816.c', '\tvideo_update();\n\tg_vbl_count++;\n', '\tvideo_update();\n\tg_vbl_count++;\n\tif(hx_active) { hx_frame_end(); g_dtime_sleep = 0.0; }\n')
edit('sim65816.c', '\tmicro_sleep(g_dtime_sleep);\n\tdtime_end2', '\tif(!hx_active) micro_sleep(g_dtime_sleep);\n\tdtime_end2')
edit('sim65816.c', '\tprintf("Starting KEGS v%s\\n", &g_kegs_version_str[0]);\n', '\tprintf("Starting KEGS v%s\\n", &g_kegs_version_str[0]);\n\thx_init();\n')
# declarations must precede first use of hx_init in kegsmain: put them at the top too
s = open(f'{d}/sim65816.c').read()
s = s.replace('word32 g_cycs_end_16ms = 0;\n' + DECL, 'word32 g_cycs_end_16ms = 0;\n', 1)
i = s.index('\n', s.index('#include "defc.h"')) + 1
open(f'{d}/sim65816.c', 'w').write(s[:i] + DECL + s[i:])
# paddles.c: scripted joystick
edit('paddles.c', 'void\npaddle_trigger(dword64 dfcyc)\n{\n', 'extern int hx_active; void hx_paddle_trigger(dword64);\nvoid\npaddle_trigger(dword64 dfcyc)\n{\n\tif(hx_active) { hx_paddle_trigger(dfcyc); return; }\n')
edit('paddles.c', 'void\npaddle_update_buttons()\n{\n', 'void\npaddle_update_buttons()\n{\n\tif(hx_active) { return; }\n')
# doc.c: count writes to the Ensoniq data register
s = open(f'{d}/doc.c').read()
m = re.search(r'void\ndoc_write_c03d\(dword64 dfcyc, word32 val\)\n\{\n', s)
assert m
s = s[:m.start()] + 'extern unsigned hx_doc_writes;\n' + s[m.start():m.end()] + '\thx_doc_writes++;\n' + s[m.end():]
open(f'{d}/doc.c', 'w').write(s)
# debugger.c: instruction breakpoints set by the harness only count, they do not stop the machine
edit('debugger.c', '\t\tif((type & bp_ptr->acc_type) == 0) {\n\t\t\tcontinue;\n\t\t}\n', '\t\tif((type & bp_ptr->acc_type) == 0) {\n\t\t\tcontinue;\n\t\t}\n\t\tif(hx_active && type == 4 && hx_is_pc(addr)) { hx_pc_hit(addr); continue; }\n')
edit('debugger.c', 'void\ncheck_breakpoints(word32 addr,', 'extern int hx_active; int hx_is_pc(word32); void hx_pc_hit(word32);\nvoid\ncheck_breakpoints(word32 addr,')
# vars: link harness.o
edit('vars_x86linux', 'OBJECTS1 = pulseaudio_driver.o xdriver.o', 'OBJECTS1 = pulseaudio_driver.o xdriver.o harness.o')
