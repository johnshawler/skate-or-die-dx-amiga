#!/bin/bash
# Build the Amiga game: dist/SkateOrDie (the program) and dist/data/ (screens, sprites, ADPCM songs, event images, the four event overlays *.ovl).
# Needs m68k-linux-gnu-gcc/binutils and python3; the data files come from tools/make_data.py (assets of the earlier demo).
# env: EXTRA="-DFPS_ON -DEV_UNPACED" adds the passes-per-second counter and removes the pacing for speed measurements.
set -e
cd "$(dirname "$0")"
ROOT=../..; T=$ROOT/tools/t816; CC=m68k-linux-gnu-gcc; B=build; mkdir -p $B dist/data
OVFL=${REGB3:+"-ffixed-a5 -DT816_REGB3"}
FL="-m68020 -O2 -ffixed-a6 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-strict-aliasing -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -DT816_AMIGA $EXTRA"
cp assets/inter.bin dist/data/inter.bin
[ -f dist/data/ow.raw ] || python3 -I tools/make_data.py $ROOT/demo/assets dist/data
# the program
$CC $FL -Os -c src/ow.c -o $B/ow.o -Isrc
m68k-linux-gnu-ld -T src/link.ld --emit-relocs -o $B/ow.elf $B/ow.o 2>&1 | grep -v "warning\|NOTE" || true
python3 -I tools/elf2hunk.py $B/ow.elf dist/SkateOrDie
# the event overlays: translated game code + runtime + platform side, one hunk file each
cfgs=(x ramp ev8 ev15 ev21 ev28); names=(x ramp hj race jam joust)
INL="-finline-functions --param max-inline-insns-auto=150 --param inline-unit-growth=200 --param large-function-growth=300"   # worth 3% in the ramp, high jump and joust; the race and jam have no memory to spare for the larger code
# INTER, the original's event and competition driver (bank 7 $6800-$7FFF; its code is the same in every overlay)
python3 -I tools/gen_ecs.py cfg/inter.json $B/inter.c --prefix=i_ 2>/dev/null
$CC $FL $OVFL -Os -Iruntime -I$T/runtime -Inative -I$T/native -Isrc -c $B/inter.c -o $B/inter.o
for n in 1 2 3 4 5; do
  case $n in 1|2|5) EVOPT="$INL";; *) EVOPT=;; esac
  python3 -I tools/gen_ecs.py cfg/${cfgs[$n]}.json $B/ev$n.c 2>/dev/null
  $CC $FL $OVFL ${EVOPT:-} -Iruntime -I$T/runtime -Inative -I$T/native -Isrc -c $B/ev$n.c -o $B/ev$n.o
  EV=src/evrun.c
  if [ -n "$STRESS" ]; then cp src/evrun.c $B/evrun_stress.c; python3 -I tools/apply_stress.py $B/evrun_stress.c; EV=$B/evrun_stress.c; fi
  $CC $FL $OVFL ${STRESS:+-DBENCH_STRESS} -Iruntime -I$T/runtime -Inative -I$T/native -Isrc -c $EV -o $B/evrun$n.o -DEV_MODE=$n
  $CC $FL $OVFL -Os -Iruntime -c runtime/m816rt.c -o $B/rt$n.o
  m68k-linux-gnu-ld -T src/link.ld --emit-relocs -o $B/ov$n.elf $B/evrun$n.o $B/ev$n.o $B/inter.o $B/rt$n.o 2>&1 | grep -v "warning\|NOTE" || true
  python3 -I tools/elf2hunk.py $B/ov$n.elf dist/data/${names[$n]}.ovl
done
ls -l dist/SkateOrDie dist/data/*.ovl
