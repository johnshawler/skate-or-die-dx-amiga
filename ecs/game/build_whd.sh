#!/bin/bash
# WHDLoad build: dist_whd/SkateOrDie/ = SkateOrDie.slave + data/ (the program image SkateOrDie.bin, the five overlays *.ovx and the data files of the normal build).
# Needs the normal build's data (dist/data from ./build.sh or tools/make_data.py). env: EXTRA as for build.sh.
set -e
cd "$(dirname "$0")"
ROOT=../..; T=$ROOT/tools/t816; CC=m68k-linux-gnu-gcc; B=build_whd; O=dist_whd/SkateOrDie; mkdir -p $B $O/data
FL="-m68020 -O2 -ffixed-a6 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-strict-aliasing -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -DT816_AMIGA -DWHDLOAD $EXTRA"
[ -f dist/data/ow.raw ] || ./build.sh > /dev/null
# the program
$CC $FL -Os -c src/ow.c -o $B/ow.o -Isrc
m68k-linux-gnu-ld -T src/link.ld --emit-relocs -o $B/ow.elf $B/ow.o 2>&1 | grep -v "warning\|NOTE" || true
python3 -I tools/elf2whd.py $B/ow.elf $O/data/SkateOrDie.bin SODW
# INTER and the event overlays (the generated C is the same as in the normal build)
cfgs=(x ramp ev8 ev15 ev21 ev28); names=(x ramp hj race jam joust)
INL="-finline-functions --param max-inline-insns-auto=150 --param inline-unit-growth=200 --param large-function-growth=300"
python3 -I tools/gen_ecs.py cfg/inter.json $B/inter.c --prefix=i_ 2>/dev/null
$CC $FL -Os -Iruntime -I$T/runtime -Inative -I$T/native -Isrc -c $B/inter.c -o $B/inter.o
for n in 1 2 3 4 5; do
  case $n in 1|2|5) EVOPT="$INL";; *) EVOPT=;; esac
  python3 -I tools/gen_ecs.py cfg/${cfgs[$n]}.json $B/ev$n.c 2>/dev/null
  case $n in 3|4) B2D=-DT816_B2DIRTY;; *) B2D=;; esac
  $CC $FL $B2D ${EVOPT:-} -Iruntime -I$T/runtime -Inative -I$T/native -Isrc -c $B/ev$n.c -o $B/ev$n.o
  $CC $FL -Iruntime -I$T/runtime -Inative -I$T/native -Isrc -c src/evrun.c -o $B/evrun$n.o -DEV_MODE=$n
  $CC $FL $B2D -Os -Iruntime -c runtime/m816rt.c -o $B/rt$n.o
  m68k-linux-gnu-ld -T src/link.ld --emit-relocs -o $B/ov$n.elf $B/evrun$n.o $B/ev$n.o $B/inter.o $B/rt$n.o 2>&1 | grep -v "warning\|NOTE" || true
  python3 -I tools/elf2whd.py $B/ov$n.elf $O/data/${names[$n]}.ovx SODO
done
# the slave
m68k-linux-gnu-gcc -m68020 -c src/whd_slave.S -o $B/slave.o
m68k-linux-gnu-ld -Ttext=0 -o $B/slave.elf $B/slave.o
python3 -I tools/elf2hunk.py $B/slave.elf $O/SkateOrDie.slave
# data files shared with the normal build (the overlays of that build are not needed)
for f in dist/data/*; do case $f in *.ovl) ;; *) cp $f $O/data/;; esac; done
ls -l $O $O/data | head -50
