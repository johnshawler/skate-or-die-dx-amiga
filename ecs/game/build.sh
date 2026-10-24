#!/bin/bash
# Build the Amiga game: dist/SkateOrDie (the program) and dist/data/ (screens, sprites, ADPCM songs, event images, the four event overlays *.ovl).
# Needs m68k-linux-gnu-gcc/binutils and python3; the data files come from tools/make_data.py (assets of the earlier demo).
# env: EXTRA="-DFPS_ON -DEV_UNPACED" adds the passes-per-second counter and removes the pacing for speed measurements.
set -e
cd "$(dirname "$0")"
ROOT=../..; T=$ROOT/tools/t816; CC=m68k-linux-gnu-gcc; B=build; mkdir -p $B dist/data
FL="-m68020 -O2 -ffixed-a6 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-strict-aliasing -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -DT816_AMIGA $EXTRA"
[ -f dist/data/ow.raw ] || python3 -I tools/make_data.py $ROOT/demo/assets dist/data
# the program
$CC $FL -Os -c src/ow.c -o $B/ow.o -Isrc
m68k-linux-gnu-ld -T src/link.ld --emit-relocs -o $B/ow.elf $B/ow.o 2>&1 | grep -v "warning\|NOTE" || true
python3 -I $ROOT/tools/elf2hunk.py $B/ow.elf dist/SkateOrDie
# the event overlays: translated game code + runtime + platform side, one hunk file each
cfgs=(x ramp ev8 ev15 ev21 ev28); names=(x ramp hj race jam joust)
for n in 1 2 3 4 5; do
  python3 -I $T/gen.py $T/cfg/${cfgs[$n]}.json $B/ev$n.c 2>/dev/null
  $CC $FL -I$T/runtime -I$T/native -Isrc -c $B/ev$n.c -o $B/ev$n.o
  EV=src/evrun.c
  if [ -n "$STRESS" ]; then cp src/evrun.c $B/evrun_stress.c; python3 -I tools/apply_stress.py $B/evrun_stress.c; EV=$B/evrun_stress.c; fi
  $CC $FL ${STRESS:+-DBENCH_STRESS} -I$T/runtime -I$T/native -Isrc -c $EV -o $B/evrun$n.o -DEV_MODE=$n
  $CC $FL -Os -I$T/runtime -c $T/runtime/m816rt.c -o $B/rt$n.o
  m68k-linux-gnu-ld -T src/link.ld --emit-relocs -o $B/ov$n.elf $B/evrun$n.o $B/ev$n.o $B/rt$n.o 2>&1 | grep -v "warning\|NOTE" || true
  python3 -I $ROOT/tools/elf2hunk.py $B/ov$n.elf dist/data/${names[$n]}.ovl
done
ls -l dist/SkateOrDie dist/data/*.ovl
