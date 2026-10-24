#!/bin/bash
# Play one event for a given time on the TIMING build (see build.sh notes) in FS-UAE and collect the per-pass work times it writes:
# run_timing.sh <timing.hdf> <mode 1-5> <seconds of play> <out dir>      (input: the synthetic stress of apply_stress.py, paced at the event's rate)
set -e
HDF=$(readlink -f "$1"); MODE=$2; SECS=$3; OUT=$4; mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")
cp "$HDF" "$OUT/run.hdf"
"$(dirname "$0")/run_uae.sh" "$OUT/run.hdf" "$OUT/uae" "wait:10 key:space wait:6 key:F$MODE wait:$SECS shot:play key:Escape wait:25 shot:after" >/dev/null 2>&1 || true
for n in $(seq 0 23); do
  l=$(printf "\\x$(printf %x $((97 + n)))")
  xdftool "$OUT/run.hdf" open part=SOD + read data/tm_${MODE}${l}.bin "$OUT/tm_${MODE}${l}.bin" >/dev/null 2>&1 || break
done
ls "$OUT"/tm_* 2>/dev/null | wc -l
