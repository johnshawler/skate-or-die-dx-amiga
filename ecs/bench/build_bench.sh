#!/bin/bash
# Chip-RAM-only timing bench: build_bench.sh <event 1-4 (ramp, high jump, race, jam)> [out dir]
# Builds a copy of the earlier AGA demo that starts straight in one event, with the other events and all large audio replaced by stubs so that it
# fits in 2 MB of Chip RAM, an on-screen passes-per-second counter and no pacing (EV_UNPACED). Reads ../../demo and ../../tools/t816 only.
set -e
EV=$1; HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$HERE/../..; OUT=${2:-$HERE/build}; W=$OUT/w$EV; mkdir -p $W/assets $W/obj
T=$ROOT/tools/t816; CC=m68k-linux-gnu-gcc
FL="-m68020 -O2 -ffixed-a6 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-strict-aliasing -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -DT816_AMIGA -DFPS_ON -DEV_UNPACED -DBENCH_EVENT=$EV"
cp $ROOT/demo/src/*.h $ROOT/demo/src/*.c $ROOT/demo/src/link.ld $W/ ; 
for f in map shop list scores board title; do cp $ROOT/demo/assets/$f.scr $W/assets/; done; cp $ROOT/demo/assets/menu.fnt $ROOT/demo/assets/skater.spr $W/assets/
for f in title.ima music.raw ev_song1.raw ev_song2.raw ev_song3.raw ev_song4.raw sfx.raw; do head -c 4096 /dev/zero > $W/assets/$f; done
for n in 1 2 3 4; do if [ $n = $EV ]; then cp $ROOT/demo/assets/ev$n.pak $W/assets/; else printf 'T8PK\0\0\0\0' > $W/assets/ev$n.pak; fi; done
cp $ROOT/demo/src/data.S $W/
[ -n "$DIFF" ] && python3 -I $HERE/c2p/apply_diff_c2p.py $W && SUF=_diff
[ -n "$STRESS" ] && python3 -I $HERE/apply_stress.py $W && SUF=${SUF}_stress && FL="$FL -DBENCH_STRESS"
python3 - $W/ow.c $EV <<'PY'
import sys
p, ev = sys.argv[1], int(sys.argv[2])
s = open(p).read()
a = '    if (title_screen(P)) quit = 1;\n'
assert a in s
s = s.replace(a, '''#ifdef BENCH_EVENT
    M->practice = 1; vbl_hz = pal_hz == 60 ? 60 : 50;
    for (;;) { ev_run(P, M, ev_pak[BENCH_EVENT], ev_disp[BENCH_EVENT], (u32)vbl_hz, BENCH_EVENT); if (ev_abort) break; }
    quit = 1;
#else
''' + a + '#endif\n', 1)
names = ['ramp', 'hj', 'race', 'jam']
b = 'static const u8 *const ev_pak[5] = {0, ev1_pak, ev2_pak, ev3_pak, ev4_pak};'
assert b in s
s = s.replace(b, b + '\n#define ev_unused_guard 1', 1)
open(p, 'w').write(s)
PY
cfg=(x ramp ev8 ev15 ev21); pre=(x ramp_ hj_ race_ jam_); obj=
python3 -I $T/gen.py $T/cfg/${cfg[$EV]}.json $W/ev.c --prefix=${pre[$EV]} 2>/dev/null
# the other events are replaced by empty dispatchers so that ow.c links
: > $W/stubs.c
for n in 1 2 3 4; do [ $n = $EV ] || echo "u32 ${pre[$n]}dispatch_call(u16 a) { (void)a; return 0; }" >> $W/stubs.c; done
echo 'typedef unsigned short u16; typedef unsigned int u32;' | cat - $W/stubs.c > $W/stubs2.c
$CC $FL -Os -c $W/ow.c -I$T/runtime -o $W/obj/ow.o -I$W
$CC $FL -I$T/runtime -I$T/native -c $W/ev.c -o $W/obj/ev.o
$CC $FL -Os -c $W/stubs2.c -o $W/obj/stubs.o
$CC $FL -Os -I$T/runtime -c $T/runtime/m816rt.c -o $W/obj/rt.o
$CC -m68020 -c $W/data.S -o $W/obj/data.o -I$W
(cd $W && m68k-linux-gnu-ld -T link.ld --emit-relocs -o bench.elf obj/ow.o obj/ev.o obj/stubs.o obj/rt.o obj/data.o 2>&1 | grep -v "warning" ; python3 -I $ROOT/tools/elf2hunk.py bench.elf $OUT/bench_ev$EV$SUF)
ls -l $OUT/bench_ev$EV$SUF
